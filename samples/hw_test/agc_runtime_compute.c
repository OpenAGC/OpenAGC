/*
 * Native-runtime compute hardware sample.
 *
 * This is deliberately separate from agc_compute.c, whose manually assembled
 * command stream remains the existing hardware-qualified baseline.  This
 * sample consumes only compiler-emitted shader/reflection artifacts and the
 * public OpenAGC runtime object API.  Build it for cross-build validation;
 * do not treat it as hardware-qualified until it is explicitly deployed.
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

#include "shaders/fill_color_native_reflection.h"
#include "shaders/fill_color_native_sb.h"

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

enum {
    kOutputWordCount = 64u,
    kCompletionTimeoutNs = 200000000u,
    kFillColor = UINT32_C(0xff00ff00),
};

static void report_result(const char *operation, int32_t result)
{
    printf("%s: 0x%08x (%s)\n", operation, (unsigned)result,
        agcErrorString(result));
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
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcDescriptorWrite descriptor = AGC_DESCRIPTOR_WRITE_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcFenceInfo fence_info = AGC_FENCE_INFO_INIT;
    AgcGpuLabelInfo label_info = AGC_GPU_LABEL_INFO_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcShaderReflection reflection;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcShader shader = NULL;
    AgcComputePipeline pipeline = NULL;
    AgcBuffer output = NULL;
    AgcGpuLabel label = NULL;
    AgcCommandBuffer signal_command_buffer = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence signal_fence = NULL;
    AgcFence fence = NULL;
    uint32_t push_constants[2] = {kOutputWordCount, kFillColor};
    uint32_t output_words[kOutputWordCount] = {0};
    bool submitted = false;
    bool completed = false;
    bool signal_submitted = false;
    bool signal_completed = false;
    bool passed = false;
    int32_t result;

    printf("=== OpenAGC native-runtime compute sample ===\n");
    if (set_gpu_credentials() != 0) {
        puts("GPU credentials: FAIL");
        goto cleanup;
    }
    if (fill_color_native_reflection_bytes_size != sizeof(reflection)) {
        puts("Reflection artifact: unexpected ABI size");
        goto cleanup;
    }
    memcpy(&reflection, fill_color_native_reflection_bytes,
        sizeof(reflection));
    if (reflection.stage != kAgcShaderStageCs ||
        reflection.descriptor_mapping_count != 1u ||
        reflection.push_constant_size != sizeof(push_constants)) {
        puts("Reflection artifact: unexpected compute contract");
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
    report_result("agcCreateQueue", result);
    if (result != AGC_OK)
        goto cleanup;

    shader_desc.stage = reflection.stage;
    shader_desc.code = fill_color_native_data;
    shader_desc.code_size = fill_color_native_data_len;
    shader_desc.reflection = &reflection;
    result = agcCreateShader(device, &shader_desc, &shader);
    report_result("agcCreateShader", result);
    if (result != AGC_OK)
        goto cleanup;

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
    result = agcCreateComputePipeline(device, &pipeline_desc, &pipeline);
    report_result("agcCreateComputePipeline", result);
    if (result != AGC_OK)
        goto cleanup;

    buffer_desc.size = sizeof(output_words);
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &output);
    report_result("agcCreateBuffer", result);
    if (result != AGC_OK)
        goto cleanup;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 512u;
    result = agcCreateCommandBuffer(device, &command_desc, &command_buffer);
    report_result("agcCreateCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc,
        &signal_command_buffer);
    report_result("agcCreateCommandBuffer(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &fence);
    report_result("agcCreateFence", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &signal_fence);
    report_result("agcCreateFence(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    label_desc.initial_value = 0u;
    result = agcCreateGpuLabel(device, &label_desc, &label);
    report_result("agcCreateGpuLabel", result);
    if (result != AGC_OK)
        goto cleanup;

    result = agcBeginCommandBuffer(signal_command_buffer);
    report_result("agcBeginCommandBuffer(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdSignalGpuLabel(signal_command_buffer, label,
        UINT32_C(0x4c414245));
    report_result("agcCmdSignalGpuLabel", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(signal_command_buffer);
    report_result("agcEndCommandBuffer(signal)", result);
    if (result != AGC_OK)
        goto cleanup;

    result = agcBeginCommandBuffer(command_buffer);
    report_result("agcBeginCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdWaitGpuLabel(command_buffer, label,
        UINT32_C(0x4c414245));
    report_result("agcCmdWaitGpuLabel", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = output;
    transition.buffer_size = sizeof(output_words);
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(command_buffer, 1u, &transition);
    report_result("agcCmdTransitionResources(undefined-to-shader-write)",
        result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdBindComputePipeline(command_buffer, pipeline);
    report_result("agcCmdBindComputePipeline", result);
    if (result != AGC_OK)
        goto cleanup;
    descriptor.set = reflection.descriptor_mappings[0].set;
    descriptor.binding = reflection.descriptor_mappings[0].binding;
    descriptor.type = reflection.descriptor_mappings[0].type;
    descriptor.buffer = output;
    descriptor.buffer_range = sizeof(output_words);
    result = agcCmdBindDescriptors(command_buffer, 1u, &descriptor);
    report_result("agcCmdBindDescriptors", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdPushConstants(command_buffer, 1u << kAgcShaderStageCs,
        0u, sizeof(push_constants), push_constants);
    report_result("agcCmdPushConstants", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdDispatch(command_buffer, 1u, 1u, 1u);
    report_result("agcCmdDispatch", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageShaderWrite;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    result = agcCmdTransitionResources(command_buffer, 1u, &transition);
    report_result("agcCmdTransitionResources(shader-write-to-host-read)",
        result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command_buffer);
    report_result("agcEndCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffer_count = 1u;
    submit.command_buffers = &signal_command_buffer;
    result = agcQueueSubmit(queue, &submit, signal_fence);
    report_result("agcQueueSubmit(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    signal_submitted = true;
    submit.command_buffers = &command_buffer;
    result = agcQueueSubmit(queue, &submit, fence);
    report_result("agcQueueSubmit", result);
    if (result != AGC_OK)
        goto cleanup;
    submitted = true;
    result = agcWaitFence(fence, kCompletionTimeoutNs);
    report_result("agcWaitFence", result);
    if (result != AGC_OK)
        goto cleanup;
    completed = true;
    result = agcWaitFence(signal_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    signal_completed = true;
    result = agcGetGpuLabelInfo(label, &label_info);
    report_result("agcGetGpuLabelInfo", result);
    if (result != AGC_OK ||
        label_info.scheduled_value != UINT32_C(0x4c414245) ||
        label_info.observed_value != UINT32_C(0x4c414245) ||
        label_info.last_signal_submission_id == 0u) {
        puts("GPU label diagnostics: FAIL");
        goto cleanup;
    }
    printf("GPU label diagnostics: value=0x%08x submission=%llu\n",
        label_info.observed_value,
        (unsigned long long)label_info.last_signal_submission_id);
    result = agcGetFenceInfo(fence, &fence_info);
    report_result("agcGetFenceInfo", result);
    if (result != AGC_OK || fence_info.state != AGC_FENCE_STATE_SIGNALED ||
        fence_info.last_wait_result != AGC_OK ||
        fence_info.completion_value != fence_info.observed_completion_value) {
        puts("Fence diagnostics: FAIL");
        goto cleanup;
    }
    printf("Fence diagnostics: submission=%llu completed=%llu profile=%s\n",
        (unsigned long long)fence_info.submission_id,
        (unsigned long long)fence_info.last_completed_submission_id,
        fence_info.profile_name);
    result = agcReadBuffer(output, 0u, output_words, sizeof(output_words));
    report_result("agcReadBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    for (uint32_t i = 0u; i < kOutputWordCount; ++i) {
        if (output_words[i] != kFillColor) {
            printf("Output mismatch at %u: 0x%08x\n", i, output_words[i]);
            goto cleanup;
        }
    }
    puts("Output verification: PASS");
    passed = true;

cleanup:
    if (signal_command_buffer && (!signal_submitted || signal_completed)) {
        result = agcResetCommandBuffer(signal_command_buffer);
        report_result("agcResetCommandBuffer(signal)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (command_buffer && (!submitted || completed)) {
        result = agcResetCommandBuffer(command_buffer);
        report_result("agcResetCommandBuffer", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (fence) {
        result = agcDestroyFence(fence);
        report_result("agcDestroyFence", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (signal_fence) {
        result = agcDestroyFence(signal_fence);
        report_result("agcDestroyFence(signal)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (label) {
        result = agcDestroyGpuLabel(label);
        report_result("agcDestroyGpuLabel", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (signal_command_buffer) {
        result = agcDestroyCommandBuffer(signal_command_buffer);
        report_result("agcDestroyCommandBuffer(signal)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (command_buffer) {
        result = agcDestroyCommandBuffer(command_buffer);
        report_result("agcDestroyCommandBuffer", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (output) {
        result = agcDestroyBuffer(output);
        report_result("agcDestroyBuffer", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (pipeline) {
        result = agcDestroyComputePipeline(pipeline);
        report_result("agcDestroyComputePipeline", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (shader) {
        result = agcDestroyShader(shader);
        report_result("agcDestroyShader", result);
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
    printf("Native runtime compute result: %s\n", passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
