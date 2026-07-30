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

#ifndef AGC_RENDER_TO_SHADER
#define AGC_RENDER_TO_SHADER 0
#endif

#if AGC_RENDER_TO_SHADER
#include "shaders/render_consume_native_reflection.h"
#include "shaders/render_consume_native_sb.h"
#endif

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

enum {
    kTargetWidth = 64u,
    kTargetHeight = 64u,
    kTargetPixelCount = kTargetWidth * kTargetHeight,
    kCompletionTimeoutNs = 200000000u,
};

static uint32_t g_target_pixels[kTargetPixelCount];
static uint32_t g_aux_target_pixels[kTargetPixelCount];
#if AGC_RENDER_TO_SHADER
static uint32_t g_consumer_pixels[kTargetPixelCount];
#endif

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
    AgcColorBlendAttachmentState attachments[2] = {
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT,
    };
    AgcDepthStencilPipelineState depth_stencil =
        AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition transitions[5] = {
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT,
        AGC_RESOURCE_TRANSITION_INIT,
    };
    AgcVertexBufferBinding vertex_binding = AGC_VERTEX_BUFFER_BINDING_INIT;
    AgcColorTargetBinding targets[2] = {
        AGC_COLOR_TARGET_BINDING_INIT,
        AGC_COLOR_TARGET_BINDING_INIT,
    };
    AgcDepthStencilTargetBinding depth_target =
        AGC_DEPTH_STENCIL_TARGET_BINDING_INIT;
    AgcViewport viewport = AGC_VIEWPORT_INIT;
    AgcScissor scissor = AGC_SCISSOR_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcShaderReflection vertex_reflection;
    AgcShaderReflection pixel_reflection;
#if AGC_RENDER_TO_SHADER
    AgcShaderDesc consumer_desc = AGC_SHADER_DESC_INIT;
    AgcComputePipelineDesc consumer_pipeline_desc =
        AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcGpuLabelDesc label_desc = AGC_GPU_LABEL_DESC_INIT;
    AgcDescriptorWrite consumer_writes[2] = {
        AGC_DESCRIPTOR_WRITE_INIT,
        AGC_DESCRIPTOR_WRITE_INIT,
    };
    AgcResourceTransition handoff = AGC_RESOURCE_TRANSITION_V2_INIT;
    AgcResourceTransition output_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcShaderReflection consumer_reflection;
#endif
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
#if AGC_RENDER_TO_SHADER
    AgcQueue compute_queue = NULL;
#endif
    AgcShader vertex = NULL;
    AgcShader pixel = NULL;
#if AGC_RENDER_TO_SHADER
    AgcShader consumer_shader = NULL;
#endif
    AgcGraphicsPipeline pipeline = NULL;
#if AGC_RENDER_TO_SHADER
    AgcComputePipeline consumer_pipeline = NULL;
#endif
    AgcBuffer vertex_buffer = NULL;
    AgcBuffer index_buffer = NULL;
#if AGC_RENDER_TO_SHADER
    AgcBuffer consumer_output = NULL;
#endif
    AgcImage first_target_image = NULL;
    AgcImage second_target_image = NULL;
    AgcImage depth_image = NULL;
#if AGC_RENDER_TO_SHADER
    AgcImageView consumer_view = NULL;
    AgcSampler consumer_sampler = NULL;
    AgcGpuLabel handoff_label = NULL;
#endif
    AgcCommandBuffer command_buffer = NULL;
    AgcCommandBuffer completion_command_buffer = NULL;
#if AGC_RENDER_TO_SHADER
    AgcCommandBuffer consumer_command_buffer = NULL;
#endif
    AgcCommandBuffer command_buffers[2];
    AgcFence fence = NULL;
#if AGC_RENDER_TO_SHADER
    AgcFence consumer_fence = NULL;
#endif
    bool submitted = false;
    bool completed = false;
#if AGC_RENDER_TO_SHADER
    bool consumer_submitted = false;
    bool consumer_completed = false;
#endif
    bool passed = false;
    uint32_t first_changed = 0u;
    uint32_t second_changed = 0u;
    uint32_t distinct_outputs = 0u;
