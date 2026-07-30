/*
 * Native-runtime present-to-render transition probe.
 *
 * The application never obtains a GPU address or registers raw VideoOut
 * buffers.  A runtime-owned present chain retains one opaque scanout image,
 * waits on finite submission fences, and validates the image's typed state
 * before each flip.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "agc_error.h"
#include "openagc/runtime.h"
#include "gpu_credentials.h"

enum {
    kWidth = 1920u,
    kHeight = 1080u,
    kPixelCount = kWidth * kHeight,
    kCompletionTimeoutNs = 200000000u,
};

#ifndef AGC_PRESENT_STAGE
#define AGC_PRESENT_STAGE 0
#endif

static uint32_t g_pixels[kPixelCount];

static void report_result(const char *operation, int32_t result)
{
    printf("%s: 0x%08x (%s)\n", operation, (unsigned)result,
        agcErrorString(result));
}

static void fill_bars(void)
{
    static const uint32_t colors[8] = {
        UINT32_C(0xffbfbfbf), UINT32_C(0xff00bfbf),
        UINT32_C(0xffbf00bf), UINT32_C(0xff00bf00),
        UINT32_C(0xffbf0000), UINT32_C(0xff0000bf),
        UINT32_C(0xffbfbf00), UINT32_C(0xff101010),
    };
    uint32_t y;

    for (y = 0u; y < kHeight; ++y) {
        uint32_t x;
        for (x = 0u; x < kWidth; ++x)
            g_pixels[y * kWidth + x] = colors[(x * 8u) / kWidth];
    }
}

int main(void)
{
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcPresentChainDesc present_desc = AGC_PRESENT_CHAIN_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcImage images[2] = {NULL, NULL};
    AgcPresentChain present_chain = NULL;
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    bool passed = false;
    int32_t result;
    uint32_t frame;

    puts("=== OpenAGC native-runtime present-to-render probe ===");
    setbuf(stdout, NULL);
    printf("Guarded stage: %u\n", (unsigned)AGC_PRESENT_STAGE);
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
    result = agcCreateQueue(device, &queue_desc, &queue);
    report_result("agcCreateQueue(graphics)", result);
    if (result != AGC_OK)
        goto cleanup;
    image_desc.width = kWidth;
    image_desc.height = kHeight;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SCANOUT_BIT |
        AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    fill_bars();
    for (frame = 0u; frame < 2u; ++frame) {
        result = agcCreateImage(device, &image_desc, &images[frame]);
        report_result("agcCreateImage(scanout)", result);
        if (result != AGC_OK)
            goto cleanup;
        result = agcWriteImage(images[frame], 0u, g_pixels,
            sizeof(g_pixels));
        report_result("agcWriteImage(color bars)", result);
        if (result != AGC_OK)
            goto cleanup;
    }
    present_desc.image_count = 2u;
    present_desc.images = images;
    result = agcCreatePresentChain(device, &present_desc, &present_chain);
    report_result("agcCreatePresentChain", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_PRESENT_STAGE == 0
    passed = true;
    goto cleanup;
#endif

    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 256u;
    result = agcCreateCommandBuffer(device, &command_desc, &command);
    report_result("agcCreateCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &fence);
    report_result("agcCreateFence", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    transition.resource_type = kAgcResourceTypeImage;
    transition.image = images[0];
    transition.image_range =
        (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageHostWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerHost;

    result = agcBeginCommandBuffer(command);
    report_result("agcBeginCommandBuffer(initial)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdTransitionResources(command, 1u, &transition);
    report_result("transition undefined -> host-write", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageHostWrite;
    transition.after = kAgcResourceUsageVideoOutScanout;
    transition.after_owner = kAgcResourceOwnerGraphics;
    result = agcCmdTransitionResources(command, 1u, &transition);
    report_result("transition host-write -> scanout", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command);
    report_result("agcEndCommandBuffer(initial)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcQueueSubmit(queue, &submit, fence);
    report_result("agcQueueSubmit(initial scanout)", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_PRESENT_STAGE == 1
    result = agcWaitFence(fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(initial scanout)", result);
    passed = result == AGC_OK;
    goto cleanup;
#endif
    result = agcPresent(present_chain, 0u, 1u, fence,
        kCompletionTimeoutNs);
    report_result("agcPresent(initial scanout)", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_PRESENT_STAGE == 2
    passed = true;
    goto cleanup;
#endif

    result = agcResetCommandBuffer(command);
    report_result("agcResetCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcResetFence(fence);
    report_result("agcResetFence", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcBeginCommandBuffer(command);
    report_result("agcBeginCommandBuffer(present-to-render)", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageVideoOutScanout;
    transition.after = kAgcResourceUsageColorTarget;
    transition.before_owner = kAgcResourceOwnerGraphics;
    transition.after_owner = kAgcResourceOwnerGraphics;
    result = agcCmdTransitionResources(command, 1u, &transition);
    report_result("transition scanout -> color-target", result);
    if (result != AGC_OK)
        goto cleanup;
    transition.before = kAgcResourceUsageColorTarget;
    transition.after = kAgcResourceUsageVideoOutScanout;
    result = agcCmdTransitionResources(command, 1u, &transition);
    report_result("transition color-target -> scanout", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command);
    report_result("agcEndCommandBuffer(present-to-render)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcQueueSubmit(queue, &submit, fence);
    report_result("agcQueueSubmit(present-to-render)", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_PRESENT_STAGE == 3
    result = agcWaitFence(fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(present-to-render)", result);
    passed = result == AGC_OK;
    goto cleanup;
#endif
    result = agcPresent(present_chain, 0u, 2u, fence,
        kCompletionTimeoutNs);
    report_result("agcPresent(present-to-render)", result);
    if (result != AGC_OK)
        goto cleanup;
    puts("Presented one bounded frame after present-to-render round trip");
    passed = true;

cleanup:
    if (command)
        report_result("agcResetCommandBuffer(cleanup)",
            agcResetCommandBuffer(command));
    if (present_chain)
        report_result("agcDestroyPresentChain",
            agcDestroyPresentChain(present_chain));
    if (fence)
        report_result("agcDestroyFence", agcDestroyFence(fence));
    if (command)
        report_result("agcDestroyCommandBuffer",
            agcDestroyCommandBuffer(command));
    for (frame = 0u; frame < 2u; ++frame)
        if (images[frame])
            report_result("agcDestroyImage",
                agcDestroyImage(images[frame]));
    if (queue)
        report_result("agcDestroyQueue", agcDestroyQueue(queue));
    if (device)
        report_result("agcDestroyDevice", agcDestroyDevice(device));
    printf("PRESENT_STAGE_%u %s\n", (unsigned)AGC_PRESENT_STAGE,
        passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
