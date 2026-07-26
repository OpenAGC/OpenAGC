#include "agc_graphics.h"

#include <stdbool.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_pm4.h"
#include "agc_registers.h"

#define AGC_GFX1013_REG_GE_PC_ALLOC 0x260u
#define AGC_GFX1013_GE_PC_ALLOC_NGG 0x000007feu

#define AGC_GFX1013_VGT_DRAW_PAYLOAD_EN_VRS_RATE (1u << 6)

static bool agcGfx1013AddressIsProgramCompatible(uint64_t address)
{
    return address != 0u && (address & 0xffu) == 0u &&
           (address >> 48) == 0u;
}

static bool agcGfx1013IsProgramLo(uint32_t offset)
{
    return offset == AGC_REG_SPI_SHADER_PGM_LO_PS ||
           offset == AGC_REG_SPI_SHADER_PGM_LO_VS ||
           offset == AGC_REG_SPI_SHADER_PGM_LO_GS ||
           offset == AGC_REG_SPI_SHADER_PGM_LO_ES ||
           offset == AGC_REG_SPI_SHADER_PGM_LO_HS ||
           offset == AGC_REG_SPI_SHADER_PGM_LO_LS;
}

static bool agcGfx1013FindProgramPair(
    const AgcGfx1013ShaderBinding *binding, uint32_t *lo_offset)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < binding->num_sh_registers; ++i) {
        uint32_t candidate = binding->sh_registers[i].offset;
        if (!agcGfx1013IsProgramLo(candidate))
            continue;
        for (j = 0; j < binding->num_sh_registers; ++j) {
            if (binding->sh_registers[j].offset == candidate + 1u) {
                *lo_offset = candidate;
                return true;
            }
        }
    }
    return false;
}

static bool agcGfx1013FindCxValue(
    const AgcGfx1013ShaderBinding *binding, uint32_t offset,
    uint32_t *value)
{
    uint32_t i;

    for (i = 0; i < binding->num_cx_registers; ++i) {
        if (binding->cx_registers[i].offset == offset) {
            *value = binding->cx_registers[i].value;
            return true;
        }
    }
    return false;
}

static bool agcGfx1013EmitCx(SceAgcCb *cb, uint32_t offset, uint32_t value)
{
    AgcRegisterValue reg = {offset, value};
    return sceAgcCbSetCxRegistersDirect(cb, &reg, 1u) != NULL;
}

static bool agcGfx1013EmitCxIndexed(
    SceAgcCb *cb, uint32_t offset, uint32_t index, uint32_t value)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3u);
    if (!cmd)
        return false;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u);
    cmd[1] = (offset & 0xFFFFu) | ((index & 0xFu) << 28);
    cmd[2] = value;
    return true;
}

static bool agcGfx1013EmitUc(SceAgcCb *cb, uint32_t offset, uint32_t value)
{
    AgcRegisterValue reg = {offset, value};
    return sceAgcCbSetUcRegistersDirect(cb, &reg, 1u) != NULL;
}

static bool agcGfx1013EmitShader(
    SceAgcCb *cb, const AgcGfx1013ShaderBinding *binding,
    uint32_t program_lo)
{
    uint32_t i;
    uint32_t lo_value = (uint32_t)(binding->code_address >> 8);
    uint32_t hi_value = (uint32_t)(binding->code_address >> 40);

    for (i = 0; i < binding->num_sh_registers; ++i) {
        AgcRegisterValue reg = binding->sh_registers[i];
        if (reg.offset == program_lo)
            reg.value = lo_value;
        else if (reg.offset == program_lo + 1u)
            reg.value = hi_value;
        if (!sceAgcCbSetShRegistersDirect(cb, &reg, 1u))
            return false;
    }
    for (i = 0; i < binding->num_cx_registers; ++i) {
        const AgcRegisterValue reg = binding->cx_registers[i];
        const bool emitted = reg.offset == AGC_REG_VGT_LS_HS_CONFIG
            ? agcGfx1013EmitCxIndexed(cb, reg.offset, 2u, reg.value)
            : sceAgcCbSetCxRegistersDirect(cb, &reg, 1u) != NULL;
        if (!emitted)
            return false;
    }
    return true;
}

