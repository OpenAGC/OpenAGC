/*
 * OpenAGC native-runtime indexed-triangle tutorial.
 * Uses only installed public headers plus compiler-generated shader artifacts.
 */

#include "agc_error.h"
#include "openagc/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime_triangle_frag_reflection.h"
#include "runtime_triangle_frag_sb.h"
#include "runtime_triangle_vert_back_sb.h"
#include "runtime_triangle_vert_front_sb.h"
#include "runtime_triangle_vert_reflection.h"

typedef struct Vertex {
    float position[3];
    float color[3];
} Vertex;

static const Vertex kVertices[] = {
    {{-0.75f, -0.75f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 0.75f, -0.75f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{ 0.00f,  0.75f, 0.0f}, {0.0f, 0.0f, 1.0f}},
};
static const uint16_t kIndices[] = {0u, 1u, 2u};

#define TRY(call) do { \
    result = (call); \
    if (result != AGC_OK) { \
        fprintf(stderr, "%s: %s (0x%08x)\n", #call, \
            agcErrorString(result), (unsigned)result); \
        goto cleanup; \
    } \
} while (0)

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
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcResourceTransition transitions[4] = {
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
    AgcViewport viewport = AGC_VIEWPORT_INIT;
    AgcScissor scissor = AGC_SCISSOR_INIT;
    AgcShaderReflection vertex_reflection;
    AgcShaderReflection pixel_reflection;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcShader vertex = NULL;
    AgcShader pixel = NULL;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer vertex_buffer = NULL;
    AgcBuffer index_buffer = NULL;
    AgcImage targets_image[2] = {NULL, NULL};
    AgcCommandBuffer command = NULL;
    AgcFence fence = NULL;
    int32_t result = AGC_OK;
    int exit_code = 1;

    if (runtime_triangle_vert_reflection_bytes_size !=
            sizeof(vertex_reflection) ||
        runtime_triangle_frag_reflection_bytes_size !=
            sizeof(pixel_reflection)) {
        fputs("reflection ABI mismatch\n", stderr);
        return 1;
    }
    memcpy(&vertex_reflection, runtime_triangle_vert_reflection_bytes,
        sizeof(vertex_reflection));
    memcpy(&pixel_reflection, runtime_triangle_frag_reflection_bytes,
        sizeof(pixel_reflection));

    device_desc.required_capability_bits = AGC_RUNTIME_CAP_BASELINE;
    TRY(agcCreateDevice(&device_desc, &device));
    queue_desc.type = kAgcQueueGraphics;
    TRY(agcCreateQueue(device, &queue_desc, &queue));

    vertex_desc.stage = vertex_reflection.stage;
    vertex_desc.code = runtime_triangle_vert_back_data;
    vertex_desc.code_size = runtime_triangle_vert_back_data_len;
    vertex_desc.front_code = runtime_triangle_vert_front_data;
    vertex_desc.front_code_size = runtime_triangle_vert_front_data_len;
    vertex_desc.reflection = &vertex_reflection;
    TRY(agcCreateShader(device, &vertex_desc, &vertex));
    pixel_desc.stage = pixel_reflection.stage;
    pixel_desc.code = runtime_triangle_frag_data;
    pixel_desc.code_size = runtime_triangle_frag_data_len;
    pixel_desc.reflection = &pixel_reflection;
    TRY(agcCreateShader(device, &pixel_desc, &pixel));

    attachments[0].format = AGC_FORMAT_RGBA8_UNORM;
    attachments[1].format = AGC_FORMAT_RGBA8_UNORM;
    pipeline_desc.vertex_shader = vertex;
    pipeline_desc.pixel_shader = pixel;
    pipeline_desc.vertex_inputs = vertex_reflection.vertex_inputs;
    pipeline_desc.vertex_input_count = vertex_reflection.vertex_input_count;
    pipeline_desc.color_attachments = attachments;
    pipeline_desc.color_attachment_count = 2u;
    pipeline_desc.dynamic_state_mask = AGC_DYNAMIC_STATE_VIEWPORT_BIT |
        AGC_DYNAMIC_STATE_SCISSOR_BIT;
    TRY(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline));

    buffer_desc.size = sizeof(kVertices);
    buffer_desc.usage = AGC_BUFFER_USAGE_VERTEX_BIT;
    buffer_desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    TRY(agcCreateBuffer(device, &buffer_desc, &vertex_buffer));
    TRY(agcWriteBuffer(vertex_buffer, 0u, kVertices, sizeof(kVertices)));
    buffer_desc.size = sizeof(kIndices);
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    TRY(agcCreateBuffer(device, &buffer_desc, &index_buffer));
    TRY(agcWriteBuffer(index_buffer, 0u, kIndices, sizeof(kIndices)));

    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_TRANSFER_SRC_BIT;
    TRY(agcCreateImage(device, &image_desc, &targets_image[0]));
    TRY(agcCreateImage(device, &image_desc, &targets_image[1]));

    command_desc.queue_type = kAgcQueueGraphics;
    command_desc.capacity_dwords = 4096u;
    TRY(agcCreateCommandBuffer(device, &command_desc, &command));
    TRY(agcCreateFence(device, &fence_desc, &fence));

    transitions[0].resource_type = kAgcResourceTypeImage;
    transitions[0].image = targets_image[0];
    transitions[0].before = kAgcResourceUsageUndefined;
    transitions[0].after = kAgcResourceUsageColorTarget;
    transitions[0].before_owner = kAgcResourceOwnerHost;
    transitions[0].after_owner = kAgcResourceOwnerGraphics;
    transitions[1] = transitions[0];
    transitions[1].image = targets_image[1];
    transitions[2].resource_type = kAgcResourceTypeBuffer;
    transitions[2].buffer = vertex_buffer;
    transitions[2].buffer_size = sizeof(kVertices);
    transitions[2].before = kAgcResourceUsageUndefined;
    transitions[2].after = kAgcResourceUsageShaderRead;
    transitions[2].before_owner = kAgcResourceOwnerHost;
    transitions[2].after_owner = kAgcResourceOwnerGraphics;
    transitions[3] = transitions[2];
    transitions[3].buffer = index_buffer;
    transitions[3].buffer_size = sizeof(kIndices);

    TRY(agcBeginCommandBuffer(command));
    TRY(agcCmdTransitionResources(command, 4u, transitions));
    TRY(agcCmdBindGraphicsPipeline(command, pipeline));
    vertex_binding.buffer = vertex_buffer;
    vertex_binding.stride = sizeof(Vertex);
    TRY(agcCmdBindVertexBuffers(command, 1u, &vertex_binding));
    targets[0].image = targets_image[0];
    targets[1].image = targets_image[1];
    TRY(agcCmdBindColorTargets(command, 2u, targets));
    viewport.width = 64.0f;
    viewport.height = 64.0f;
    scissor.width = 64u;
    scissor.height = 64u;
    TRY(agcCmdSetViewport(command, &viewport));
    TRY(agcCmdSetScissor(command, &scissor));
    TRY(agcCmdBindIndexBuffer(command, index_buffer, 0u, kAgcIndexSize16));
    TRY(agcCmdDrawIndexed(command, 3u, 1u, 0u, 0, 0u));
    transitions[0].before = kAgcResourceUsageColorTarget;
    transitions[0].after = kAgcResourceUsageHostRead;
    transitions[0].before_owner = kAgcResourceOwnerGraphics;
    transitions[0].after_owner = kAgcResourceOwnerHost;
    transitions[1] = transitions[0];
    transitions[1].image = targets_image[1];
    TRY(agcCmdTransitionResources(command, 2u, transitions));
    TRY(agcEndCommandBuffer(command));

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command;
    TRY(agcQueueSubmit(queue, &submit, fence));
    TRY(agcWaitFence(fence, UINT64_C(200000000)));
    puts("indexed triangle submission completed");
    exit_code = 0;

cleanup:
    if (command)
        (void)agcResetCommandBuffer(command);
    if (fence)
        (void)agcDestroyFence(fence);
    if (command)
        (void)agcDestroyCommandBuffer(command);
    if (targets_image[1])
        (void)agcDestroyImage(targets_image[1]);
    if (targets_image[0])
        (void)agcDestroyImage(targets_image[0]);
    if (index_buffer)
        (void)agcDestroyBuffer(index_buffer);
    if (vertex_buffer)
        (void)agcDestroyBuffer(vertex_buffer);
    if (pipeline)
        (void)agcDestroyGraphicsPipeline(pipeline);
    if (pixel)
        (void)agcDestroyShader(pixel);
    if (vertex)
        (void)agcDestroyShader(vertex);
    if (queue)
        (void)agcDestroyQueue(queue);
    if (device)
        (void)agcDestroyDevice(device);
    return exit_code;
}
