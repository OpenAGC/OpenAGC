/* Native-runtime graphics batch submission wait/signal-list hardware oracle. */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "agc_error.h"
#include "openagc/runtime.h"
#include "gpu_credentials.h"

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
    AgcSubmitInfo batch_submit = AGC_SUBMIT_INFO_V2_INIT;
    AgcSubmitInfo consumer_submit = AGC_SUBMIT_INFO_V2_INIT;
    AgcGpuLabelPoint waits[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcGpuLabelPoint signals[1] = { AGC_GPU_LABEL_POINT_INIT };
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcCommandBuffer producer = NULL;
    AgcCommandBuffer batch_first = NULL;
    AgcCommandBuffer batch_last = NULL;
    AgcCommandBuffer consumer = NULL;
    AgcCommandBuffer batch_commands[2] = { NULL, NULL };
    AgcFence producer_fence = NULL;
    AgcFence batch_fence = NULL;
    AgcFence consumer_fence = NULL;
    AgcGpuLabel source = NULL;
    AgcGpuLabel destination = NULL;
    AgcGpuLabel first_body = NULL;
    AgcGpuLabel last_body = NULL;
    bool producer_submitted = false;
    bool batch_submitted = false;
    bool consumer_submitted = false;
    bool producer_completed = false;
    bool batch_completed = false;
    bool consumer_completed = false;
    bool passed = false;
    int32_t result;

    puts("=== OpenAGC graphics batch submit-list sample ===");
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

    queue_desc.type = kAgcQueueGraphics;
    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 32u;
    result = agcCreateQueue(device, &queue_desc, &queue);
    report_result("agcCreateQueue", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &producer);
    report_result("agcCreateCommandBuffer(producer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &batch_first);
    report_result("agcCreateCommandBuffer(batch first)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &batch_last);
    report_result("agcCreateCommandBuffer(batch last)", result);
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
    result = agcCreateFence(device, &fence_desc, &batch_fence);
    report_result("agcCreateFence(batch)", result);
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
    result = agcCreateGpuLabel(device, &label_desc, &first_body);
    report_result("agcCreateGpuLabel(first body)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateGpuLabel(device, &label_desc, &last_body);
    report_result("agcCreateGpuLabel(last body)", result);
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

    result = agcBeginCommandBuffer(batch_first);
    report_result("agcBeginCommandBuffer(batch first)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdSignalGpuLabel(batch_first, first_body, 1u);
    report_result("agcCmdSignalGpuLabel(batch first body)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(batch_first);
    report_result("agcEndCommandBuffer(batch first)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcBeginCommandBuffer(batch_last);
    report_result("agcBeginCommandBuffer(batch last)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdSignalGpuLabel(batch_last, last_body, 1u);
    report_result("agcCmdSignalGpuLabel(batch last body)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(batch_last);
    report_result("agcEndCommandBuffer(batch last)", result);
    if (result != AGC_OK)
        goto cleanup;

    waits[0].label = source;
    waits[0].value = 1u;
    signals[0].label = destination;
    signals[0].value = 2u;
    batch_commands[0] = batch_first;
    batch_commands[1] = batch_last;
    batch_submit.command_buffer_count = 2u;
    batch_submit.command_buffers = batch_commands;
    batch_submit.wait_count = 1u;
    batch_submit.waits = waits;
    batch_submit.signal_count = 1u;
    batch_submit.signals = signals;
    result = agcQueueSubmit(queue, &batch_submit, batch_fence);
    report_result("agcQueueSubmit(graphics batch wait/signal)", result);
    if (result != AGC_OK)
        goto cleanup;
    batch_submitted = true;

    result = agcBeginCommandBuffer(consumer);
    report_result("agcBeginCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(consumer);
    report_result("agcEndCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_submit.command_buffer_count = 1u;
    consumer_submit.command_buffers = &consumer;
    consumer_submit.wait_count = 1u;
    consumer_submit.waits = signals;
    result = agcQueueSubmit(queue, &consumer_submit, consumer_fence);
    report_result("agcQueueSubmit(consumer wait)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_submitted = true;
    puts("Submitted producer, two-command graphics batch wait/signal, and consumer without CPU waits.");
    result = agcWaitFence(consumer_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_completed = true;
    result = agcWaitFence(batch_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(batch)", result);
    if (result != AGC_OK)
        goto cleanup;
    batch_completed = true;
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
    if (batch_last && (!batch_submitted || batch_completed)) {
        result = agcResetCommandBuffer(batch_last);
        report_result("agcResetCommandBuffer(batch last)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (batch_first && (!batch_submitted || batch_completed)) {
        result = agcResetCommandBuffer(batch_first);
        report_result("agcResetCommandBuffer(batch first)", result);
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
    if (batch_fence) {
        result = agcDestroyFence(batch_fence);
        report_result("agcDestroyFence(batch)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (producer_fence) {
        result = agcDestroyFence(producer_fence);
        report_result("agcDestroyFence(producer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (last_body) {
        result = agcDestroyGpuLabel(last_body);
        report_result("agcDestroyGpuLabel(last body)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (first_body) {
        result = agcDestroyGpuLabel(first_body);
        report_result("agcDestroyGpuLabel(first body)", result);
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
    if (batch_last) {
        result = agcDestroyCommandBuffer(batch_last);
        report_result("agcDestroyCommandBuffer(batch last)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (batch_first) {
        result = agcDestroyCommandBuffer(batch_first);
        report_result("agcDestroyCommandBuffer(batch first)", result);
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
    printf("Native runtime graphics batch submit-list result: %s\n",
        passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
