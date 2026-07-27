#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_graphics.h"
#include "agc_shader.h"
#include "agc_texture.h"
#include "agcdriver.h"

#include "ps5_platform.h"
#include "shaders/cube_ngg_front_sb.h"
#include "shaders/cube_ngg_back_sb.h"
#include "shaders/cube_frag_sb.h"

#define CUBE_FRAME_COUNT 3600u
#define CUBE_FENCE_TIMEOUT_US 2000000u
#define CUBE_DCB_BYTES 0x4000u
#define CUBE_SLOT_BYTES 0x800000u
#define CUBE_FRAME_BASE 0x10000u
#define CUBE_POOL_BYTES 0x1900000u
#define CUBE_FRONT_CODE_OFFSET 0x0000u
#define CUBE_BACK_CODE_OFFSET 0x4000u
#define CUBE_PIXEL_CODE_OFFSET 0x8000u
#define CUBE_VERTEX_COUNT 24u
#define CUBE_INDEX_COUNT 36u

typedef struct CubeVertex {
    float position[3];
    float color[3];
} CubeVertex;

typedef struct ParsedShader {
    AgcShaderRecord record;
    const uint8_t *code;
    size_t code_size;
    const AgcRegisterValue *sh_registers;
    uint32_t num_sh_registers;
    const AgcRegisterValue *cx_registers;
    uint32_t num_cx_registers;
    const AgcShaderSpecials *specials;
} ParsedShader;

typedef struct CubeFrame {
    uint32_t *render_target;
    uint32_t *dcb;
    CubeVertex *vertices;
    uint16_t *indices;
    AgcGfx1013BufferDescriptor *vertex_descriptor;
    volatile uint32_t *fence;
    uint32_t submitted_fence;
} CubeFrame;

typedef struct CubePoint {
    float x;
    float y;
    float z;
} CubePoint;

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1u) / alignment * alignment;
}

static bool shader_range_valid(uint64_t offset, uint64_t size, size_t blob_size)
{
    return offset >= sizeof(AgcShaderRecord) &&
        offset <= blob_size && size <= blob_size - offset;
}

static bool parse_shader(
    ParsedShader *out, const uint8_t *blob, size_t blob_size, const char *name)
{
    const AgcShaderRecord *source;
    uint32_t num_inputs = 0u;
    uint32_t num_outputs = 0u;
    size_t cx_end;
    const uint64_t *following;
    uint64_t following_offsets[4];

    if (!out || !blob || blob_size < sizeof(AgcShaderRecord))
        return false;
    memset(out, 0, sizeof(*out));
    source = (const AgcShaderRecord *)blob;
    if (source->magic != AGC_SHADER_RECORD_MAGIC ||
        source->version != AGC_SHADER_RECORD_VERSION_GEN5 ||
        !shader_range_valid(source->code, 0u, blob_size)) {
        printf("%s: invalid shader record\n", name);
        return false;
    }
    memcpy(&num_inputs, source->num_input_semantics, sizeof(num_inputs));
    memcpy(&num_outputs, source->num_output_semantics, sizeof(num_outputs));
    if ((source->num_sh_registers &&
         !shader_range_valid(source->sh_registers,
             (uint64_t)source->num_sh_registers * sizeof(AgcRegisterValue),
             blob_size)) ||
        (source->specials &&
         !shader_range_valid(source->specials, sizeof(AgcShaderSpecials),
             blob_size)) ||
        (num_inputs &&
         !shader_range_valid(source->input_semantics,
             (uint64_t)num_inputs * sizeof(AgcShaderSemantic), blob_size)) ||
        (num_outputs &&
         !shader_range_valid(source->output_semantics,
             (uint64_t)num_outputs * sizeof(AgcShaderSemantic), blob_size))) {
        printf("%s: shader sub-block is out of range\n", name);
        return false;
    }

    cx_end = (size_t)source->code;
    following_offsets[0] = source->specials;
    following_offsets[1] = source->input_semantics;
    following_offsets[2] = source->output_semantics;
    following_offsets[3] = source->code;
    following = following_offsets;
    if (source->cx_registers) {
        if (!shader_range_valid(source->cx_registers, 0u, blob_size))
            return false;
        for (uint32_t i = 0u; i < 4u; ++i) {
            if (following[i] > source->cx_registers && following[i] < cx_end)
                cx_end = (size_t)following[i];
        }
        if ((cx_end - (size_t)source->cx_registers) %
                sizeof(AgcRegisterValue) != 0u)
            return false;
        out->cx_registers = (const AgcRegisterValue *)(
            blob + source->cx_registers);
        out->num_cx_registers = (uint32_t)(
            (cx_end - (size_t)source->cx_registers) /
            sizeof(AgcRegisterValue));
    }

    out->record = *source;
    out->code = blob + source->code;
    out->code_size = blob_size - (size_t)source->code;
    out->sh_registers = source->sh_registers ?
        (const AgcRegisterValue *)(blob + source->sh_registers) : NULL;
    out->num_sh_registers = source->num_sh_registers;
    out->specials = source->specials ?
        (const AgcShaderSpecials *)(blob + source->specials) : NULL;
    out->record.sh_registers = (uint64_t)(uintptr_t)out->sh_registers;
    out->record.cx_registers = (uint64_t)(uintptr_t)out->cx_registers;
    out->record.specials = (uint64_t)(uintptr_t)out->specials;
    out->record.input_semantics = source->input_semantics ?
        (uint64_t)(uintptr_t)(blob + source->input_semantics) : 0u;
    out->record.output_semantics = source->output_semantics ?
        (uint64_t)(uintptr_t)(blob + source->output_semantics) : 0u;
    printf("%s: type=%u SH=%u CX=%u code=%zu bytes\n",
        name, out->record.shader_type, out->num_sh_registers,
        out->num_cx_registers, out->code_size);
    return true;
}