#if AGC_RENDER_TO_SHADER
    uint32_t consumer_mismatches = 0u;
#endif
    int32_t result;

    puts(AGC_RENDER_TO_SHADER ?
        "=== OpenAGC native-runtime render-to-shader probe ===" :
        "=== OpenAGC native-runtime graphics probe ===");
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
#if AGC_RENDER_TO_SHADER
    if (render_consume_native_reflection_bytes_size !=
            sizeof(consumer_reflection)) {
        puts("Consumer reflection artifact: unexpected ABI size");
        goto cleanup;
    }
    memcpy(&consumer_reflection, render_consume_native_reflection_bytes,
        sizeof(consumer_reflection));
#endif
    if (vertex_reflection.stage != kAgcShaderStageVs ||
        vertex_reflection.vertex_input_count != 2u ||
        pixel_reflection.stage != kAgcShaderStagePs ||
        pixel_reflection.color_export_count != 2u) {
        puts("Reflection artifact: unexpected graphics contract");
        goto cleanup;
    }
#if AGC_RENDER_TO_SHADER
    if (consumer_reflection.stage != kAgcShaderStageCs ||
        consumer_reflection.descriptor_mapping_count != 2u ||
        consumer_reflection.local_size_x != 8u ||
        consumer_reflection.local_size_y != 8u) {
        puts("Consumer reflection artifact: unexpected compute contract");
        goto cleanup;
    }
#endif

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
#if AGC_RENDER_TO_SHADER
    queue_desc.type = kAgcQueueCompute;
    result = agcCreateQueue(device, &queue_desc, &compute_queue);
    report_result("agcCreateQueue(compute)", result);
    if (result != AGC_OK)
        goto cleanup;
