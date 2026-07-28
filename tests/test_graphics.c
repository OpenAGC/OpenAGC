#include "test.h"

#include <string.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_graphics.h"
#include "../samples/triangle/triangle.h"
#include "agc_pm4.h"
#include "agc_registers.h"

static void set_u32_bytes(uint8_t bytes[4], uint32_t value)
{
    memcpy(bytes, &value, sizeof(value));
}

static bool find_register(
    const uint32_t *commands, uint32_t used, uint32_t opcode,
    uint32_t offset, uint32_t *value)
{
    uint32_t cursor = 0;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || cursor + length > used)
            return false;
        if (agcPm4Opcode(commands[cursor]) == opcode &&
            commands[cursor + 1u] == offset && length >= 3u) {
            *value = commands[cursor + 2u];
            return true;
        }
        cursor += length;
    }
    return false;
}

static bool find_indexed_register(
    const uint32_t *commands, uint32_t used, uint32_t opcode,
    uint32_t offset, uint32_t index, uint32_t *value)
{
    uint32_t cursor = 0;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || cursor + length > used)
            return false;
        if (agcPm4Opcode(commands[cursor]) == opcode && length >= 3u &&
            (commands[cursor + 1u] & 0xFFFFu) == offset &&
            (commands[cursor + 1u] >> 28) == index) {
            *value = commands[cursor + 2u];
            return true;
        }
        cursor += length;
    }
    return false;
}

static bool find_last_register(
    const uint32_t *commands, uint32_t used, uint32_t opcode,
    uint32_t offset, uint32_t *value)
{
    uint32_t cursor = 0;
    bool found = false;

    while (cursor < used) {
        uint32_t length = agcPm4Length(commands[cursor]);
        if (length < 2u || cursor + length > used)
            return false;
        if (agcPm4Opcode(commands[cursor]) == opcode &&
            commands[cursor + 1u] == offset && length >= 3u) {
            *value = commands[cursor + 2u];
            found = true;
        }
        cursor += length;
    }
    return found;
}

static void make_wave32_state(
    AgcGfx1013Wave32VsPsState *state,
    AgcShaderRecord *primitive_record,
    AgcShaderRecord *pixel_record,
    AgcShaderSpecials *specials,
    AgcRegisterValue primitive_sh[2],
    AgcRegisterValue pixel_sh[2],
    AgcRegisterValue pixel_cx[1])
{
    memset(state, 0, sizeof(*state));
    memset(primitive_record, 0, sizeof(*primitive_record));
    memset(pixel_record, 0, sizeof(*pixel_record));
    memset(specials, 0, sizeof(*specials));

    specials->ge_cntl = (AgcShaderSpecialRegister){AGC_REG_GE_CNTL, 0x10u};
    specials->vgt_shader_stages_en = (AgcShaderSpecialRegister){
        AGC_REG_VGT_SHADER_STAGES_EN,
        AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN,
    };
    specials->vgt_gs_out_prim_type = (AgcShaderSpecialRegister){
        AGC_REG_VGT_GS_OUT_PRIM_TYPE, 0u,
    };
    specials->ge_user_vgpr_en = (AgcShaderSpecialRegister){
        AGC_REG_GE_USER_VGPR_EN, 0u,
    };

    primitive_sh[0] = (AgcRegisterValue){AGC_REG_SPI_SHADER_PGM_LO_GS, 0u};
    primitive_sh[1] = (AgcRegisterValue){AGC_REG_SPI_SHADER_PGM_HI_GS, 0u};
    pixel_sh[0] = (AgcRegisterValue){AGC_REG_SPI_SHADER_PGM_LO_PS, 0u};
    pixel_sh[1] = (AgcRegisterValue){AGC_REG_SPI_SHADER_PGM_HI_PS, 0u};
    pixel_cx[0] = (AgcRegisterValue){
        AGC_REG_SPI_PS_IN_CONTROL,
        AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN,
    };

    primitive_record->magic = AGC_SHADER_RECORD_MAGIC;
    primitive_record->version = AGC_SHADER_RECORD_VERSION_GEN5;
    /* FuseShaderHalves converts GsFront(4) + GsBack(6) to fused Gs(2). */
    primitive_record->shader_type = (uint8_t)kAgcShaderBinaryTypeGs;
    primitive_record->num_sh_registers = 2u;
    primitive_record->sh_registers = (uint64_t)(uintptr_t)primitive_sh;
    primitive_record->specials = (uint64_t)(uintptr_t)specials;

    pixel_record->magic = AGC_SHADER_RECORD_MAGIC;
    pixel_record->version = AGC_SHADER_RECORD_VERSION_GEN5;
    pixel_record->shader_type = kAgcShaderTypePs;
    pixel_record->num_sh_registers = 2u;
    pixel_record->sh_registers = (uint64_t)(uintptr_t)pixel_sh;
    set_u32_bytes(pixel_record->num_input_semantics, 0u);

    state->primitive = (AgcGfx1013ShaderBinding){
        primitive_record, primitive_sh, 2u, NULL, 0u,
        0x0000001234567800ull,
    };
    state->pixel = (AgcGfx1013ShaderBinding){
        pixel_record, pixel_sh, 2u, pixel_cx, 1u,
        0x0000002234567800ull,
    };
    state->primitive_type = 4u;
}

static AgcGfx1013FrameState make_frame_state(void)
{
    const AgcGfx1013FrameState state = {
        .color_target = {
            0x0000000201600000ull, 1920u, 1080u,
            AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
            AGC_GFX1013_SURFACE_NUMBER_UNORM,
            AGC_GFX1013_SURFACE_SWAP_ALT,
        },
        .viewport = {1920u, 1080u},
        .scissor = {0u, 0u, 1920u, 1080u},
        .target_mask = AGC_GFX1013_TARGET_MASK_RGBA0,
        .context_load_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE,
        .context_shadow_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE,
        .max_vertex_index = 0xffffffffu,
        .ngg_mode_control = AGC_GFX1013_NGG_MODE_CONTROL,
        .vertex_reuse_block_control = AGC_GFX1013_VERTEX_REUSE_BLOCK,
        .instance_step_rate = 1u,
    };
    return state;
}

static void test_public_triangle_example(void)
{
    uint32_t buffer[2400] = {0};
    uint32_t expected[2400] = {0};
    SceAgcCb cb;
    SceAgcCb expected_cb;
    OpenAgcTrianglePass pass;
    AgcGfx1013BaselineDrawState expected_draw;
    AgcGfx1013GraphicsDefaultStats stats;
    AgcGfx1013ResourceTransition present;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[2];
    AgcRegisterValue pixel_sh[2];
    AgcRegisterValue pixel_cx[1];
    uint32_t expected_dwords;

    memset(&pass, 0, sizeof(pass));
    pass.frame = make_frame_state();
    make_wave32_state(&pass.draw.shaders, &primitive_record, &pixel_record,
        &specials, primitive_sh, pixel_sh, pixel_cx);
    pass.draw.index_type = kAgcIndexSize16;
    pass.draw.instance_count = 1u;
    pass.draw.vertex_count = 3u;
    pass.draw.draw_modifier = 0x40000000u;
    pass.completion_address = 0x00000002014bb000ull;
    pass.completion_value = 0x1234abcdu;

    expected_draw = pass.draw;
    expected_draw.frame = &pass.frame;
    present.before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
    present.after = AGC_GFX1013_RESOURCE_USAGE_PRESENT;
    present.completion_address = pass.completion_address;
    present.completion_value = pass.completion_value;

    agcCbInit(&expected_cb, expected, sizeof(expected));
    TEST_ASSERT_EQ(agcGfx1013BuildFramePrologue(
        &expected_cb, &pass.frame, &stats), AGC_OK,
        "triangle example expected frame prologue succeeds");
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(
        &expected_cb, &expected_draw), AGC_OK,
        "triangle example expected draw succeeds");
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(
        &expected_cb, &present), AGC_OK,
        "triangle example expected present transition succeeds");
    expected_dwords = agcCbUsedDwords(&expected_cb);

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(openagcTriangleRecord(&cb, &pass), AGC_OK,
        "public triangle example records");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), expected_dwords,
        "public triangle example exact cursor advance");
    TEST_ASSERT(memcmp(buffer, expected,
        expected_dwords * sizeof(uint32_t)) == 0,
        "public triangle example exact packet composition");

    agcCbInit(&cb, buffer,
        (expected_dwords - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(openagcTriangleRecord(&cb, &pass),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "public triangle example rejects short buffer");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "public triangle example restores cursor on failure");
}

static void test_gfx1013_wave32_vs_ps_binding(void)
{
    uint32_t buffer[128] = {0};
    SceAgcCb cb;
    AgcGfx1013Wave32VsPsState state;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[3];
    AgcRegisterValue pixel_sh[2];
    AgcRegisterValue pixel_cx[1];
    uint32_t value = 0;

    make_wave32_state(&state, &primitive_record, &pixel_record, &specials,
        primitive_sh, pixel_sh, pixel_cx);
    primitive_sh[2] = (AgcRegisterValue){
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 1u,
        OPENAGC_NEXT_STAGE_PC_PLACEHOLDER,
    };
    primitive_record.num_sh_registers = 3u;
    state.primitive.num_sh_registers = 3u;
    state.primitive_back_code_address = 0x0000003234567800ull;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ValidateWave32VsPs(&state), AGC_OK,
        "gfx1013 Wave32 VS+PS validation succeeds");
    TEST_ASSERT_EQ(agcGfx1013BindWave32VsPs(&cb, &state), AGC_OK,
        "gfx1013 Wave32 VS+PS binding succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 39u,
        "gfx1013 Wave32 VS+PS exact dword count");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_PGM_LO_GS, &value),
        "gfx1013 primitive program low emitted");
    TEST_ASSERT_EQ(value, (uint32_t)(state.primitive.code_address >> 8),
        "gfx1013 primitive program address encoding");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_PGM_LO_PS, &value),
        "gfx1013 pixel program low emitted");
    TEST_ASSERT_EQ(value, (uint32_t)(state.pixel.code_address >> 8),
        "gfx1013 pixel program address encoding");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_CONTEXT_REG,
        AGC_REG_SPI_PS_IN_CONTROL, &value),
        "gfx1013 pixel Wave32 control emitted");
    TEST_ASSERT_EQ(value, AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN,
        "gfx1013 pixel Wave32 control value");
    TEST_ASSERT(find_last_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 1u, &value),
        "gfx1013 primitive continuation SGPR emitted");
    TEST_ASSERT_EQ(value, 0x34567800u,
        "gfx1013 primitive continuation placeholder patched");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_CONTEXT_REG,
        AGC_REG_VGT_DRAW_PAYLOAD_CNTL, &value),
        "gfx1013 draw payload control emitted");
    TEST_ASSERT_EQ(value, 1u << 6,
        "gfx1013 GFX10.3 VRS-rate payload channel enabled");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_UCONFIG_REG,
        0x260u, &value),
        "gfx1013 NGG parameter-cache allocation emitted");
    TEST_ASSERT_EQ(value, 0x000007feu,
        "gfx1013 NGG late allocation remains disabled");
}

static void test_gfx1013_baseline_draw_wrapper(void)
{
    uint32_t buffer[128] = {0};
    SceAgcCb cb;
    AgcGfx1013BaselineDrawState draw;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[2];
    AgcRegisterValue pixel_sh[2];
    AgcRegisterValue pixel_cx[1];
    const AgcRegisterValue post_bind_cx[] = {
        {AGC_REG_DB_DEPTH_CONTROL, 0u},
    };
    const AgcGfx1013FrameState frame = make_frame_state();

    memset(&draw, 0, sizeof(draw));
    make_wave32_state(&draw.shaders, &primitive_record, &pixel_record,
        &specials, primitive_sh, pixel_sh, pixel_cx);
    draw.index_type = kAgcIndexSize16;
    draw.instance_count = 1u;
    draw.vertex_count = 3u;
    draw.draw_modifier = 0x40000000u;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(&cb, &draw), AGC_OK,
        "gfx1013 baseline draw wrapper succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 44u,
        "gfx1013 baseline draw wrapper exact dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[36]), AGC_PM4_OP_SET_INDEX_SIZE,
        "gfx1013 baseline draw index-size opcode");
    TEST_ASSERT_EQ(agcPm4Length(buffer[36]), 3u,
        "gfx1013 baseline draw index-size length");
    TEST_ASSERT_EQ(buffer[37], 0x20000243u,
        "gfx1013 baseline draw index-size control");
    TEST_ASSERT_EQ(buffer[38], 0x400u,
        "gfx1013 baseline draw 16-bit unswapped index state");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[39]), AGC_PM4_OP_NUM_INSTANCES,
        "gfx1013 baseline draw instance opcode");
    TEST_ASSERT_EQ(buffer[40], 1u,
        "gfx1013 baseline draw instance count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[41]), AGC_PM4_OP_DRAW_INDEX_AUTO,
        "gfx1013 baseline auto-index opcode");
    TEST_ASSERT_EQ(agcPm4Length(buffer[41]), 3u,
        "gfx1013 baseline auto-index length");
    TEST_ASSERT_EQ(buffer[42], 3u,
        "gfx1013 baseline auto-index vertex count");
    TEST_ASSERT_EQ(buffer[43], 2u,
        "gfx1013 baseline auto-index initiator");

    draw.post_bind_cx_registers = post_bind_cx;
    draw.num_post_bind_cx_registers = 1u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(&cb, &draw), AGC_OK,
        "gfx1013 baseline wrapper accepts post-bind overrides");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 47u,
        "gfx1013 baseline post-bind override exact dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[36]), AGC_PM4_OP_SET_CONTEXT_REG,
        "gfx1013 baseline post-bind override opcode");
    TEST_ASSERT_EQ(buffer[37], AGC_REG_DB_DEPTH_CONTROL,
        "gfx1013 baseline post-bind override register");
    TEST_ASSERT_EQ(buffer[38], 0u,
        "gfx1013 baseline post-bind override value");

    draw.post_bind_cx_registers = NULL;
    draw.num_post_bind_cx_registers = 0u;
    draw.frame = &frame;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(&cb, &draw), AGC_OK,
        "gfx1013 baseline wrapper applies frame post-bind state");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 68u,
        "gfx1013 baseline frame post-bind exact dword count");
    TEST_ASSERT_EQ(buffer[36],
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        "gfx1013 baseline frame depth starts after shader bind");
    TEST_ASSERT_EQ(buffer[37], AGC_REG_DB_DEPTH_INFO,
        "gfx1013 baseline frame depth register order");

    agcCbInit(&cb, buffer, 67u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(&cb, &draw),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "gfx1013 baseline wrapper rejects short buffer");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 baseline short buffer is atomic");

    draw.index_type = 2u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(&cb, &draw),
        AGC_ERROR_INVALID_ARGUMENT,
        "gfx1013 baseline wrapper rejects invalid index type");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 baseline invalid state is atomic");
}

static void test_gfx1013_indexed_indirect_draw_wrappers(void)
{
    uint32_t buffer[128] = {0};
    SceAgcCb cb;
    AgcGfx1013IndexedDrawState indexed;
    AgcGfx1013IndirectDrawState indirect;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[2];
    AgcRegisterValue pixel_sh[2];
    AgcRegisterValue pixel_cx[1];

    memset(&indexed, 0, sizeof(indexed));
    make_wave32_state(&indexed.draw.shaders, &primitive_record, &pixel_record,
        &specials, primitive_sh, pixel_sh, pixel_cx);
    indexed.draw.index_type = kAgcIndexSize16;
    indexed.draw.instance_count = 2u;
    indexed.index_buffer_address = UINT64_C(0x200010000);
    indexed.index_buffer_count = 12u;
    indexed.first_index = 3u;
    indexed.index_count = 6u;
    indexed.draw_initiator = 2u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexed(&cb, &indexed), AGC_OK,
        "gfx1013 direct indexed wrapper succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 47u,
        "gfx1013 direct indexed exact dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[41]), AGC_PM4_OP_DRAW_INDEX_2,
        "gfx1013 direct indexed draw opcode");
    TEST_ASSERT_EQ(buffer[42], 9u,
        "gfx1013 direct indexed remaining max count");
    TEST_ASSERT_EQ(buffer[43], 0x00010006u,
        "gfx1013 direct indexed first-index address adjustment");
    TEST_ASSERT_EQ(buffer[44], 2u,
        "gfx1013 direct indexed high address");
    TEST_ASSERT_EQ(buffer[45], 6u,
        "gfx1013 direct indexed draw count");
    TEST_ASSERT_EQ(buffer[46], 2u,
        "gfx1013 direct indexed initiator");

    agcCbInit(&cb, buffer, 46u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexed(&cb, &indexed),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "gfx1013 direct indexed short buffer rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 direct indexed rejection is atomic");
    indexed.index_count = 10u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexed(&cb, &indexed),
        AGC_ERROR_INVALID_ARGUMENT,
        "gfx1013 direct indexed out-of-range draw rejects");

    memset(&indirect, 0, sizeof(indirect));
    make_wave32_state(&indirect.draw.shaders, &primitive_record, &pixel_record,
        &specials, primitive_sh, pixel_sh, pixel_cx);
    indirect.draw.index_type = kAgcIndexSize16;
    indirect.argument_buffer_address = UINT64_C(0x200020000);
    indirect.argument_offset = 0x20u;
    indirect.draw_count = 1u;
    indirect.base_vertex_location = 5u;
    indirect.start_instance_location = 6u;
    indirect.draw_initiator = 2u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndirect(&cb, &indirect), AGC_OK,
        "gfx1013 non-indexed indirect wrapper succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 45u,
        "gfx1013 non-indexed indirect exact dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[36]), AGC_PM4_OP_SET_BASE,
        "gfx1013 indirect argument base opcode");
    TEST_ASSERT_EQ(buffer[36],
        agcPm4Header3(AGC_PM4_OP_SET_BASE, 4u),
        "gfx1013 indirect argument base uses canonical header controls");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[40]), AGC_PM4_OP_DRAW_INDIRECT,
        "gfx1013 indirect draw opcode");
    TEST_ASSERT_EQ(buffer[41], 0x20u,
        "gfx1013 indirect argument offset");
    TEST_ASSERT_EQ(buffer[42], 5u,
        "gfx1013 indirect base-vertex register location");
    TEST_ASSERT_EQ(buffer[43], 6u,
        "gfx1013 indirect start-instance register location");

    indirect.indexed = 1u;
    indirect.index_buffer_address = UINT64_C(0x200030000);
    indirect.index_buffer_count = 64u;
    indirect.draw_count = 3u;
    indirect.stride = 20u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndirect(&cb, &indirect), AGC_OK,
        "gfx1013 indexed multi-indirect wrapper succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 55u,
        "gfx1013 indexed multi-indirect exact dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[36]), AGC_PM4_OP_SET_INDEX_SIZE,
        "gfx1013 indexed indirect index type precedes buffer");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[39]), AGC_PM4_OP_INDEX_BASE,
        "gfx1013 indexed indirect index base opcode");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[44]), AGC_PM4_OP_SET_BASE,
        "gfx1013 indexed indirect argument base opcode");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[48]),
        AGC_PM4_OP_DRAW_INDEX_INDIRECT_MULTI,
        "gfx1013 indexed multi-indirect draw opcode");
    TEST_ASSERT_EQ(buffer[52], 3u,
        "gfx1013 indexed multi-indirect draw count");
    TEST_ASSERT_EQ(buffer[53], 20u,
        "gfx1013 indexed multi-indirect stride");

    agcCbInit(&cb, buffer, 54u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndirect(&cb, &indirect),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "gfx1013 indexed multi-indirect short buffer rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 indexed multi-indirect rejection is atomic");
    indirect.stride = 16u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndirect(&cb, &indirect),
        AGC_ERROR_INVALID_ARGUMENT,
        "gfx1013 indexed multi-indirect short stride rejects");
    indirect.stride = 20u;
    indirect.draw_index_location = 7u;
    indirect.draw_index_enable = 1u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndirect(&cb, &indirect),
        AGC_ERROR_INVALID_ARGUMENT,
        "gfx1013 indirect rejects unqualified draw-index packet control");
}