typedef struct AgcGfx1013RuntimePatches {
    uint64_t ring_descriptor_address;
    uint64_t next_stage_address;
    uint32_t tcs_offchip_layout;
} AgcGfx1013RuntimePatches;

static bool agcGfx1013BindingHasValue(
    const AgcGfx1013ShaderBinding *binding, uint32_t value)
{
    uint32_t i;

    for (i = 0; i < binding->num_sh_registers; ++i) {
        if (binding->sh_registers[i].value == value)
            return true;
    }
    return false;
}

static bool agcGfx1013EmitShaderPatched(
    SceAgcCb *cb, const AgcGfx1013ShaderBinding *binding,
    uint32_t program_lo, const AgcGfx1013RuntimePatches *patches)
{
    uint32_t i;
    uint32_t lo_value = (uint32_t)(binding->code_address >> 8);
    uint32_t hi_value = (uint32_t)(binding->code_address >> 40);

    for (i = 0; i < binding->num_sh_registers; ++i) {
        AgcRegisterValue reg = binding->sh_registers[i];
        if (reg.offset == program_lo)
            reg.value = lo_value;
        else if (reg.offset == program_lo + 1u)
            reg.value = hi_value;
        else if (reg.value == OPENAGC_RING_OFFSETS_LO_PLACEHOLDER)
            reg.value = (uint32_t)patches->ring_descriptor_address;
        else if (reg.value == OPENAGC_RING_OFFSETS_HI_PLACEHOLDER)
            reg.value = (uint32_t)(patches->ring_descriptor_address >> 32);
        else if (reg.value == OPENAGC_TCS_OFFCHIP_LAYOUT_PLACEHOLDER)
            reg.value = patches->tcs_offchip_layout;
        else if (reg.value == OPENAGC_NEXT_STAGE_PC_PLACEHOLDER)
            reg.value = (uint32_t)patches->next_stage_address;
        if (!sceAgcCbSetShRegistersDirect(cb, &reg, 1u))
            return false;
    }
    for (i = 0; i < binding->num_cx_registers; ++i) {
        const AgcRegisterValue reg = binding->cx_registers[i];
        const bool emitted = reg.offset == AGC_REG_VGT_LS_HS_CONFIG
            ? agcGfx1013EmitCxIndexed(cb, reg.offset, 2u, reg.value)
            : sceAgcCbSetCxRegistersDirect(cb, &reg, 1u) != NULL;
        if (!emitted)
            return false;
    }
    return true;
}

static int32_t agcGfx1013ValidateVsPsImpl(
    const AgcGfx1013Wave32VsPsState *state,
    bool require_primitive_wave32)
{
    const AgcShaderSpecials *specials;
    uint32_t primitive_lo;
    uint32_t pixel_lo;
    uint32_t ps_control;

    if (!state || !state->primitive.record || !state->pixel.record ||
        !state->primitive.sh_registers || !state->pixel.sh_registers ||
        state->primitive.num_sh_registers == 0u ||
        state->pixel.num_sh_registers == 0u ||
        state->primitive.num_sh_registers !=
            state->primitive.record->num_sh_registers ||
        state->pixel.num_sh_registers != state->pixel.record->num_sh_registers ||
        (state->primitive.num_cx_registers != 0u &&
         !state->primitive.cx_registers) ||
        (state->pixel.num_cx_registers != 0u &&
         !state->pixel.cx_registers))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcShaderRecordIsValid(state->primitive.record) ||
        !agcShaderRecordIsValid(state->pixel.record) ||
        state->primitive.record->shader_type !=
            (uint8_t)kAgcShaderBinaryTypeGs ||
        state->pixel.record->shader_type != kAgcShaderTypePs)
        return AGC_ERROR_SHADER_INVALID_TYPE;
    if (!agcGfx1013AddressIsProgramCompatible(
            state->primitive.code_address) ||
        !agcGfx1013AddressIsProgramCompatible(state->pixel.code_address))
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (!agcGfx1013FindProgramPair(&state->primitive, &primitive_lo) ||
        !agcGfx1013FindProgramPair(&state->pixel, &pixel_lo))
        return AGC_ERROR_SHADER_INVALID;

    specials = agcShaderRecordGetSpecialsTyped(state->primitive.record);
    if (!specials ||
        specials->vgt_shader_stages_en.register_offset !=
            AGC_REG_VGT_SHADER_STAGES_EN ||
        (require_primitive_wave32 &&
         (specials->vgt_shader_stages_en.value &
          AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN) == 0u) ||
        !agcGfx1013FindCxValue(
            &state->pixel, AGC_REG_SPI_PS_IN_CONTROL, &ps_control) ||
        (ps_control & AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN) == 0u ||
        agcShaderRecordGetNumInputSemantics(state->pixel.record) > 32u)
        return AGC_ERROR_VALIDATION_FAILED;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013ValidateVsPs(
    const AgcGfx1013Wave32VsPsState *state)
{
    return agcGfx1013ValidateVsPsImpl(state, false);
}