#endif
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
    attachments[0].format = AGC_FORMAT_RGBA8_UNORM;
    attachments[1].format = AGC_FORMAT_RGBA8_UNORM;
    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.vertex_inputs = vertex_reflection.vertex_inputs;
    pipeline_desc.vertex_input_count = vertex_reflection.vertex_input_count;
    pipeline_desc.color_attachments = attachments;
    pipeline_desc.color_attachment_count = 2u;
    depth_stencil.format = AGC_FORMAT_D16_UNORM;
    depth_stencil.depth_test_enable = 1u;
    depth_stencil.depth_write_enable = 1u;
    pipeline_desc.depth_stencil = &depth_stencil;
    pipeline_desc.dynamic_state_mask = AGC_DYNAMIC_STATE_VIEWPORT_BIT |
        AGC_DYNAMIC_STATE_SCISSOR_BIT;
    result = agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline);
    report_result("agcCreateGraphicsPipeline", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_RENDER_TO_SHADER
    consumer_desc.stage = consumer_reflection.stage;
    consumer_desc.code = render_consume_native_data;
    consumer_desc.code_size = render_consume_native_data_len;
    consumer_desc.reflection = &consumer_reflection;
    result = agcCreateShader(device, &consumer_desc, &consumer_shader);
    report_result("agcCreateShader(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_pipeline_desc.shader = consumer_shader;
    consumer_pipeline_desc.local_size_x = consumer_reflection.local_size_x;
    consumer_pipeline_desc.local_size_y = consumer_reflection.local_size_y;
    consumer_pipeline_desc.local_size_z = consumer_reflection.local_size_z;
    consumer_pipeline_desc.descriptor_mappings =
        consumer_reflection.descriptor_mappings;
    consumer_pipeline_desc.descriptor_mapping_count =
        consumer_reflection.descriptor_mapping_count;
    result = agcCreateComputePipeline(device, &consumer_pipeline_desc,
        &consumer_pipeline);
    report_result("agcCreateComputePipeline(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
#endif

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
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_TRANSFER_SRC_BIT | AGC_IMAGE_USAGE_TRANSFER_DST_BIT
#if AGC_RENDER_TO_SHADER
        | AGC_IMAGE_USAGE_SAMPLED_BIT
#endif
        ;
    result = agcCreateImage(device, &image_desc, &first_target_image);
    report_result("agcCreateImage(color target 0)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateImage(device, &image_desc, &second_target_image);
    report_result("agcCreateImage(color target 1)", result);
    if (result != AGC_OK)
        goto cleanup;
    image_desc.format = AGC_FORMAT_D16_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT;
    result = agcCreateImage(device, &image_desc, &depth_image);
    report_result("agcCreateImage(depth target)", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_RENDER_TO_SHADER
    view_desc.image = first_target_image;
    view_desc.format = AGC_FORMAT_RGBA8_UNORM;
    result = agcCreateImageView(device, &view_desc, &consumer_view);
    report_result("agcCreateImageView(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateSampler(device, &sampler_desc, &consumer_sampler);
    report_result("agcCreateSampler(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    buffer_desc.size = sizeof(g_consumer_pixels);
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    result = agcCreateBuffer(device, &buffer_desc, &consumer_output);
    report_result("agcCreateBuffer(consumer output)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateGpuLabel(device, &label_desc, &handoff_label);
    report_result("agcCreateGpuLabel(handoff)", result);
    if (result != AGC_OK)
        goto cleanup;
#endif

    memset(g_target_pixels, 0xa5, sizeof(g_target_pixels));
    result = agcWriteImage(first_target_image, 0u, g_target_pixels,
        sizeof(g_target_pixels));
    report_result("agcWriteImage(color target 0)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcWriteImage(second_target_image, 0u, g_target_pixels,
        sizeof(g_target_pixels));
    report_result("agcWriteImage(color target 1)", result);
    if (result != AGC_OK)
        goto cleanup;

    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 4096u;
    result = agcCreateCommandBuffer(device, &command_desc, &command_buffer);
    report_result("agcCreateCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateCommandBuffer(device, &command_desc,
        &completion_command_buffer);
    report_result("agcCreateCommandBuffer(completion)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &fence);
    report_result("agcCreateFence", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_RENDER_TO_SHADER
    command_desc.queue_type = kAgcQueueCompute;
    result = agcCreateCommandBuffer(device, &command_desc,
        &consumer_command_buffer);
    report_result("agcCreateCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCreateFence(device, &fence_desc, &consumer_fence);
    report_result("agcCreateFence(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
#endif
    result = agcBeginCommandBuffer(command_buffer);
    report_result("agcBeginCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    transitions[0].resource_type = kAgcResourceTypeImage;
    transitions[0].image = first_target_image;
    transitions[0].before = kAgcResourceUsageUndefined;
    transitions[0].after = kAgcResourceUsageColorTarget;
    transitions[0].before_owner = kAgcResourceOwnerHost;
    transitions[0].after_owner = kAgcResourceOwnerGraphics;
    transitions[1] = transitions[0];
    transitions[1].image = second_target_image;
    transitions[2] = transitions[0];
    transitions[2].image = depth_image;
    transitions[2].after = kAgcResourceUsageDepthStencilWrite;
    transitions[2].image_range.aspect_mask = AGC_IMAGE_ASPECT_DEPTH_BIT;
    transitions[3].resource_type = kAgcResourceTypeBuffer;
    transitions[3].buffer = vertex_buffer;
    transitions[3].buffer_size = sizeof(kVertices);
    transitions[3].before = kAgcResourceUsageUndefined;
    transitions[3].after = kAgcResourceUsageShaderRead;
    transitions[3].before_owner = kAgcResourceOwnerHost;
    transitions[3].after_owner = kAgcResourceOwnerGraphics;
    transitions[4] = transitions[3];
    transitions[4].buffer = index_buffer;
    transitions[4].buffer_size = sizeof(kIndices);
    result = agcCmdTransitionResources(command_buffer, 5u, transitions);
    report_result("agcCmdTransitionResources(undefined-to-targets)", result);
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
    targets[0].image = first_target_image;
    targets[1].image = second_target_image;
    result = agcCmdBindColorTargets(command_buffer, 2u, targets);
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
#if AGC_RENDER_TO_SHADER
    handoff.resource_type = kAgcResourceTypeImage;
    handoff.image = first_target_image;
    handoff.before = kAgcResourceUsageColorTarget;
    handoff.after = kAgcResourceUsageShaderRead;
    handoff.before_owner = kAgcResourceOwnerGraphics;
    handoff.after_owner = kAgcResourceOwnerCompute;
    handoff.flags = AGC_RESOURCE_TRANSITION_RELEASE_BIT;
    handoff.image_range.aspect_mask = AGC_IMAGE_ASPECT_COLOR_BIT;
    handoff.image_range.mip_level_count = 1u;
    handoff.image_range.array_layer_count = 1u;
    handoff.dependency_label = handoff_label;
    handoff.dependency_value = 1u;
    result = agcCmdTransitionResources(command_buffer, 1u, &handoff);
    report_result("agcCmdTransitionResources(render release)", result);
    if (result != AGC_OK)
        goto cleanup;
#endif
    transitions[0].before = kAgcResourceUsageColorTarget;
    transitions[0].after = kAgcResourceUsageHostRead;
    transitions[0].before_owner = kAgcResourceOwnerGraphics;
    transitions[0].after_owner = kAgcResourceOwnerHost;
    transitions[1] = transitions[0];
    transitions[1].image = second_target_image;
#if AGC_RENDER_TO_SHADER
    result = agcCmdTransitionResources(command_buffer, 1u, &transitions[1]);
    report_result("agcCmdTransitionResources(aux-to-host-read)", result);
#else
    result = agcCmdTransitionResources(command_buffer, 2u, transitions);
    report_result("agcCmdTransitionResources(color-to-host-read)", result);
#endif
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(command_buffer);
    report_result("agcEndCommandBuffer", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcBeginCommandBuffer(completion_command_buffer);
    report_result("agcBeginCommandBuffer(completion)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdBindGraphicsPipeline(completion_command_buffer, pipeline);
    report_result("agcCmdBindGraphicsPipeline(completion)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(completion_command_buffer);
    report_result("agcEndCommandBuffer(completion)", result);
    if (result != AGC_OK)
        goto cleanup;
    command_buffers[0] = command_buffer;
    command_buffers[1] = completion_command_buffer;
    submit.command_buffer_count = 2u;
    submit.command_buffers = command_buffers;
    result = agcQueueSubmit(queue, &submit, fence);
    report_result("agcQueueSubmit", result);
    if (result != AGC_OK)
        goto cleanup;
    submitted = true;
#if AGC_RENDER_TO_SHADER
    result = agcBeginCommandBuffer(consumer_command_buffer);
    report_result("agcBeginCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    handoff.flags = AGC_RESOURCE_TRANSITION_ACQUIRE_BIT;
    result = agcCmdTransitionResources(consumer_command_buffer, 1u, &handoff);
    report_result("agcCmdTransitionResources(render acquire)", result);
    if (result != AGC_OK)
        goto cleanup;
    output_transition.resource_type = kAgcResourceTypeBuffer;
    output_transition.buffer = consumer_output;
    output_transition.buffer_size = sizeof(g_consumer_pixels);
    output_transition.before = kAgcResourceUsageUndefined;
    output_transition.after = kAgcResourceUsageShaderWrite;
    output_transition.before_owner = kAgcResourceOwnerHost;
    output_transition.after_owner = kAgcResourceOwnerCompute;
    result = agcCmdTransitionResources(consumer_command_buffer, 1u,
        &output_transition);
    report_result("agcCmdTransitionResources(consumer output)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdBindComputePipeline(consumer_command_buffer,
        consumer_pipeline);
    report_result("agcCmdBindComputePipeline(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_writes[0].set = consumer_reflection.descriptor_mappings[0].set;
    consumer_writes[0].binding =
        consumer_reflection.descriptor_mappings[0].binding;
    consumer_writes[0].type =
        consumer_reflection.descriptor_mappings[0].type;
    consumer_writes[0].image_view = consumer_view;
    consumer_writes[0].sampler = consumer_sampler;
    consumer_writes[1].set = consumer_reflection.descriptor_mappings[1].set;
    consumer_writes[1].binding =
        consumer_reflection.descriptor_mappings[1].binding;
    consumer_writes[1].type =
        consumer_reflection.descriptor_mappings[1].type;
    consumer_writes[1].buffer = consumer_output;
    consumer_writes[1].buffer_range = sizeof(g_consumer_pixels);
    result = agcCmdBindDescriptors(consumer_command_buffer, 2u,
        consumer_writes);
    report_result("agcCmdBindDescriptors(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdDispatch(consumer_command_buffer,
        kTargetWidth / consumer_reflection.local_size_x,
        kTargetHeight / consumer_reflection.local_size_y, 1u);
    report_result("agcCmdDispatch(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    output_transition.before = kAgcResourceUsageShaderWrite;
    output_transition.after = kAgcResourceUsageHostRead;
    output_transition.before_owner = kAgcResourceOwnerCompute;
    output_transition.after_owner = kAgcResourceOwnerHost;
    result = agcCmdTransitionResources(consumer_command_buffer, 1u,
        &output_transition);
    report_result("agcCmdTransitionResources(consumer readback)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(consumer_command_buffer);
    report_result("agcEndCommandBuffer(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffer_count = 1u;
    submit.command_buffers = &consumer_command_buffer;
    result = agcQueueSubmit(compute_queue, &submit, consumer_fence);
    report_result("agcQueueSubmit(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_submitted = true;
    puts("Submitted render release and shader consumer without a CPU wait.");
#endif
    result = agcWaitFence(fence, kCompletionTimeoutNs);
    report_result("agcWaitFence", result);
    if (result != AGC_OK)
        goto cleanup;
    completed = true;
#if AGC_RENDER_TO_SHADER
    result = agcWaitFence(consumer_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(consumer)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_completed = true;
    result = agcResetCommandBuffer(consumer_command_buffer);
    report_result("agcResetCommandBuffer(consumer dispatch)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcResetFence(consumer_fence);
    report_result("agcResetFence(consumer dispatch)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_submitted = false;
    consumer_completed = false;

    transitions[0] = (AgcResourceTransition)AGC_RESOURCE_TRANSITION_INIT;
    transitions[0].resource_type = kAgcResourceTypeImage;
    transitions[0].image = first_target_image;
    transitions[0].before = kAgcResourceUsageShaderRead;
    transitions[0].after = kAgcResourceUsageHostRead;
    transitions[0].before_owner = kAgcResourceOwnerCompute;
    transitions[0].after_owner = kAgcResourceOwnerHost;
    result = agcBeginCommandBuffer(consumer_command_buffer);
    report_result("agcBeginCommandBuffer(image readback)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcCmdTransitionResources(consumer_command_buffer, 1u,
        &transitions[0]);
    report_result("agcCmdTransitionResources(image readback)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcEndCommandBuffer(consumer_command_buffer);
    report_result("agcEndCommandBuffer(image readback)", result);
    if (result != AGC_OK)
        goto cleanup;
    submit.command_buffers = &consumer_command_buffer;
    result = agcQueueSubmit(compute_queue, &submit, consumer_fence);
    report_result("agcQueueSubmit(image readback)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_submitted = true;
    result = agcWaitFence(consumer_fence, kCompletionTimeoutNs);
    report_result("agcWaitFence(image readback)", result);
    if (result != AGC_OK)
        goto cleanup;
    consumer_completed = true;
#endif
    result = agcReadImage(first_target_image, 0u, g_target_pixels,
        sizeof(g_target_pixels));
    report_result("agcReadImage(color target 0)", result);
    if (result != AGC_OK)
        goto cleanup;
    result = agcReadImage(second_target_image, 0u, g_aux_target_pixels,
        sizeof(g_aux_target_pixels));
    report_result("agcReadImage(color target 1)", result);
    if (result != AGC_OK)
        goto cleanup;
#if AGC_RENDER_TO_SHADER
    result = agcReadBuffer(consumer_output, 0u, g_consumer_pixels,
        sizeof(g_consumer_pixels));
    report_result("agcReadBuffer(consumer output)", result);
    if (result != AGC_OK)
        goto cleanup;
#endif
    for (uint32_t i = 0u; i < kTargetPixelCount; ++i) {
        first_changed += g_target_pixels[i] != UINT32_C(0xa5a5a5a5);
        second_changed += g_aux_target_pixels[i] != UINT32_C(0xa5a5a5a5);
        distinct_outputs += g_target_pixels[i] != g_aux_target_pixels[i];
#if AGC_RENDER_TO_SHADER
        consumer_mismatches +=
            g_consumer_pixels[i] != g_target_pixels[i];
#endif
    }
    printf("MRT readback: target0=%u target1=%u distinct=%u\n",
        first_changed, second_changed, distinct_outputs);
    if (first_changed <= 256u || second_changed != first_changed ||
        distinct_outputs <= 256u) {
        puts("MRT readback: FAIL");
        goto cleanup;
    }
    puts("MRT readback: PASS");
#if AGC_RENDER_TO_SHADER
    printf("Render-to-shader readback: mismatches=%u\n", consumer_mismatches);
    if (consumer_mismatches != 0u) {
        puts("Render-to-shader readback: FAIL");
        goto cleanup;
    }
    puts("Render-to-shader readback: PASS");
#endif
    passed = true;

cleanup:
    if (command_buffer && (!submitted || completed)) {
        result = agcResetCommandBuffer(command_buffer);
        report_result("agcResetCommandBuffer", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (completion_command_buffer && (!submitted || completed)) {
        result = agcResetCommandBuffer(completion_command_buffer);
        report_result("agcResetCommandBuffer(completion)", result);
        if (result != AGC_OK)
            passed = false;
    }
#if AGC_RENDER_TO_SHADER
    if (consumer_command_buffer &&
        (!consumer_submitted || consumer_completed)) {
        result = agcResetCommandBuffer(consumer_command_buffer);
        report_result("agcResetCommandBuffer(consumer cleanup)", result);
        if (result != AGC_OK)
            passed = false;
    }
#endif
    if (fence) {
        result = agcDestroyFence(fence);
        report_result("agcDestroyFence", result);
        if (result != AGC_OK)
            passed = false;
    }
#if AGC_RENDER_TO_SHADER
    if (consumer_fence) {
        result = agcDestroyFence(consumer_fence);
        report_result("agcDestroyFence(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
#endif
    if (command_buffer) {
        result = agcDestroyCommandBuffer(command_buffer);
        report_result("agcDestroyCommandBuffer", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (completion_command_buffer) {
        result = agcDestroyCommandBuffer(completion_command_buffer);
        report_result("agcDestroyCommandBuffer(completion)", result);
        if (result != AGC_OK)
            passed = false;
    }
#if AGC_RENDER_TO_SHADER
    if (consumer_command_buffer) {
        result = agcDestroyCommandBuffer(consumer_command_buffer);
        report_result("agcDestroyCommandBuffer(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (handoff_label) {
        result = agcDestroyGpuLabel(handoff_label);
        report_result("agcDestroyGpuLabel(handoff)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_sampler) {
        result = agcDestroySampler(consumer_sampler);
        report_result("agcDestroySampler(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_view) {
        result = agcDestroyImageView(consumer_view);
        report_result("agcDestroyImageView(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
#endif
    if (second_target_image) {
        result = agcDestroyImage(second_target_image);
        report_result("agcDestroyImage(color target 1)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (first_target_image) {
        result = agcDestroyImage(first_target_image);
        report_result("agcDestroyImage(color target 0)", result);
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
#if AGC_RENDER_TO_SHADER
    if (consumer_output) {
        result = agcDestroyBuffer(consumer_output);
        report_result("agcDestroyBuffer(consumer output)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (consumer_pipeline) {
        result = agcDestroyComputePipeline(consumer_pipeline);
        report_result("agcDestroyComputePipeline(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
#endif
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
#if AGC_RENDER_TO_SHADER
    if (consumer_shader) {
        result = agcDestroyShader(consumer_shader);
        report_result("agcDestroyShader(consumer)", result);
        if (result != AGC_OK)
            passed = false;
    }
    if (compute_queue) {
        result = agcDestroyQueue(compute_queue);
        report_result("agcDestroyQueue(compute)", result);
        if (result != AGC_OK)
            passed = false;
    }
#endif
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
    printf("Native runtime %s result: %s\n",
        AGC_RENDER_TO_SHADER ? "render-to-shader" : "graphics",
        passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