static void test_gfx1013_wave32_rejects_but_generic_accepts_wave64(void)
{
    uint32_t buffer[128] = {0};
    SceAgcCb cb;
    AgcGfx1013Wave32VsPsState state;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[2];
    AgcRegisterValue pixel_sh[2];
    AgcRegisterValue pixel_cx[1];

    make_wave32_state(&state, &primitive_record, &pixel_record, &specials,
        primitive_sh, pixel_sh, pixel_cx);
    specials.vgt_shader_stages_en.value = 0u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(
        agcGfx1013BindWave32VsPs(&cb, &state),
        AGC_ERROR_VALIDATION_FAILED,
        "gfx1013 non-Wave32 primitive rejected");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 rejected state emits no packets");
    TEST_ASSERT_EQ(
        agcGfx1013ValidateVsPs(&state), AGC_OK,
        "gfx1013 generic validation accepts Wave64 primitive");
    TEST_ASSERT_EQ(
        agcGfx1013BindVsPs(&cb, &state), AGC_OK,
        "gfx1013 generic binding accepts Wave64 primitive");
}

static void test_gfx1013_buffer_copy_packets(void)
{
    uint32_t buffer[16] = {0};
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    TEST_ASSERT_EQ(agcGfx1013CopyBuffer(&cb,
        UINT64_C(0x200010000), UINT64_C(0x200020000), 0x100u), AGC_OK,
        "gfx1013 buffer copy succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 8u,
        "gfx1013 buffer copy exact dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[0]), AGC_PM4_OP_DMA_DATA,
        "gfx1013 buffer copy uses raw DMA_DATA");
    TEST_ASSERT_EQ(buffer[1], 0u,
        "gfx1013 buffer copy disables byte swapping");
    TEST_ASSERT_EQ(buffer[2], 0x100u,
        "gfx1013 buffer copy byte count");
    TEST_ASSERT_EQ(buffer[3], 0x00020000u,
        "gfx1013 buffer copy destination low");
    TEST_ASSERT_EQ(buffer[4], 2u,
        "gfx1013 buffer copy destination high");
    TEST_ASSERT_EQ(buffer[5], 0x00010000u,
        "gfx1013 buffer copy source low");
    TEST_ASSERT_EQ(buffer[6], 2u,
        "gfx1013 buffer copy source high");
    TEST_ASSERT_EQ(buffer[7], 0u,
        "gfx1013 buffer copy reserved word");

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013CopyBuffer(&cb,
        UINT64_C(0x200010000), UINT64_C(0x200020000),
        UINT64_C(0x100000000)), AGC_OK,
        "gfx1013 large buffer copy splits into packets");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 16u,
        "gfx1013 split buffer copy exact dword count");
    TEST_ASSERT_EQ(buffer[2], 0xfffffffcu,
        "gfx1013 split buffer copy first packet maximum");
    TEST_ASSERT_EQ(buffer[10], 4u,
        "gfx1013 split buffer copy remainder");

    agcCbInit(&cb, buffer, 7u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013CopyBuffer(&cb,
        UINT64_C(0x200010000), UINT64_C(0x200020000), 4u),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "gfx1013 buffer copy rejects short command buffer");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 short buffer rejection is atomic");
    TEST_ASSERT_EQ(agcGfx1013CopyBuffer(&cb,
        UINT64_C(0x200010002), UINT64_C(0x200020000), 4u),
        AGC_ERROR_INVALID_ALIGNMENT,
        "gfx1013 buffer copy rejects unaligned source");
}

static void test_gfx1013_wave32_rejects_small_buffer_atomically(void)
{
    uint32_t buffer[16] = {0};
    SceAgcCb cb;
    AgcGfx1013Wave32VsPsState state;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[2];
    AgcRegisterValue pixel_sh[2];
    AgcRegisterValue pixel_cx[1];

    make_wave32_state(&state, &primitive_record, &pixel_record, &specials,
        primitive_sh, pixel_sh, pixel_cx);
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(
        agcGfx1013BindWave32VsPs(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "gfx1013 short command buffer rejected");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 short buffer rejection is atomic");
}

static void test_gfx1013_wave32_tessellation_binding(void)
{
    uint32_t buffer[192] = {0};
    SceAgcCb cb;
    AgcGfx1013Wave32TessVsPsState state = {0};
    AgcShaderRecord hs_record = {0};
    AgcShaderRecord gs_record = {0};
    AgcShaderRecord ps_record = {0};
    AgcShaderSpecials hs_specials = {0};
    AgcShaderSpecials gs_specials = {0};
    AgcRegisterValue hs_sh[] = {
        {AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_HS,
         OPENAGC_RING_OFFSETS_LO_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_HS,
         OPENAGC_RING_OFFSETS_HI_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 11u,
         OPENAGC_TCS_OFFCHIP_LAYOUT_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 13u,
         OPENAGC_NEXT_STAGE_PC_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_PGM_LO_HS, 0u},
        {AGC_REG_SPI_SHADER_PGM_HI_HS, 0u},
        {AGC_REG_SPI_SHADER_PGM_RSRC2_HS, 0x1cu},
        {0x220u, OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER},
    };
    AgcRegisterValue gs_sh[] = {
        {AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_GS,
         OPENAGC_RING_OFFSETS_LO_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_GS,
         OPENAGC_RING_OFFSETS_HI_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 11u,
         OPENAGC_TCS_OFFCHIP_LAYOUT_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 1u,
         OPENAGC_NEXT_STAGE_PC_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_PGM_LO_GS, 0u},
        {AGC_REG_SPI_SHADER_PGM_HI_GS, 0u},
    };
    AgcRegisterValue ps_sh[] = {
        {AGC_REG_SPI_SHADER_PGM_LO_PS, 0u},
        {AGC_REG_SPI_SHADER_PGM_HI_PS, 0u},
        {0x221u, OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u)},
    };
    AgcRegisterValue gs_cx[] = {
        {AGC_REG_VGT_LS_HS_CONFIG, 0x0000C308u},
    };
    AgcRegisterValue ps_cx[] = {
        {AGC_REG_SPI_PS_IN_CONTROL,
         AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN},
    };
    uint32_t value = 0;

    hs_specials.vgt_shader_stages_en = (AgcShaderSpecialRegister){
        AGC_REG_VGT_SHADER_STAGES_EN, 0x00200105u};
    gs_specials.ge_cntl = (AgcShaderSpecialRegister){
        AGC_REG_GE_CNTL, 0x00018000u};
    gs_specials.vgt_shader_stages_en = (AgcShaderSpecialRegister){
        AGC_REG_VGT_SHADER_STAGES_EN, 0x00c02008u};
    gs_specials.vgt_gs_out_prim_type = (AgcShaderSpecialRegister){
        AGC_REG_VGT_GS_OUT_PRIM_TYPE, 2u};
    gs_specials.ge_user_vgpr_en = (AgcShaderSpecialRegister){
        AGC_REG_GE_USER_VGPR_EN, 0u};

    hs_record.magic = gs_record.magic = ps_record.magic =
        AGC_SHADER_RECORD_MAGIC;
    hs_record.version = gs_record.version = ps_record.version =
        AGC_SHADER_RECORD_VERSION_GEN5;
    hs_record.shader_type = (uint8_t)kAgcShaderBinaryTypeHs;
    gs_record.shader_type = (uint8_t)kAgcShaderBinaryTypeGs;
    ps_record.shader_type = kAgcShaderTypePs;
    hs_record.num_sh_registers =
        (uint32_t)(sizeof(hs_sh) / sizeof(hs_sh[0]));
    gs_record.num_sh_registers =
        (uint32_t)(sizeof(gs_sh) / sizeof(gs_sh[0]));
    ps_record.num_sh_registers =
        (uint32_t)(sizeof(ps_sh) / sizeof(ps_sh[0]));
    hs_record.sh_registers = (uint64_t)(uintptr_t)hs_sh;
    gs_record.sh_registers = (uint64_t)(uintptr_t)gs_sh;
    ps_record.sh_registers = (uint64_t)(uintptr_t)ps_sh;
    hs_record.specials = (uint64_t)(uintptr_t)&hs_specials;
    gs_record.specials = (uint64_t)(uintptr_t)&gs_specials;
    set_u32_bytes(ps_record.num_input_semantics, 0u);

    state.hull = (AgcGfx1013ShaderBinding){
        &hs_record, hs_sh, hs_record.num_sh_registers, NULL, 0u,
        0x0000001234500000ull};
    state.primitive = (AgcGfx1013ShaderBinding){
        &gs_record, gs_sh, gs_record.num_sh_registers, gs_cx, 1u,
        0x0000001234600000ull};
    state.pixel = (AgcGfx1013ShaderBinding){
        &ps_record, ps_sh, ps_record.num_sh_registers, ps_cx, 1u,
        0x0000001234700000ull};
    state.hull_back_code_address = 0x0000001234501000ull;
    state.primitive_back_code_address = 0x0000001234601000ull;
    state.ring_descriptor_address = 0x0000001234800000ull;
    state.tcs_offchip_layout = 0x21022108u;
    state.tes_offchip_layout = 0x21022188u;
    state.hull_lds_size = 1536u;
    state.primitive_type = 9u;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(
        agcGfx1013ValidateWave32TessVsPs(&state), AGC_OK,
        "gfx1013 tessellation validation succeeds");
    TEST_ASSERT_EQ(
        agcGfx1013BindWave32TessVsPs(&cb, &state), AGC_OK,
        "gfx1013 tessellation binding succeeds");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_HS, &value),
        "HS ring descriptor low emitted");
    TEST_ASSERT_EQ(value, (uint32_t)state.ring_descriptor_address,
        "HS ring descriptor low patched");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_GS, &value),
        "TES ring descriptor low emitted");
    TEST_ASSERT_EQ(value, (uint32_t)state.ring_descriptor_address,
        "TES ring descriptor low patched");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_USER_DATA_HS_0 + 11u, &value),
        "HS offchip layout emitted");
    TEST_ASSERT_EQ(value, state.tcs_offchip_layout,
        "HS offchip layout patched");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 11u, &value),
        "TES offchip layout emitted");
    TEST_ASSERT_EQ(value, state.tes_offchip_layout,
        "TES offchip layout patched");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_PGM_RSRC2_HS, &value),
        "HS resource register emitted");
    TEST_ASSERT_EQ(value, 0x0010001cu,
        "HS LDS allocation rounds to a gfx1013 1 KiB block");
    TEST_ASSERT(find_indexed_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_CONTEXT_REG,
        AGC_REG_VGT_LS_HS_CONFIG, 2u, &value),
        "VGT_LS_HS_CONFIG emitted with GFX7+ index 2");
    TEST_ASSERT_EQ(value, 0x0000C308u,
        "indexed VGT_LS_HS_CONFIG value preserved");
    TEST_ASSERT(find_last_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_CONTEXT_REG,
        AGC_REG_VGT_SHADER_STAGES_EN, &value),
        "combined tessellation stages emitted");
    TEST_ASSERT_EQ(value, 0x00e0210du,
        "LS+HS+DS+NGG Wave32 stage enables combined");

    state.hull_lds_size = 1u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(
        agcGfx1013BindWave32TessVsPs(&cb, &state), AGC_OK,
        "gfx1013 minimum hull LDS allocation binds");
    TEST_ASSERT(find_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_SH_REG,
        AGC_REG_SPI_SHADER_PGM_RSRC2_HS, &value),
        "minimum HS LDS resource register emitted");
    TEST_ASSERT_EQ(value, 0x0008001cu,
        "minimum HS LDS allocation encodes one 1 KiB block as two units");
    state.hull_lds_size = 1536u;

    /* TES-as-NGG can be a complete front program with an inert back half.
     * ACO then allocates no continuation SGPR; ring pointers remain required. */
    gs_sh[3].value = 0u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(
        agcGfx1013ValidateWave32TessVsPs(&state), AGC_OK,
        "TES front-only record validates without continuation SGPR");
    TEST_ASSERT_EQ(
        agcGfx1013BindWave32TessVsPs(&cb, &state), AGC_OK,
        "TES front-only record binds without continuation SGPR");

    const AgcGfx1013TessellationState tessellation = {
        .offchip_ring_address = 0x0000000202610000ull,
        .factor_ring_address = 0x0000000202618000ull,
        .offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE,
        .factor_ring_size = AGC_GFX1013_TESS_FACTOR_RING_SIZE,
        .max_tess_level = 0x42800000u,
        .esgs_ring_itemsize = 1u,
        .distribution = 0xd8181e0cu,
        .tf_param = 0x61u,
    };
    const AgcGfx1013ResourceTableBinding hull_table = {
        OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER,
        0x0000000202601000ull,
    };
    const AgcGfx1013ResourceTableBinding pixel_table = {
        OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
        0x0000000202702000ull,
    };
    const AgcRegisterValue post_cx = {AGC_REG_DB_DEPTH_CONTROL, 0u};
    const AgcGfx1013DepthStencilState depth_stencil = {
        .depth_test_enable = 1u,
        .depth_write_enable = 1u,
        .depth_compare_operation = AGC_GFX1013_COMPARE_LESS,
        .min_depth_bounds = 0.0f,
        .max_depth_bounds = 1.0f,
    };
    const AgcGfx1013FrameState frame = make_frame_state();
    AgcGfx1013TessDrawState draw = {
        .shaders = state,
        .frame = &frame,
        .tessellation = &tessellation,
        .depth_stencil_state = &depth_stencil,
        .hull_resource_tables = &hull_table,
        .num_hull_resource_tables = 1u,
        .pixel_resource_tables = &pixel_table,
        .num_pixel_resource_tables = 1u,
        .post_bind_cx_registers = &post_cx,
        .num_post_bind_cx_registers = 1u,
        .instance_count = 1u,
        .vertex_count = 3u,
        .draw_modifier = 0x40000000u,
    };

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawTessIndexAuto(&cb, &draw), AGC_OK,
        "gfx1013 tessellation draw composes");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        134u + AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS,
        "gfx1013 tessellation draw exact dword count");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_SH_REG, 0x220u, &value),
        "tessellation hull resource emitted");
    TEST_ASSERT_EQ(value, 0x02601000u,
        "tessellation hull resource address32");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_SH_REG, 0x221u, &value),
        "tessellation pixel resource emitted");
    TEST_ASSERT_EQ(value, 0x02702000u,
        "tessellation pixel resource address32");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_CONTEXT_REG, AGC_REG_VGT_TF_PARAM, &value),
        "post-bind tessellation context emitted");
    TEST_ASSERT_EQ(value, 0x61u, "tessellation parameter preserved");
    TEST_ASSERT_EQ(buffer[102],
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        "tessellation frame depth follows tessellation context");
    TEST_ASSERT_EQ(buffer[103], AGC_REG_DB_DEPTH_INFO,
        "tessellation frame depth register order");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_CONTEXT_REG, AGC_REG_DB_DEPTH_CONTROL, &value),
        "tessellation depth control emitted");
    TEST_ASSERT_EQ(value, 0u,
        "caller post-bind depth override follows typed depth state");
    TEST_ASSERT_EQ(agcPm4Opcode(
        buffer[129u + AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS]),
        AGC_PM4_OP_NUM_INSTANCES,
        "tessellation draw instance packet order");
    TEST_ASSERT_EQ(buffer[130u + AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS], 1u,
        "tessellation draw instance count");
    TEST_ASSERT_EQ(agcPm4Opcode(
        buffer[131u + AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS]),
        AGC_PM4_OP_DRAW_INDEX_AUTO,
        "tessellation draw packet order");
    TEST_ASSERT_EQ(buffer[132u + AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS], 3u,
        "tessellation draw vertex count");

    agcCbReset(&cb, buffer,
        (133u + AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013DrawTessIndexAuto(&cb, &draw),
        AGC_ERROR_BUFFER_TOO_SMALL, "short tessellation draw rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short tessellation draw is atomic");
    draw.post_bind_cx_registers = NULL;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013DrawTessIndexAuto(&cb, &draw),
        AGC_ERROR_INVALID_ARGUMENT, "invalid tessellation override rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid tessellation override is atomic");
}

static void test_gfx1013_polygon_modes(void)
{
    AgcGfx1013FrameState frame = {0};
    const uint32_t preserved = 0xa5a51807u;

    frame.raster_mode_control = preserved | 0x7f8u;
    TEST_ASSERT_EQ(agcGfx1013ApplyPolygonMode(
        &frame, AGC_GFX1013_POLYGON_MODE_FILL), AGC_OK,
        "gfx1013 fill polygon mode applies");
    TEST_ASSERT_EQ(frame.raster_mode_control, preserved | 0x240u,
        "gfx1013 fill polygon mode exact bits");
    TEST_ASSERT_EQ(agcGfx1013ApplyPolygonMode(
        &frame, AGC_GFX1013_POLYGON_MODE_LINE), AGC_OK,
        "gfx1013 line polygon mode applies");
    TEST_ASSERT_EQ(frame.raster_mode_control, preserved | 0x128u,
        "gfx1013 line polygon mode exact bits");
    TEST_ASSERT_EQ(agcGfx1013ApplyPolygonMode(
        &frame, AGC_GFX1013_POLYGON_MODE_POINT), AGC_OK,
        "gfx1013 point polygon mode applies");
    TEST_ASSERT_EQ(frame.raster_mode_control, preserved | 0x008u,
        "gfx1013 point polygon mode exact bits");

    const uint32_t before_invalid = frame.raster_mode_control;
    TEST_ASSERT_EQ(agcGfx1013ApplyPolygonMode(
        &frame, AGC_GFX1013_POLYGON_MODE_COUNT),
        AGC_ERROR_INVALID_ARGUMENT, "invalid polygon mode rejects");
    TEST_ASSERT_EQ(frame.raster_mode_control, before_invalid,
        "invalid polygon mode preserves state");
    TEST_ASSERT_EQ(agcGfx1013ApplyPolygonMode(
        NULL, AGC_GFX1013_POLYGON_MODE_FILL),
        AGC_ERROR_INVALID_ARGUMENT, "null polygon state rejects");
}