static void *upload_code(
    uint8_t *pool, size_t offset, const ParsedShader *shader)
{
    void *address = pool + offset;
    memcpy(address, shader->code, shader->code_size);
    __builtin___clear_cache(
        (char *)address, (char *)address + shader->code_size);
    return address;
}

static CubePoint rotate_and_project(
    CubePoint point, float sin_y, float cos_y, float sin_x, float cos_x)
{
    CubePoint result;
    const float x = cos_y * point.x + sin_y * point.z;
    const float z = -sin_y * point.x + cos_y * point.z;
    const float y = cos_x * point.y - sin_x * z;
    const float camera_z = sin_x * point.y + cos_x * z + 4.0f;

    result.x = x * 1.15f / camera_z;
    result.y = y * 2.05f / camera_z;
    result.z = camera_z;
    return result;
}

static void update_cube(
    CubeFrame *frame, float sin_y, float cos_y, float sin_x, float cos_x)
{
    static const CubePoint corners[8] = {
        {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f},
        { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f},
        {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f},
        { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f},
    };
    static const uint8_t face_corners[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
        {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0},
    };
    static const float colors[6][3] = {
        {0.95f, 0.16f, 0.12f}, {0.12f, 0.72f, 1.00f},
        {0.20f, 0.95f, 0.36f}, {1.00f, 0.42f, 0.10f},
        {0.72f, 0.22f, 1.00f}, {1.00f, 0.88f, 0.12f},
    };
    static const uint16_t local_indices[6] = {0, 1, 2, 2, 3, 0};
    CubePoint transformed[8];
    float face_depth[6];
    uint8_t order[6] = {0, 1, 2, 3, 4, 5};

    for (uint32_t i = 0u; i < 8u; ++i)
        transformed[i] = rotate_and_project(
            corners[i], sin_y, cos_y, sin_x, cos_x);
    for (uint32_t face = 0u; face < 6u; ++face) {
        face_depth[face] = 0.0f;
        for (uint32_t corner = 0u; corner < 4u; ++corner)
            face_depth[face] +=
                transformed[face_corners[face][corner]].z * 0.25f;
    }
    for (uint32_t i = 1u; i < 6u; ++i) {
        uint8_t selected = order[i];
        uint32_t j = i;
        while (j > 0u && face_depth[order[j - 1u]] < face_depth[selected]) {
            order[j] = order[j - 1u];
            --j;
        }
        order[j] = selected;
    }

    for (uint32_t face = 0u; face < 6u; ++face) {
        const uint32_t vertex_base = face * 4u;
        for (uint32_t corner = 0u; corner < 4u; ++corner) {
            CubeVertex *vertex = &frame->vertices[vertex_base + corner];
            const CubePoint point =
                transformed[face_corners[face][corner]];
            const float shade = 0.72f + 0.09f * (float)corner;
            vertex->position[0] = point.x;
            vertex->position[1] = point.y;
            vertex->position[2] = 0.5f;
            vertex->color[0] = colors[face][0] * shade;
            vertex->color[1] = colors[face][1] * shade;
            vertex->color[2] = colors[face][2] * shade;
        }
    }
    for (uint32_t sorted = 0u; sorted < 6u; ++sorted) {
        const uint16_t base = (uint16_t)(order[sorted] * 4u);
        for (uint32_t index = 0u; index < 6u; ++index)
            frame->indices[sorted * 6u + index] =
                (uint16_t)(base + local_indices[index]);
    }
}

