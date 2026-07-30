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
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
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
    result = agcCreateQueue(device, &queue_desc, &queue);
    report_result("agcCreateQueue", result);
    if (result != AGC_OK)
        goto cleanup;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 32u;
    result = agcCreateCommandBuffer(device, &command_desc, &command_buffer);
    report_result("agcCreateCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &fence);
    report_result("agcCreateFence", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcBeginCommandBuffer(command_buffer);
    report_result("agcBeginCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command_buffer);
    report_result("agcEndCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    puts("Submitting no application commands; runtime supplies EOP completion.");
    submit.command_buffer_count = 1u;
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
    passed = true;

cleanup:
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
    if (command_buffer) {
        result = agcDestroyCommandBuffer(command_buffer);
        report_result("agcDestroyCommandBuffer", result);
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
    printf("Native runtime EOP-only result: %s\n", passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