static void test_gfx1013_raster_primitives(void)
{
    static const uint32_t expected_types[] = {1u, 2u, 3u, 4u, 5u, 9u};
    const uint32_t expected_state[] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG,
            AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS),
        AGC_REG_PA_SU_POINT_SIZE,
        0x00080008u,
        0x02000008u,
        0x00000040u,
    };
    uint32_t buffer[AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013PrimitiveSizeState state = {1.0f, 1.0f, 64.0f, 8.0f};

    for (uint32_t topology = 0u;
         topology < AGC_GFX1013_TOPOLOGY_COUNT; ++topology) {
        uint32_t primitive_type = 0u;
        TEST_ASSERT_EQ(agcGfx1013GetPrimitiveType(
            (AgcGfx1013PrimitiveTopology)topology, &primitive_type), AGC_OK,
            "gfx1013 primitive topology maps");
        TEST_ASSERT_EQ(primitive_type, expected_types[topology],
            "gfx1013 primitive topology exact type");
    }
    TEST_ASSERT_EQ(agcGfx1013GetPrimitiveType(
        AGC_GFX1013_TOPOLOGY_COUNT, &buffer[0]),
        AGC_ERROR_INVALID_ARGUMENT, "invalid primitive topology rejects");
    TEST_ASSERT_EQ(agcGfx1013GetPrimitiveType(
        (AgcGfx1013PrimitiveTopology)-1, &buffer[0]),
        AGC_ERROR_INVALID_ARGUMENT, "negative primitive topology rejects");
    TEST_ASSERT_EQ(agcGfx1013GetPrimitiveType(
        AGC_GFX1013_TOPOLOGY_POINT_LIST, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "null primitive type output rejects");

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetPrimitiveSizeState(&cb, &state), AGC_OK,
        "gfx1013 primitive size state emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS,
        "gfx1013 primitive size exact dword count");
    TEST_ASSERT(memcmp(buffer, expected_state, sizeof(expected_state)) == 0,
        "gfx1013 primitive size exact packet stream");

    agcCbReset(&cb, buffer, (AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS - 1u) *
        sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetPrimitiveSizeState(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL, "short primitive size state rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short primitive size state is atomic");
    state.point_size_max = 0.5f;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetPrimitiveSizeState(&cb, &state),
        AGC_ERROR_INVALID_ARGUMENT, "invalid primitive size range rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid primitive size state is atomic");
    TEST_ASSERT_EQ(agcGfx1013SetPrimitiveSizeState(NULL, &state),
        AGC_ERROR_INVALID_ARGUMENT, "null primitive size command rejects");
}

static void test_gfx1013_frame_state(void)
{
    static const struct {
        AgcGfx1013ColorTargetFormat format;
        uint32_t shader_export;
        const char *name;
    } qualification_formats[] = {
        {AGC_GFX1013_RT_FORMAT_R8_UNORM, 4u, "R8 post-bind emits"},
        {AGC_GFX1013_RT_FORMAT_RG8_UNORM, 4u, "RG8 post-bind emits"},
        {AGC_GFX1013_RT_FORMAT_R32_FLOAT, 1u, "R32 post-bind emits"},
        {AGC_GFX1013_RT_FORMAT_RG32_FLOAT, 2u, "RG32 post-bind emits"},
        {AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT, 9u, "RGBA32 post-bind emits"},
    };
    uint32_t buffer[AGC_GFX1013_FRAME_PROLOGUE_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013FrameState frame = make_frame_state();
    AgcGfx1013GraphicsDefaultStats stats = {0};

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013BuildFramePrologue(
        &cb, &frame, &stats), AGC_OK,
        "gfx1013 frame prologue composes");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_FRAME_PROLOGUE_BASE_DWORDS,
        "gfx1013 frame prologue exact dword count");
    TEST_ASSERT_EQ(stats.sh_register_count, 174u,
        "gfx1013 frame SH defaults count");
    TEST_ASSERT_EQ(stats.cx_register_count, 493u,
        "gfx1013 frame CX defaults count");
    TEST_ASSERT_EQ(stats.uc_register_count, 61u,
        "gfx1013 frame UC defaults count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[0]), AGC_PM4_OP_CONTEXT_CONTROL,
        "gfx1013 frame context-control first");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[3]), AGC_PM4_OP_CLEAR_STATE_AGC,
        "gfx1013 frame clear-state second");
    TEST_ASSERT_EQ(buffer[2190], AGC_REG_CB_COLOR0_BASE,
        "gfx1013 frame color target follows defaults");
    TEST_ASSERT_EQ(buffer[2218], AGC_REG_PA_CL_VPORT_XSCALE,
        "gfx1013 frame viewport follows target");
    TEST_ASSERT_EQ(buffer[2233], AGC_REG_PA_SC_SCREEN_SCISSOR_TL,
        "gfx1013 frame scissor follows viewport");
    TEST_ASSERT_EQ(buffer[2255], AGC_REG_CB_TARGET_MASK,
        "gfx1013 frame target mask order");
    TEST_ASSERT_EQ(buffer[2258], AGC_REG_GE_MIN_VTX_INDX,
        "gfx1013 frame vertex bounds order");
    TEST_ASSERT_EQ(buffer[2267], AGC_REG_PA_SC_NGG_MODE_CNTL,
        "gfx1013 frame NGG launch state order");
    TEST_ASSERT_EQ(buffer[2273], AGC_REG_VGT_INSTANCE_STEP_RATE_0,
        "gfx1013 frame instance-step register");

    agcCbReset(&cb, buffer, AGC_GFX1013_FRAME_POST_BIND_DWORDS *
        sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013ApplyFramePostBind(&cb, &frame), AGC_OK,
        "gfx1013 frame post-bind state composes");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_FRAME_POST_BIND_DWORDS,
        "gfx1013 frame post-bind exact dword count");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_DB_DEPTH_INFO,
        "gfx1013 frame post-bind depth first");
    TEST_ASSERT_EQ(buffer[16], AGC_REG_SPI_SHADER_COL_FORMAT,
        "gfx1013 frame post-bind color export after depth");
    TEST_ASSERT_EQ(buffer[17], 4u,
        "gfx1013 frame post-bind RGBA8 export value");
    TEST_ASSERT_EQ(buffer[19], AGC_REG_PA_CL_CLIP_CNTL,
        "gfx1013 frame post-bind clip after color export");
    TEST_ASSERT_EQ(buffer[22], AGC_REG_PA_SU_SC_MODE_CNTL,
        "gfx1013 frame post-bind raster last");

    for (uint32_t i = 0u;
         i < sizeof(qualification_formats) / sizeof(qualification_formats[0]);
         ++i) {
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&frame.color_target,
            0x0000000201600000ull, 2048u, 1080u,
            qualification_formats[i].format), AGC_OK,
            "gfx1013 qualification target initializes");
        agcCbReset(&cb, buffer, sizeof(buffer));
        TEST_ASSERT_EQ(agcGfx1013ApplyFramePostBind(&cb, &frame), AGC_OK,
            qualification_formats[i].name);
        TEST_ASSERT_EQ(buffer[17], qualification_formats[i].shader_export,
            "gfx1013 qualification exact SPI color export");
    }

    TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&frame.color_target,
        0x0000000201600000ull, 2048u, 1080u,
        AGC_GFX1013_RT_FORMAT_RGBA8_UNORM), AGC_OK,
        "gfx1013 MRT slot zero initializes");
    TEST_ASSERT_EQ(agcGfx1013InitColorTarget(
        &frame.additional_color_targets[0], 0x0000000201800000ull,
        2048u, 1080u, AGC_GFX1013_RT_FORMAT_RGBA8_UNORM), AGC_OK,
        "gfx1013 MRT slot one initializes");
    frame.color_target_count = 2u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013BuildFramePrologue(&cb, &frame, NULL), AGC_OK,
        "gfx1013 two-target frame prologue composes");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_FRAME_PROLOGUE_BASE_DWORDS + 28u,
        "gfx1013 two-target prologue exact dword count");
    TEST_ASSERT_EQ(buffer[2218], AGC_REG_CB_COLOR0_BASE + 15u,
        "gfx1013 frame binds color slot one after slot zero");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ApplyFramePostBind(&cb, &frame), AGC_OK,
        "gfx1013 two-target post-bind composes");
    TEST_ASSERT_EQ(buffer[17], 0x44u,
        "gfx1013 two-target SPI exports contain both slots");

    agcCbReset(&cb, buffer,
        (AGC_GFX1013_FRAME_PROLOGUE_BASE_DWORDS + 27u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013BuildFramePrologue(
        &cb, &frame, NULL), AGC_ERROR_BUFFER_TOO_SMALL,
        "short gfx1013 frame prologue rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short gfx1013 frame prologue is atomic");
    frame.scissor.right = frame.color_target.width + 1u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013BuildFramePrologue(
        &cb, &frame, NULL), AGC_ERROR_INVALID_ARGUMENT,
        "out-of-target frame scissor rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid gfx1013 frame prologue is atomic");
}

static void test_gfx1013_eop_completion_fence(void)
{
    uint32_t buffer[16] = {0};
    SceAgcCb cb;
    AgcGfx1013EopFenceState fence = {
        .address = 0x00000002014bb000ull,
        .value = 0xdeadcafeu,
    };

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SignalEopFence(&cb, &fence), AGC_OK,
        "gfx1013 EOP completion fence emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), AGC_GFX1013_EOP_FENCE_DWORDS,
        "gfx1013 EOP completion exact dword count");
    TEST_ASSERT_EQ(buffer[0],
        agcPm4Header3(AGC_PM4_OP_RELEASE_MEM, 8u),
        "gfx1013 EOP release header");
    TEST_ASSERT_EQ(buffer[1], 0x06603514u,
        "gfx1013 EOP event GCR and LRU policy");
    TEST_ASSERT_EQ(buffer[2], 0x20000000u,
        "gfx1013 EOP SEND_DATA32 selection");
    TEST_ASSERT_EQ(buffer[3], 0x014bb000u,
        "gfx1013 EOP address low");
    TEST_ASSERT_EQ(buffer[4], 0x00000002u,
        "gfx1013 EOP address high");
    TEST_ASSERT_EQ(buffer[5], 0xdeadcafeu,
        "gfx1013 EOP fence value");
    TEST_ASSERT_EQ(buffer[6], 0u, "gfx1013 EOP fence value high");
    TEST_ASSERT_EQ(buffer[8], agcPm4Header3(AGC_PM4_OP_NOP, 2u),
        "gfx1013 EOP trailer header");

    agcCbReset(&cb, buffer, 9u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SignalEopFence(&cb, &fence),
        AGC_ERROR_BUFFER_TOO_SMALL, "short EOP fence rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short EOP fence is atomic");
    fence.address++;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SignalEopFence(&cb, &fence),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned EOP fence rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "unaligned EOP fence is atomic");
}

static void test_gfx1013_occlusion_snapshot(void)
{
    uint32_t buffer[8] = {0};
    SceAgcCb cb;
    const uint64_t address = 0x00000002014bc008ull;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013WriteOcclusionSnapshot(&cb, address), AGC_OK,
        "gfx1013 occlusion snapshot emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_OCCLUSION_SNAPSHOT_DWORDS,
        "gfx1013 occlusion snapshot exact dword count");
    TEST_ASSERT_EQ(buffer[0], agcPm4Header3(AGC_PM4_OP_EVENT_WRITE, 4u),
        "gfx1013 occlusion event header");
    TEST_ASSERT_EQ(buffer[1], 0x115u,
        "gfx1013 occlusion ZPASS event control");
    TEST_ASSERT_EQ(buffer[2], 0x014bc008u,
        "gfx1013 occlusion address low");
    TEST_ASSERT_EQ(buffer[3], 2u, "gfx1013 occlusion address high");

    agcCbReset(&cb, buffer, 3u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013WriteOcclusionSnapshot(&cb, address),
        AGC_ERROR_BUFFER_TOO_SMALL, "short occlusion snapshot rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short occlusion snapshot is atomic");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013WriteOcclusionSnapshot(&cb, address + 1u),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned occlusion snapshot rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "unaligned occlusion snapshot emits nothing");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013BeginOcclusionQuery(&cb, address, 0u), AGC_OK,
        "gfx1013 occlusion begin emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_OCCLUSION_QUERY_OP_DWORDS,
        "gfx1013 occlusion begin exact dword count");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_DB_COUNT_CONTROL,
        "gfx1013 occlusion begin selects count control");
    TEST_ASSERT_EQ(buffer[2], 0x11000102u,
        "gfx1013 occlusion begin enables perfect ZPASS counting");
    TEST_ASSERT_EQ(buffer[4], 0x115u,
        "gfx1013 occlusion begin snapshots after enabling counters");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013EndOcclusionQuery(&cb, address + 8u), AGC_OK,
        "gfx1013 occlusion end emits");
    TEST_ASSERT_EQ(buffer[1], 0x115u,
        "gfx1013 occlusion end snapshots before disabling counters");
    TEST_ASSERT_EQ(buffer[5], AGC_REG_DB_COUNT_CONTROL,
        "gfx1013 occlusion end selects count control");
    TEST_ASSERT_EQ(buffer[6], 1u,
        "gfx1013 occlusion end disables ZPASS increments");

    agcCbReset(&cb, buffer, 6u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013BeginOcclusionQuery(&cb, address, 0u),
        AGC_ERROR_BUFFER_TOO_SMALL, "short occlusion begin rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short occlusion begin is atomic");
}

static void test_gfx1013_resource_transitions(void)
{
    uint32_t buffer[AGC_GFX1013_TRANSITION_MAX_DWORDS] = {0};
    uint32_t expected[AGC_GFX1013_TRANSITION_MAX_DWORDS] = {0};
    SceAgcCb cb;
    uint32_t dword_count = 0u;
    AgcGfx1013ResourceTransition transition = {
        .before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET,
        .after = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ,
        .completion_address = 0x00000002014bb000ull,
        .completion_value = 0x1234abcdu,
    };
    const uint32_t release[AGC_GFX1013_EOP_FENCE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_RELEASE_MEM, 8u),
        0x06603514u, 0x20000000u, 0x014bb000u, 0x00000002u,
        0x1234abcdu, 0u, 0u,
        agcPm4Header3(AGC_PM4_OP_NOP, 2u), 0u,
    };
    const uint32_t acquire[AGC_GFX1013_ACQUIRE_MEM_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_ACQUIRE_MEM, 8u),
        0u, 0xffffffffu, 0x00ffffffu, 0u, 0u,
        AGC_GFX1013_ACQUIRE_POLL_INTERVAL,
        AGC_GFX1013_ACQUIRE_GCR_ALL,
    };
    const uint32_t depth_release[
        AGC_GFX1013_DB_META_FLUSH_DWORDS +
        AGC_GFX1013_EOP_FENCE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_EVENT_WRITE, 2u),
        AGC_GFX1013_DB_META_FLUSH_EVENT,
        agcPm4Header3(AGC_PM4_OP_RELEASE_MEM, 8u),
        0x0660352bu, 0x20000000u, 0x014bb000u, 0x00000002u,
        0x1234abcdu, 0u, 0u,
        agcPm4Header3(AGC_PM4_OP_NOP, 2u), 0u,
    };

    memcpy(expected, release, sizeof(release));
    memcpy(&expected[AGC_GFX1013_EOP_FENCE_DWORDS], acquire,
        sizeof(acquire));
    TEST_ASSERT_EQ(agcGfx1013GetResourceTransitionDwords(
        &transition, &dword_count), AGC_OK,
        "render-to-shader transition sizes");
    TEST_ASSERT_EQ(dword_count,
        AGC_GFX1013_EOP_FENCE_DWORDS + AGC_GFX1013_ACQUIRE_MEM_DWORDS,
        "render-to-shader release and acquire size");
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "render-to-shader transition emits");
    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0,
        "render-to-shader exact release/acquire order");

    transition.after = AGC_GFX1013_RESOURCE_USAGE_PRESENT;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "render-to-present transition emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), AGC_GFX1013_EOP_FENCE_DWORDS,
        "render-to-present release-only size");
    TEST_ASSERT(memcmp(buffer, release, sizeof(release)) == 0,
        "render-to-present exact EOP stream");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION;
    transition.completion_address = 0u;
    transition.completion_value = 0u;
    expected[2] = 0u;
    expected[3] = 0u;
    expected[4] = 0u;
    expected[5] = 0u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "compute-to-copy transition emits");
    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0,
        "compute-to-copy exact release/acquire order");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "copy-to-shader transition emits");
    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0,
        "copy-to-shader exact release/acquire order");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_PRESENT;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "present-to-render transition emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), AGC_GFX1013_ACQUIRE_MEM_DWORDS,
        "present-to-render acquire-only size");
    TEST_ASSERT(memcmp(buffer, acquire, sizeof(acquire)) == 0,
        "present-to-render exact acquire stream");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_COPY_SOURCE;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "read-to-read transition succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "read-to-read transition is an explicit no-op");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ;
    transition.completion_address = 0x00000002014bb000ull;
    transition.completion_value = 0x1234abcdu;
    memcpy(expected, depth_release, sizeof(depth_release));
    memcpy(&expected[sizeof(depth_release) / sizeof(uint32_t)], acquire,
        sizeof(acquire));
    TEST_ASSERT_EQ(agcGfx1013GetResourceTransitionDwords(
        &transition, &dword_count), AGC_OK,
        "depth-write-to-depth-read transition sizes");
    TEST_ASSERT_EQ(dword_count, AGC_GFX1013_TRANSITION_MAX_DWORDS,
        "depth transition includes metadata, data, and acquire packets");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "depth-write-to-depth-read transition emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_TRANSITION_MAX_DWORDS,
        "depth transition exact dword count");
    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0,
        "depth transition exact metadata/release/acquire stream");

    transition.after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "depth-write-to-host transition emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_DB_META_FLUSH_DWORDS +
        AGC_GFX1013_EOP_FENCE_DWORDS,
        "depth-to-host is metadata plus release only");
    TEST_ASSERT(memcmp(buffer, depth_release, sizeof(depth_release)) == 0,
        "depth-to-host exact DB flush stream");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ;
    transition.completion_address = 0u;
    transition.completion_value = 0u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition), AGC_OK,
        "depth-read-to-shader-read transition succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "depth-read-to-shader-read is an explicit no-op");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ;
    agcCbReset(&cb, buffer,
        (AGC_GFX1013_EOP_FENCE_DWORDS +
         AGC_GFX1013_ACQUIRE_MEM_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition),
        AGC_ERROR_BUFFER_TOO_SMALL, "short transition rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short transition is atomic");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE;
    agcCbReset(&cb, buffer,
        (AGC_GFX1013_TRANSITION_MAX_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition),
        AGC_ERROR_BUFFER_TOO_SMALL, "short depth transition rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short depth transition is atomic");

    transition.before = AGC_GFX1013_RESOURCE_USAGE_COUNT;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition),
        AGC_ERROR_INVALID_ARGUMENT, "invalid source usage rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid usage is atomic");
    transition.before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
    transition.completion_address = 3u;
    TEST_ASSERT_EQ(agcGfx1013TransitionResource(&cb, &transition),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned transition signal rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "unaligned signal is atomic");
}