static bool initialize_frame(CubeFrame *frame, uint8_t *slot)
{
    const size_t render_bytes =
        (size_t)PS5_CUBE_WIDTH * PS5_CUBE_HEIGHT * sizeof(uint32_t);
    size_t offset = 0u;

    memset(frame, 0, sizeof(*frame));
    frame->render_target = (uint32_t *)(slot + offset);
    offset = align_up(offset + render_bytes, 256u);
    frame->dcb = (uint32_t *)(slot + offset);
    offset = align_up(offset + CUBE_DCB_BYTES, 256u);
    frame->vertices = (CubeVertex *)(slot + offset);
    offset = align_up(offset + sizeof(CubeVertex) * CUBE_VERTEX_COUNT, 256u);
    frame->indices = (uint16_t *)(slot + offset);
    offset = align_up(offset + sizeof(uint16_t) * CUBE_INDEX_COUNT, 256u);
    frame->vertex_descriptor = (AgcGfx1013BufferDescriptor *)(slot + offset);
    offset = align_up(offset + sizeof(*frame->vertex_descriptor), 256u);
    frame->fence = (volatile uint32_t *)(slot + offset);
    if (offset + sizeof(*frame->fence) > CUBE_SLOT_BYTES)
        return false;
    *frame->fence = 0u;
    return true;
}

static bool record_and_submit(
    CubeFrame *slot, const AgcGfx1013Wave32VsPsState *shaders,
    uint32_t fence_value)
{
    AgcGfx1013FrameState frame_state;
    AgcGfx1013ResourceTableBinding vertex_table;
    AgcGfx1013BaselineDrawState baseline;
    AgcGfx1013IndexedDrawState draw;
    AgcGfx1013ResourceTransition acquire;
    AgcGfx1013ResourceTransition release;
    AgcGfx1013GraphicsDefaultStats stats;
    AgcCommandBufferSubmit submit;
    SceAgcCb cb;
    int32_t error;

    error = agcGfx1013BufferDescriptorEncode(
        slot->vertex_descriptor, (uint64_t)(uintptr_t)slot->vertices,
        sizeof(CubeVertex), CUBE_VERTEX_COUNT);
    if (error != AGC_OK) {
        printf("Vertex descriptor failed: 0x%08x\n", (unsigned)error);
        return false;
    }
    memset(&frame_state, 0, sizeof(frame_state));
    error = agcGfx1013InitColorTarget(
        &frame_state.color_target,
        (uint64_t)(uintptr_t)slot->render_target,
        PS5_CUBE_WIDTH, PS5_CUBE_HEIGHT,
        AGC_GFX1013_RT_FORMAT_BGRA8_UNORM);
    if (error != AGC_OK) {
        printf("Color target failed: 0x%08x\n", (unsigned)error);
        return false;
    }
    frame_state.viewport.width = PS5_CUBE_WIDTH;
    frame_state.viewport.height = PS5_CUBE_HEIGHT;
    frame_state.scissor.right = PS5_CUBE_WIDTH;
    frame_state.scissor.bottom = PS5_CUBE_HEIGHT;
    frame_state.target_mask = AGC_GFX1013_TARGET_MASK_RGBA0;
    frame_state.context_load_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE;
    frame_state.context_shadow_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE;
    frame_state.max_vertex_index = 0xffffffffu;
    frame_state.ngg_mode_control = AGC_GFX1013_NGG_MODE_CONTROL;
    frame_state.vertex_reuse_block_control = AGC_GFX1013_VERTEX_REUSE_BLOCK;
    frame_state.instance_step_rate = 1u;

    vertex_table.placeholder = OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER;
    vertex_table.address = (uint64_t)(uintptr_t)slot->vertex_descriptor;
    memset(&baseline, 0, sizeof(baseline));
    baseline.shaders = *shaders;
    baseline.frame = &frame_state;
    baseline.primitive_resource_tables = &vertex_table;
    baseline.num_primitive_resource_tables = 1u;
    baseline.index_type = kAgcIndexSize16;
    baseline.instance_count = 1u;
    baseline.vertex_count = CUBE_INDEX_COUNT;
    baseline.draw_modifier = 0x40000000u;
    memset(&draw, 0, sizeof(draw));
    draw.draw = baseline;
    draw.index_buffer_address = (uint64_t)(uintptr_t)slot->indices;
    draw.index_buffer_count = CUBE_INDEX_COUNT;
    draw.index_count = CUBE_INDEX_COUNT;

    agcCbInit(&cb, slot->dcb, CUBE_DCB_BYTES);
    acquire.before = AGC_GFX1013_RESOURCE_USAGE_HOST_READ;
    acquire.after = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
    acquire.completion_address = 0u;
    acquire.completion_value = 0u;
    error = agcGfx1013TransitionResource(&cb, &acquire);
    if (error != AGC_OK) {
        printf("Host-to-RT transition failed: 0x%08x\n", (unsigned)error);
        return false;
    }
    error = agcGfx1013BuildFramePrologue(&cb, &frame_state, &stats);
    if (error != AGC_OK) {
        printf("Frame prologue failed: 0x%08x\n", (unsigned)error);
        return false;
    }
    error = agcGfx1013DrawBaselineIndexed(&cb, &draw);
    if (error != AGC_OK) {
        printf("Indexed draw recording failed: 0x%08x\n", (unsigned)error);
        return false;
    }
    release.before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
    release.after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ;
    release.completion_address = (uint64_t)(uintptr_t)slot->fence;
    release.completion_value = fence_value;
    error = agcGfx1013TransitionResource(&cb, &release);
    if (error != AGC_OK) {
        printf("RT-to-host transition failed: 0x%08x\n", (unsigned)error);
        return false;
    }

    *slot->fence = 0u;
    submit.command_address = (uintptr_t)slot->dcb;
    submit.dword_count = agcCbUsedDwords(&cb);
    submit.reserved = 0u;
    if (fence_value == 1u)
        printf("Submitting first-frame DCB: %u dwords\n", submit.dword_count);
    error = sceAgcDriverSubmitDcb(&submit);
    if (error != AGC_OK) {
        printf("SubmitDcb failed: 0x%08x\n", (unsigned)error);
        return false;
    }
    if (fence_value == 1u)
        printf("First-frame DCB accepted\n");
    slot->submitted_fence = fence_value;
    return true;
}

