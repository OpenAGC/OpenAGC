/*
 * Native-runtime EOP-only hardware diagnostic.
 *
 * Uses no application shader or descriptor state. It first establishes a
 * compute-owned writable-resource state, then validates the v2 compute-to-
 * graphics RELEASE/ACQUIRE protocol: the source EOP release writes a label,
 * and the graphics queue waits for that label before its all-cache acquire.
 * AGC_IMAGE_HANDOFF selects an RGBA8 storage image; AGC_PARTIAL_HANDOFF
 * restricts the transfer to one buffer byte range or one image mip/layer.
 * The default remains the historically qualified whole storage-buffer oracle.
 * Do not hardware-qualify this probe until it is explicitly deployed and both
 * fences complete.
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

#ifndef AGC_IMAGE_HANDOFF
#define AGC_IMAGE_HANDOFF 0
#endif

#ifndef AGC_TIMELINE_WAIT
#define AGC_TIMELINE_WAIT 0
#endif

#ifndef AGC_PARTIAL_HANDOFF
#define AGC_PARTIAL_HANDOFF 0
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
#if AGC_IMAGE_HANDOFF
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
#else
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
#endif
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcResourceTransition handoff = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
#if AGC_TIMELINE_WAIT
    AgcGpuLabelInfo label_info = AGC_GPU_LABEL_INFO_INIT;
#endif
    AgcDevice device = NULL;
    AgcQueue compute_queue = NULL;
    AgcQueue graphics_queue = NULL;
    AgcGpuLabel label = NULL;
    AgcBuffer buffer = NULL;
    AgcImage image = NULL;
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

    puts(AGC_IMAGE_HANDOFF ?
        "=== OpenAGC native-runtime image handoff sample ===" :
        "=== OpenAGC native-runtime buffer handoff sample ===");
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
#if AGC_IMAGE_HANDOFF
    image_desc.width = 8u;
    image_desc.height = 8u;
#if AGC_PARTIAL_HANDOFF
    image_desc.mip_levels = 2u;
    image_desc.array_layers = 2u;
#endif
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_STORAGE_BIT |
        AGC_IMAGE_USAGE_SAMPLED_BIT;
    result = agcCreateImage(device, &image_desc, &image);
    report_result("agcCreateImage", result);
#else
    buffer_desc.size = AGC_PARTIAL_HANDOFF ? 128u : 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT |
        AGC_BUFFER_USAGE_TRANSFER_SRC_BIT | AGC_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &buffer);
    report_result("agcCreateBuffer", result);
#endif
    if (result != AGC_OK)
        goto cleanup;

#if AGC_IMAGE_HANDOFF
    transition.resource_type = kAgcResourceTypeImage;
    transition.image = image;
    transition.image_range.aspect_mask = AGC_IMAGE_ASPECT_COLOR_BIT;
    transition.image_range.mip_level_count = image_desc.mip_levels;
    transition.image_range.array_layer_count = image_desc.array_layers;
#else
    transition.resource_type = kAgcResourceTypeBuffer;
    transition.buffer = buffer;
    transition.buffer_size = buffer_desc.size;
#endif
    transition.before = kAgcResourceUsageUndefined;
    transition.after = kAgcResourceUsageShaderWrite;
    transition.before_owner = kAgcResourceOwnerHost;
    transition.after_owner = kAgcResourceOwnerCompute;
    result = agcBeginCommandBuffer(signal_command_buffer);
    report_result("agcBeginCommandBuffer(setup)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdTransitionResources(signal_command_buffer, 1u, &transition);
    report_result("agcCmdTransitionResources(setup)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(signal_command_buffer);
    report_result("agcEndCommandBuffer(setup)", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffer_count = 1u;
    submit.command_buffers = &signal_command_buffer;
    result = agcQueueSubmit(compute_queue, &submit, signal_fence);
    report_result("agcQueueSubmit(compute setup)", result);
    if (result != AGC_OK)
        goto cleanup;
    signal_submitted = true;
    result = agcWaitFence(signal_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(setup)", result);
    if (result != AGC_OK)
        goto cleanup;
    signal_completed = true;
    result = agcResetCommandBuffer(signal_command_buffer);
    report_result("agcResetCommandBuffer(setup)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcResetFence(signal_fence);
    report_result("agcResetFence(setup)", result);
    if (result != AGC_OK)
        goto cleanup;
    signal_submitted = false;
    signal_completed = false;

#if AGC_IMAGE_HANDOFF
    handoff.resource_type = kAgcResourceTypeImage;
    handoff.image = image;
    handoff.image_range.aspect_mask = AGC_IMAGE_ASPECT_COLOR_BIT;
    handoff.image_range.mip_level_count = image_desc.mip_levels;
    handoff.image_range.array_layer_count = image_desc.array_layers;
#if AGC_PARTIAL_HANDOFF
    handoff.image_range.base_mip_level = 1u;
    handoff.image_range.mip_level_count = 1u;
    handoff.image_range.base_array_layer = 1u;
    handoff.image_range.array_layer_count = 1u;
#endif
#else
    handoff.resource_type = kAgcResourceTypeBuffer;
    handoff.buffer = buffer;
    handoff.buffer_size = buffer_desc.size;
#if AGC_PARTIAL_HANDOFF
    handoff.buffer_offset = 32u;
    handoff.buffer_size = 64u;
#endif
#endif
    handoff.before = kAgcResourceUsageShaderWrite;
    handoff.after = kAgcResourceUsageShaderRead;
    handoff.before_owner = kAgcResourceOwnerCompute;
    handoff.after_owner = kAgcResourceOwnerGraphics;
    handoff.flags = AGC_RESOURCE_TRANSITION_RELEASE_BIT;
    handoff.dependency_label = label;
    handoff.dependency_value = 1u;
    result = agcBeginCommandBuffer(signal_command_buffer);
    report_result("agcBeginCommandBuffer(release)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdTransitionResources(signal_command_buffer, 1u, &handoff);
    report_result("agcCmdTransitionResources(release)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(signal_command_buffer);
    report_result("agcEndCommandBuffer(release)", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffers = &signal_command_buffer;
    result = agcQueueSubmit(compute_queue, &submit, signal_fence);
    report_result("agcQueueSubmit(compute release)", result);
    if (result != AGC_OK)
        goto cleanup;
    signal_submitted = true;
#if AGC_TIMELINE_WAIT
    result = agcWaitGpuLabel(label, 1u, kCompletionTimeoutNs);
    report_result("agcWaitGpuLabel(1)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcGetGpuLabelStatus(label, 1u);
    report_result("agcGetGpuLabelStatus(1)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcGetGpuLabelInfo(label, &label_info);
    report_result("agcGetGpuLabelInfo(timeline)", result);
    if (result != AGC_OK || label_info.scheduled_value != 1u ||
        label_info.observed_value < 1u || label_info.last_wait_value != 1u ||
        label_info.last_wait_result != AGC_OK) {
        puts("TIMELINE_WAIT FAIL");
        goto cleanup;
    }
    puts("TIMELINE_WAIT PASS");
#endif
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
    report_result("agcBeginCommandBuffer(acquire)", result);
    if (result != AGC_OK)
        goto cleanup;
    handoff.flags = AGC_RESOURCE_TRANSITION_ACQUIRE_BIT;
    result = agcCmdTransitionResources(command_buffer, 1u, &handoff);
    report_result("agcCmdTransitionResources(acquire)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command_buffer);
    report_result("agcEndCommandBuffer(acquire)", result);
    if (result != AGC_OK)
        goto cleanup;
    puts("Submitting compute release then graphics acquire without CPU wait.");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    result = agcQueueSubmit(graphics_queue, &submit, fence);
    report_result("agcQueueSubmit(graphics acquire)", result);
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
    if (buffer) {
        result = agcDestroyBuffer(buffer);
        report_result("agcDestroyBuffer", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (image) {
        result = agcDestroyImage(image);
        report_result("agcDestroyImage", result);
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
    printf("Native runtime %s%s handoff result: %s\n",
        AGC_PARTIAL_HANDOFF ? "partial " : "",
        AGC_IMAGE_HANDOFF ? "image" : "buffer", passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
