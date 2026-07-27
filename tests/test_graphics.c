#include "test.h"

#include <string.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_graphics.h"
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

static void test_gfx1013_wave32_vs_ps_binding(void)
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
    uint32_t value = 0;

    make_wave32_state(&state, &primitive_record, &pixel_record, &specials,
        primitive_sh, pixel_sh, pixel_cx);
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013ValidateWave32VsPs(&state), AGC_OK,
        "gfx1013 Wave32 VS+PS validation succeeds");
    TEST_ASSERT_EQ(agcGfx1013BindWave32VsPs(&cb, &state), AGC_OK,
        "gfx1013 Wave32 VS+PS binding succeeds");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 36u,
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
    agcCbInit(&cb, buffer, 43u * sizeof(uint32_t));
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
    state.tcs_offchip_layout = 0x21042108u;
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
    TEST_ASSERT_EQ(value, state.tcs_offchip_layout,
        "TES offchip layout patched");
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
}

static void test_gfx1013_fixed_function_packets(void)
{
    uint32_t buffer[64] = {0};
    SceAgcCb cb;
    const AgcGfx1013ColorTargetState color = {
        0x0000000201600000ull, 1920u, 1080u,
        AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
        AGC_GFX1013_SURFACE_NUMBER_UNORM,
        AGC_GFX1013_SURFACE_SWAP_ALT,
    };
    const AgcGfx1013ViewportState viewport = {1920u, 1080u};
    const AgcGfx1013ScissorState scissor = {0u, 0u, 1920u, 1080u};
    const uint32_t expected_color[28] = {
        agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 16u),
        AGC_REG_CB_COLOR0_BASE,
        0x02016000u, 0x000000efu, 0x0003f47fu, 0u,
        0x00010828u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
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

    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &color), AGC_OK,
        "gfx1013 RGBA8 target emits");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 28u,
        "gfx1013 color target exact dword count");
    TEST_ASSERT(memcmp(buffer, expected_color, sizeof(expected_color)) == 0,
        "gfx1013 color target exact packet stream");

    agcCbReset(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(agcGfx1013SetViewport(&cb, &viewport), AGC_OK,
        "gfx1013 viewport emits");
    TEST_ASSERT(memcmp(buffer, expected_viewport,
        sizeof(expected_viewport)) == 0,
        "gfx1013 viewport exact packet stream");

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
    const AgcGfx1013ScissorState scissor = {0u, 0u, 1920u, 1080u};

    agcCbInit(&cb, buffer, 27u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetColorTarget(&cb, &color),
        AGC_ERROR_BUFFER_TOO_SMALL, "short color target rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short color target is atomic");
    agcCbReset(&cb, buffer, 14u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013SetViewport(&cb, &viewport),
        AGC_ERROR_BUFFER_TOO_SMALL, "short viewport rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u, "short viewport is atomic");
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
    TEST_ASSERT_EQ(buffer[43], 0x41u, "gfx1013 dispatch initiator");

    agcCbReset(&cb, buffer, 43u * sizeof(uint32_t));
    TEST_ASSERT_EQ(agcGfx1013DispatchCompute(&cb, &state),
        AGC_ERROR_BUFFER_TOO_SMALL, "short compute buffer rejects");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "short compute dispatch is atomic");
    state.code_address++;
    TEST_ASSERT_EQ(agcGfx1013ValidateCompute(&state),
        AGC_ERROR_INVALID_ALIGNMENT, "unaligned compute program rejects");
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
    uint32_t buffer[96] = {0};
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
    SceAgcCb cb;
    uint32_t value;

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
    draw.index_type = kAgcIndexSize16;
    draw.instance_count = 1u;
    draw.vertex_count = 3u;
    agcCbInit(&cb, buffer, sizeof(buffer));

    TEST_ASSERT_EQ(agcGfx1013DrawBaselineIndexAuto(&cb, &draw), AGC_OK,
        "baseline draw binds resource tables");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 56u,
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
}

void test_suite_graphics(void)
{
    TEST_SUITE("GFX1013 Graphics State");
    TEST_RUN(test_gfx1013_wave32_vs_ps_binding);
    TEST_RUN(test_gfx1013_baseline_draw_wrapper);
    TEST_RUN(test_gfx1013_wave32_rejects_but_generic_accepts_wave64);
    TEST_RUN(test_gfx1013_wave32_rejects_small_buffer_atomically);
    TEST_RUN(test_gfx1013_wave32_tessellation_binding);
    TEST_RUN(test_gfx1013_fixed_function_packets);
    TEST_RUN(test_gfx1013_graphics_defaults_v8);
    TEST_RUN(test_gfx1013_fixed_function_rejects_atomically);
    TEST_RUN(test_gfx1013_compute_packets);
    TEST_RUN(test_gfx1013_compute_defaults_v8);
    TEST_RUN(test_gfx1013_resource_table_binding);
    TEST_RUN(test_gfx1013_resource_table_binding_rejects);
    TEST_RUN(test_gfx1013_baseline_draw_binds_resources);
}
