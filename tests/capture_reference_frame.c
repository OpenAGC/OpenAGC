/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic host reference frame for the public capture contract.
 */

#include "agc_error.h"
#include "openagc/capture.h"
#include "openagc/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../samples/hw_test/shaders/fill_color_native_reflection.h"
#include "../samples/hw_test/shaders/fill_color_native_sb.h"

typedef struct CaptureFile {
    FILE *file;
    int failed;
} CaptureFile;

static void PS5_SYSV_ABI write_capture(
    void *user_data, const void *data, size_t size)
{
    CaptureFile *sink = (CaptureFile *)user_data;

    if (!sink->failed && fwrite(data, 1u, size, sink->file) != size)
        sink->failed = 1;
}

static int check_result(const char *operation, int32_t result)
{
    if (result == AGC_OK)
        return 1;
    fprintf(stderr, "%s failed: 0x%08x\n", operation, (unsigned)result);
    return 0;
}

#define CHECK(call) do { if (!check_result(#call, (call))) return 1; } while (0)

int main(int argc, char **argv)
{
    const uint32_t push_constants[] = {64u, UINT32_C(0xff00ff00)};
    CaptureFile sink = {0};
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcCaptureDesc capture_desc = AGC_CAPTURE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcShaderDesc shader_desc = AGC_SHADER_DESC_INIT;
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcDescriptorWrite descriptor = AGC_DESCRIPTOR_WRITE_INIT;
    AgcResourceTransition transitions[2] = {
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT
    };
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcShaderReflection reflection;
    AgcDevice device = NULL;
    AgcCapture capture = NULL;
    AgcQueue queue = NULL;
    AgcBuffer buffer = NULL;
    AgcShader shader = NULL;
    AgcComputePipeline pipeline = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;

    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.oagc\n", argv[0]);
        return 2;
    }
    if (fill_color_native_reflection_bytes_size != sizeof(reflection)) {
        fprintf(stderr, "reflection ABI size mismatch\n");
        return 1;
    }
    memcpy(&reflection, fill_color_native_reflection_bytes,
        sizeof(reflection));

    sink.file = fopen(argv[1], "wb");
    if (!sink.file) {
        perror(argv[1]);
        return 1;
    }

    CHECK(agcCreateDevice(&device_desc, &device));
    capture_desc.write = write_capture;
    capture_desc.user_data = &sink;
    CHECK(agcCreateCapture(device, &capture_desc, &capture));
    CHECK(agcBeginCapture(capture));

    queue_desc.type = kAgcQueueCompute;
    CHECK(agcCreateQueue(device, &queue_desc, &queue));

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    CHECK(agcCreateBuffer(device, &buffer_desc, &buffer));
    CHECK(agcSetObjectDebugName(device, AGC_OBJECT_TYPE_BUFFER, buffer,
        "reference-output"));

    shader_desc.stage = reflection.stage;
    shader_desc.code = fill_color_native_data;
    shader_desc.code_size = fill_color_native_data_len;
    shader_desc.reflection = &reflection;
    CHECK(agcCreateShader(device, &shader_desc, &shader));

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = reflection.local_size_x;
    pipeline_desc.local_size_y = reflection.local_size_y;
    pipeline_desc.local_size_z = reflection.local_size_z;
    pipeline_desc.descriptor_mapping_count =
        reflection.descriptor_mapping_count;
    pipeline_desc.descriptor_mappings = reflection.descriptor_mappings;
    pipeline_desc.push_constant_range_count =
        reflection.push_constant_range_count;
    pipeline_desc.push_constant_ranges = reflection.push_constant_ranges;
    CHECK(agcCreateComputePipeline(device, &pipeline_desc, &pipeline));

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 512u;
    CHECK(agcCreateCommandBuffer(device, &command_desc, &command));
    CHECK(agcSetObjectDebugName(device, AGC_OBJECT_TYPE_COMMAND_BUFFER,
        command, "reference-frame"));
    CHECK(agcCreateFence(device, &fence_desc, &fence));

    descriptor.set = reflection.descriptor_mappings[0].set;
    descriptor.binding = reflection.descriptor_mappings[0].binding;
    descriptor.type = reflection.descriptor_mappings[0].type;
    descriptor.buffer = buffer;
    descriptor.buffer_range = buffer_desc.size;

    transitions[0].resource_type = kAgcResourceTypeBuffer;
    transitions[0].buffer = buffer;
    transitions[0].buffer_size = buffer_desc.size;
    transitions[0].before = kAgcResourceUsageUndefined;
    transitions[0].after = kAgcResourceUsageShaderWrite;
    transitions[0].before_owner = kAgcResourceOwnerHost;
    transitions[0].after_owner = kAgcResourceOwnerCompute;
    transitions[1] = transitions[0];
    transitions[1].before = kAgcResourceUsageShaderWrite;
    transitions[1].after = kAgcResourceUsageHostRead;
    transitions[1].before_owner = kAgcResourceOwnerCompute;
    transitions[1].after_owner = kAgcResourceOwnerHost;

    CHECK(agcBeginCommandBuffer(command));
    CHECK(agcCmdBindComputePipeline(command, pipeline));
    CHECK(agcCmdTransitionResources(command, 1u, &transitions[0]));
    CHECK(agcCmdBindDescriptors(command, 1u, &descriptor));
    CHECK(agcCmdPushConstants(command, 1u << kAgcShaderStageCs, 0u,
        sizeof(push_constants), push_constants));
    CHECK(agcCmdDispatch(command, 1u, 1u, 1u));
    CHECK(agcCmdTransitionResources(command, 1u, &transitions[1]));
    CHECK(agcEndCommandBuffer(command));

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    CHECK(agcQueueSubmit(queue, &submit, fence));
    CHECK(agcWaitFence(fence, UINT64_C(1000000)));
    CHECK(agcCaptureRecordReadbackHash(capture, AGC_CAPTURE_OBJECT_BUFFER,
        buffer, 0u, buffer_desc.size));
    CHECK(agcResetCommandBuffer(command));

    CHECK(agcDestroyFence(fence));
    CHECK(agcDestroyCommandBuffer(command));
    CHECK(agcDestroyComputePipeline(pipeline));
    CHECK(agcDestroyShader(shader));
    CHECK(agcDestroyBuffer(buffer));
    CHECK(agcDestroyQueue(queue));
    CHECK(agcEndCapture(capture));
    CHECK(agcDestroyCapture(capture));
    CHECK(agcDestroyDevice(device));

    if (fclose(sink.file) != 0 || sink.failed) {
        fprintf(stderr, "capture write failed\n");
        return 1;
    }
    return 0;
}
