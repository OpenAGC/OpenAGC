#include "agc_graphics.h"

#include <stdbool.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_registers.h"

#define AGC_GFX1013_REG_GE_PC_ALLOC 0x260u
#define AGC_GFX1013_GE_PC_ALLOC_ALL 0x000007feu

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
        if (!sceAgcCbSetCxRegistersDirect(
                cb, &binding->cx_registers[i], 1u))
            return false;
    }
    return true;
}

int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32VsPs(
    const AgcGfx1013Wave32VsPsState *state)
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
        (specials->vgt_shader_stages_en.value &
         AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN) == 0u ||
        !agcGfx1013FindCxValue(
            &state->pixel, AGC_REG_SPI_PS_IN_CONTROL, &ps_control) ||
        (ps_control & AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN) == 0u ||
        agcShaderRecordGetNumInputSemantics(state->pixel.record) > 32u)
        return AGC_ERROR_VALIDATION_FAILED;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013BindWave32VsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state)
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
    error = agcGfx1013ValidateWave32VsPs(state);
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
    required_dwords = 18u + input_count * 3u +
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
    agcGfx1013EmitUc(
        cb, AGC_GFX1013_REG_GE_PC_ALLOC, AGC_GFX1013_GE_PC_ALLOC_ALL);
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