static void test_gfx1013_fixed_function_packets(void)
{
    static const struct {
        AgcGfx1013ColorTargetFormat format;
        uint32_t color_format;
        uint32_t number_type;
        uint32_t component_swap;
        uint32_t bytes_per_pixel;
        uint32_t spi_export;
        uint32_t color_info;
    } format_cases[] = {
        {AGC_GFX1013_RT_FORMAT_R8_UNORM, 0x01u, 0u, 0u, 1u, 4u,
         0x00028004u},
        {AGC_GFX1013_RT_FORMAT_RG8_UNORM, 0x03u, 0u, 0u, 2u, 4u,
         0x0002800cu},
        {AGC_GFX1013_RT_FORMAT_RGBA8_UNORM, 0x0au, 0u, 0u, 4u, 4u,
         0x00028028u},
        {AGC_GFX1013_RT_FORMAT_BGRA8_UNORM, 0x0au, 0u, 1u, 4u, 4u,
         0x00028828u},
        {AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM, 0x08u, 0u, 0u, 4u, 4u,
         0x00028020u},
        {AGC_GFX1013_RT_FORMAT_R16_FLOAT, 0x02u, 7u, 0u, 2u, 4u,
         0x00060708u},
        {AGC_GFX1013_RT_FORMAT_RG16_FLOAT, 0x05u, 7u, 0u, 4u, 4u,
         0x00060714u},
        {AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT, 0x0cu, 7u, 0u, 8u, 4u,
         0x00060730u},
        {AGC_GFX1013_RT_FORMAT_R32_FLOAT, 0x04u, 7u, 0u, 4u, 1u,
         0x00060710u},
        {AGC_GFX1013_RT_FORMAT_RG32_FLOAT, 0x0bu, 7u, 0u, 8u, 2u,
         0x0006072cu},
        {AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT, 0x0eu, 7u, 0u, 16u, 9u,
         0x00060738u},
        {AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT, 0x06u, 7u, 0u, 4u, 4u,
         0x00060718u},
        {AGC_GFX1013_RT_FORMAT_RGBA8_SRGB, 0x0au, 6u, 0u, 4u, 4u,
         0x00028628u},
        {AGC_GFX1013_RT_FORMAT_BGRA8_SRGB, 0x0au, 6u, 1u, 4u, 4u,
         0x00028e28u},
    };
    uint32_t buffer[64] = {0};
    uint32_t expected_format[28];
    SceAgcCb cb;
    AgcGfx1013ColorTargetState typed_color;
    AgcGfx1013ColorTargetFormatInfo format_info;
    uint32_t i;
    const AgcGfx1013ColorTargetState color = {
        0x0000000201600000ull, 1920u, 1080u,
        AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
        AGC_GFX1013_SURFACE_NUMBER_UNORM,
        AGC_GFX1013_SURFACE_SWAP_ALT,
    };
    const AgcGfx1013ViewportState viewport = {1920u, 1080u};
    const AgcGfx1013ViewportState vulkan_viewport = {
        1920u, 1080u, AGC_GFX1013_CLIP_SPACE_ZERO_TO_ONE,
    };
    const AgcGfx1013ScissorState scissor = {0u, 0u, 1920u, 1080u};
    const uint32_t expected_color[28] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 16u),
        AGC_REG_CB_COLOR0_BASE,
        0x02016000u, 0x000000efu, 0x0003f47fu, 0u,
        0x00028828u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_CB_COLOR0_BASE_EXT, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_CB_COLOR0_ATTRIB2, 0x01dfc437u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_CB_COLOR0_ATTRIB3, 0x09000001u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_CB_COLOR_CONTROL, 0x00cc0010u,
    };
    const uint32_t expected_viewport[15] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 8u),
        AGC_REG_PA_CL_VPORT_XSCALE,
        0x44070000u, 0x44700000u, 0xc4070000u,
        0x44070000u, 0x3f000000u, 0x3f000000u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u),
        AGC_REG_PA_SC_VPORT_ZMIN_0, 0u, 0x3f800000u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_CL_VTE_CNTL, 0x0000043fu,
    };
    const uint32_t expected_vulkan_viewport[15] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 8u),
        AGC_REG_PA_CL_VPORT_XSCALE,
        0x44070000u, 0x44700000u, 0xc4070000u,
        0x44070000u, 0x3f800000u, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u),
        AGC_REG_PA_SC_VPORT_ZMIN_0, 0u, 0x3f800000u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_CL_VTE_CNTL, 0x0000043fu,
    };
    const uint32_t packed_br = 0x04380780u;
    const uint32_t expected_scissor[22] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u),
        AGC_REG_PA_SC_SCREEN_SCISSOR_TL, 0u, packed_br,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_SC_WINDOW_SCISSOR_TL, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_SC_WINDOW_SCISSOR_BR, packed_br,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_SC_GENERIC_SCISSOR_TL, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_SC_GENERIC_SCISSOR_BR, packed_br,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_SC_VPORT_SCISSOR_0_TL, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_SC_VPORT_SCISSOR_0_BR, packed_br,
    };
    const uint32_t expected_depth[15] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_INFO, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_Z_INFO, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_STENCIL_INFO, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_SHADER_CONTROL, 0x10u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_CONTROL, 0u,
    };

    TEST_ASSERT_EQ((uint32_t)AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT,
        11u, "gfx1013 existing color-format enum value is stable");
    TEST_ASSERT_EQ((uint32_t)AGC_GFX1013_RT_FORMAT_RGBA8_SRGB,
        12u, "gfx1013 RGBA8 SRGB enum is appended");
    TEST_ASSERT_EQ((uint32_t)AGC_GFX1013_RT_FORMAT_BGRA8_SRGB,
        13u, "gfx1013 BGRA8 SRGB enum is appended");

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &color), AGC_OK,
        "gfx1013 RGBA8 target emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 28u,
        "gfx1013 color target exact dword count");
    TEST_ASSERT(memcmp(buffer, expected_color, sizeof(expected_color)) == 0,
        "gfx1013 color target exact packet stream");

    for (i = 0u; i < sizeof(format_cases) / sizeof(format_cases[0]); ++i) {
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            format_cases[i].format, &format_info), AGC_OK,
            "gfx1013 color format resolves");
        TEST_ASSERT_EQ(format_info.color_format,
            format_cases[i].color_format, "gfx1013 exact CB format");
        TEST_ASSERT_EQ(format_info.number_type,
            format_cases[i].number_type, "gfx1013 exact number type");
        TEST_ASSERT_EQ(format_info.component_swap,
            format_cases[i].component_swap, "gfx1013 exact component swap");
        TEST_ASSERT_EQ(format_info.bytes_per_pixel,
            format_cases[i].bytes_per_pixel, "gfx1013 exact pixel size");
        TEST_ASSERT_EQ(format_info.spi_shader_export_format,
            format_cases[i].spi_export, "gfx1013 matching SPI export");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(
            &typed_color, color.address, 2048u, color.height,
            format_cases[i].format), AGC_OK,
            "gfx1013 typed color target initializes");
        memcpy(expected_format, expected_color, sizeof(expected_format));
        expected_format[3] = 0x000000ffu;
        expected_format[4] = 0x000437ffu;
        expected_format[6] = format_cases[i].color_info;
        expected_format[21] = 0x01ffc437u;
        agcCbReset(&cb, buffer, sizeof(buffer));
        TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &typed_color), AGC_OK,
            "gfx1013 typed color target emits");
        TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 28u,
            "gfx1013 typed color target exact dword count");
        TEST_ASSERT(memcmp(buffer, expected_format,
            sizeof(expected_format)) == 0,
            "gfx1013 typed color target exact packet stream");
    }

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetViewport(&cb, &viewport), AGC_OK,
        "gfx1013 viewport emits");
    TEST_ASSERT(memcmp(buffer, expected_viewport,
        sizeof(expected_viewport)) == 0,
        "gfx1013 viewport exact packet stream");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetViewport(&cb, &vulkan_viewport), AGC_OK,
        "gfx1013 Vulkan viewport emits");
    TEST_ASSERT(memcmp(buffer, expected_vulkan_viewport,
        sizeof(expected_vulkan_viewport)) == 0,
        "gfx1013 Vulkan viewport exact packet stream");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetScissor(&cb, &scissor), AGC_OK,
        "gfx1013 scissor emits");
    TEST_ASSERT(memcmp(buffer, expected_scissor,
        sizeof(expected_scissor)) == 0,
        "gfx1013 scissor exact packet stream");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetTargetMask(
        &cb, AGC_GFX1013_TARGET_MASK_RGBA0), AGC_OK,
        "gfx1013 target mask emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 3u,
        "gfx1013 target mask exact dword count");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_CB_TARGET_MASK,
        "gfx1013 target mask offset");
    TEST_ASSERT_EQ(buffer[2], AGC_GFX1013_TARGET_MASK_RGBA0,
        "gfx1013 target mask value");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthDisabled(&cb), AGC_OK,
        "gfx1013 depth-disabled state emits");
    TEST_ASSERT(memcmp(buffer, expected_depth, sizeof(expected_depth)) == 0,
        "gfx1013 depth-disabled exact packet stream");
}

static void test_gfx1013_blend_depth_stencil_packets(void)
{
    uint32_t buffer[32] = {0};
    SceAgcCb cb;
    AgcGfx1013ColorBlendState blend = {0};
    AgcGfx1013DepthBiasState bias = {
        .format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT,
        .constant_factor = 2.0f,
        .clamp = 0.25f,
        .slope_factor = -1.5f,
    };
    AgcGfx1013DepthStencilState depth = {0};
    const uint32_t expected_blend[AGC_GFX1013_BLEND_STATE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 10u),
        AGC_REG_CB_BLEND0_CONTROL,
        0x65010504u, 0x41010101u, 0u, 0u, 0u, 0u, 0u, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 10u),
        AGC_REG_SX_MRT0_BLEND_OPT,
        0x01770177u, 0x01770177u,
        0x06770677u, 0x06770677u, 0x06770677u,
        0x06770677u, 0x06770677u, 0x06770677u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_CB_TARGET_MASK, 0x0000007fu,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 6u),
        AGC_REG_CB_BLEND_RED,
        0x3e800000u, 0x3f000000u, 0x3f400000u, 0x3f800000u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_CB_COLOR_CONTROL, 0x00cc0010u,
    };
    const uint8_t expected_rop3[AGC_GFX1013_LOGIC_OP_COUNT] = {
        0x00u, 0x88u, 0x44u, 0xccu,
        0x22u, 0xaau, 0x66u, 0xeeu,
        0x11u, 0x99u, 0x55u, 0xddu,
        0x33u, 0xbbu, 0x77u, 0xffu,
    };
    const uint32_t expected_depth[AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_CONTROL, 0x001007bfu,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_STENCIL_CONTROL, 0x00971530u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u),
        AGC_REG_DB_STENCILREFMASK, 0x12cdab12u, 0x34785634u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u),
        AGC_REG_DB_DEPTH_BOUNDS_MIN, 0x3e800000u, 0x3f400000u,
    };
    const uint32_t expected_bias[AGC_GFX1013_DEPTH_BIAS_STATE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_PA_SU_POLY_OFFSET_DB_FMT_CNTL, 0x000001e9u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 7u),
        AGC_REG_PA_SU_POLY_OFFSET_CLAMP, 0x3e800000u, 0xc1c00000u,
        0x40000000u, 0xc1c00000u, 0x40000000u,
    };

    TEST_ASSERT_EQ(AGC_GFX1013_BLEND_CONSTANT_ALPHA, 19u,
        "gfx1013 constant-alpha blend encoding");
    TEST_ASSERT_EQ(AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_ALPHA, 20u,
        "gfx1013 inverse constant-alpha encoding");
    TEST_ASSERT_EQ(AGC_GFX1013_BLEND_OP_REVERSE_SUBTRACT, 4u,
        "gfx1013 reverse-subtract encoding");
    TEST_ASSERT_EQ(AGC_GFX1013_COMPARE_ALWAYS, 7u,
        "gfx1013 compare encoding");
    TEST_ASSERT_EQ(AGC_GFX1013_STENCIL_REPLACE, 3u,
        "gfx1013 stencil replace-test encoding");
    TEST_ASSERT_EQ(AGC_GFX1013_STENCIL_DECREMENT_WRAP, 9u,
        "gfx1013 stencil decrement-wrap encoding");

    blend.target_count = 2u;
    blend.targets[0].enable = 1u;
    blend.targets[0].color_source = AGC_GFX1013_BLEND_SRC_ALPHA;
    blend.targets[0].color_destination =
        AGC_GFX1013_BLEND_ONE_MINUS_SRC_ALPHA;
    blend.targets[0].separate_alpha = 1u;
    blend.targets[0].alpha_source = AGC_GFX1013_BLEND_ONE;
    blend.targets[0].alpha_destination =
        AGC_GFX1013_BLEND_ONE_MINUS_SRC_ALPHA;
    blend.targets[0].write_mask = 0xfu;
    blend.targets[1].enable = 1u;
    blend.targets[1].color_source = AGC_GFX1013_BLEND_ONE;
    blend.targets[1].color_destination = AGC_GFX1013_BLEND_ONE;
    blend.targets[1].alpha_source = AGC_GFX1013_BLEND_ONE;
    blend.targets[1].alpha_destination = AGC_GFX1013_BLEND_ONE;
    blend.targets[1].write_mask = 0x7u;
    blend.constants[0] = 0.25f;
    blend.constants[1] = 0.5f;
    blend.constants[2] = 0.75f;
    blend.constants[3] = 1.0f;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorBlendState(&cb, &blend), AGC_OK,
        "gfx1013 typed blend state emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), AGC_GFX1013_BLEND_STATE_DWORDS,
        "gfx1013 blend exact dword count");
    TEST_ASSERT(memcmp(buffer, expected_blend, sizeof(expected_blend)) == 0,
        "gfx1013 blend exact packet stream");
    blend.logic_enable = 1u;
    for (uint32_t operation = 0u;
         operation < AGC_GFX1013_LOGIC_OP_COUNT; ++operation) {
        blend.logic_operation = (AgcGfx1013LogicOp)operation;
        agcCbReset(&cb, buffer, sizeof(buffer));
        TEST_ASSERT_EQ(agcGfx1013SetColorBlendState(&cb, &blend), AGC_OK,
            "gfx1013 logic operation emits");
        TEST_ASSERT_EQ(buffer[AGC_GFX1013_BLEND_STATE_DWORDS - 1u],
            ((uint32_t)expected_rop3[operation] << 16u) | 0x10u,
            "gfx1013 logic operation exact ROP3");
    }
    blend.logic_enable = 0u;
    blend.logic_operation = AGC_GFX1013_LOGIC_CLEAR;

    blend.target_count = 1u;
    blend.targets[0].color_source = AGC_GFX1013_BLEND_SRC1_COLOR;
    blend.targets[0].color_destination = AGC_GFX1013_BLEND_ZERO;
    blend.targets[0].alpha_source = AGC_GFX1013_BLEND_SRC1_ALPHA;
    blend.targets[0].alpha_destination = AGC_GFX1013_BLEND_ZERO;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorBlendState(&cb, &blend), AGC_OK,
        "gfx1013 dual-source blend state emits");
    for (uint32_t target = 0u; target < AGC_GFX1013_MAX_COLOR_TARGETS;
         ++target) {
        TEST_ASSERT_EQ(buffer[12u + target], 0u,
            "gfx1013 dual-source blend disables RB+ optimization");
    }
    TEST_ASSERT_EQ(buffer[AGC_GFX1013_BLEND_STATE_DWORDS - 1u],
        0x00cc0011u,
        "gfx1013 dual-source blend disables dual-quad mode");

    blend.target_count = 2u;
    blend.targets[0].color_source = AGC_GFX1013_BLEND_ONE;
    blend.targets[0].alpha_source = AGC_GFX1013_BLEND_ONE;
    blend.targets[1].color_source = AGC_GFX1013_BLEND_SRC1_COLOR;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorBlendState(&cb, &blend),
        AGC_ERROR_INVALID_ARGUMENT,
        "gfx1013 dual-source blend rejects nonzero targets");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid dual-source blend state is atomic");
    blend.target_count = 1u;
    blend.targets[1].color_source = AGC_GFX1013_BLEND_ONE;

    TEST_ASSERT_EQ(AGC_GFX1013_DEPTH_BIAS_RASTER_MODE, 0x00001800u,
        "gfx1013 depth-bias front/back enable mask");
    TEST_ASSERT_EQ(AGC_GFX1013_VULKAN_CLIP_CONTROL, 0x00080000u,
        "gfx1013 Vulkan zero-to-one clip control");
    TEST_ASSERT_EQ(AGC_GFX1013_DEPTH_CLAMP_CLIP_CONTROL, 0x0c080000u,
        "gfx1013 Vulkan depth-clamp clip control");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthBiasState(&cb, &bias), AGC_OK,
        "gfx1013 typed depth-bias state emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_DEPTH_BIAS_STATE_DWORDS,
        "gfx1013 depth-bias exact dword count");
    TEST_ASSERT(memcmp(buffer, expected_bias, sizeof(expected_bias)) == 0,
        "gfx1013 depth-bias exact packet stream");

    agcCbReset(&cb, buffer,
        (AGC_GFX1013_DEPTH_BIAS_STATE_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetDepthBiasState(&cb, &bias),
        AGC_ERROR_BUFFER_TOO_SMALL, "short depth-bias state rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short depth-bias state is atomic");
    bias.format = AGC_GFX1013_DEPTH_FORMAT_S8_UINT;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthBiasState(&cb, &bias),
        AGC_ERROR_INVALID_ARGUMENT, "stencil-only depth bias rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid depth-bias state is atomic");

    depth.depth_test_enable = 1u;
    depth.depth_write_enable = 1u;
    depth.depth_compare_operation = AGC_GFX1013_COMPARE_LESS_EQUAL;
    depth.depth_bounds_enable = 1u;
    depth.min_depth_bounds = 0.25f;
    depth.max_depth_bounds = 0.75f;
    depth.stencil_test_enable = 1u;
    depth.back_face_enable = 1u;
    depth.front.compare_operation = AGC_GFX1013_COMPARE_ALWAYS;
    depth.front.fail_operation = AGC_GFX1013_STENCIL_KEEP;
    depth.front.depth_fail_operation = AGC_GFX1013_STENCIL_INCREMENT_CLAMP;
    depth.front.pass_operation = AGC_GFX1013_STENCIL_REPLACE;
    depth.front.reference = 0x12u;
    depth.front.compare_mask = 0xabu;
    depth.front.write_mask = 0xcdu;
    depth.back.compare_operation = AGC_GFX1013_COMPARE_LESS;
    depth.back.fail_operation = AGC_GFX1013_STENCIL_ZERO;
    depth.back.depth_fail_operation = AGC_GFX1013_STENCIL_DECREMENT_WRAP;
    depth.back.pass_operation = AGC_GFX1013_STENCIL_INVERT;
    depth.back.reference = 0x34u;
    depth.back.compare_mask = 0x56u;
    depth.back.write_mask = 0x78u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthStencilState(&cb, &depth), AGC_OK,
        "gfx1013 typed depth/stencil state emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS,
        "gfx1013 depth/stencil exact dword count");
    TEST_ASSERT(memcmp(buffer, expected_depth, sizeof(expected_depth)) == 0,
        "gfx1013 depth/stencil exact packet stream");

    agcCbReset(&cb, buffer,
        (AGC_GFX1013_BLEND_STATE_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetColorBlendState(&cb, &blend),
        AGC_ERROR_BUFFER_TOO_SMALL, "short blend state rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "short blend state is atomic");
    blend.targets[0].write_mask = 0x10u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorBlendState(&cb, &blend),
        AGC_ERROR_INVALID_ARGUMENT, "invalid blend mask rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "invalid blend is atomic");
    blend.targets[0].write_mask = 0xfu;
    blend.logic_operation = AGC_GFX1013_LOGIC_OP_COUNT;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorBlendState(&cb, &blend),
        AGC_ERROR_INVALID_ARGUMENT, "invalid logic operation rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid logic operation is atomic");

    agcCbReset(&cb, buffer,
        (AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetDepthStencilState(&cb, &depth),
        AGC_ERROR_BUFFER_TOO_SMALL, "short depth/stencil state rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short depth/stencil state is atomic");
    depth.depth_test_enable = 0u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthStencilState(&cb, &depth),
        AGC_ERROR_INVALID_ARGUMENT, "depth write without test rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid depth/stencil is atomic");
    depth.depth_test_enable = 1u;
    depth.front.pass_operation = (AgcGfx1013StencilOp)4;
    TEST_ASSERT_EQ(agcGfx1013SetDepthStencilState(&cb, &depth),
        AGC_ERROR_INVALID_ARGUMENT, "nonstandard stencil op rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid stencil op is atomic");
}

