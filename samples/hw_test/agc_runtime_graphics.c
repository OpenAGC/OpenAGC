/*
 * Native-runtime graphics submission probe.
 *
 * This program deliberately uses only public OpenAGC runtime objects and the
 * compiler-emitted NGG vertex/fragment reflection sidecars. It is separate
 * from agc_graphics.c, whose manually assembled command stream remains the
 * hardware-qualified baseline. A completed fence proves only this probe's
 * submission path; do not call it a pixel-output qualification until an
 * explicit deployed readback or presentation oracle exists.
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

#include "shaders/runtime_triangle_frag_reflection.h"
#include "shaders/runtime_triangle_frag_sb.h"
#include "shaders/runtime_triangle_vert_back_sb.h"
#include "shaders/runtime_triangle_vert_front_sb.h"
#include "shaders/runtime_triangle_vert_reflection.h"

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

enum {
    kTargetWidth = 64u,
    kTargetHeight = 64u,
    kCompletionTimeoutNs = 200000000u,
};

typedef struct RuntimeVertex {
    float position[3];
    float color[3];
} RuntimeVertex;

_Static_assert(sizeof(RuntimeVertex) == 24u,
    "native runtime triangle vertex layout must remain 24 bytes");

static const RuntimeVertex kVertices[] = {
    {{-0.75f, -0.75f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 0.75f, -0.75f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{ 0.00f,  0.75f, 0.0f}, {0.0f, 0.0f, 1.0f}},
};
static const uint16_t kIndices[] = {0u, 1u, 2u};

static void report_result(const char *operation, int32_t result)
{
    printf("%s: 0x%08x (%s)\n", operation, (unsigned)result,
        agcErrorString(result));
}

int main(void)
{
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcShaderDesc vertex_desc = AGC_SHADER_DESC_INIT;
    AgcShaderDesc pixel_desc = AGC_SHADER_DESC_INIT;
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcColorBlendAttachmentState attachment =
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT;
    AgcDepthStencilPipelineState depth_stencil =
        AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcVertexBufferBinding vertex_binding = AGC_VERTEX_BUFFER_BINDING_INIT;
    AgcColorTargetBinding target = AGC_COLOR_TARGET_BINDING_INIT;
    AgcDepthStencilTargetBinding depth_target =
        AGC_DEPTH_STENCIL_TARGET_BINDING_INIT;
    AgcViewport viewport = AGC_VIEWPORT_INIT;
    AgcScissor scissor = AGC_SCISSOR_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcShaderReflection vertex_reflection;
    AgcShaderReflection pixel_reflection;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcShader vertex = NULL;
    AgcShader pixel = NULL;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer vertex_buffer = NULL;
    AgcBuffer index_buffer = NULL;
    AgcImage target_image = NULL;
    AgcImage depth_image = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    bool submitted = false;
    bool completed = false;
    bool passed = false;
    int32_t result;

    puts("=== OpenAGC native-runtime graphics probe ===");
    if (set_gpu_credentials() != 0) {
        puts("GPU credentials: FAIL");
        goto cleanup;
    }
    if (runtime_triangle_vert_reflection_bytes_size !=
            sizeof(vertex_reflection) ||
        runtime_triangle_frag_reflection_bytes_size !=
            sizeof(pixel_reflection)) {
        puts("Reflection artifact: unexpected ABI size");
        goto cleanup;
    }
    memcpy(&vertex_reflection, runtime_triangle_vert_reflection_bytes,
        sizeof(vertex_reflection));
    memcpy(&pixel_reflection, runtime_triangle_frag_reflection_bytes,
        sizeof(pixel_reflection));
    if (vertex_reflection.stage != kAgcShaderStageVs ||
        vertex_reflection.vertex_input_count != 2u ||
        pixel_reflection.stage != kAgcShaderStagePs ||
        pixel_reflection.color_export_count != 1u) {
        puts("Reflection artifact: unexpected graphics contract");
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
    report_result("agcCreateQueue", result);
    if (result != AGC_OK)
        goto cleanup;
    vertex_desc.stage = vertex_reflection.stage;
    vertex_desc.code = runtime_triangle_vert_back_data;
    vertex_desc.code_size = runtime_triangle_vert_back_data_len;
    vertex_desc.front_code = runtime_triangle_vert_front_data;
    vertex_desc.front_code_size = runtime_triangle_vert_front_data_len;
    vertex_desc.reflection = &vertex_reflection;
    result = agcCreateShader(device, &vertex_desc, &vertex);
    report_result("agcCreateShader(vertex)", result);
    if (result != AGC_OK)
        goto cleanup;
    pixel_desc.stage = pixel_reflection.stage;
    pixel_desc.code = runtime_triangle_frag_data;
    pixel_desc.code_size = runtime_triangle_frag_data_len;
    pixel_desc.reflection = &pixel_reflection;
    result = agcCreateShader(device, &pixel_desc, &pixel);
    report_result("agcCreateShader(fragment)", result);
    if (result != AGC_OK)
        goto cleanup;
    attachment.format = AGC_FORMAT_RGBA8_UNORM;
    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.vertex_inputs = vertex_reflection.vertex_inputs;
    pipeline_desc.vertex_input_count = vertex_reflection.vertex_input_count;
    pipeline_desc.color_attachments = &attachment;
    pipeline_desc.color_attachment_count = 1u;
    depth_stencil.format = AGC_FORMAT_D16_UNORM;
    pipeline_desc.depth_stencil = &depth_stencil;
    pipeline_desc.dynamic_state_mask = AGC_DYNAMIC_STATE_VIEWPORT_BIT |
        AGC_DYNAMIC_STATE_SCISSOR_BIT;
    result = agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline);
    report_result("agcCreateGraphicsPipeline", result);
    if (result != AGC_OK)
        goto cleanup;

    buffer_desc.size = sizeof(kVertices);
    buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &vertex_buffer);
    report_result("agcCreateBuffer(vertex)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcWriteBuffer(vertex_buffer, 0u, kVertices, sizeof(kVertices));
    report_result("agcWriteBuffer(vertex)", result);
    if (result != AGC_OK)
        goto cleanup;
    buffer_desc.size = sizeof(kIndices);
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &index_buffer);
    report_result("agcCreateBuffer(index)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcWriteBuffer(index_buffer, 0u, kIndices, sizeof(kIndices));
    report_result("agcWriteBuffer(index)", result);
    if (result != AGC_OK)
        goto cleanup;
    image_desc.width = kTargetWidth;
    image_desc.height = kTargetHeight;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    result = agcCreateImage(device, &image_desc, &target_image);
    report_result("agcCreateImage(color target)", result);
    if (result != AGC_OK)
        goto cleanup;
    image_desc.format = AGC_FORMAT_D16_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT;
    result = agcCreateImage(device, &image_desc, &depth_image);
    report_result("agcCreateImage(depth target)", result);
    if (result != AGC_OK)
        goto cleanup;

    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 512u;
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
    result = agcCmdBindGraphicsPipeline(command_buffer, pipeline);
    report_result("agcCmdBindGraphicsPipeline", result);
    if (result != AGC_OK)
        goto cleanup;
    vertex_binding.buffer = vertex_buffer;
    vertex_binding.stride = sizeof(RuntimeVertex);
    result = agcCmdBindVertexBuffers(command_buffer, 1u, &vertex_binding);
    report_result("agcCmdBindVertexBuffers", result);
    if (result != AGC_OK)
        goto cleanup;
    target.image = target_image;
    result = agcCmdBindColorTargets(command_buffer, 1u, &target);
    report_result("agcCmdBindColorTargets", result);
    if (result != AGC_OK)
        goto cleanup;
    depth_target.image = depth_image;
    result = agcCmdBindDepthStencilTarget(command_buffer, &depth_target);
    report_result("agcCmdBindDepthStencilTarget", result);
    if (result != AGC_OK)
        goto cleanup;
    viewport.width = (float)kTargetWidth;
    viewport.height = (float)kTargetHeight;
    scissor.width = kTargetWidth;
    scissor.height = kTargetHeight;
    result = agcCmdSetViewport(command_buffer, &viewport);
    report_result("agcCmdSetViewport", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdSetScissor(command_buffer, &scissor);
    report_result("agcCmdSetScissor", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdBindIndexBuffer(command_buffer, index_buffer, 0u,
        kAgcIndexSize16);
    report_result("agcCmdBindIndexBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdDrawIndexed(command_buffer, 3u, 1u, 0u, 0, 0u);
    report_result("agcCmdDrawIndexed", result);
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
    puts("Submission fence: PASS (no pixel-output oracle in this probe)");
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
    if (target_image) {
        result = agcDestroyImage(target_image);
        report_result("agcDestroyImage", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (depth_image) {
        result = agcDestroyImage(depth_image);
        report_result("agcDestroyImage(depth)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (index_buffer) {
        result = agcDestroyBuffer(index_buffer);
        report_result("agcDestroyBuffer(index)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (vertex_buffer) {
        result = agcDestroyBuffer(vertex_buffer);
        report_result("agcDestroyBuffer(vertex)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (pipeline) {
        result = agcDestroyGraphicsPipeline(pipeline);
        report_result("agcDestroyGraphicsPipeline", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (pixel) {
        result = agcDestroyShader(pixel);
        report_result("agcDestroyShader(fragment)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (vertex) {
        result = agcDestroyShader(vertex);
        report_result("agcDestroyShader(vertex)", result);
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
    printf("Native runtime graphics result: %s\n", passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