int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32VsPs(
    const AgcGfx1013Wave32VsPsState *state)
{
    return agcGfx1013ValidateVsPsImpl(state, true);
}

static int32_t agcGfx1013BindVsPsImpl(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state,
    bool require_primitive_wave32)
{
    AgcShaderRegister prim_cx[2] = {{0}};
    AgcShaderRegister prim_uc[3] = {{0}};
    AgcShaderRegister interpolants[32] = {{0}};
    uint32_t primitive_lo;
    uint32_t pixel_lo;
    uint32_t input_count;
    uint32_t required_dwords;
    uint32_t i;
    int32_t error;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013ValidateVsPsImpl(state, require_primitive_wave32);
    if (error != AGC_OK)
        return error;

    error = sceAgcCreatePrimState(
        prim_cx, prim_uc, NULL, state->primitive.record,
        state->primitive_type);
    if (error != AGC_OK)
        return error;
    error = sceAgcCreateInterpolantMapping(
        interpolants, state->primitive.record, state->pixel.record);
    if (error != AGC_OK)
        return error;

    input_count = agcShaderRecordGetNumInputSemantics(state->pixel.record);
    required_dwords = 21u + input_count * 3u +
        (state->primitive.num_sh_registers +
         state->primitive.num_cx_registers +
         state->pixel.num_sh_registers +
         state->pixel.num_cx_registers) * 3u;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    for (i = 0; i < 2u; ++i)
        agcGfx1013EmitCx(cb, prim_cx[i].offset, prim_cx[i].value);
    for (i = 0; i < 3u; ++i)
        agcGfx1013EmitUc(cb, prim_uc[i].offset, prim_uc[i].value);
    agcGfx1013EmitCx(
        cb, AGC_REG_VGT_DRAW_PAYLOAD_CNTL,
        AGC_GFX1013_VGT_DRAW_PAYLOAD_EN_VRS_RATE);
    agcGfx1013EmitUc(
        cb, AGC_GFX1013_REG_GE_PC_ALLOC, AGC_GFX1013_GE_PC_ALLOC_NGG);
    for (i = 0; i < input_count; ++i) {
        agcGfx1013EmitCx(
            cb, AGC_REG_SPI_PS_INPUT_CNTL_0 + i, interpolants[i].value);
    }

    agcGfx1013FindProgramPair(&state->primitive, &primitive_lo);
    agcGfx1013FindProgramPair(&state->pixel, &pixel_lo);
    if (!agcGfx1013EmitShader(cb, &state->primitive, primitive_lo) ||
        !agcGfx1013EmitShader(cb, &state->pixel, pixel_lo))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013BindVsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state)
{
    return agcGfx1013BindVsPsImpl(cb, state, false);
}

int32_t PS5_SYSV_ABI agcGfx1013BindWave32VsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state)
{
    return agcGfx1013BindVsPsImpl(cb, state, true);
}