static void test_gfx1013_depth_surface_packets(void)
{
    uint32_t buffer[AGC_GFX1013_DEPTH_SURFACE_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013DepthSurfaceState surface = {
        .depth_read_address = 0x0000123456789000ull,
        .depth_write_address = 0x0000123456790000ull,
        .stencil_read_address = 0x0000123456800000ull,
        .stencil_write_address = 0x0000123456810000ull,
        .htile_address = 0x0000123456820000ull,
        .width = 1920u,
        .height = 1080u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT,
        .depth_swizzle_mode = 13u,
        .stencil_swizzle_mode = 14u,
        .mip_level = 3u,
        .mip_level_count = 8u,
        .first_layer = 0x801u,
        .last_layer = 0x1002u,
        .sample_count = 4u,
        .depth_read_only = 1u,
        .htile_enable = 1u,
        .allow_expclear = 1u,
    };
    const uint32_t expected[AGC_GFX1013_DEPTH_SURFACE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_VIEW, 0x8d004801u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_HTILE_SURFACE, 0x00040000u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_HTILE_DATA_BASE, 0x34568200u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_SIZE_XY, 0x0437077fu,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 8u),
        AGC_REG_DB_Z_INFO, 0x280700dbu, 0x080000e1u,
        0x34567890u, 0x34568000u, 0x34567900u, 0x34568100u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 7u),
        AGC_REG_DB_Z_READ_BASE_HI,
        0x12u, 0x12u, 0x12u, 0x12u, 0x12u,
    };

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface), AGC_OK,
        "gfx1013 typed depth surface emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), AGC_GFX1013_DEPTH_SURFACE_DWORDS,
        "gfx1013 depth surface exact dword count");
    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0,
        "gfx1013 depth surface exact packet stream");

    agcCbReset(&cb, buffer,
        (AGC_GFX1013_DEPTH_SURFACE_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface),
        AGC_ERROR_BUFFER_TOO_SMALL, "short depth surface rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short depth surface is atomic");

    surface.depth_read_address++;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned depth address rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "unaligned depth surface is atomic");
    surface.depth_read_address--;
    surface.sample_count = 3u;
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface),
        AGC_ERROR_INVALID_ARGUMENT, "invalid depth sample count rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid sample count is atomic");
    surface.sample_count = 4u;
    surface.format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT;
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface),
        AGC_ERROR_INVALID_ARGUMENT,
        "depth-only format rejects stencil addresses");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "mismatched depth aspects are atomic");
    surface.format = (AgcGfx1013DepthSurfaceFormat)
        AGC_GFX1013_DEPTH_FORMAT_COUNT;
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface),
        AGC_ERROR_NOT_SUPPORTED, "unknown depth format rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "unsupported depth format is atomic");

    surface = (AgcGfx1013DepthSurfaceState){
        .depth_read_address = 0x0000123456790100ull,
        .depth_write_address = 0x0000123456790100ull,
        .width = 1920u,
        .height = 1080u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT,
        .depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
        .mip_level_count = 1u,
        .sample_count = 1u,
    };
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface),
        AGC_ERROR_INVALID_ALIGNMENT,
        "64KB_Z_X depth requires its layout alignment");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "misaligned 64KB_Z_X depth is atomic");
}

static void test_gfx1013_htile_operation_packets(void)
{
    uint32_t buffer[AGC_GFX1013_HTILE_OPERATION_DWORDS] = {0};
    SceAgcCb cb;
    const uint32_t header = agcPm4Header3(
        AGC_PM4_OP_SET_CONTEXT_REG, AGC_GFX1013_HTILE_OPERATION_DWORDS);

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(
        &cb, AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH), AGC_OK,
        "gfx1013 typed HTILE depth decompress emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_HTILE_OPERATION_DWORDS,
        "gfx1013 HTILE operation exact dword count");
    TEST_ASSERT_EQ(buffer[0], header, "HTILE decompress context header");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_DB_RENDER_CONTROL,
        "HTILE decompress render-control register");
    TEST_ASSERT_EQ(buffer[2], 0x00001040u,
        "HTILE decompress exact render-control value");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(
        &cb, AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH_STENCIL), AGC_OK,
        "gfx1013 typed HTILE depth-stencil decompress emits");
    TEST_ASSERT_EQ(buffer[2], 0x00001060u,
        "HTILE depth-stencil decompress exact render-control value");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(
        &cb, AGC_GFX1013_HTILE_OPERATION_RESUMMARIZE_DEPTH), AGC_OK,
        "gfx1013 typed HTILE depth resummarize emits");
    TEST_ASSERT_EQ(buffer[2], 0x00000010u,
        "HTILE resummarize exact render-control value");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(
        &cb, AGC_GFX1013_HTILE_OPERATION_NONE), AGC_OK,
        "gfx1013 typed HTILE neutral state emits");
    TEST_ASSERT_EQ(buffer[2], 0u,
        "HTILE neutral state clears render control");

    agcCbReset(&cb, buffer,
        (AGC_GFX1013_HTILE_OPERATION_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(
        &cb, AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH),
        AGC_ERROR_BUFFER_TOO_SMALL, "short HTILE operation rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short HTILE operation is atomic");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(&cb,
        (AgcGfx1013HtileOperation)AGC_GFX1013_HTILE_OPERATION_COUNT),
        AGC_ERROR_INVALID_ARGUMENT, "invalid HTILE operation rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid HTILE operation is atomic");
}

static void test_gfx1013_d16_htile_qualification_fixture(void)
{
    uint32_t buffer[AGC_GFX1013_DEPTH_SURFACE_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013DepthSurfaceLayoutInput depth_input = {
        .width = 1920u, .height = 1080u, .layer_count = 1u,
        .mip_level_count = 1u, .sample_count = 1u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM,
        .depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
    };
    AgcGfx1013DepthSurfaceLayout depth_layout = {0};
    AgcGfx1013HtileLayoutInput htile_input = {
        .width = 1920u, .height = 1080u, .layer_count = 1u,
        .mip_level_count = 1u, .first_mip_in_tail = 1u,
        .pipe_count = 8u,
        .swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
    };
    AgcGfx1013HtileLayout htile_layout = {0};
    const AgcGfx1013DepthSurfaceState surface = {
        .depth_read_address = 0x0000000203000000ull,
        .depth_write_address = 0x0000000203000000ull,
        .htile_address = 0x0000000203480000ull,
        .width = 1920u, .height = 1080u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM,
        .depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
        .mip_level_count = 1u, .sample_count = 1u,
        .htile_enable = 1u,
    };
    const uint32_t expected[AGC_GFX1013_DEPTH_SURFACE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_VIEW, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_HTILE_SURFACE, 0x00040000u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_HTILE_DATA_BASE, 0x02034800u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_SIZE_XY, 0x0437077fu,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 8u),
        AGC_REG_DB_Z_INFO, 0xa0000181u, 0u,
        0x02030000u, 0u, 0x02030000u, 0u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 7u),
        AGC_REG_DB_Z_READ_BASE_HI, 0u, 0u, 0u, 0u, 0u,
    };
    uint32_t expected_expclear[AGC_GFX1013_DEPTH_SURFACE_DWORDS];
    AgcGfx1013DepthSurfaceState expclear_surface = surface;

    TEST_ASSERT_EQ(AGC_GFX1013_HTILE_UNCOMPRESSED_D16, 0xfffc000fu,
        "gfx1013 D16 exact uncompressed HTILE word");
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(
        &depth_input, &depth_layout), AGC_OK,
        "gfx1013 D16 HTILE depth layout queries");
    TEST_ASSERT_EQ(depth_layout.depth.pitch, 2048u,
        "D16 HTILE exact depth pitch");
    TEST_ASSERT_EQ(depth_layout.depth.padded_height, 1152u,
        "D16 HTILE exact padded depth height");
    TEST_ASSERT_EQ(depth_layout.depth.allocation_size, 0x480000ull,
        "D16 HTILE exact depth allocation");
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(
        &htile_input, &htile_layout), AGC_OK,
        "gfx1013 D16 HTILE metadata layout queries");
    TEST_ASSERT_EQ(htile_layout.allocation_size, 0x30000ull,
        "D16 exact HTILE allocation");
    TEST_ASSERT_EQ(htile_layout.alignment, 0x4000u,
        "D16 exact HTILE alignment");
    TEST_ASSERT_EQ(htile_layout.meta_block_width, 512u,
        "D16 exact HTILE meta-block width");
    TEST_ASSERT_EQ(htile_layout.meta_block_height, 512u,
        "D16 exact HTILE meta-block height");
    TEST_ASSERT_EQ(htile_layout.meta_blocks_per_slice, 12u,
        "D16 exact HTILE blocks per slice");

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface), AGC_OK,
        "gfx1013 D16 HTILE surface emits");
    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0,
        "gfx1013 D16 HTILE exact 27-dword bind stream");

    memcpy(expected_expclear, expected, sizeof(expected_expclear));
    expected_expclear[14] = 0xaf800181u;
    expclear_surface.allow_expclear = 1u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(
        &cb, &expclear_surface), AGC_OK,
        "gfx1013 D16 HTILE expclear surface emits");
    TEST_ASSERT_EQ(buffer[14], expected_expclear[14],
        "gfx1013 D16 HTILE expclear exact DB_Z_INFO");
    TEST_ASSERT(memcmp(buffer, expected_expclear,
        sizeof(expected_expclear)) == 0,
        "gfx1013 D16 HTILE expclear exact 27-dword bind stream");
    TEST_ASSERT_EQ(AGC_GFX1013_HTILE_CLEAR_DEPTH_ONE, 0xfffffff0u,
        "gfx1013 D16 exact depth-one HTILE clear word");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(
        &cb, AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH), AGC_OK,
        "D16 HTILE decompress operation emits");
    TEST_ASSERT_EQ(buffer[2], 0x00001040u,
        "D16 HTILE exact decompress operation");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetHtileOperation(
        &cb, AGC_GFX1013_HTILE_OPERATION_RESUMMARIZE_DEPTH), AGC_OK,
        "D16 HTILE resummarize operation emits");
    TEST_ASSERT_EQ(buffer[2], 0x00000010u,
        "D16 HTILE exact resummarize operation");
}

static void test_gfx1013_depth_expclear_packets(void)
{
    uint32_t buffer[AGC_GFX1013_DEPTH_STENCIL_EXPCLEAR_MAX_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013DepthExpclearState state = {1.0f};

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthExpclear(&cb, &state), AGC_OK,
        "gfx1013 typed depth expclear emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_DEPTH_EXPCLEAR_DWORDS,
        "gfx1013 depth expclear exact dword count");
    TEST_ASSERT_EQ(buffer[0], agcPm4Header3(
        AGC_PM4_OP_SET_CONTEXT_REG, AGC_GFX1013_DEPTH_EXPCLEAR_DWORDS),
        "depth expclear context header");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_DB_DEPTH_CLEAR,
        "depth expclear register");
    TEST_ASSERT_EQ(buffer[2], 0x3f800000u,
        "depth-one expclear bits");

    state.clear_depth = 0.0f;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthExpclear(&cb, &state), AGC_OK,
        "gfx1013 depth-zero expclear emits");
    TEST_ASSERT_EQ(buffer[2], 0u, "depth-zero expclear bits");

    state.clear_depth = 0.5f;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthExpclear(&cb, &state),
        AGC_ERROR_NOT_SUPPORTED, "noncanonical expclear rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "unsupported expclear is atomic");

    state.clear_depth = 1.0f;
    agcCbReset(&cb, buffer,
        (AGC_GFX1013_DEPTH_EXPCLEAR_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetDepthExpclear(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL, "short depth expclear rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short depth expclear is atomic");

    AgcGfx1013HtileExpclearPlanState plan_state = {
        .aspects = AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH,
        .clear_depth = 1.0f,
    };
    AgcGfx1013HtileExpclearPlan plan = {0};
    TEST_ASSERT_EQ(agcGfx1013BuildHtileExpclearPlan(
        &plan_state, &plan), AGC_OK,
        "depth-only HTILE expclear plan builds");
    TEST_ASSERT_EQ(plan.write_value, 0xfffffff0u,
        "depth-only plan uses hardware-proven clear-one word");
    TEST_ASSERT_EQ(plan.write_mask, 0xffffffffu,
        "depth-only plan replaces the whole Z-only word");
    TEST_ASSERT_EQ(plan.requires_read_modify_write, 0u,
        "depth-only plan needs no read-modify-write");
    TEST_ASSERT_EQ(plan.hardware_enabled, 1u,
        "hardware-proven depth-only plan stays enabled");

    plan_state.has_stencil = 1u;
    TEST_ASSERT_EQ(agcGfx1013BuildHtileExpclearPlan(
        &plan_state, &plan), AGC_OK,
        "combined HTILE depth-aspect plan builds");
    TEST_ASSERT_EQ(plan.write_value, 0xfffc0000u,
        "combined depth-one plan value excludes stencil bits");
    TEST_ASSERT_EQ(plan.write_mask, 0xfffff00fu,
        "combined depth plan exact aspect mask");
    TEST_ASSERT_EQ((0xfffff30fu & ~plan.write_mask) |
        (plan.write_value & plan.write_mask), 0xfffc0300u,
        "combined depth plan preserves stencil metadata");
    TEST_ASSERT_EQ(plan.requires_read_modify_write, 1u,
        "combined depth plan requires read-modify-write");
    TEST_ASSERT_EQ(plan.hardware_enabled, 1u,
        "combined depth plan is hardware-enabled");

    plan_state.aspects = AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL;
    plan_state.clear_stencil = 0x5au;
    TEST_ASSERT_EQ(agcGfx1013BuildHtileExpclearPlan(
        &plan_state, &plan), AGC_OK,
        "combined HTILE stencil-aspect plan builds");
    TEST_ASSERT_EQ(plan.write_value, 0x000000f0u,
        "stencil plan records cleared pretest state");
    TEST_ASSERT_EQ(plan.write_mask, 0x000003f0u,
        "combined stencil plan exact aspect mask");
    TEST_ASSERT_EQ((0xfffff30fu & ~plan.write_mask) |
        (plan.write_value & plan.write_mask), 0xfffff0ffu,
        "combined stencil plan preserves depth metadata");
    TEST_ASSERT_EQ(plan.hardware_enabled, 1u,
        "combined stencil plan is hardware-enabled");

    plan_state.aspects =
        AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH |
        AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL;
    plan_state.clear_depth = 1.0f;
    TEST_ASSERT_EQ(agcGfx1013BuildHtileExpclearPlan(
        &plan_state, &plan), AGC_OK,
        "combined two-aspect HTILE expclear plan builds");
    TEST_ASSERT_EQ(plan.write_value, 0xfffc00f0u,
        "combined two-aspect clear-one value");
    TEST_ASSERT_EQ(plan.write_mask, 0xfffff3ffu,
        "combined two-aspect mask preserves reserved bits");
    TEST_ASSERT_EQ(plan.hardware_enabled, 1u,
        "combined two-aspect plan is hardware-enabled");

    plan.write_value = 0x11223344u;
    plan_state.clear_stencil = 0x100u;
    TEST_ASSERT_EQ(agcGfx1013BuildHtileExpclearPlan(
        &plan_state, &plan), AGC_ERROR_INVALID_ARGUMENT,
        "out-of-range stencil clear rejects");
    TEST_ASSERT_EQ(plan.write_value, 0x11223344u,
        "invalid combined plan preserves output");

    AgcGfx1013DepthStencilExpclearState clear_state = {
        .aspects = AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH |
            AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL,
        .clear_depth = 1.0f,
        .clear_stencil = 0x5au,
    };
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthStencilExpclear(
        &cb, &clear_state), AGC_OK,
        "combined clear registers emit");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 4u,
        "combined clear registers use one contiguous packet");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_DB_STENCIL_CLEAR,
        "combined clear begins at stencil register");
    TEST_ASSERT_EQ(buffer[2], 0x5au,
        "combined clear exact stencil value");
    TEST_ASSERT_EQ(buffer[3], 0x3f800000u,
        "combined clear exact depth value");

    clear_state.aspects = AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthStencilExpclear(
        &cb, &clear_state), AGC_OK,
        "stencil-only clear register emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 3u,
        "stencil-only clear exact packet size");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_DB_STENCIL_CLEAR,
        "stencil-only clear exact register");
    TEST_ASSERT_EQ(buffer[2], 0x5au,
        "stencil-only clear exact value");
}

