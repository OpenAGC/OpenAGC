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
        {AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 1u,
         OPENAGC_NEXT_STAGE_PC_PLACEHOLDER},
        {AGC_REG_SPI_SHADER_PGM_LO_GS, 0u},
        {AGC_REG_SPI_SHADER_PGM_HI_GS, 0u},
    };
    AgcRegisterValue ps_sh[] = {
        {AGC_REG_SPI_SHADER_PGM_LO_PS, 0u},
        {AGC_REG_SPI_SHADER_PGM_HI_PS, 0u},
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
        &gs_record, gs_sh, gs_record.num_sh_registers, NULL, 0u,
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
    TEST_ASSERT(find_last_register(
        buffer, agcCbUsedDwords(&cb), AGC_PM4_OP_SET_CONTEXT_REG,
        AGC_REG_VGT_SHADER_STAGES_EN, &value),
        "combined tessellation stages emitted");
    TEST_ASSERT_EQ(value, 0x00e0210du,
        "LS+HS+DS+NGG Wave32 stage enables combined");

    /* TES-as-NGG can be a complete front program with an inert back half.
     * ACO then allocates no continuation SGPR; ring pointers remain required. */
    gs_sh[2].value = 0u;
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT_EQ(
        agcGfx1013ValidateWave32TessVsPs(&state), AGC_OK,
        "TES front-only record validates without continuation SGPR");
    TEST_ASSERT_EQ(
        agcGfx1013BindWave32TessVsPs(&cb, &state), AGC_OK,
        "TES front-only record binds without continuation SGPR");
}

void test_suite_graphics(void)
{
    TEST_SUITE("GFX1013 Graphics State");
    TEST_RUN(test_gfx1013_wave32_vs_ps_binding);
    TEST_RUN(test_gfx1013_wave32_rejects_but_generic_accepts_wave64);
    TEST_RUN(test_gfx1013_wave32_rejects_small_buffer_atomically);
    TEST_RUN(test_gfx1013_wave32_tessellation_binding);
}
