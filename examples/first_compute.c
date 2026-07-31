/*
 * OpenAGC native-runtime compute tutorial.
 * Uses only installed public headers plus compiler-generated shader artifacts.
 */

#include "agc_error.h"
#include "openagc/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fill_color_native_reflection.h"
#include "fill_color_native_sb.h"

#define TRY(call) do { \
    result = (call); \
    if (result != AGC_OK) { \
        fprintf(stderr, "%s: %s (0x%08x)\n", #call, \
            agcErrorString(result), (unsigned)result); \
        goto cleanup; \
    } \
} while (0)

int main(void)
{
    const uint32_t push_constants[2] = {64u, UINT32_C(0xff00ff00)};
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcShaderDesc shader_desc = AGC_SHADER_DESC_INIT;
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcDescriptorWrite descriptor = AGC_DESCRIPTOR_WRITE_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcShaderReflection reflection;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcShader shader = NULL;
    AgcComputePipeline pipeline = NULL;
    AgcBuffer output = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    int32_t result = AGC_OK;
    int exit_code = 1;

    if (fill_color_native_reflection_bytes_size != sizeof(reflection)) {
        fputs("reflection ABI mismatch\n", stderr);
        return 1;
    }
    memcpy(&reflection, fill_color_native_reflection_bytes,
        sizeof(reflection));

    device_desc.required_capability_bits = AGC_RUNTIME_CAP_BASELINE;
    TRY(agcCreateDevice(&device_desc, &device));
    queue_desc.type = kAgcQueueCompute;
    TRY(agcCreateQueue(device, &queue_desc, &queue));

    shader_desc.stage = reflection.stage;
    shader_desc.code = fill_color_native_data;
    shader_desc.code_size = fill_color_native_data_len;
    shader_desc.reflection = &reflection;
    TRY(agcCreateShader(device, &shader_desc, &shader));

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
    TRY(agcCreateComputePipeline(device, &pipeline_desc, &pipeline));

    buffer_desc.size = 64u * sizeof(uint32_t);
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    TRY(agcCreateBuffer(device, &buffer_desc, &output));

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 512u;
    TRY(agcCreateCommandBuffer(device, &command_desc, &command));
    TRY(agcCreateFence(device, &fence_desc, &fence));

    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = output;
    transition.buffer_size = buffer_desc.size;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;

    descriptor.set = reflection.descriptor_mappings[0].set;
    descriptor.binding = reflection.descriptor_mappings[0].binding;
    descriptor.type = reflection.descriptor_mappings[0].type;
    descriptor.buffer = output;
    descriptor.buffer_range = buffer_desc.size;

    TRY(agcBeginCommandBuffer(command));
    TRY(agcCmdTransitionResources(command, 1u, &transition));
    TRY(agcCmdBindComputePipeline(command, pipeline));
    TRY(agcCmdBindDescriptors(command, 1u, &descriptor));
    TRY(agcCmdPushConstants(command, 1u << kAgcShaderStageCs, 0u,
        sizeof(push_constants), push_constants));
    TRY(agcCmdDispatch(command, 1u, 1u, 1u));
    transition.before = kAgcResourceUsageShaderWrite;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    TRY(agcCmdTransitionResources(command, 1u, &transition));
    TRY(agcEndCommandBuffer(command));

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TRY(agcQueueSubmit(queue, &submit, fence));
    TRY(agcWaitFence(fence, UINT64_C(200000000)));
    puts("compute submission completed");
    exit_code = 0;

cleanup:
    if (command)
        (void)agcResetCommandBuffer(command);
    if (fence)
        (void)agcDestroyFence(fence);
    if (command)
        (void)agcDestroyCommandBuffer(command);
    if (output)
        (void)agcDestroyBuffer(output);
    if (pipeline)
        (void)agcDestroyComputePipeline(pipeline);
    if (shader)
        (void)agcDestroyShader(shader);
    if (queue)
        (void)agcDestroyQueue(queue);
    if (device)
        (void)agcDestroyDevice(device);
    return exit_code;
}