static void test_gfx1013_selective_expclear_surface(void)
{
    uint32_t buffer[AGC_GFX1013_DEPTH_SURFACE_DWORDS] = {0};
    AgcGfx1013DepthSurfaceState surface = {
        .depth_read_address = 0x0000000202610000ull,
        .depth_write_address = 0x0000000202610000ull,
        .stencil_read_address = 0x0000000202e80000ull,
        .stencil_write_address = 0x0000000202e80000ull,
        .htile_address = 0x0000000203300000ull,
        .width = 1920u,
        .height = 1080u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT,
        .depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
        .stencil_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
        .mip_level_count = 1u,
        .sample_count = 1u,
        .htile_enable = 1u,
        .allow_expclear = 1u,
        .expclear_aspects = AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH,
    };
    SceAgcCb cb;
    uint32_t cursor;
    uint32_t z_info = 0u;
    uint32_t stencil_info = 0u;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface), AGC_OK,
        "depth-only expclear combined surface emits");
    for (cursor = 0u; cursor < agcCbUsedDwords(&cb);) {
        uint32_t length = agcPm4Length(buffer[cursor]);
        if (buffer[cursor + 1u] == AGC_REG_DB_Z_INFO) {
            z_info = buffer[cursor + 2u];
            stencil_info = buffer[cursor + 3u];
            break;
        }
        cursor += length;
    }
    TEST_ASSERT((z_info & AGC_REG_SET(
        DB_Z_INFO, ALLOW_EXPCLEAR, 1u)) != 0u,
        "depth aspect enables depth expclear");
    TEST_ASSERT((stencil_info & AGC_REG_SET(
        DB_STENCIL_INFO, ALLOW_EXPCLEAR, 1u)) == 0u,
        "depth aspect preserves stencil expclear disable");

    surface.expclear_aspects =
        AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthSurface(&cb, &surface), AGC_OK,
        "stencil-only expclear combined surface emits");
    for (cursor = 0u; cursor < agcCbUsedDwords(&cb);) {
        uint32_t length = agcPm4Length(buffer[cursor]);
        if (buffer[cursor + 1u] == AGC_REG_DB_Z_INFO) {
            z_info = buffer[cursor + 2u];
            stencil_info = buffer[cursor + 3u];
            break;
        }
        cursor += length;
    }
    TEST_ASSERT((z_info & AGC_REG_SET(
        DB_Z_INFO, ALLOW_EXPCLEAR, 1u)) == 0u,
        "stencil aspect preserves depth expclear disable");
    TEST_ASSERT((stencil_info & AGC_REG_SET(
        DB_STENCIL_INFO, ALLOW_EXPCLEAR, 1u)) != 0u,
        "stencil aspect enables stencil expclear");
}

static void test_gfx1013_depth_surface_layout(void)
{
    AgcGfx1013DepthSurfaceLayoutInput input = {
        .width = 1920u, .height = 1080u, .layer_count = 1u,
        .mip_level_count = 1u, .sample_count = 1u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT,
        .depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
    };
    AgcGfx1013DepthSurfaceLayout layout = {0};

    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout), AGC_OK,
        "gfx1013 D32 64KB-Z-X layout queries");
    TEST_ASSERT_EQ(layout.depth.pitch, 1920u, "D32 layout exact pitch");
    TEST_ASSERT_EQ(layout.depth.padded_height, 1152u,
        "D32 layout exact padded height");
    TEST_ASSERT_EQ(layout.depth.slice_size, 0x870000ull,
        "D32 layout exact slice size");
    TEST_ASSERT_EQ(layout.depth.allocation_size, 0x870000ull,
        "D32 layout exact allocation size");
    TEST_ASSERT_EQ(layout.depth.alignment, 0x10000u,
        "D32 layout exact alignment");
    TEST_ASSERT_EQ(layout.depth.block_width, 128u,
        "D32 layout exact block width");
    TEST_ASSERT_EQ(layout.depth.block_height, 128u,
        "D32 layout exact block height");
    TEST_ASSERT_EQ(layout.depth.first_mip_in_tail, 1u,
        "single-level D32 has no mip tail");
    TEST_ASSERT_EQ(layout.stencil.allocation_size, 0u,
        "D32 layout leaves stencil plane empty");

    input.layer_count = 3u;
    input.sample_count = 4u;
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout), AGC_OK,
        "gfx1013 multisampled array layout queries");
    TEST_ASSERT_EQ(layout.depth.block_width, 64u,
        "4x D32 layout exact block width");
    TEST_ASSERT_EQ(layout.depth.block_height, 64u,
        "4x D32 layout exact block height");
    TEST_ASSERT_EQ(layout.depth.slice_size, 0x1fe0000ull,
        "4x D32 layout exact slice size");
    TEST_ASSERT_EQ(layout.depth.allocation_size, 0x5fa0000ull,
        "4x D32 array exact allocation size");

    input.width = 1024u; input.height = 1024u; input.layer_count = 2u;
    input.mip_level_count = 11u; input.sample_count = 1u;
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout), AGC_OK,
        "gfx1013 D32 mip-chain layout queries");
    TEST_ASSERT_EQ(layout.depth.first_mip_in_tail, 4u,
        "D32 mip tail begins at exact level");
    TEST_ASSERT_EQ(layout.depth.slice_size, 0x600000ull,
        "D32 mip chain exact slice size");
    TEST_ASSERT_EQ(layout.depth.allocation_size, 0xc00000ull,
        "D32 mip array exact allocation size");

    input.width = 640u; input.height = 480u; input.layer_count = 1u;
    input.mip_level_count = 1u;
    input.format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT;
    input.stencil_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X;
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout), AGC_OK,
        "gfx1013 split depth/stencil layout queries");
    TEST_ASSERT_EQ(layout.depth.allocation_size, 0xc0000ull,
        "D16 plane exact allocation size");
    TEST_ASSERT_EQ(layout.stencil.allocation_size, 0x60000ull,
        "S8 plane exact allocation size");

    input.width = 0x4000u; input.height = 0x4000u;
    input.layer_count = 0x2000u; input.sample_count = 8u;
    input.format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT;
    input.stencil_swizzle_mode = 0u;
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout), AGC_OK,
        "largest bindable D32 layout retains 64-bit size");
    TEST_ASSERT_EQ(layout.depth.allocation_size, 0x400000000000ull,
        "largest bindable D32 allocation does not truncate");

    layout.depth.allocation_size = 0x1122334455667788ull;
    input.layer_count = 0x2001u;
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout),
        AGC_ERROR_INVALID_ARGUMENT, "unrepresentable layer count rejects");
    TEST_ASSERT_EQ(layout.depth.allocation_size, 0x1122334455667788ull,
        "invalid large layout preserves output");
    input.layer_count = 1u; input.width = 1920u; input.height = 1080u;
    input.mip_level_count = 2u;
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout),
        AGC_ERROR_INVALID_ARGUMENT, "multisampled mip chain rejects");
    input.sample_count = 1u; input.mip_level_count = 1u;
    input.depth_swizzle_mode = 23u;
    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout),
        AGC_ERROR_NOT_SUPPORTED, "unimplemented depth swizzle rejects");
}

static void test_gfx1013_htile_layout(void)
{
    AgcGfx1013HtileLayoutInput input = {
        .width = 1920u,
        .height = 1080u,
        .layer_count = 1u,
        .mip_level_count = 1u,
        .first_mip_in_tail = 1u,
        .pipe_count = 8u,
        .swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
    };
    AgcGfx1013HtileLayout layout = {0};

    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout), AGC_OK,
        "gfx1013 HTILE layout queries");
    TEST_ASSERT_EQ(layout.pitch, 2048u, "HTILE exact pitch");
    TEST_ASSERT_EQ(layout.padded_height, 1536u,
        "HTILE exact padded height");
    TEST_ASSERT_EQ(layout.alignment, 0x4000u,
        "eight-pipe HTILE exact alignment");
    TEST_ASSERT_EQ(layout.meta_block_size, 0x4000u,
        "eight-pipe HTILE exact metadata block size");
    TEST_ASSERT_EQ(layout.meta_block_width, 512u,
        "eight-pipe HTILE exact metadata block width");
    TEST_ASSERT_EQ(layout.meta_block_height, 512u,
        "eight-pipe HTILE exact metadata block height");
    TEST_ASSERT_EQ(layout.meta_blocks_per_slice, 12u,
        "HTILE exact metadata blocks per slice");
    TEST_ASSERT_EQ(layout.slice_size, 0x30000ull,
        "HTILE exact slice size");
    TEST_ASSERT_EQ(layout.allocation_size, 0x30000ull,
        "HTILE exact allocation size");

    input.layer_count = 3u;
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout), AGC_OK,
        "gfx1013 HTILE array layout queries");
    TEST_ASSERT_EQ(layout.slice_size, 0x30000ull,
        "HTILE array preserves slice size");
    TEST_ASSERT_EQ(layout.allocation_size, 0x90000ull,
        "HTILE array exact allocation size");

    input.width = 1024u;
    input.height = 1024u;
    input.layer_count = 2u;
    input.mip_level_count = 11u;
    input.first_mip_in_tail = 4u;
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout), AGC_OK,
        "gfx1013 HTILE mip layout queries");
    TEST_ASSERT_EQ(layout.meta_blocks_per_slice, 8u,
        "HTILE mip chain exact block count");
    TEST_ASSERT_EQ(layout.slice_size, 0x20000ull,
        "HTILE mip chain exact slice size");
    TEST_ASSERT_EQ(layout.allocation_size, 0x40000ull,
        "HTILE mip array exact allocation size");

    AgcGfx1013HtileSubresourceLayout subresource = {0};
    TEST_ASSERT_EQ(agcGfx1013GetHtileSubresourceLayout(
        &input, 0u, 1u, &subresource), AGC_OK,
        "gfx1013 HTILE mip0 layer1 subresource queries");
    TEST_ASSERT_EQ(subresource.offset, 0x30000ull,
        "HTILE mip0 layer1 exact reverse-chain offset");
    TEST_ASSERT_EQ(subresource.size, 0x10000ull,
        "HTILE mip0 exact metadata size");
    TEST_ASSERT_EQ(subresource.in_mip_tail, 0u,
        "HTILE mip0 is outside tail");
    TEST_ASSERT_EQ(agcGfx1013GetHtileSubresourceLayout(
        &input, 1u, 1u, &subresource), AGC_OK,
        "gfx1013 HTILE mip1 layer1 subresource queries");
    TEST_ASSERT_EQ(subresource.offset, 0x2c000ull,
        "HTILE mip1 layer1 exact reverse-chain offset");
    TEST_ASSERT_EQ(subresource.size, 0x4000ull,
        "HTILE mip1 exact metadata size");
    TEST_ASSERT_EQ(agcGfx1013GetHtileSubresourceLayout(
        &input, 4u, 1u, &subresource), AGC_OK,
        "gfx1013 HTILE tail layer1 subresource queries");
    TEST_ASSERT_EQ(subresource.offset, 0x20000ull,
        "HTILE tail layer1 starts at slice base");
    TEST_ASSERT_EQ(subresource.size, 0x4000ull,
        "HTILE tail reports shared metadata block");
    TEST_ASSERT_EQ(subresource.in_mip_tail, 1u,
        "HTILE tail is identified explicitly");
    subresource.offset = 0x1122334455667788ull;
    TEST_ASSERT_EQ(agcGfx1013GetHtileSubresourceLayout(
        &input, 11u, 0u, &subresource), AGC_ERROR_INVALID_ARGUMENT,
        "out-of-range HTILE mip rejects");
    TEST_ASSERT_EQ(subresource.offset, 0x1122334455667788ull,
        "invalid HTILE subresource preserves output");

    input.width = 1920u;
    input.height = 1080u;
    input.layer_count = 1u;
    input.mip_level_count = 1u;
    input.first_mip_in_tail = 1u;
    input.pipe_count = 16u;
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout), AGC_OK,
        "gfx1013 sixteen-pipe HTILE layout queries");
    TEST_ASSERT_EQ(layout.alignment, 0x8000u,
        "sixteen-pipe HTILE exact alignment");
    TEST_ASSERT_EQ(layout.meta_block_width, 1024u,
        "sixteen-pipe HTILE exact metadata block width");
    TEST_ASSERT_EQ(layout.meta_block_height, 512u,
        "sixteen-pipe HTILE exact metadata block height");
    TEST_ASSERT_EQ(layout.slice_size, 0x30000ull,
        "sixteen-pipe HTILE exact slice size");

    input.width = 0x4000u;
    input.height = 0x4000u;
    input.layer_count = 0x2000u;
    input.pipe_count = 64u;
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout), AGC_OK,
        "largest bindable HTILE layout retains 64-bit size");
    TEST_ASSERT_EQ(layout.slice_size, 0x1000000ull,
        "largest bindable HTILE exact slice size");
    TEST_ASSERT_EQ(layout.allocation_size, 0x2000000000ull,
        "largest bindable HTILE allocation does not truncate");

    layout.allocation_size = 0x1122334455667788ull;
    input.pipe_count = 3u;
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout),
        AGC_ERROR_INVALID_ARGUMENT, "non-power-of-two HTILE pipe count rejects");
    TEST_ASSERT_EQ(layout.allocation_size, 0x1122334455667788ull,
        "invalid HTILE layout preserves output");
    input.pipe_count = 8u;
    input.mip_level_count = 4u;
    input.first_mip_in_tail = 5u;
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout),
        AGC_ERROR_INVALID_ARGUMENT, "invalid HTILE mip-tail level rejects");
    input.first_mip_in_tail = 4u;
    input.swizzle_mode = 23u;
    TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&input, &layout),
        AGC_ERROR_NOT_SUPPORTED, "unimplemented HTILE swizzle rejects");
}

static void test_gfx1013_stencil_gate_fixture(void)
{
    uint32_t buffer[AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013DepthSurfaceLayoutInput input = {
        .width = 1920u,
        .height = 1080u,
        .layer_count = 1u,
        .mip_level_count = 1u,
        .sample_count = 1u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT,
        .depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
        .stencil_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
    };
    AgcGfx1013DepthSurfaceLayout layout = {0};
    AgcGfx1013DepthStencilState state = {
        .depth_test_enable = 1u,
        .depth_write_enable = 1u,
        .depth_compare_operation = AGC_GFX1013_COMPARE_LESS,
        .stencil_test_enable = 1u,
        .front = {
            .compare_operation = AGC_GFX1013_COMPARE_ALWAYS,
            .fail_operation = AGC_GFX1013_STENCIL_KEEP,
            .depth_fail_operation = AGC_GFX1013_STENCIL_KEEP,
            .pass_operation = AGC_GFX1013_STENCIL_REPLACE,
            .reference = 0x5au,
            .compare_mask = 0xffu,
            .write_mask = 0xffu,
        },
    };
    const uint32_t expected[AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_DEPTH_CONTROL, 0x00700717u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u),
        AGC_REG_DB_STENCIL_CONTROL, 0x00030030u,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u),
        AGC_REG_DB_STENCILREFMASK, 0x5affff5au, 0x5affff5au,
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u),
        AGC_REG_DB_DEPTH_BOUNDS_MIN, 0u, 0u,
    };

    TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&input, &layout), AGC_OK,
        "stencil gate split D32/S8 layout queries");
    TEST_ASSERT_EQ(layout.depth.allocation_size, 0x870000ull,
        "stencil gate exact D32 allocation");
    TEST_ASSERT_EQ(layout.stencil.pitch, 2048u,
        "stencil gate exact S8 pitch");
    TEST_ASSERT_EQ(layout.stencil.padded_height, 1280u,
        "stencil gate exact S8 padded height");
    TEST_ASSERT_EQ(layout.stencil.allocation_size, 0x280000ull,
        "stencil gate exact S8 allocation");
    TEST_ASSERT_EQ(layout.stencil.alignment, 0x10000u,
        "stencil gate exact S8 alignment");

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetDepthStencilState(&cb, &state), AGC_OK,
        "stencil gate compare/write/replace state emits");
    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0,
        "stencil gate exact packet stream");
}

static void test_gfx1013_graphics_defaults_v8(void)
{
    uint32_t buffer[2184] = {0};
    SceAgcCb cb;
    AgcGfx1013GraphicsDefaultStats stats = {0};
    uint32_t sh = 0u;
    uint32_t cx = 0u;
    uint32_t uc = 0u;
    uint32_t cursor = 0u;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ApplyGraphicsDefaultsV8(&cb, &stats), AGC_OK,
        "gfx1013 FW 5.50 graphics defaults emit");
    TEST_ASSERT_EQ(stats.sh_register_count, 174u,
        "gfx1013 FW 5.50 SH default count");
    TEST_ASSERT_EQ(stats.cx_register_count, 493u,
        "gfx1013 FW 5.50 CX default count");
    TEST_ASSERT_EQ(stats.uc_register_count, 61u,
        "gfx1013 FW 5.50 UC default count");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 2184u,
        "gfx1013 FW 5.50 defaults exact dword count");
    while (cursor < agcCbUsedDwords(&cb)) {
        TEST_ASSERT_EQ(agcPm4Length(buffer[cursor]), 3u,
            "each default is an individual register packet");
        if (agcPm4Opcode(buffer[cursor]) == AGC_PM4_OP_SET_SH_REG)
            sh++;
        else if (agcPm4Opcode(buffer[cursor]) == AGC_PM4_OP_SET_CONTEXT_REG)
            cx++;
        else if (agcPm4Opcode(buffer[cursor]) == AGC_PM4_OP_SET_UCONFIG_REG)
            uc++;
        cursor += 3u;
    }
    TEST_ASSERT_EQ(sh, stats.sh_register_count,
        "default SH packets match stats");
    TEST_ASSERT_EQ(cx, stats.cx_register_count,
        "default CX packets match stats");
    TEST_ASSERT_EQ(uc, stats.uc_register_count,
        "default UC packets match stats");
}

