/*
 * Native-runtime EOP-only hardware diagnostic.
 *
 * Records no application commands. On Prospero, agcQueueSubmit appends the
 * runtime-owned EOP completion write, isolating its memory and fence path from
 * shader, descriptor, and dispatch state. Do not hardware-qualify this probe
 * until it is explicitly deployed and its fence completes.
 */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "agc_error.h"
#include "openagc/runtime.h"
#include "gpu_credentials.h"

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

enum { kCompletionTimeoutNs = 200000000u };

static void report_result(const char *operation, int32_t result)
{
    printf("%s: 0x%08x (%s)\n", operation, (unsigned)result,
        agcErrorString(result));
}

int main(void)
{
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcDevice device = NULL;
    AgcQueue compute_queue = NULL;
    AgcQueue graphics_queue = NULL;
    AgcGpuLabel label = NULL;
    AgcCommandBuffer signal_command_buffer = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence signal_fence = NULL;
    AgcFence fence = NULL;
    bool signal_submitted = false;
    bool signal_completed = false;
    bool submitted = false;
    bool completed = false;
    bool passed = false;
    int32_t result;

    puts("=== OpenAGC native-runtime EOP-only sample ===");
    if (set_gpu_credentials() != 0) {
        puts("GPU credentials: FAIL");
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
    result = agcCreateQueue(device, &queue_desc, &compute_queue);
    report_result("agcCreateQueue(compute)", result);
    if (result != AGC_OK)
        goto cleanup;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 32u;
    result = agcCreateCommandBuffer(device, &command_desc,
        &signal_command_buffer);
    report_result("agcCreateCommandBuffer(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &signal_fence);
    report_result("agcCreateFence(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateGpuLabel(device, &label_desc, &label);
    report_result("agcCreateGpuLabel", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcBeginCommandBuffer(signal_command_buffer);
    report_result("agcBeginCommandBuffer(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdSignalGpuLabel(signal_command_buffer, label, 1u);
    report_result("agcCmdSignalGpuLabel", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(signal_command_buffer);
    report_result("agcEndCommandBuffer(signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffer_count = 1u;
    submit.command_buffers = &signal_command_buffer;
    result = agcQueueSubmit(compute_queue, &submit, signal_fence);
    report_result("agcQueueSubmit(compute signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    signal_submitted = true;
    queue_desc.type = kAgcQueueGraphics;
    result = agcCreateQueue(device, &queue_desc, &graphics_queue);
    report_result("agcCreateQueue(graphics)", result);
    if (result != AGC_OK)
        goto cleanup;
    command_desc.queue_type = kAgcQueueGraphics;
    result = agcCreateCommandBuffer(device, &command_desc, &command_buffer);
    report_result("agcCreateCommandBuffer(wait)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &fence);
    report_result("agcCreateFence(wait)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcBeginCommandBuffer(command_buffer);
    report_result("agcBeginCommandBuffer(wait)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdWaitGpuLabel(command_buffer, label, 1u);
    report_result("agcCmdWaitGpuLabel(graphics)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command_buffer);
    report_result("agcEndCommandBuffer(wait)", result);
    if (result != AGC_OK)
        goto cleanup;
    puts("Submitting compute signal then graphics GPU wait without CPU wait.");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    result = agcQueueSubmit(graphics_queue, &submit, fence);
    report_result("agcQueueSubmit(graphics wait)", result);
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
    if (graphics_queue) {
        result = agcDestroyQueue(graphics_queue);
        report_result("agcDestroyQueue(graphics)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (compute_queue) {
        result = agcDestroyQueue(compute_queue);
        report_result("agcDestroyQueue(compute)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (device) {
        result = agcDestroyDevice(device);
        report_result("agcDestroyDevice", result);
        if (result != AGC_OK)
            passed = false;
    }
    printf("Native runtime EOP-only result: %s\n", passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