int main(void)
{
    Ps5CubePlatform platform;
    ParsedShader front;
    ParsedShader back;
    ParsedShader pixel;
    AgcShaderRecord front_record;
    AgcShaderRecord back_record;
    AgcShaderRecord fused_record;
    AgcRegisterValue fused_registers[24];
    AgcGfx1013Wave32VsPsState shaders;
    CubeFrame frames[PS5_CUBE_BUFFER_COUNT];
    uint8_t *pool = NULL;
    void *front_code;
    void *back_code;
    void *pixel_code;
    float sin_y = 0.0f;
    float cos_y = 1.0f;
    float sin_x = 0.0f;
    float cos_x = 1.0f;
    int result = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("OpenAGC standalone rotating cube\n");
    if (ps5CubePlatformInitialize(&platform) != 0) {
        printf("PS5 platform initialization failed\n");
        return 1;
    }
    if (sce_agc_initialize() != AGC_OK ||
        sce_agc_initialize_internal_memory() != AGC_OK ||
        sceAgcDriverNotifyDefaultStates(0u) != AGC_OK ||
        sceAgcDriverSetupAsyncGraphics(1u) != AGC_OK) {
        printf("OpenAGC initialization failed\n");
        goto cleanup;
    }
    if (ps5CubePlatformMapFlexible(
            (void **)&pool, CUBE_POOL_BYTES, "openagc standalone cube") != 0 ||
        !pool) {
        printf("Flexible-memory allocation failed\n");
        goto cleanup;
    }
    for (uint32_t i = 0u; i < PS5_CUBE_BUFFER_COUNT; ++i) {
        if (!initialize_frame(
                &frames[i], pool + CUBE_FRAME_BASE + i * CUBE_SLOT_BYTES)) {
            printf("Frame-slot layout failed\n");
            goto cleanup;
        }
    }
    if (!parse_shader(&front, cube_ngg_front_data,
            sizeof(cube_ngg_front_data), "NGG front") ||
        !parse_shader(&back, cube_ngg_back_data,
            sizeof(cube_ngg_back_data), "NGG back") ||
        !parse_shader(&pixel, cube_frag_data,
            sizeof(cube_frag_data), "pixel"))
        goto cleanup;
    if (back.num_sh_registers > 24u)
        goto cleanup;
    front_code = upload_code(pool, CUBE_FRONT_CODE_OFFSET, &front);
    back_code = upload_code(pool, CUBE_BACK_CODE_OFFSET, &back);
    pixel_code = upload_code(pool, CUBE_PIXEL_CODE_OFFSET, &pixel);
    front_record = front.record;
    back_record = back.record;
    front_record.code = (uint64_t)(uintptr_t)front_code;
    back_record.code = (uint64_t)(uintptr_t)back_code;
    memset(fused_registers, 0, sizeof(fused_registers));
    if (sceAgcFuseShaderHalves_0200(
            &fused_record, &front_record, &back_record,
            fused_registers) != AGC_OK) {
        printf("NGG shader fusion failed\n");
        goto cleanup;
    }
    pixel.record.code = (uint64_t)(uintptr_t)pixel_code;
    memset(&shaders, 0, sizeof(shaders));
    shaders.primitive.record = &fused_record;
    shaders.primitive.sh_registers = fused_registers;
    shaders.primitive.num_sh_registers = fused_record.num_sh_registers;
    shaders.primitive.cx_registers = back.cx_registers;
    shaders.primitive.num_cx_registers = back.num_cx_registers;
    shaders.primitive.code_address = (uint64_t)(uintptr_t)back_code;
    shaders.pixel.record = &pixel.record;
    shaders.pixel.sh_registers = pixel.sh_registers;
    shaders.pixel.num_sh_registers = pixel.num_sh_registers;
    shaders.pixel.cx_registers = pixel.cx_registers;
    shaders.pixel.num_cx_registers = pixel.num_cx_registers;
    shaders.pixel.code_address = (uint64_t)(uintptr_t)pixel_code;
    shaders.primitive_back_code_address = (uint64_t)(uintptr_t)back_code;
    shaders.primitive_type = 4u;
    if (agcGfx1013ValidateWave32VsPs(&shaders) != AGC_OK) {
        printf("Wave32 VS/PS validation failed\n");
        goto cleanup;
    }

    printf("Rendering %u triple-buffered frames\n", CUBE_FRAME_COUNT);
    for (uint32_t frame_number = 0u;
         frame_number < CUBE_FRAME_COUNT; ++frame_number) {
        const uint32_t frame_index = frame_number % PS5_CUBE_BUFFER_COUNT;
        CubeFrame *frame = &frames[frame_index];
        const uint32_t fence_value = frame_number + 1u;
        const size_t render_bytes =
            (size_t)PS5_CUBE_WIDTH * PS5_CUBE_HEIGHT * sizeof(uint32_t);
        float next_sin;
        float next_cos;

        if (frame->submitted_fence &&
            ps5CubePlatformWaitFence(
                frame->fence, frame->submitted_fence,
                CUBE_FENCE_TIMEOUT_US) != 0) {
            printf("Frame-slot %u reuse fence timed out\n", frame_index);
            goto cleanup;
        }
        memset(frame->render_target, 0x24, render_bytes);
        update_cube(frame, sin_y, cos_y, sin_x, cos_x);
        if (frame_number == 0u)
            printf("First frame resources updated\n");
        if (!record_and_submit(frame, &shaders, fence_value))
            goto cleanup;
        if (ps5CubePlatformWaitFence(
                frame->fence, fence_value, CUBE_FENCE_TIMEOUT_US) != 0) {
            printf("Frame %u GPU fence timed out\n", frame_number);
            goto cleanup;
        }
        if (frame_number == 0u)
            printf("First frame GPU fence reached\n");
        memcpy(platform.display_buffers[frame_index],
            frame->render_target, render_bytes);
        if (frame_number == 0u)
            printf("First frame copied to garlic display buffer\n");
        if (ps5CubePlatformPresent(
                &platform, frame_index, frame_number) != 0) {
            printf("Frame %u presentation failed\n", frame_number);
            goto cleanup;
        }
        if (frame_number == 0u)
            printf("First frame flip completed\n");

        next_sin = sin_y * 0.999847695f + cos_y * 0.017452406f;
        next_cos = cos_y * 0.999847695f - sin_y * 0.017452406f;
        sin_y = next_sin;
        cos_y = next_cos;
        next_sin = sin_x * 0.999961923f + cos_x * 0.008726535f;
        next_cos = cos_x * 0.999961923f - sin_x * 0.008726535f;
        sin_x = next_sin;
        cos_x = next_cos;
        if ((frame_number % 600u) == 0u)
            printf("Presented frame %u\n", frame_number);
    }
    result = 0;

cleanup:
    if (pool)
        ps5CubePlatformUnmapFlexible(pool, CUBE_POOL_BYTES);
    ps5CubePlatformShutdown(&platform);
    printf("OpenAGC cube exit: %s\n", result == 0 ? "clean" : "failure");
    return result;
}