static void test_gfx1013_fixed_function_rejects_atomically(void)
{
    uint32_t buffer[2183] = {0};
    SceAgcCb cb;
    AgcGfx1013GraphicsDefaultStats stats = {0};
    AgcGfx1013ColorTargetState color = {
        0x0000000201600000ull, 1920u, 1080u,
        AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
        AGC_GFX1013_SURFACE_NUMBER_UNORM,
        AGC_GFX1013_SURFACE_SWAP_ALT,
    };
    const AgcGfx1013ViewportState viewport = {1920u, 1080u};
    const AgcGfx1013ViewportState invalid_viewport = {1920u, 1080u, 2u};
    const AgcGfx1013ScissorState scissor = {0u, 0u, 1920u, 1080u};
    AgcGfx1013ColorTargetFormatInfo format_info;

    agcCbInit(&cb, buffer, 27u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &color),
        AGC_ERROR_BUFFER_TOO_SMALL, "short color target rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short color target is atomic");
    agcCbReset(&cb, buffer, 14u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetViewport(&cb, &viewport),
        AGC_ERROR_BUFFER_TOO_SMALL, "short viewport rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "short viewport is atomic");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetViewport(&cb, &invalid_viewport),
        AGC_ERROR_INVALID_ARGUMENT, "unknown viewport clip space rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid viewport clip space emits no packets");
    agcCbReset(&cb, buffer, 21u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetScissor(&cb, &scissor),
        AGC_ERROR_BUFFER_TOO_SMALL, "short scissor rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "short scissor is atomic");
    agcCbReset(&cb, buffer, 2u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetTargetMask(&cb, 0x0fu),
        AGC_ERROR_BUFFER_TOO_SMALL, "short target mask rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "short target mask is atomic");
    agcCbReset(&cb, buffer, 14u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetDepthDisabled(&cb),
        AGC_ERROR_BUFFER_TOO_SMALL, "short depth state rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "short depth state is atomic");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ApplyGraphicsDefaultsV8(&cb, &stats),
        AGC_ERROR_BUFFER_TOO_SMALL, "short defaults reject");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "short defaults are atomic");

    agcCbReset(&cb, buffer, sizeof(buffer));
    color.address++;
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &color),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned target rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid target emits no packets");

    TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
        AGC_GFX1013_RT_FORMAT_COUNT, &format_info),
        AGC_ERROR_NOT_SUPPORTED, "unknown color format rejects");
    TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
        AGC_GFX1013_RT_FORMAT_RGBA8_UNORM, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "null color format output rejects");
    color.address--;
    color.width = 8u;
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &color),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned linear row pitch rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid row pitch emits no packets");
}

static void test_gfx1013_compute_packets(void)
{
    uint32_t buffer[256] = {0};
    uint32_t user_data[6] = {
        0u, 0u, 0x01600000u, 0x00000002u, 2073600u, 0xff00ff00u,
    };
    AgcRegisterValue sh[3] = {
        {AGC_REG_COMPUTE_PGM_RSRC1, 0x000000c0u},
        {AGC_REG_COMPUTE_PGM_RSRC2, 0x0000008cu},
        {AGC_REG_COMPUTE_PGM_RSRC3, 0x00000000u},
    };
    AgcShaderRecord record;
    AgcGfx1013ComputeState state;
    SceAgcCb cb;

    memset(&record, 0, sizeof(record));
    record.magic = AGC_SHADER_RECORD_MAGIC;
    record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    record.shader_type = kAgcShaderTypeCs;
    record.num_sh_registers = 3u;
    memset(&state, 0, sizeof(state));
    state.record = &record;
    state.sh_registers = sh;
    state.num_sh_registers = 3u;
    state.code_address = 0x0000000201de9000ull;
    state.user_data = user_data;
    state.num_user_data = 6u;
    state.local_size_x = 64u;
    state.local_size_y = 1u;
    state.local_size_z = 1u;
    state.group_count_x = 32400u;
    state.group_count_y = 1u;
    state.group_count_z = 1u;
    state.modifier = AGC_GFX1013_COMPUTE_DISPATCH_WAVE32;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ValidateCompute(&state), AGC_OK,
        "gfx1013 compute state validates");
    TEST_ASSERT_EQ(agcGfx1013DispatchCompute(&cb, &state), AGC_OK,
        "gfx1013 compute dispatch emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 44u,
        "gfx1013 compute exact dword count");
    TEST_ASSERT_EQ(buffer[0],
        agcPm4Header3(AGC_PM4_OP_CONTEXT_CONTROL, 3u),
        "gfx1013 context-control header");
    TEST_ASSERT_EQ(buffer[1], 0x80000000u,
        "gfx1013 context-control load value");
    TEST_ASSERT_EQ(buffer[2], 0x80000000u,
        "gfx1013 context-control shadow value");
    TEST_ASSERT_EQ(buffer[3] & 1u, 1u,
        "gfx1013 compute limits select compute shader");
    TEST_ASSERT_EQ(buffer[4], AGC_REG_COMPUTE_RESOURCE_LIMITS,
        "gfx1013 compute limits offset");
    TEST_ASSERT_EQ(buffer[12] & 1u, 1u,
        "gfx1013 thread packet selects compute shader");
    TEST_ASSERT_EQ(buffer[13], AGC_REG_COMPUTE_START_X,
        "gfx1013 compute start offset");
    TEST_ASSERT_EQ(buffer[17], 64u, "gfx1013 local size X");
    TEST_ASSERT_EQ(buffer[20] & 1u, 1u,
        "gfx1013 program packet selects compute shader");
    TEST_ASSERT_EQ(buffer[22], 0x0201de90u,
        "gfx1013 compute program low encoding");
    TEST_ASSERT_EQ(buffer[23], 0x00000000u,
        "gfx1013 compute program high encoding");
    TEST_ASSERT_EQ(buffer[31] & 1u, 1u,
        "gfx1013 user-data packet selects compute shader");
    TEST_ASSERT_EQ(buffer[32], AGC_REG_COMPUTE_USER_DATA_0,
        "gfx1013 user-data offset");
    TEST_ASSERT_EQ(buffer[35], 0x01600000u,
        "gfx1013 buffer address low in s2");
    TEST_ASSERT_EQ(buffer[36], 0x00000002u,
        "gfx1013 buffer address high in s3");
    TEST_ASSERT_EQ(buffer[39] & 1u, 1u,
        "gfx1013 dispatch selects compute shader");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[39]), AGC_PM4_OP_DISPATCH_DIRECT,
        "gfx1013 dispatch opcode");
    TEST_ASSERT_EQ(buffer[40], 32400u, "gfx1013 dispatch group X");
    TEST_ASSERT_EQ(buffer[43], 0x8041u,
        "gfx1013 Wave32 dispatch initiator");

    agcCbReset(&cb, buffer, 43u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013DispatchCompute(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL, "short compute buffer rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short compute dispatch is atomic");
    state.code_address++;
    TEST_ASSERT_EQ(agcGfx1013ValidateCompute(&state),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned compute program rejects");
}

static void test_gfx1013_compute_resource_table(void)
{
    uint32_t buffer[64] = {0};
    AgcRegisterValue sh[4] = {
        {AGC_REG_COMPUTE_PGM_RSRC1, 0x000000c0u},
        {AGC_REG_COMPUTE_PGM_RSRC2, 0x0000008cu},
        {AGC_REG_COMPUTE_PGM_RSRC3, 0x00000000u},
        {AGC_REG_COMPUTE_USER_DATA_0 + 2u,
            OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u)},
    };
    AgcGfx1013ResourceTableBinding table = {
        OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
        0x0000000202600000ull,
    };
    AgcShaderRecord record;
    AgcGfx1013ComputeState state;
    SceAgcCb cb;

    memset(&record, 0, sizeof(record));
    record.magic = AGC_SHADER_RECORD_MAGIC;
    record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    record.shader_type = kAgcShaderTypeCs;
    record.num_sh_registers = 4u;
    memset(&state, 0, sizeof(state));
    state.record = &record;
    state.sh_registers = sh;
    state.num_sh_registers = 4u;
    state.code_address = 0x0000000201de9000ull;
    state.local_size_x = 64u;
    state.local_size_y = 1u;
    state.local_size_z = 1u;
    state.group_count_x = 1u;
    state.group_count_y = 1u;
    state.group_count_z = 1u;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ValidateCompute(&state),
        AGC_ERROR_RESOURCE_NOT_BOUND,
        "compute placeholder requires resource table");
    state.resource_tables = &table;
    state.num_resource_tables = 1u;
    TEST_ASSERT_EQ(agcGfx1013DispatchCompute(&cb, &state), AGC_OK,
        "compute resource table dispatch emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 39u,
        "compute resource table exact dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[31]), AGC_PM4_OP_SET_SH_REG,
        "compute resource table uses SET_SH_REG");
    TEST_ASSERT_EQ(buffer[32], AGC_REG_COMPUTE_USER_DATA_0 + 2u,
        "compute resource table patches compiler register");
    TEST_ASSERT_EQ(buffer[33], 0x02600000u,
        "compute resource table patches address low");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[34]), AGC_PM4_OP_DISPATCH_DIRECT,
        "resource table is patched before dispatch");
}

static void test_gfx1013_compute_defaults_v8(void)
{
    uint32_t buffer[1024] = {0};
    AgcGfx1013ComputeDefaultStats stats = {0};
    SceAgcCb cb;
    uint32_t cursor = 0u;
    uint32_t registers = 0u;
    uint32_t packets = 0u;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ApplyComputeDefaultsV8(&cb, &stats), AGC_OK,
        "gfx1013 compute defaults emit");
    while (cursor < agcCbUsedDwords(&cb)) {
        uint32_t length = agcPm4Length(buffer[cursor]);
        TEST_ASSERT_EQ(agcPm4Opcode(buffer[cursor]), AGC_PM4_OP_SET_SH_REG,
            "compute default uses SET_SH_REG");
        TEST_ASSERT_EQ(buffer[cursor] & 1u, 1u,
            "compute default selects compute shader");
        registers += length - 2u;
        packets++;
        cursor += length;
    }
    TEST_ASSERT_EQ(stats.sh_register_count, 174u,
        "gfx1013 compute SH default count");
    TEST_ASSERT_EQ(registers, stats.sh_register_count,
        "compute defaults emitted register count");
    TEST_ASSERT_EQ(packets, stats.packet_count,
        "compute defaults emitted packet count");
}

static void test_gfx1013_htile_rmw_packets(void)
{
    uint32_t buffer[AGC_GFX1013_HTILE_RMW_DWORDS] = {0};
    AgcRegisterValue sh[3] = {
        {AGC_REG_COMPUTE_PGM_RSRC1, 0x000000c0u},
        {AGC_REG_COMPUTE_PGM_RSRC2, 0x0000008eu},
        {AGC_REG_COMPUTE_PGM_RSRC3, 0x00000000u},
    };
    AgcShaderRecord record;
    AgcGfx1013HtileSubresourceLayout subresource = {
        .offset = 0x30000u,
        .size = 0x30000u,
        .width = 1920u,
        .height = 1080u,
        .pitch = 2048u,
        .padded_height = 1536u,
    };
    AgcGfx1013HtileExpclearPlan plan = {
        .write_value = 0xfffc00f0u,
        .write_mask = 0xfffff3ffu,
        .requires_read_modify_write = 1u,
        .hardware_enabled = 0u,
    };
    AgcGfx1013HtileRmwState state;
    SceAgcCb cb;

    memset(&record, 0, sizeof(record));
    record.magic = AGC_SHADER_RECORD_MAGIC;
    record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    record.shader_type = kAgcShaderTypeCs;
    record.num_sh_registers = 3u;
    memset(&state, 0, sizeof(state));
    state.record = &record;
    state.sh_registers = sh;
    state.num_sh_registers = 3u;
    state.code_address = 0x0000000201de9000ull;
    state.htile_address = 0x00000002036f0000ull;
    state.htile_allocation_size = 0x60000u;
    state.subresource = &subresource;
    state.plan = &plan;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013RmwHtile(&cb, &state), AGC_OK,
        "gfx1013 exact-range HTILE RMW emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_HTILE_RMW_DWORDS,
        "HTILE RMW exact synchronized dword count");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[0]), AGC_PM4_OP_EVENT_WRITE,
        "HTILE RMW begins with DB metadata flush");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[20]), AGC_PM4_OP_CONTEXT_CONTROL,
        "HTILE RMW compute begins after DB release/acquire");
    TEST_ASSERT_EQ(buffer[55], 0x03720000u,
        "HTILE RMW exact subresource address low in s2");
    TEST_ASSERT_EQ(buffer[56], 0x00000002u,
        "HTILE RMW exact subresource address high in s3");
    TEST_ASSERT_EQ(buffer[57], 0x0000c000u,
        "HTILE RMW exact word count in s4");
    TEST_ASSERT_EQ(buffer[58], plan.write_value,
        "HTILE RMW masked value in s5");
    TEST_ASSERT_EQ(buffer[59], plan.write_mask,
        "HTILE RMW aspect mask in s6");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[60]),
        AGC_PM4_OP_DISPATCH_DIRECT,
        "HTILE RMW dispatch follows user data");
    TEST_ASSERT_EQ(buffer[61], 768u,
        "HTILE RMW dispatch covers exact word range");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[65]), AGC_PM4_OP_RELEASE_MEM,
        "HTILE RMW compute release precedes DB acquire");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[75]), AGC_PM4_OP_ACQUIRE_MEM,
        "HTILE RMW ends with DB-visible acquire");

    agcCbReset(&cb, buffer,
        (AGC_GFX1013_HTILE_RMW_DWORDS - 1u) * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013RmwHtile(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL, "short HTILE RMW buffer rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short HTILE RMW is atomic");

    subresource.in_mip_tail = 1u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013RmwHtile(&cb, &state),
        AGC_ERROR_NOT_SUPPORTED, "shared HTILE mip tail rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "unsupported HTILE tail is atomic");
    subresource.in_mip_tail = 0u;
    subresource.offset = 0x50000u;
    TEST_ASSERT_EQ(agcGfx1013RmwHtile(&cb, &state),
        AGC_ERROR_INVALID_ARGUMENT, "out-of-allocation HTILE range rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid HTILE range is atomic");
    subresource.offset = 0x30000u;
    sh[1].value = 0x0000008cu;
    TEST_ASSERT_EQ(agcGfx1013RmwHtile(&cb, &state),
        AGC_ERROR_SHADER_INVALID,
        "HTILE RMW rejects wrong user-SGPR shader contract");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid HTILE shader contract is atomic");
}

static void test_gfx1013_resource_table_binding(void)
{
    uint32_t buffer[16] = {0};
    AgcRegisterValue registers[2] = {
        {0x220u, OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER},
        {0x221u, OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u)},
    };
    AgcShaderRecord record;
    AgcGfx1013ShaderBinding shader;
    const AgcGfx1013ResourceTableBinding tables[2] = {
        {OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER,
            0x0000000202601000ull},
        {OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
            0x0000000202702000ull},
    };
    SceAgcCb cb;

    memset(&record, 0, sizeof(record));
    record.magic = AGC_SHADER_RECORD_MAGIC;
    record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    record.shader_type = kAgcShaderTypePs;
    record.num_sh_registers = 2u;
    memset(&shader, 0, sizeof(shader));
    shader.record = &record;
    shader.sh_registers = registers;
    shader.num_sh_registers = 2u;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013BindResourceTables(
        &cb, &shader, tables, 2u), AGC_OK,
        "gfx1013 resource tables bind");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 6u,
        "two resource tables emit two register packets");
    TEST_ASSERT_EQ(buffer[0],
        agcPm4Header3(AGC_PM4_OP_SET_SH_REG, 3u),
        "graphics resource table uses graphics SET_SH_REG");
    TEST_ASSERT_EQ(buffer[1], 0x220u,
        "vertex table target register");
    TEST_ASSERT_EQ(buffer[2], 0x02601000u,
        "vertex table address32 value");
    TEST_ASSERT_EQ(buffer[4], 0x221u,
        "descriptor set target register");
    TEST_ASSERT_EQ(buffer[5], 0x02702000u,
        "descriptor set address32 value");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013BindResourceTables(
        &cb, &shader, tables, 1u), AGC_ERROR_RESOURCE_NOT_BOUND,
        "missing descriptor set rejects atomically");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "missing table emits no packets");

    record.shader_type = kAgcShaderTypeCs;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013BindResourceTables(
        &cb, &shader, tables, 2u), AGC_OK,
        "compute resource tables bind");
    TEST_ASSERT_EQ(buffer[0] & 1u, 1u,
        "compute resource table selects compute shader");
}

static void test_gfx1013_resource_table_binding_rejects(void)
{
    uint32_t buffer[8] = {0};
    AgcRegisterValue reg = {
        0x220u, OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
    };
    AgcShaderRecord record;
    AgcGfx1013ShaderBinding shader;
    AgcGfx1013ResourceTableBinding table = {
        OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
        0x0000000302702000ull,
    };
    SceAgcCb cb;

    memset(&record, 0, sizeof(record));
    record.magic = AGC_SHADER_RECORD_MAGIC;
    record.version = AGC_SHADER_RECORD_VERSION_GEN5;
    record.shader_type = kAgcShaderTypePs;
    record.num_sh_registers = 1u;
    memset(&shader, 0, sizeof(shader));
    shader.record = &record;
    shader.sh_registers = &reg;
    shader.num_sh_registers = 1u;
    agcCbInit(&cb, buffer, sizeof(buffer));

    TEST_ASSERT_EQ(agcGfx1013BindResourceTables(
        &cb, &shader, &table, 1u), AGC_ERROR_INVALID_ALIGNMENT,
        "non-address32 table rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "invalid table address emits no packets");
    table.address = 0x0000000202702000ull;
    agcCbReset(&cb, buffer, 2u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013BindResourceTables(
        &cb, &shader, &table, 1u), AGC_ERROR_BUFFER_TOO_SMALL,
        "short resource binding rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short resource binding is atomic");
}

static void test_gfx1013_baseline_draw_binds_resources(void)
{
    uint32_t buffer[144] = {0};
    AgcGfx1013Wave32VsPsState shaders;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[3];
    AgcRegisterValue pixel_sh[3];
    AgcRegisterValue pixel_cx[1];
    const AgcGfx1013ResourceTableBinding primitive_table = {
        OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER,
        0x0000000202601000ull,
    };
    const AgcGfx1013ResourceTableBinding pixel_table = {
        OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
        0x0000000202702000ull,
    };
    AgcGfx1013BaselineDrawState draw;
    const AgcGfx1013DepthStencilState depth = {
        .depth_test_enable = 1u,
        .depth_write_enable = 1u,
        .depth_compare_operation = AGC_GFX1013_COMPARE_LESS,
        .min_depth_bounds = 0.0f,
        .max_depth_bounds = 1.0f,
    };
    const AgcGfx1013DepthSurfaceState depth_surface = {
        .depth_read_address = 0x0000000202800000ull,
        .depth_write_address = 0x0000000202800000ull,
        .width = 64u,
        .height = 64u,
        .format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT,
        .depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X,
        .mip_level_count = 1u,
        .sample_count = 1u,
    };
    SceAgcCb cb;
    uint32_t value;
    bool found_depth_address = false;

    make_wave32_state(&shaders, &primitive_record, &pixel_record, &specials,
        primitive_sh, pixel_sh, pixel_cx);
    primitive_sh[2] = (AgcRegisterValue){
        0x220u, OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER,
    };
    pixel_sh[2] = (AgcRegisterValue){
        0x221u, OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
    };
    primitive_record.num_sh_registers = 3u;
    pixel_record.num_sh_registers = 3u;
    shaders.primitive.num_sh_registers = 3u;
    shaders.pixel.num_sh_registers = 3u;
    memset(&draw, 0, sizeof(draw));
    draw.shaders = shaders;
    draw.primitive_resource_tables = &primitive_table;
    draw.num_primitive_resource_tables = 1u;
    draw.pixel_resource_tables = &pixel_table;
    draw.num_pixel_resource_tables = 1u;
    draw.depth_surface_state = &depth_surface;
    draw.depth_stencil_state = &depth;
    draw.index_type = kAgcIndexSize16;
    draw.instance_count = 1u;
    draw.vertex_count = 3u;
    agcCbInit(&cb, buffer, sizeof(buffer));

    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(&cb, &draw), AGC_OK,
        "baseline draw binds resource tables");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 97u,
        "baseline resource draw exact dword count");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_SH_REG, 0x220u, &value),
        "baseline vertex table register emitted");
    TEST_ASSERT_EQ(value, 0x02601000u,
        "baseline vertex placeholder resolved after shader bind");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_SH_REG, 0x221u, &value),
        "baseline descriptor-set register emitted");
    TEST_ASSERT_EQ(value, 0x02702000u,
        "baseline descriptor placeholder resolved after shader bind");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_CONTEXT_REG, AGC_REG_DB_DEPTH_CONTROL, &value),
        "baseline depth control emitted after shader bind");
    TEST_ASSERT_EQ(value, 0x00000016u,
        "baseline depth state restores less-test and writes");
    TEST_ASSERT(find_last_register(buffer, agcCbUsedDwords(&cb),
        AGC_PM4_OP_SET_CONTEXT_REG, AGC_REG_DB_DEPTH_SIZE_XY, &value),
        "baseline depth surface emitted after shader bind");
    for (uint32_t i = 0u; i < agcCbUsedDwords(&cb); ++i)
        found_depth_address |= buffer[i] == 0x02028000u;
    TEST_ASSERT(found_depth_address,
        "baseline depth surface restores the typed DB address");
}

