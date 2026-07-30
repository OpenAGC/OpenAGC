/*
 * Native-runtime compute-to-copy-to-shader hardware sample.
 *
 * Three ordered compute DCBs exercise the public API's same-queue batch
 * dependency carrier: a reflected producer writes a storage buffer, the
 * runtime copies it, then a reflected consumer reads that copied buffer and
 * writes a readback result.  The only CPU observation occurs after the single
 * bounded batch fence.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "agc_error.h"
#include "openagc/runtime.h"
#include "gpu_credentials.h"

#include "shaders/copy_consume_native_reflection.h"
#include "shaders/copy_consume_native_sb.h"
#include "shaders/fill_color_native_reflection.h"
#include "shaders/fill_color_native_sb.h"

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

enum {
    kWordCount = 64u,
    kCompletionTimeoutNs = 200000000u,
    kFillColor = UINT32_C(0xff40b0ff),
};

static void report_result(const char *operation, int32_t result)
{
    printf("%s: 0x%08x (%s)\n", operation, (unsigned)result,
        agcErrorString(result));
}

static int load_reflection(AgcShaderReflection *reflection,
    const uint8_t *bytes, unsigned long size, const char *name)
{
    if (size != sizeof(*reflection)) {
        printf("%s reflection: unexpected ABI size\n", name);
        return 0;
    }
    memcpy(reflection, bytes, sizeof(*reflection));
    if (reflection->stage != kAgcShaderStageCs) {
        printf("%s reflection: expected compute stage\n", name);
        return 0;
    }
    return 1;
}

int main(void)
{
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcShaderDesc shader_desc = AGC_SHADER_DESC_INIT;
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceTransition dependency = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcDescriptorWrite writes[2] = {
        AGC_DESCRIPTOR_WRITE_INIT, AGC_DESCRIPTOR_WRITE_INIT
    };
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcFenceInfo fence_info = AGC_FENCE_INFO_INIT;
    AgcShaderReflection producer_reflection;
    AgcShaderReflection consumer_reflection;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcShader producer_shader = NULL;
    AgcShader consumer_shader = NULL;
    AgcComputePipeline producer_pipeline = NULL;
    AgcComputePipeline consumer_pipeline = NULL;
    AgcBuffer produced = NULL;
    AgcBuffer copied = NULL;
    AgcBuffer output = NULL;
    AgcCommandBuffer producer_command = NULL;
    AgcCommandBuffer copy_command = NULL;
    AgcCommandBuffer consumer_command = NULL;
    AgcCommandBuffer commands[3] = { NULL, NULL, NULL };
    AgcFence fence = NULL;
    uint32_t producer_constants[2] = {kWordCount, kFillColor};
    uint32_t consumer_constants[1] = {kWordCount};
    uint32_t output_words[kWordCount] = {0};
    bool submitted = false;
    bool completed = false;
    bool passed = false;
    int32_t result;
    uint32_t i;

    puts("=== OpenAGC native-runtime compute-copy-shader sample ===");
    if (set_gpu_credentials() != 0) {
        puts("GPU credentials: FAIL");
        goto cleanup;
    }
    if (!load_reflection(&producer_reflection, fill_color_native_reflection_bytes,
            fill_color_native_reflection_bytes_size, "producer") ||
        !load_reflection(&consumer_reflection, copy_consume_native_reflection_bytes,
            copy_consume_native_reflection_bytes_size, "consumer") ||
        producer_reflection.descriptor_mapping_count != 1u ||
        producer_reflection.push_constant_size != sizeof(producer_constants) ||
        consumer_reflection.descriptor_mapping_count != 2u ||
        consumer_reflection.push_constant_size != sizeof(consumer_constants)) {
        puts("Reflection artifacts: unexpected contract");
        goto cleanup;
    }

    device_desc.required_capability_bits = AGC_RUNTIME_CAP_BASELINE;
    result = agcCreateDevice(&device_desc, &device);
    report_result("agcCreateDevice", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcGetRuntimeInfo(device, &runtime_info);
    report_result("agcGetRuntimeInfo", result);
    if (result != AGC_OK)
        goto cleanup;
    printf("Runtime profile: %s (FW ABI 0x%04x)\n",
        runtime_info.profile_name, runtime_info.firmware_abi_key);

    queue_desc.type = kAgcQueueCompute;
    result = agcCreateQueue(device, &queue_desc, &queue);
    report_result("agcCreateQueue(compute)", result);
    if (result != AGC_OK)
        goto cleanup;
    shader_desc.stage = producer_reflection.stage;
    shader_desc.code = fill_color_native_data;
    shader_desc.code_size = fill_color_native_data_len;
    shader_desc.reflection = &producer_reflection;
    result = agcCreateShader(device, &shader_desc, &producer_shader);
    report_result("agcCreateShader(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    pipeline_desc.shader = producer_shader;
    pipeline_desc.local_size_x = producer_reflection.local_size_x;
    pipeline_desc.local_size_y = producer_reflection.local_size_y;
    pipeline_desc.local_size_z = producer_reflection.local_size_z;
    pipeline_desc.descriptor_mapping_count =
        producer_reflection.descriptor_mapping_count;
    pipeline_desc.descriptor_mappings = producer_reflection.descriptor_mappings;
    pipeline_desc.push_constant_range_count =
        producer_reflection.push_constant_range_count;
    pipeline_desc.push_constant_ranges = producer_reflection.push_constant_ranges;
    result = agcCreateComputePipeline(device, &pipeline_desc, &producer_pipeline);
    report_result("agcCreateComputePipeline(producer)", result);
    if (result != AGC_OK)
        goto cleanup;

    shader_desc = (AgcShaderDesc)AGC_SHADER_DESC_INIT;
    shader_desc.stage = consumer_reflection.stage;
    shader_desc.code = copy_consume_native_data;
    shader_desc.code_size = copy_consume_native_data_len;
    shader_desc.reflection = &consumer_reflection;
    result = agcCreateShader(device, &shader_desc, &consumer_shader);
    report_result("agcCreateShader(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    pipeline_desc = (AgcComputePipelineDesc)AGC_COMPUTE_PIPELINE_DESC_INIT;
    pipeline_desc.shader = consumer_shader;
    pipeline_desc.local_size_x = consumer_reflection.local_size_x;
    pipeline_desc.local_size_y = consumer_reflection.local_size_y;
    pipeline_desc.local_size_z = consumer_reflection.local_size_z;
    pipeline_desc.descriptor_mapping_count =
        consumer_reflection.descriptor_mapping_count;
    pipeline_desc.descriptor_mappings = consumer_reflection.descriptor_mappings;
    pipeline_desc.push_constant_range_count =
        consumer_reflection.push_constant_range_count;
    pipeline_desc.push_constant_ranges = consumer_reflection.push_constant_ranges;
    result = agcCreateComputePipeline(device, &pipeline_desc, &consumer_pipeline);
    report_result("agcCreateComputePipeline(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;

    buffer_desc.size = sizeof(output_words);
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT |
        AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &produced);
    report_result("agcCreateBuffer(produced)", result);
    if (result != AGC_OK)
        goto cleanup;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT |
        AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &copied);
    report_result("agcCreateBuffer(copied)", result);
    if (result != AGC_OK)
        goto cleanup;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &output);
    report_result("agcCreateBuffer(output)", result);
    if (result != AGC_OK)
        goto cleanup;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 1024u;
    result = agcCreateCommandBuffer(device, &command_desc, &producer_command);
    report_result("agcCreateCommandBuffer(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &copy_command);
    report_result("agcCreateCommandBuffer(copy)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &consumer_command);
    report_result("agcCreateCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &fence);
    report_result("agcCreateFence", result);
    if (result != AGC_OK)
        goto cleanup;

    result = agcBeginCommandBuffer(producer_command);
    report_result("agcBeginCommandBuffer(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = produced;
    transition.buffer_size = sizeof(output_words);
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(producer_command, 1u, &transition);
    report_result("agcCmdTransitionResources(produced shader-write)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdBindComputePipeline(producer_command, producer_pipeline);
    report_result("agcCmdBindComputePipeline(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    writes[0].set = producer_reflection.descriptor_mappings[0].set;
    writes[0].binding = producer_reflection.descriptor_mappings[0].binding;
    writes[0].type = producer_reflection.descriptor_mappings[0].type;
    writes[0].buffer = produced;
    writes[0].buffer_range = sizeof(output_words);
    result = agcCmdBindDescriptors(producer_command, 1u, writes);
    report_result("agcCmdBindDescriptors(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdPushConstants(producer_command, 1u << kAgcShaderStageCs,
        0u, sizeof(producer_constants), producer_constants);
    report_result("agcCmdPushConstants(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdDispatch(producer_command, 1u, 1u, 1u);
    report_result("agcCmdDispatch(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(producer_command);
    report_result("agcEndCommandBuffer(producer)", result);
    if (result != AGC_OK)
        goto cleanup;

    result = agcBeginCommandBuffer(copy_command);
    report_result("agcBeginCommandBuffer(copy)", result);
    if (result != AGC_OK)
        goto cleanup;
    dependency.resource_type = kAgcResourceTypeBuffer;
    dependency.buffer = produced;
    dependency.buffer_size = sizeof(output_words);
    dependency.before = kAgcResourceUsageShaderWrite;
    dependency.after = kAgcResourceUsageCopySource;
    dependency.before_owner = kAgcResourceOwnerCompute;
    dependency.after_owner = kAgcResourceOwnerCompute;
    dependency.flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;
    result = agcCmdTransitionResources(copy_command, 1u, &dependency);
    report_result("agcCmdTransitionResources(shader-write-to-copy-source)",
        result);
    if (result != AGC_OK)
        goto cleanup;
    transition = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT;
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = copied;
    transition.buffer_size = sizeof(output_words);
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageCopyDestination;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(copy_command, 1u, &transition);
    report_result("agcCmdTransitionResources(copied copy-destination)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdCopyBuffer(copy_command, produced, 0u, copied, 0u,
        sizeof(output_words));
    report_result("agcCmdCopyBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageCopyDestination;
    transition.after = kAgcResourceUsageShaderRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(copy_command, 1u, &transition);
    report_result("agcCmdTransitionResources(copied shader-read)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(copy_command);
    report_result("agcEndCommandBuffer(copy)", result);
    if (result != AGC_OK)
        goto cleanup;

    result = agcBeginCommandBuffer(consumer_command);
    report_result("agcBeginCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    dependency = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_V2_INIT;
    dependency.resource_type = kAgcResourceTypeBuffer;
    dependency.buffer = copied;
    dependency.buffer_size = sizeof(output_words);
    dependency.before = kAgcResourceUsageShaderRead;
    dependency.after = kAgcResourceUsageShaderRead;
    dependency.before_owner = kAgcResourceOwnerCompute;
    dependency.after_owner = kAgcResourceOwnerCompute;
    dependency.flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;
    result = agcCmdTransitionResources(consumer_command, 1u, &dependency);
    report_result("agcCmdTransitionResources(shader-read dependency)", result);
    if (result != AGC_OK)
        goto cleanup;
    transition = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT;
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = output;
    transition.buffer_size = sizeof(output_words);
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(consumer_command, 1u, &transition);
    report_result("agcCmdTransitionResources(output shader-write)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdBindComputePipeline(consumer_command, consumer_pipeline);
    report_result("agcCmdBindComputePipeline(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    for (i = 0u; i < 2u; ++i) {
        writes[i] = (AgcDescriptorWrite)AGC_DESCRIPTOR_WRITE_INIT;
        writes[i].set = consumer_reflection.descriptor_mappings[i].set;
        writes[i].binding = consumer_reflection.descriptor_mappings[i].binding;
        writes[i].type = consumer_reflection.descriptor_mappings[i].type;
        writes[i].buffer = i == 0u ? copied : output;
        writes[i].buffer_range = sizeof(output_words);
    }
    result = agcCmdBindDescriptors(consumer_command, 2u, writes);
    report_result("agcCmdBindDescriptors(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdPushConstants(consumer_command, 1u << kAgcShaderStageCs,
        0u, sizeof(consumer_constants), consumer_constants);
    report_result("agcCmdPushConstants(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdDispatch(consumer_command, 1u, 1u, 1u);
    report_result("agcCmdDispatch(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageShaderWrite;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    result = agcCmdTransitionResources(consumer_command, 1u, &transition);
    report_result("agcCmdTransitionResources(output host-read)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(consumer_command);
    report_result("agcEndCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;

    commands[0] = producer_command;
    commands[1] = copy_command;
    commands[2] = consumer_command;
    submit.command_buffer_count = 3u;
    submit.command_buffers = commands;
    result = agcQueueSubmit(queue, &submit, fence);
    report_result("agcQueueSubmit(compute-copy-shader batch)", result);
    if (result != AGC_OK)
        goto cleanup;
    submitted = true;
    result = agcWaitFence(fence, kCompletionTimeoutNs);
    report_result("agcWaitFence", result);
    if (result != AGC_OK)
        goto cleanup;
    completed = true;
    result = agcGetFenceInfo(fence, &fence_info);
    report_result("agcGetFenceInfo", result);
    if (result != AGC_OK || fence_info.state != AGC_FENCE_STATE_SIGNALED ||
        fence_info.last_wait_result != AGC_OK ||
        fence_info.completion_value != fence_info.observed_completion_value) {
        puts("Fence diagnostics: FAIL");
        goto cleanup;
    }
    result = agcReadBuffer(output, 0u, output_words, sizeof(output_words));
    report_result("agcReadBuffer(output)", result);
    if (result != AGC_OK)
        goto cleanup;
    for (i = 0u; i < kWordCount; ++i) {
        if (output_words[i] != kFillColor) {
            printf("Output mismatch at %u: 0x%08x\n", i, output_words[i]);
            goto cleanup;
        }
    }
    printf("Compute-copy-shader verification: words=%u color=0x%08x PASS\n",
        kWordCount, kFillColor);
    passed = true;

cleanup:
    if (producer_command && (!submitted || completed)) {
        result = agcResetCommandBuffer(producer_command);
        report_result("agcResetCommandBuffer(producer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (copy_command && (!submitted || completed)) {
        result = agcResetCommandBuffer(copy_command);
        report_result("agcResetCommandBuffer(copy)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_command && (!submitted || completed)) {
        result = agcResetCommandBuffer(consumer_command);
        report_result("agcResetCommandBuffer(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (fence) {
        result = agcDestroyFence(fence);
        report_result("agcDestroyFence", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (producer_command) {
        result = agcDestroyCommandBuffer(producer_command);
        report_result("agcDestroyCommandBuffer(producer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (copy_command) {
        result = agcDestroyCommandBuffer(copy_command);
        report_result("agcDestroyCommandBuffer(copy)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_command) {
        result = agcDestroyCommandBuffer(consumer_command);
        report_result("agcDestroyCommandBuffer(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (output) {
        result = agcDestroyBuffer(output);
        report_result("agcDestroyBuffer(output)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (copied) {
        result = agcDestroyBuffer(copied);
        report_result("agcDestroyBuffer(copied)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (produced) {
        result = agcDestroyBuffer(produced);
        report_result("agcDestroyBuffer(produced)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_pipeline) {
        result = agcDestroyComputePipeline(consumer_pipeline);
        report_result("agcDestroyComputePipeline(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (producer_pipeline) {
        result = agcDestroyComputePipeline(producer_pipeline);
        report_result("agcDestroyComputePipeline(producer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_shader) {
        result = agcDestroyShader(consumer_shader);
        report_result("agcDestroyShader(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (producer_shader) {
        result = agcDestroyShader(producer_shader);
        report_result("agcDestroyShader(producer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (queue) {
        result = agcDestroyQueue(queue);
        report_result("agcDestroyQueue", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (device) {
        result = agcDestroyDevice(device);
        report_result("agcDestroyDevice", result);
        if (result != AGC_OK)
            passed = false;
    }
    printf("Native runtime compute-copy-shader result: %s\n",
        passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
