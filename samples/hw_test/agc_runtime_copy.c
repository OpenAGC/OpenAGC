/*
 * Native-runtime typed buffer-copy hardware sample.
 *
 * This validates the public agcCmdCopyBuffer path, including upload and
 * readback objects, explicit ownership/state transitions, DMA submission,
 * bounded fence completion, and byte-for-byte readback.  It deliberately
 * uses a small single-queue transfer only; compute-to-copy and copy-to-shader
 * remain separate qualification rows.
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

enum {
    kWordCount = 256u,
    kCompletionTimeoutNs = 200000000u,
};

static void report_result(const char *operation, int32_t result)
{
    printf("%s: 0x%08x (%s)\n", operation, (unsigned)result,
        agcErrorString(result));
}

static uint64_t hash_words(const uint32_t *words, uint32_t count)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t i;

    for (i = 0u; i < count; ++i) {
        hash ^= words[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int main(void)
{
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcBufferDesc source_desc = AGC_BUFFER_DESC_INIT;
    AgcBufferDesc destination_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcFenceInfo fence_info = AGC_FENCE_INFO_INIT;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcBuffer source = NULL;
    AgcBuffer destination = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    uint32_t source_words[kWordCount];
    uint32_t destination_words[kWordCount] = {0};
    bool submitted = false;
    bool completed = false;
    bool passed = false;
    int32_t result;
    uint32_t i;

    puts("=== OpenAGC native-runtime typed buffer copy sample ===");
    if (set_gpu_credentials() != 0) {
        puts("GPU credentials: FAIL");
        goto cleanup;
    }
    for (i = 0u; i < kWordCount; ++i)
        source_words[i] = UINT32_C(0xa5a55a5a) ^
            i * UINT32_C(0x9e3779b9);

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
    source_desc.size = sizeof(source_words);
    source_desc.usage = AGC_BUFFER_USAGE_TRANSFER_SRC_BIT;
    source_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    result = agcCreateBuffer(device, &source_desc, &source);
    report_result("agcCreateBuffer(source)", result);
    if (result != AGC_OK)
        goto cleanup;
    destination_desc.size = sizeof(destination_words);
    destination_desc.usage = AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    destination_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    result = agcCreateBuffer(device, &destination_desc, &destination);
    report_result("agcCreateBuffer(destination)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcWriteBuffer(source, 0u, source_words, sizeof(source_words));
    report_result("agcWriteBuffer(source)", result);
    if (result != AGC_OK)
        goto cleanup;

    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 128u;
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

    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = source;
    transition.buffer_size = sizeof(source_words);
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageHostWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerHost;
    result = agcCmdTransitionResources(command_buffer, 1u, &transition);
    report_result("agcCmdTransitionResources(source host-write)", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageHostWrite;
    transition.after = kAgcResourceUsageCopySource;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(command_buffer, 1u, &transition);
    report_result("agcCmdTransitionResources(source copy-source)", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.buffer = destination;
    transition.buffer_size = sizeof(destination_words);
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageCopyDestination;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(command_buffer, 1u, &transition);
    report_result("agcCmdTransitionResources(destination copy-destination)",
        result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdCopyBuffer(command_buffer, source, 0u, destination, 0u,
        sizeof(source_words));
    report_result("agcCmdCopyBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageCopyDestination;
    transition.after = kAgcResourceUsageHostRead;
    transition.before_owner = kAgcResourceOwnerCompute;
    transition.after_owner = kAgcResourceOwnerHost;
    result = agcCmdTransitionResources(command_buffer, 1u, &transition);
    report_result("agcCmdTransitionResources(destination host-read)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command_buffer);
    report_result("agcEndCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
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
    result = agcReadBuffer(destination, 0u, destination_words,
        sizeof(destination_words));
    report_result("agcReadBuffer(destination)", result);
    if (result != AGC_OK)
        goto cleanup;
    for (i = 0u; i < kWordCount; ++i) {
        if (source_words[i] != destination_words[i]) {
            printf("Copy mismatch at %u: source=0x%08x destination=0x%08x\n",
                i, source_words[i], destination_words[i]);
            goto cleanup;
        }
    }
    printf("Copy verification: words=%u source-fnv64=0x%016llx "
           "destination-fnv64=0x%016llx PASS\n", kWordCount,
        (unsigned long long)hash_words(source_words, kWordCount),
        (unsigned long long)hash_words(destination_words, kWordCount));
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
    if (destination) {
        result = agcDestroyBuffer(destination);
        report_result("agcDestroyBuffer(destination)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (source) {
        result = agcDestroyBuffer(source);
        report_result("agcDestroyBuffer(source)", result);
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
    printf("Native runtime typed buffer copy result: %s\n",
        passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