static void test_gfx1013_tessellation_state_builders(void)
{
    uint32_t buffer[32] = {0};
    AgcGfx1013TessellationRingTable table;
    const AgcGfx1013TessellationState state = {
        .offchip_ring_address = 0x0000000202610000ull,
        .factor_ring_address = 0x0000000202618000ull,
        .offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE,
        .factor_ring_size = AGC_GFX1013_TESS_FACTOR_RING_SIZE,
        .offchip_param = AGC_GFX1013_TESS_OFFCHIP_PARAM,
        .max_tess_level = 0x42800000u,
        .min_tess_level = 0u,
        .esgs_ring_itemsize = 1u,
        .distribution = 0xd8181e0cu,
        .tf_param = 0x00000061u,
    };
    const uint32_t factor_slot = AGC_GFX1013_TESS_FACTOR_RING_SLOT * 4u;
    const uint32_t offchip_slot = AGC_GFX1013_TESS_OFFCHIP_RING_SLOT * 4u;
    const AgcGfx1013TessellationLayoutState layout_state = {
        .patch_count = 8u,
        .input_control_points = 3u,
        .output_control_points = 4u,
        .vertex_output_count = 1u,
        .control_output_count = 2u,
        .primitive_mode = 1u,
        .tes_reads_tess_factors = 0u,
    };
    uint32_t tcs_layout = 0u;
    uint32_t tes_layout = 0u;
    SceAgcCb cb;

    TEST_ASSERT_EQ(AGC_GFX1013_TESS_OFFCHIP_BUFFERS_PER_SE, 40u,
        "gfx1013 tessellation buffers are derived per shader engine");
    TEST_ASSERT_EQ(AGC_GFX1013_TESS_OFFCHIP_BUFFER_COUNT, 160u,
        "gfx1013 tessellation buffering field uses the global count");
    TEST_ASSERT_EQ(AGC_GFX1013_TESS_OFFCHIP_RING_SIZE, 0x500000u,
        "gfx1013 tessellation ring covers every global buffer");
    TEST_ASSERT_EQ(agcGfx1013BuildTessellationOffchipLayouts(
        &layout_state, &tcs_layout, &tes_layout), AGC_OK,
        "gfx1013 tessellation offchip layouts build");
    TEST_ASSERT_EQ(tcs_layout, 0x21022108u,
        "TCS layout uses input patch size and compiler output counts");
    TEST_ASSERT_EQ(tes_layout, 0x21022188u,
        "TES layout uses output patch size independently");

    TEST_ASSERT_EQ(agcGfx1013BuildTessellationRingTable(&table, &state),
        AGC_OK, "gfx1013 tessellation ring table builds");
    TEST_ASSERT_EQ(table.words[factor_slot], 0x02618000u,
        "tess factor descriptor address low");
    TEST_ASSERT_EQ(table.words[factor_slot + 1u], 2u,
        "tess factor descriptor address high");
    TEST_ASSERT_EQ(table.words[factor_slot + 2u], 0x1e000u,
        "tess factor descriptor size");
    TEST_ASSERT_EQ(table.words[factor_slot + 3u], 0x31016facu,
        "tess factor descriptor controls");
    TEST_ASSERT_EQ(table.words[offchip_slot], 0x02610000u,
        "tess offchip descriptor address low");
    TEST_ASSERT_EQ(table.words[offchip_slot + 2u], 0x500000u,
        "tess offchip descriptor size");
    TEST_ASSERT_EQ(table.words[0], 0u,
        "unused tessellation table slots clear");

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetTessellationRings(&cb, &state), AGC_OK,
        "gfx1013 tessellation ring registers emit");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 12u,
        "tessellation ring state exact dword count");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_VGT_TF_RING_SIZE,
        "tessellation factor ring size register");
    TEST_ASSERT_EQ(buffer[2], 0x7800u,
        "tessellation factor ring size in dwords");
    TEST_ASSERT_EQ(buffer[4], AGC_REG_VGT_HS_OFFCHIP_PARAM,
        "tessellation offchip buffering register");
    TEST_ASSERT_EQ(buffer[5], 159u,
        "tessellation provisions four workgroups per physical CU");
    TEST_ASSERT_EQ(buffer[7], AGC_REG_VGT_TF_MEMORY_BASE,
        "tessellation factor base register");
    TEST_ASSERT_EQ(buffer[8], 0x02026180u,
        "tessellation factor base encoding");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetTessellationContext(&cb, &state), AGC_OK,
        "gfx1013 tessellation context emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 15u,
        "tessellation context exact dword count");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_VGT_HOS_MAX_TESS_LEVEL,
        "tessellation max level register");
    TEST_ASSERT_EQ(buffer[2], 0x42800000u,
        "tessellation max level value");
    TEST_ASSERT_EQ(buffer[13], AGC_REG_VGT_TF_PARAM,
        "tessellation parameter register");
    TEST_ASSERT_EQ(buffer[14], 0x61u,
        "tessellation parameter value");
}

static void test_gfx1013_tessellation_state_rejects_atomically(void)
{
    uint32_t buffer[16] = {0};
    AgcGfx1013TessellationRingTable table = {{1u}};
    AgcGfx1013TessellationState state = {
        .offchip_ring_address = 0x0000000202610001ull,
        .factor_ring_address = 0x0000000202618000ull,
        .offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE,
        .factor_ring_size = AGC_GFX1013_TESS_FACTOR_RING_SIZE,
        .offchip_param = AGC_GFX1013_TESS_OFFCHIP_PARAM,
    };
    AgcGfx1013TessellationLayoutState layout_state = {0};
    uint32_t tcs_layout = 0x11111111u;
    uint32_t tes_layout = 0x22222222u;
    SceAgcCb cb;

    TEST_ASSERT_EQ(agcGfx1013BuildTessellationOffchipLayouts(
        &layout_state, &tcs_layout, &tes_layout),
        AGC_ERROR_INVALID_ARGUMENT,
        "empty tessellation offchip layout rejects");
    TEST_ASSERT_EQ(tcs_layout, 0x11111111u,
        "invalid TCS layout preserves output");
    TEST_ASSERT_EQ(tes_layout, 0x22222222u,
        "invalid TES layout preserves output");

    TEST_ASSERT_EQ(agcGfx1013BuildTessellationRingTable(&table, &state),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned tessellation ring rejects");
    TEST_ASSERT_EQ(table.words[0], 1u,
        "invalid tessellation table preserves output");
    state.offchip_ring_address--;
    state.offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE - 4u;
    TEST_ASSERT_EQ(agcGfx1013BuildTessellationRingTable(&table, &state),
        AGC_ERROR_VALIDATION_FAILED,
        "undersized offchip ring rejects its buffering profile");
    TEST_ASSERT_EQ(table.words[0], 1u,
        "undersized offchip ring preserves output");
    state.offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE;
    agcCbInit(&cb, buffer, 11u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetTessellationRings(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL, "short tessellation ring state rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short tessellation ring state is atomic");
    agcCbReset(&cb, buffer, 14u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetTessellationContext(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL, "short tessellation context rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short tessellation context is atomic");
}

static void test_gfx1013_msaa_state_and_layout(void)
{
    uint32_t buffer[AGC_GFX1013_SAMPLE_STATE_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013SampleState samples = {4u, 1u, 0xFu};
    AgcGfx1013ColorSurfaceLayoutInput input = {
        1920u, 1080u, 1u, 1u, 4u,
        AGC_GFX1013_RT_FORMAT_RGBA8_UNORM,
        AGC_GFX1013_SWIZZLE_64KB_R_X,
    };
    AgcGfx1013ColorSurfaceLayout layout;
    AgcGfx1013ColorTargetState target;

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetSampleState(&cb, &samples), AGC_OK,
        "gfx1013 4x sample state emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb),
        AGC_GFX1013_SAMPLE_STATE_DWORDS, "gfx1013 sample-state size");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_PA_SC_AA_CONFIG,
        "gfx1013 PA_SC_AA_CONFIG register");
    TEST_ASSERT_EQ(buffer[2], 0x2020C002u,
        "gfx1013 exact 4x PA_SC_AA_CONFIG");
    TEST_ASSERT_EQ(buffer[4], AGC_REG_DB_EQAA,
        "gfx1013 DB_EQAA register");
    TEST_ASSERT_EQ(buffer[5], 0x00002202u,
        "gfx1013 exact 4x DB_EQAA");
    TEST_ASSERT_EQ(buffer[15], 0xE62A62AEu,
        "gfx1013 standard DX 4x sample locations");
    TEST_ASSERT_EQ(buffer[27], 0x000F000Fu,
        "gfx1013 full 4x coverage mask 0");
    TEST_ASSERT_EQ(buffer[28], 0x000F000Fu,
        "gfx1013 full 4x coverage mask 1");

    TEST_ASSERT_EQ(agcGfx1013GetColorSurfaceLayout(&input, &layout),
        AGC_OK, "gfx1013 4x RGBA8 color layout computes");
    TEST_ASSERT_EQ(layout.pitch, 1920u, "gfx1013 4x color pitch");
    TEST_ASSERT_EQ(layout.padded_height, 1088u,
        "gfx1013 4x color padded height");
    TEST_ASSERT_EQ(layout.block_width, 64u,
        "gfx1013 4x color block width");
    TEST_ASSERT_EQ(layout.block_height, 64u,
        "gfx1013 4x color block height");
    TEST_ASSERT_EQ(layout.allocation_size, UINT64_C(33423360),
        "gfx1013 4x color allocation size");

    TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&target,
        0x0000000203000000ull, 1920u, 1080u,
        AGC_GFX1013_RT_FORMAT_RGBA8_UNORM), AGC_OK,
        "gfx1013 4x target initializes from typed format");
    target.sample_count = 4u;
    target.fragment_count = 4u;
    target.swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_R_X;
    agcCbReset(&cb, buffer, 27u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &target),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "gfx1013 4x target preserves atomic short-buffer behavior");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 short 4x target emits nothing");
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &target), AGC_OK,
        "gfx1013 4x color target emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 28u,
        "gfx1013 4x color-target exact size");
    TEST_ASSERT_EQ(buffer[3], 0x000000EFu,
        "gfx1013 4x color pitch tiles");
    TEST_ASSERT_EQ(buffer[4], 0x000FEFFFu,
        "gfx1013 4x color slice tiles and samples");
    TEST_ASSERT_EQ(buffer[7], 0x00012000u,
        "gfx1013 color NUM_SAMPLES and NUM_FRAGMENTS");
    TEST_ASSERT_EQ(buffer[24], 0x0906C001u,
        "gfx1013 color 64KB_R_X attrib3");

    TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&target,
        0x0000000203000000ull, 96u, 64u,
        AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT), AGC_OK,
        "gfx1013 padded RGBA16F 4x target initializes");
    target.sample_count = 4u;
    target.fragment_count = 4u;
    target.swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_R_X;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &target), AGC_OK,
        "gfx1013 padded RGBA16F 4x target emits");
    TEST_ASSERT_EQ(buffer[3], 15u,
        "gfx1013 4x color pitch uses padded 128-pixel layout");

    target.address += 0x100u;
    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &target),
        AGC_ERROR_INVALID_ALIGNMENT,
        "gfx1013 64KB_R_X target requires 64KB alignment");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "misaligned gfx1013 64KB_R_X target is atomic");
}

static void test_gfx1013_color_resolve_rejects_atomically(void)
{
    uint32_t buffer[AGC_GFX1013_TRANSITION_MAX_DWORDS] = {0};
    SceAgcCb cb;
    AgcGfx1013BaselineDrawState draw;
    AgcGfx1013ColorResolveState resolve;
    AgcGfx1013FrameState frame = make_frame_state();
    AgcGfx1013ColorTargetState source = frame.color_target;
    AgcShaderRecord primitive_record;
    AgcShaderRecord pixel_record;
    AgcShaderSpecials specials;
    AgcRegisterValue primitive_sh[2];
    AgcRegisterValue pixel_sh[2];
    AgcRegisterValue pixel_cx[1];
    uint32_t i;

    memset(&draw, 0, sizeof(draw));
    make_wave32_state(&draw.shaders, &primitive_record, &pixel_record,
        &specials, primitive_sh, pixel_sh, pixel_cx);
    draw.frame = &frame;
    draw.index_type = kAgcIndexSize16;
    draw.instance_count = 1u;
    draw.vertex_count = 3u;
    source.address += AGC_GFX1013_64KB_SURFACE_ALIGNMENT;
    source.sample_count = 4u;
    source.fragment_count = 4u;
    source.swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_R_X;
    resolve.source = &source;
    resolve.draw = &draw;

    source.width--;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ResolveColor4x(&cb, &resolve),
        AGC_ERROR_INVALID_ARGUMENT,
        "gfx1013 resolve rejects mismatched source extent");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 mismatched resolve emits nothing");
    source.width++;

    source.color_format++;
    TEST_ASSERT_EQ(agcGfx1013ResolveColor4x(&cb, &resolve),
        AGC_ERROR_INVALID_ARGUMENT,
        "gfx1013 resolve rejects mismatched source format");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 mismatched format emits nothing");
    source.color_format--;

    for (i = 0u; i < AGC_GFX1013_TRANSITION_MAX_DWORDS; ++i)
        buffer[i] = 0xA5A5A5A5u;
    TEST_ASSERT_EQ(agcGfx1013ResolveColor4x(&cb, &resolve),
        AGC_ERROR_BUFFER_TOO_SMALL,
        "gfx1013 resolve rejects insufficient aggregate capacity");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "gfx1013 short resolve preserves cursor");
    for (i = 0u; i < AGC_GFX1013_TRANSITION_MAX_DWORDS; ++i) {
        TEST_ASSERT_EQ(buffer[i], 0xA5A5A5A5u,
            "gfx1013 short resolve preserves command memory");
    }
}

void test_suite_graphics(void)
{
    TEST_SUITE("GFX1013 Graphics State");
    TEST_RUN(test_public_triangle_example);
    TEST_RUN(test_gfx1013_wave32_vs_ps_binding);
    TEST_RUN(test_gfx1013_baseline_draw_wrapper);
    TEST_RUN(test_gfx1013_indexed_indirect_draw_wrappers);
    TEST_RUN(test_gfx1013_buffer_copy_packets);
    TEST_RUN(test_gfx1013_wave32_rejects_but_generic_accepts_wave64);
    TEST_RUN(test_gfx1013_wave32_rejects_small_buffer_atomically);
    TEST_RUN(test_gfx1013_wave32_tessellation_binding);
    TEST_RUN(test_gfx1013_eop_completion_fence);
    TEST_RUN(test_gfx1013_occlusion_snapshot);
    TEST_RUN(test_gfx1013_resource_transitions);
    TEST_RUN(test_gfx1013_fixed_function_packets);
    TEST_RUN(test_gfx1013_blend_depth_stencil_packets);
    TEST_RUN(test_gfx1013_depth_surface_packets);
    TEST_RUN(test_gfx1013_htile_operation_packets);
    TEST_RUN(test_gfx1013_d16_htile_qualification_fixture);
    TEST_RUN(test_gfx1013_depth_expclear_packets);
    TEST_RUN(test_gfx1013_selective_expclear_surface);
    TEST_RUN(test_gfx1013_depth_surface_layout);
    TEST_RUN(test_gfx1013_htile_layout);
    TEST_RUN(test_gfx1013_stencil_gate_fixture);
    TEST_RUN(test_gfx1013_polygon_modes);
    TEST_RUN(test_gfx1013_raster_primitives);
    TEST_RUN(test_gfx1013_frame_state);
    TEST_RUN(test_gfx1013_graphics_defaults_v8);
    TEST_RUN(test_gfx1013_fixed_function_rejects_atomically);
    TEST_RUN(test_gfx1013_compute_packets);
    TEST_RUN(test_gfx1013_compute_resource_table);
    TEST_RUN(test_gfx1013_compute_defaults_v8);
    TEST_RUN(test_gfx1013_htile_rmw_packets);
    TEST_RUN(test_gfx1013_resource_table_binding);
    TEST_RUN(test_gfx1013_resource_table_binding_rejects);
    TEST_RUN(test_gfx1013_baseline_draw_binds_resources);
    TEST_RUN(test_gfx1013_tessellation_state_builders);
    TEST_RUN(test_gfx1013_tessellation_state_rejects_atomically);
    TEST_RUN(test_gfx1013_msaa_state_and_layout);
    TEST_RUN(test_gfx1013_color_resolve_rejects_atomically);
}
