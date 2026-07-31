/* Native-runtime batch submission plus deferred-retirement stress gate. */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "agc_error.h"
#include "openagc/runtime.h"
#include "gpu_credentials.h"

enum {
    kCycles = 32u,
    kCompletionTimeoutNs = 200000000u,
};

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

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
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcMemoryStats baseline = AGC_MEMORY_STATS_INIT;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcCommandBuffer commands[2] = {NULL, NULL};
    AgcFence fence = NULL;
    AgcGpuLabel labels[2] = {NULL, NULL};
    AgcBuffer buffer = NULL;
    AgcImage image = NULL;
    bool submitted = false;
    bool completed = false;
    bool buffer_retired = false;
    bool image_retired = false;
    bool passed = false;
    uint32_t cycle;
    int32_t result;

    setbuf(stdout, NULL);
    puts("=== OpenAGC batch/deferred-retirement stress gate ===");
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
    report_result("agcCreateQueue(compute)", result);
    if (result != AGC_OK)
        goto cleanup;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 256u;
    result = agcCreateCommandBuffer(device, &command_desc, &commands[0]);
    report_result("agcCreateCommandBuffer(first)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc, &commands[1]);
    report_result("agcCreateCommandBuffer(second)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &fence);
    report_result("agcCreateFence", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateGpuLabel(device, &label_desc, &labels[0]);
    report_result("agcCreateGpuLabel(first)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateGpuLabel(device, &label_desc, &labels[1]);
    report_result("agcCreateGpuLabel(second)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcGetMemoryStats(device, &baseline);
    report_result("agcGetMemoryStats(baseline)", result);
    if (result != AGC_OK)
        goto cleanup;

    submit.command_buffer_count = 2u;
    submit.command_buffers = commands;
    buffer_desc.size = 4096u;
    buffer_desc.usage = AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_TRANSFER_SRC_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;

    for (cycle = 0u; cycle < kCycles; ++cycle) {
        AgcResourceTransition first[2] = {
            AGC_RESOURCE_TRANSITION_INIT,
            AGC_RESOURCE_TRANSITION_INIT,
        };
        AgcResourceTransition second[2] = {
            AGC_RESOURCE_TRANSITION_V2_INIT,
            AGC_RESOURCE_TRANSITION_V2_INIT,
        };
        AgcMemoryStats stats = AGC_MEMORY_STATS_INIT;

        submitted = false;
        completed = false;
        buffer_retired = false;
        image_retired = false;
        result = agcCreateBuffer(device, &buffer_desc, &buffer);
        if (result != AGC_OK) {
            report_result("agcCreateBuffer", result);
            goto cleanup;
        }
        result = agcCreateImage(device, &image_desc, &image);
        if (result != AGC_OK) {
            report_result("agcCreateImage", result);
            goto cleanup;
        }
        first[0].resource_type = kAgcResourceTypeBuffer;
        first[0].buffer = buffer;
        first[0].buffer_size = buffer_desc.size;
        first[0].before = kAgcResourceUsageUndefined;
        first[0].after = kAgcResourceUsageCopyDestination;
        first[0].before_owner = kAgcResourceOwnerHost;
        first[0].after_owner = kAgcResourceOwnerCompute;
        first[1].resource_type = kAgcResourceTypeImage;
        first[1].image = image;
        first[1].image_range =
            (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
        first[1].before = kAgcResourceUsageUndefined;
        first[1].after = kAgcResourceUsageCopyDestination;
        first[1].before_owner = kAgcResourceOwnerHost;
        first[1].after_owner = kAgcResourceOwnerCompute;
        second[0].resource_type = kAgcResourceTypeBuffer;
        second[0].buffer = buffer;
        second[0].buffer_size = buffer_desc.size;
        second[0].before = kAgcResourceUsageCopyDestination;
        second[0].after = kAgcResourceUsageHostRead;
        second[0].before_owner = kAgcResourceOwnerCompute;
        second[0].after_owner = kAgcResourceOwnerHost;
        second[0].flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;
        second[1].resource_type = kAgcResourceTypeImage;
        second[1].image = image;
        second[1].image_range =
            (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
        second[1].before = kAgcResourceUsageCopyDestination;
        second[1].after = kAgcResourceUsageHostRead;
        second[1].before_owner = kAgcResourceOwnerCompute;
        second[1].after_owner = kAgcResourceOwnerHost;
        second[1].flags = AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT;

        result = agcBeginCommandBuffer(commands[0]);
        if (result == AGC_OK)
            result = agcCmdTransitionResources(commands[0], 2u, first);
        if (result == AGC_OK)
            result = agcCmdSignalGpuLabel(commands[0], labels[0], cycle + 1u);
        if (result == AGC_OK)
            result = agcEndCommandBuffer(commands[0]);
        if (result != AGC_OK) {
            report_result("record first batch command", result);
            goto cleanup;
        }
        result = agcBeginCommandBuffer(commands[1]);
        if (result == AGC_OK)
            result = agcCmdTransitionResources(commands[1], 2u, second);
        if (result == AGC_OK)
            result = agcCmdSignalGpuLabel(commands[1], labels[1], cycle + 1u);
        if (result == AGC_OK)
            result = agcEndCommandBuffer(commands[1]);
        if (result != AGC_OK) {
            report_result("record second batch command", result);
            goto cleanup;
        }
        result = agcQueueSubmit(queue, &submit, fence);
        if (result != AGC_OK) {
            report_result("agcQueueSubmit(batch)", result);
            goto cleanup;
        }
        submitted = true;
        result = agcDestroyBufferDeferred(buffer, fence);
        if (result == AGC_OK) {
            buffer_retired = true;
            result = agcDestroyImageDeferred(image, fence);
        }
        if (result == AGC_OK)
            image_retired = true;
        if (result != AGC_OK) {
            report_result("queue deferred retirement", result);
            goto cleanup;
        }
        result = agcWaitFence(fence, kCompletionTimeoutNs);
        if (result != AGC_OK) {
            report_result("agcWaitFence(batch)", result);
            goto cleanup;
        }
        completed = true;
        result = agcCollectDeferredFrees(device);
        if (result != AGC_ERROR_BUSY) {
            report_result("pre-reset collection expected busy", result);
            goto cleanup;
        }
        result = agcRecycleCommandBuffers(fence, 2u, commands);
        if (result != AGC_OK) {
            report_result("agcRecycleCommandBuffers(batch)", result);
            goto cleanup;
        }
        result = agcCollectDeferredFrees(device);
        if (result != AGC_OK) {
            report_result("agcCollectDeferredFrees", result);
            goto cleanup;
        }
        buffer = NULL;
        image = NULL;
        buffer_retired = false;
        image_retired = false;
        result = agcGetMemoryStats(device, &stats);
        if (result != AGC_OK || stats.deferred_free_count != 0u ||
            stats.live_allocation_count != baseline.live_allocation_count ||
            stats.live_bytes != baseline.live_bytes) {
            report_result("memory baseline check", result);
            printf("cycle=%u deferred=%llu live=%llu/%llu bytes=%llu/%llu\n",
                cycle, (unsigned long long)stats.deferred_free_count,
                (unsigned long long)stats.live_allocation_count,
                (unsigned long long)baseline.live_allocation_count,
                (unsigned long long)stats.live_bytes,
                (unsigned long long)baseline.live_bytes);
            goto cleanup;
        }
        result = agcResetFence(fence);
        if (result != AGC_OK) {
            report_result("agcResetFence", result);
            goto cleanup;
        }
        submitted = false;
        completed = false;
        if ((cycle + 1u) % 8u == 0u)
            printf("Completed %u/%u retirement cycles\n", cycle + 1u,
                kCycles);
    }
    passed = true;

cleanup:
    if (commands[1] && (!submitted || completed))
        (void)agcResetCommandBuffer(commands[1]);
    if (commands[0] && (!submitted || completed))
        (void)agcResetCommandBuffer(commands[0]);
    if ((buffer_retired || image_retired) && completed) {
        if (agcCollectDeferredFrees(device) == AGC_OK) {
            if (buffer_retired)
                buffer = NULL;
            if (image_retired)
                image = NULL;
            buffer_retired = false;
            image_retired = false;
        }
    }
    if (image && !image_retired)
        report_result("agcDestroyImage(cleanup)", agcDestroyImage(image));
    if (buffer && !buffer_retired)
        report_result("agcDestroyBuffer(cleanup)", agcDestroyBuffer(buffer));
    if (labels[1]) {
        result = agcDestroyGpuLabel(labels[1]);
        report_result("agcDestroyGpuLabel(second)", result);
        if (result != AGC_OK) passed = false;
    }
    if (labels[0]) {
        result = agcDestroyGpuLabel(labels[0]);
        report_result("agcDestroyGpuLabel(first)", result);
        if (result != AGC_OK) passed = false;
    }
    if (fence) {
        result = agcDestroyFence(fence);
        report_result("agcDestroyFence", result);
        if (result != AGC_OK) passed = false;
    }
    if (commands[1]) {
        result = agcDestroyCommandBuffer(commands[1]);
        report_result("agcDestroyCommandBuffer(second)", result);
        if (result != AGC_OK) passed = false;
    }
    if (commands[0]) {
        result = agcDestroyCommandBuffer(commands[0]);
        report_result("agcDestroyCommandBuffer(first)", result);
        if (result != AGC_OK) passed = false;
    }
    if (queue) {
        result = agcDestroyQueue(queue);
        report_result("agcDestroyQueue", result);
        if (result != AGC_OK) passed = false;
    }
    if (device) {
        result = agcDestroyDevice(device);
        report_result("agcDestroyDevice", result);
        if (result != AGC_OK) passed = false;
    }
    puts(passed ? "BATCH_RETIREMENT_STRESS PASS" :
        "BATCH_RETIREMENT_STRESS FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
