/* Native-runtime submission wait/signal-list hardware oracle. */

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
    AgcSubmitInfo list_submit = AGC_SUBMIT_INFO_V2_INIT;
    AgcGpuLabelPoint waits[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGpuLabelPoint signals[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcCommandBuffer producer = NULL;
    AgcCommandBuffer bridge = NULL;
    AgcCommandBuffer consumer = NULL;
    AgcFence producer_fence = NULL;
    AgcFence bridge_fence = NULL;
    AgcFence consumer_fence = NULL;
    AgcGpuLabel source = NULL;
    AgcGpuLabel destination = NULL;
    bool producer_submitted = false;
    bool bridge_submitted = false;
    bool consumer_submitted = false;
    bool producer_completed = false;
    bool bridge_completed = false;
    bool consumer_completed = false;
    bool passed = false;
    int32_t result;

    puts("=== OpenAGC native-runtime submit-list sample ===");
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
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 32u;
    result = agcCreateQueue(device, &queue_desc, &queue);
    report_result("agcCreateQueue", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &producer);
    report_result("agcCreateCommandBuffer(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &bridge);
    report_result("agcCreateCommandBuffer(bridge)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &consumer);
    report_result("agcCreateCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &producer_fence);
    report_result("agcCreateFence(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &bridge_fence);
    report_result("agcCreateFence(bridge)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &consumer_fence);
    report_result("agcCreateFence(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateGpuLabel(device, &label_desc, &source);
    report_result("agcCreateGpuLabel(source)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateGpuLabel(device, &label_desc, &destination);
    report_result("agcCreateGpuLabel(destination)", result);
    if (result != AGC_OK)
        goto cleanup;

    result = agcBeginCommandBuffer(producer);
    report_result("agcBeginCommandBuffer(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdSignalGpuLabel(producer, source, 1u);
    report_result("agcCmdSignalGpuLabel(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(producer);
    report_result("agcEndCommandBuffer(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffer_count = 1u;
    submit.command_buffers = &producer;
    result = agcQueueSubmit(queue, &submit, producer_fence);
    report_result("agcQueueSubmit(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    producer_submitted = true;

    waits[0].label = source;
    waits[0].value = 1u;
    signals[0].label = destination;
    signals[0].value = 2u;
    result = agcBeginCommandBuffer(bridge);
    report_result("agcBeginCommandBuffer(bridge)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(bridge);
    report_result("agcEndCommandBuffer(bridge)", result);
    if (result != AGC_OK)
        goto cleanup;
    list_submit.command_buffer_count = 1u;
    list_submit.command_buffers = &bridge;
    list_submit.wait_count = 1u;
    list_submit.waits = waits;
    list_submit.signal_count = 1u;
    list_submit.signals = signals;
    result = agcQueueSubmit(queue, &list_submit, bridge_fence);
    report_result("agcQueueSubmit(bridge wait/signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    bridge_submitted = true;

    result = agcBeginCommandBuffer(consumer);
    report_result("agcBeginCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(consumer);
    report_result("agcEndCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    list_submit.command_buffers = &consumer;
    list_submit.wait_count = 1u;
    list_submit.waits = signals;
    list_submit.signal_count = 0u;
    list_submit.signals = NULL;
    result = agcQueueSubmit(queue, &list_submit, consumer_fence);
    report_result("agcQueueSubmit(consumer wait)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_submitted = true;
    puts("Submitted producer, bridge wait/signal, and consumer wait without CPU waits.");
    result = agcWaitFence(consumer_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_completed = true;
    result = agcWaitFence(bridge_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(bridge)", result);
    if (result != AGC_OK)
        goto cleanup;
    bridge_completed = true;
    result = agcWaitFence(producer_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    producer_completed = true;
    passed = true;

cleanup:
    if (consumer && (!consumer_submitted || consumer_completed)) {
        result = agcResetCommandBuffer(consumer);
        report_result("agcResetCommandBuffer(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (bridge && (!bridge_submitted || bridge_completed)) {
        result = agcResetCommandBuffer(bridge);
        report_result("agcResetCommandBuffer(bridge)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (producer && (!producer_submitted || producer_completed)) {
        result = agcResetCommandBuffer(producer);
        report_result("agcResetCommandBuffer(producer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_fence) {
        result = agcDestroyFence(consumer_fence);
        report_result("agcDestroyFence(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (bridge_fence) {
        result = agcDestroyFence(bridge_fence);
        report_result("agcDestroyFence(bridge)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (producer_fence) {
        result = agcDestroyFence(producer_fence);
        report_result("agcDestroyFence(producer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (destination) {
        result = agcDestroyGpuLabel(destination);
        report_result("agcDestroyGpuLabel(destination)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (source) {
        result = agcDestroyGpuLabel(source);
        report_result("agcDestroyGpuLabel(source)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer) {
        result = agcDestroyCommandBuffer(consumer);
        report_result("agcDestroyCommandBuffer(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (bridge) {
        result = agcDestroyCommandBuffer(bridge);
        report_result("agcDestroyCommandBuffer(bridge)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (producer) {
        result = agcDestroyCommandBuffer(producer);
        report_result("agcDestroyCommandBuffer(producer)", result);
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
    printf("Native runtime submit-list result: %s\n", passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