int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32TessVsPs(
    const AgcGfx1013Wave32TessVsPsState *state)
{
    const AgcShaderSpecials *hs_specials;
    const AgcShaderSpecials *gs_specials;
    uint32_t hs_program_lo;
    uint32_t gs_program_lo;
    uint32_t ps_program_lo;
    uint32_t ps_control;

    if (!state || !state->hull.record || !state->primitive.record ||
        !state->pixel.record || !state->hull.sh_registers ||
        !state->primitive.sh_registers || !state->pixel.sh_registers ||
        state->hull.num_sh_registers == 0u ||
        state->primitive.num_sh_registers == 0u ||
        state->pixel.num_sh_registers == 0u ||
        state->hull.num_sh_registers != state->hull.record->num_sh_registers ||
        state->primitive.num_sh_registers !=
            state->primitive.record->num_sh_registers ||
        state->pixel.num_sh_registers != state->pixel.record->num_sh_registers ||
        (state->hull.num_cx_registers != 0u && !state->hull.cx_registers) ||
        (state->primitive.num_cx_registers != 0u &&
         !state->primitive.cx_registers) ||
        (state->pixel.num_cx_registers != 0u && !state->pixel.cx_registers))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcShaderRecordIsValid(state->hull.record) ||
        !agcShaderRecordIsValid(state->primitive.record) ||
        !agcShaderRecordIsValid(state->pixel.record) ||
        state->hull.record->shader_type != (uint8_t)kAgcShaderBinaryTypeHs ||
        state->primitive.record->shader_type !=
            (uint8_t)kAgcShaderBinaryTypeGs ||
        state->pixel.record->shader_type != kAgcShaderTypePs)
        return AGC_ERROR_SHADER_INVALID_TYPE;
    if (!agcGfx1013AddressIsProgramCompatible(state->hull.code_address) ||
        !agcGfx1013AddressIsProgramCompatible(state->primitive.code_address) ||
        !agcGfx1013AddressIsProgramCompatible(state->pixel.code_address) ||
        !agcGfx1013AddressIsProgramCompatible(state->hull_back_code_address) ||
        !agcGfx1013AddressIsProgramCompatible(
            state->primitive_back_code_address) ||
        (state->ring_descriptor_address & 0xfu) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if ((state->hull.code_address >> 32) !=
            (state->hull_back_code_address >> 32) ||
        (state->primitive.code_address >> 32) !=
            (state->primitive_back_code_address >> 32))
        return AGC_ERROR_VALIDATION_FAILED;
    if (!agcGfx1013FindProgramPair(&state->hull, &hs_program_lo) ||
        !agcGfx1013FindProgramPair(&state->primitive, &gs_program_lo) ||
        !agcGfx1013FindProgramPair(&state->pixel, &ps_program_lo))
        return AGC_ERROR_SHADER_INVALID;

    hs_specials = agcShaderRecordGetSpecialsTyped(state->hull.record);
    gs_specials = agcShaderRecordGetSpecialsTyped(state->primitive.record);
    if (!hs_specials || !gs_specials ||
        hs_specials->vgt_shader_stages_en.register_offset !=
            AGC_REG_VGT_SHADER_STAGES_EN ||
        gs_specials->vgt_shader_stages_en.register_offset !=
            AGC_REG_VGT_SHADER_STAGES_EN ||
        (hs_specials->vgt_shader_stages_en.value &
         AGC_GFX1013_VGT_SHADER_STAGES_EN_HS_W32_EN) == 0u ||
        (gs_specials->vgt_shader_stages_en.value &
         AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN) == 0u ||
        !agcGfx1013FindCxValue(
            &state->pixel, AGC_REG_SPI_PS_IN_CONTROL, &ps_control) ||
        (ps_control & AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN) == 0u ||
        agcShaderRecordGetNumInputSemantics(state->pixel.record) > 32u ||
        !agcGfx1013BindingHasValue(
            &state->hull, OPENAGC_RING_OFFSETS_LO_PLACEHOLDER) ||
        !agcGfx1013BindingHasValue(
            &state->hull, OPENAGC_RING_OFFSETS_HI_PLACEHOLDER) ||
        !agcGfx1013BindingHasValue(
            &state->hull, OPENAGC_TCS_OFFCHIP_LAYOUT_PLACEHOLDER) ||
        !agcGfx1013BindingHasValue(
            &state->hull, OPENAGC_NEXT_STAGE_PC_PLACEHOLDER) ||
        !agcGfx1013BindingHasValue(
            &state->primitive, OPENAGC_RING_OFFSETS_LO_PLACEHOLDER) ||
        !agcGfx1013BindingHasValue(
            &state->primitive, OPENAGC_RING_OFFSETS_HI_PLACEHOLDER) ||
        !agcGfx1013BindingHasValue(
            &state->primitive, OPENAGC_TCS_OFFCHIP_LAYOUT_PLACEHOLDER))
        return AGC_ERROR_VALIDATION_FAILED;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013BindWave32TessVsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32TessVsPsState *state)
{
    AgcShaderRegister prim_cx[2] = {{0}};
    AgcShaderRegister prim_uc[3] = {{0}};
    AgcShaderRegister interpolants[32] = {{0}};
    AgcGfx1013RuntimePatches hs_patches;
    AgcGfx1013RuntimePatches gs_patches;
    const AgcShaderSpecials *hs_specials;
    const AgcShaderSpecials *gs_specials;
    uint32_t hs_program_lo;
    uint32_t gs_program_lo;
    uint32_t ps_program_lo;
    uint32_t input_count;
    uint32_t required_dwords;
    uint32_t i;
    int32_t error;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013ValidateWave32TessVsPs(state);
    if (error != AGC_OK)
        return error;
    error = sceAgcCreatePrimState(
        prim_cx, prim_uc, NULL, state->primitive.record,
        state->primitive_type);
    if (error != AGC_OK)
        return error;
    error = sceAgcCreateInterpolantMapping(
        interpolants, state->primitive.record, state->pixel.record);
    if (error != AGC_OK)
        return error;

    input_count = agcShaderRecordGetNumInputSemantics(state->pixel.record);
    required_dwords = 24u + input_count * 3u +
        (state->hull.num_sh_registers + state->hull.num_cx_registers +
         state->primitive.num_sh_registers +
         state->primitive.num_cx_registers +
         state->pixel.num_sh_registers + state->pixel.num_cx_registers) * 3u;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    for (i = 0; i < 2u; ++i)
        agcGfx1013EmitCx(cb, prim_cx[i].offset, prim_cx[i].value);
    for (i = 0; i < 3u; ++i)
        agcGfx1013EmitUc(cb, prim_uc[i].offset, prim_uc[i].value);
    hs_specials = agcShaderRecordGetSpecialsTyped(state->hull.record);
    gs_specials = agcShaderRecordGetSpecialsTyped(state->primitive.record);
    agcGfx1013EmitCx(
        cb, AGC_REG_VGT_SHADER_STAGES_EN,
        hs_specials->vgt_shader_stages_en.value |
        gs_specials->vgt_shader_stages_en.value);
    agcGfx1013EmitCx(
        cb, AGC_REG_VGT_DRAW_PAYLOAD_CNTL,
        AGC_GFX1013_VGT_DRAW_PAYLOAD_EN_VRS_RATE);
    agcGfx1013EmitUc(
        cb, AGC_GFX1013_REG_GE_PC_ALLOC, AGC_GFX1013_GE_PC_ALLOC_NGG);
    for (i = 0; i < input_count; ++i) {
        agcGfx1013EmitCx(
            cb, AGC_REG_SPI_PS_INPUT_CNTL_0 + i, interpolants[i].value);
    }

    agcGfx1013FindProgramPair(&state->hull, &hs_program_lo);
    agcGfx1013FindProgramPair(&state->primitive, &gs_program_lo);
    agcGfx1013FindProgramPair(&state->pixel, &ps_program_lo);
    hs_patches = (AgcGfx1013RuntimePatches){
        state->ring_descriptor_address,
        state->hull_back_code_address,
        state->tcs_offchip_layout,
    };
    gs_patches = (AgcGfx1013RuntimePatches){
        state->ring_descriptor_address,
        state->primitive_back_code_address,
        state->tcs_offchip_layout,
    };
    if (!agcGfx1013EmitShaderPatched(
            cb, &state->hull, hs_program_lo, &hs_patches) ||
        !agcGfx1013EmitShaderPatched(
            cb, &state->primitive, gs_program_lo, &gs_patches) ||
        !agcGfx1013EmitShader(cb, &state->pixel, ps_program_lo))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}
