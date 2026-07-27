#include "agc_graphics.h"

#include <stdbool.h>
#include <string.h>

#include "agc_cb.h"
#include "agc_context.h"
#include "agc_error.h"
#include "agc_pm4.h"
#include "agc_registers.h"

#define AGC_GFX1013_REG_GE_PC_ALLOC 0x260u
#define AGC_GFX1013_GE_PC_ALLOC_NGG 0x000007feu

#define AGC_GFX1013_VGT_DRAW_PAYLOAD_EN_VRS_RATE (1u << 6)

static int32_t agcGfx1013ValidateTessellationState(
    const AgcGfx1013TessellationState *state);

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
    if (agcGfx1013BindingHasValue(
            &state->primitive, OPENAGC_NEXT_STAGE_PC_PLACEHOLDER) &&
        !agcGfx1013AddressIsProgramCompatible(
            state->primitive_back_code_address))
        return AGC_ERROR_INVALID_ALIGNMENT;

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
    const AgcGfx1013RuntimePatches primitive_patches = {
        .next_stage_address = state->primitive_back_code_address,
    };
    if (!agcGfx1013EmitShaderPatched(
            cb, &state->primitive, primitive_lo, &primitive_patches) ||
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

int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndexAuto(
    SceAgcCb *cb, const AgcGfx1013BaselineDrawState *state)
{
    uint32_t input_count;
    uint32_t required_dwords;
    uint32_t primitive_resource_count = 0u;
    uint32_t pixel_resource_count = 0u;
    uint32_t i;
    int32_t error;

    if (!cb || !state)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (state->index_type > (uint32_t)kAgcIndexSize32 ||
        state->index_swap > 1u || state->instance_count == 0u ||
        state->vertex_count == 0u) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if ((state->num_post_bind_sh_registers != 0u &&
         !state->post_bind_sh_registers) ||
        (state->num_post_bind_cx_registers != 0u &&
         !state->post_bind_cx_registers) ||
        (state->num_post_bind_uc_registers != 0u &&
         !state->post_bind_uc_registers) ||
        state->num_post_bind_sh_registers > 0x3ffeu ||
        state->num_post_bind_cx_registers > 0x3ffeu ||
        state->num_post_bind_uc_registers > 0x3ffeu) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }

    error = agcGfx1013ValidateVsPs(&state->shaders);
    if (error != AGC_OK)
        return error;
    if (state->num_primitive_resource_tables != 0u) {
        error = agcGfx1013ValidateResourceTables(
            &state->shaders.primitive, state->primitive_resource_tables,
            state->num_primitive_resource_tables, &primitive_resource_count);
        if (error != AGC_OK)
            return error;
    }
    if (state->num_pixel_resource_tables != 0u) {
        error = agcGfx1013ValidateResourceTables(
            &state->shaders.pixel, state->pixel_resource_tables,
            state->num_pixel_resource_tables, &pixel_resource_count);
        if (error != AGC_OK)
            return error;
    }

    input_count = agcShaderRecordGetNumInputSemantics(
        state->shaders.pixel.record);
    required_dwords = 29u + input_count * 3u +
        (state->shaders.primitive.num_sh_registers +
         state->shaders.primitive.num_cx_registers +
         state->shaders.pixel.num_sh_registers +
         state->shaders.pixel.num_cx_registers +
         primitive_resource_count + pixel_resource_count +
         state->num_post_bind_sh_registers +
         state->num_post_bind_cx_registers +
         state->num_post_bind_uc_registers) * 3u;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    error = agcGfx1013BindVsPs(cb, &state->shaders);
    if (error != AGC_OK)
        return error;
    if (state->num_primitive_resource_tables != 0u) {
        error = agcGfx1013BindResourceTables(
            cb, &state->shaders.primitive,
            state->primitive_resource_tables,
            state->num_primitive_resource_tables);
        if (error != AGC_OK)
            return error;
    }
    if (state->num_pixel_resource_tables != 0u) {
        error = agcGfx1013BindResourceTables(
            cb, &state->shaders.pixel, state->pixel_resource_tables,
            state->num_pixel_resource_tables);
        if (error != AGC_OK)
            return error;
    }
    for (i = 0; i < state->num_post_bind_sh_registers; ++i) {
        if (!sceAgcCbSetShRegistersDirect(
                cb, &state->post_bind_sh_registers[i], 1u)) {
            return AGC_ERROR_INTERNAL;
        }
    }
    for (i = 0; i < state->num_post_bind_cx_registers; ++i) {
        if (!sceAgcCbSetCxRegistersDirect(
                cb, &state->post_bind_cx_registers[i], 1u)) {
            return AGC_ERROR_INTERNAL;
        }
    }
    for (i = 0; i < state->num_post_bind_uc_registers; ++i) {
        if (!sceAgcCbSetUcRegistersDirect(
                cb, &state->post_bind_uc_registers[i], 1u)) {
            return AGC_ERROR_INTERNAL;
        }
    }
    if (!sceAgcDcbSetIndexSize(cb, state->index_type, state->index_swap) ||
        !sceAgcDcbSetNumInstances(cb, state->instance_count) ||
        !sceAgcDcbDrawIndexAuto(
            cb, state->vertex_count, state->draw_modifier)) {
        return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
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

int32_t PS5_SYSV_ABI agcGfx1013DrawTessIndexAuto(
    SceAgcCb *cb, const AgcGfx1013TessDrawState *state)
{
    uint32_t hull_resource_count = 0u;
    uint32_t primitive_resource_count = 0u;
    uint32_t pixel_resource_count = 0u;
    uint32_t input_count;
    uint32_t required_dwords;
    uint32_t i;
    int32_t error;

    if (!cb || !state || !state->tessellation ||
        state->instance_count == 0u || state->vertex_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((state->num_post_bind_sh_registers != 0u &&
         !state->post_bind_sh_registers) ||
        (state->num_post_bind_cx_registers != 0u &&
         !state->post_bind_cx_registers) ||
        (state->num_post_bind_uc_registers != 0u &&
         !state->post_bind_uc_registers) ||
        state->num_post_bind_sh_registers > 0x3ffeu ||
        state->num_post_bind_cx_registers > 0x3ffeu ||
        state->num_post_bind_uc_registers > 0x3ffeu)
        return AGC_ERROR_INVALID_ARGUMENT;

    error = agcGfx1013ValidateWave32TessVsPs(&state->shaders);
    if (error != AGC_OK)
        return error;
    error = agcGfx1013ValidateTessellationState(state->tessellation);
    if (error != AGC_OK)
        return error;
    if (state->num_hull_resource_tables != 0u) {
        error = agcGfx1013ValidateResourceTables(
            &state->shaders.hull, state->hull_resource_tables,
            state->num_hull_resource_tables, &hull_resource_count);
        if (error != AGC_OK)
            return error;
    }
    if (state->num_primitive_resource_tables != 0u) {
        error = agcGfx1013ValidateResourceTables(
            &state->shaders.primitive, state->primitive_resource_tables,
            state->num_primitive_resource_tables, &primitive_resource_count);
        if (error != AGC_OK)
            return error;
    }
    if (state->num_pixel_resource_tables != 0u) {
        error = agcGfx1013ValidateResourceTables(
            &state->shaders.pixel, state->pixel_resource_tables,
            state->num_pixel_resource_tables, &pixel_resource_count);
        if (error != AGC_OK)
            return error;
    }

    input_count = agcShaderRecordGetNumInputSemantics(
        state->shaders.pixel.record);
    required_dwords = 44u + input_count * 3u +
        (state->shaders.hull.num_sh_registers +
         state->shaders.hull.num_cx_registers +
         state->shaders.primitive.num_sh_registers +
         state->shaders.primitive.num_cx_registers +
         state->shaders.pixel.num_sh_registers +
         state->shaders.pixel.num_cx_registers +
         hull_resource_count + primitive_resource_count +
         pixel_resource_count + state->num_post_bind_sh_registers +
         state->num_post_bind_cx_registers +
         state->num_post_bind_uc_registers) * 3u;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    error = agcGfx1013BindWave32TessVsPs(cb, &state->shaders);
    if (error != AGC_OK)
        return error;
    if (state->num_hull_resource_tables != 0u &&
        agcGfx1013BindResourceTables(
            cb, &state->shaders.hull, state->hull_resource_tables,
            state->num_hull_resource_tables) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->num_primitive_resource_tables != 0u &&
        agcGfx1013BindResourceTables(
            cb, &state->shaders.primitive,
            state->primitive_resource_tables,
            state->num_primitive_resource_tables) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->num_pixel_resource_tables != 0u &&
        agcGfx1013BindResourceTables(
            cb, &state->shaders.pixel, state->pixel_resource_tables,
            state->num_pixel_resource_tables) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (agcGfx1013SetTessellationContext(
            cb, state->tessellation) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    for (i = 0u; i < state->num_post_bind_sh_registers; ++i) {
        if (!sceAgcCbSetShRegistersDirect(
                cb, &state->post_bind_sh_registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    for (i = 0u; i < state->num_post_bind_cx_registers; ++i) {
        if (!sceAgcCbSetCxRegistersDirect(
                cb, &state->post_bind_cx_registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    for (i = 0u; i < state->num_post_bind_uc_registers; ++i) {
        if (!sceAgcCbSetUcRegistersDirect(
                cb, &state->post_bind_uc_registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    if (!sceAgcDcbSetNumInstances(cb, state->instance_count) ||
        !sceAgcDcbDrawIndexAuto(
            cb, state->vertex_count, state->draw_modifier))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SignalEopFence(
    SceAgcCb *cb, const AgcGfx1013EopFenceState *state)
{
    if (!cb || !state || state->address == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((state->address & 3u) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if ((state->address >> 48u) != 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_EOP_FENCE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    if (!sceAgcCbReleaseMem(
            cb, AGC_GFX1013_EOP_CACHE_FLUSH_EVENT,
            AGC_GFX1013_EOP_GCR_CONTROL, 0u,
            AGC_GFX1013_EOP_CACHE_POLICY_LRU, state->address, 1u,
            state->value, 0u, 0u, 0u, 0u) ||
        !sceAgcCbNop(cb, 2u))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

static uint32_t agcGfx1013FloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int32_t PS5_SYSV_ABI agcGfx1013SetColorTarget(
    SceAgcCb *cb, const AgcGfx1013ColorTargetState *state)
{
    uint32_t *cmd;
    uint32_t regs[14] = {0};
    uint32_t tiles_per_row;
    uint64_t tile_count;
    bool supported;

    if (!cb || !state || state->address == 0u || state->width == 0u ||
        state->height == 0u || state->width > 0x4000u ||
        state->height > 0x4000u || (state->width & 7u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((state->address & 0xffu) != 0u || (state->address >> 48) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;

    supported =
        (state->color_format == AGC_GFX1013_COLOR_FORMAT_8_8_8_8 &&
         state->number_type == AGC_GFX1013_SURFACE_NUMBER_UNORM &&
         state->component_swap == AGC_GFX1013_SURFACE_SWAP_ALT) ||
        (state->color_format == AGC_GFX1013_COLOR_FORMAT_16_16_16_16 &&
         state->number_type == AGC_GFX1013_SURFACE_NUMBER_FLOAT &&
         state->component_swap == AGC_GFX1013_SURFACE_SWAP_STD);
    if (!supported)
        return AGC_ERROR_NOT_SUPPORTED;

    tiles_per_row = state->width / 8u;
    tile_count = (uint64_t)tiles_per_row * state->height;
    if (tiles_per_row > 0x800u || tile_count == 0u || tile_count > 0x400000u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < 28u)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    regs[0] = (uint32_t)(state->address >> 8);
    regs[1] = tiles_per_row - 1u;
    regs[2] = (uint32_t)tile_count - 1u;
    regs[4] =
        (state->color_format << AGC_REG_CB_COLOR0_INFO_FORMAT_SHIFT) |
        (state->number_type << AGC_REG_CB_COLOR0_INFO_NUMBER_TYPE_SHIFT) |
        (state->component_swap << 11) | (1u << 16);

    cmd = agcCbAllocDwords(cb, 16u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 16u);
    cmd[1] = AGC_REG_CB_COLOR0_BASE;
    memcpy(&cmd[2], regs, sizeof(regs));

    if (!agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR0_BASE_EXT,
            (uint32_t)(state->address >> 40)) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR0_ATTRIB2,
            ((state->height - 1u) & 0x3fffu) |
            (((state->width - 1u) & 0x3fffu) << 14)) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR0_ATTRIB3, 0x09000001u) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR_CONTROL, 0x00cc0010u))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetViewport(
    SceAgcCb *cb, const AgcGfx1013ViewportState *state)
{
    uint32_t *cmd;
    uint32_t extent;
    uint32_t regs[6];

    if (!cb || !state || state->width == 0u || state->height == 0u ||
        state->width > 0x7fffu || state->height > 0x7fffu)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < 15u)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    extent = state->width < state->height ? state->width : state->height;
    regs[0] = agcGfx1013FloatBits((float)extent * 0.5f);
    regs[1] = agcGfx1013FloatBits((float)state->width * 0.5f);
    regs[2] = agcGfx1013FloatBits(-(float)extent * 0.5f);
    regs[3] = agcGfx1013FloatBits((float)state->height * 0.5f);
    regs[4] = agcGfx1013FloatBits(0.5f);
    regs[5] = agcGfx1013FloatBits(0.5f);

    cmd = agcCbAllocDwords(cb, 8u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 8u);
    cmd[1] = AGC_REG_PA_CL_VPORT_XSCALE;
    memcpy(&cmd[2], regs, sizeof(regs));

    cmd = agcCbAllocDwords(cb, 4u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
    cmd[1] = AGC_REG_PA_SC_VPORT_ZMIN_0;
    cmd[2] = agcGfx1013FloatBits(0.0f);
    cmd[3] = agcGfx1013FloatBits(1.0f);
    if (!agcGfx1013EmitCx(cb, AGC_REG_PA_CL_VTE_CNTL, 0x0000043fu))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetScissor(
    SceAgcCb *cb, const AgcGfx1013ScissorState *state)
{
    static const uint32_t offsets[6] = {
        AGC_REG_PA_SC_WINDOW_SCISSOR_TL,
        AGC_REG_PA_SC_WINDOW_SCISSOR_BR,
        AGC_REG_PA_SC_GENERIC_SCISSOR_TL,
        AGC_REG_PA_SC_GENERIC_SCISSOR_BR,
        AGC_REG_PA_SC_VPORT_SCISSOR_0_TL,
        AGC_REG_PA_SC_VPORT_SCISSOR_0_BR,
    };
    uint32_t *cmd;
    uint32_t tl;
    uint32_t br;
    uint32_t i;

    if (!cb || !state || state->left >= state->right ||
        state->top >= state->bottom || state->right > 0x7fffu ||
        state->bottom > 0x7fffu)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < 22u)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    tl = state->left | (state->top << 16);
    br = state->right | (state->bottom << 16);
    cmd = agcCbAllocDwords(cb, 4u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
    cmd[1] = AGC_REG_PA_SC_SCREEN_SCISSOR_TL;
    cmd[2] = tl;
    cmd[3] = br;
    for (i = 0; i < 6u; ++i) {
        uint32_t value = (i & 1u) == 0u ? tl : br;
        if (!agcGfx1013EmitCx(cb, offsets[i], value))
            return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetTargetMask(
    SceAgcCb *cb, uint32_t mask)
{
    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < 3u)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    return agcGfx1013EmitCx(cb, AGC_REG_CB_TARGET_MASK, mask)
        ? AGC_OK : AGC_ERROR_INTERNAL;
}

int32_t PS5_SYSV_ABI agcGfx1013SetDepthDisabled(SceAgcCb *cb)
{
    static const AgcRegisterValue regs[5] = {
        {AGC_REG_DB_DEPTH_INFO, 0u},
        {AGC_REG_DB_Z_INFO, 0u},
        {AGC_REG_DB_STENCIL_INFO, 0u},
        {AGC_REG_DB_SHADER_CONTROL, 0x00000010u},
        {AGC_REG_DB_DEPTH_CONTROL, 0u},
    };
    uint32_t i;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < 15u)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    for (i = 0; i < 5u; ++i) {
        if (!agcGfx1013EmitCx(cb, regs[i].offset, regs[i].value))
            return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
}

static uint32_t agcGfx1013CountDefaults(
    const AgcRegisterDefaultsGroup *groups, uint32_t group_count,
    AgcRegisterDefaultSpace space)
{
    uint32_t count = 0u;
    uint32_t i;
    for (i = 0; i < group_count; ++i) {
        if (groups[i].space == space)
            count += groups[i].register_count;
    }
    return count;
}

static bool agcGfx1013EmitDefaults(
    SceAgcCb *cb, const AgcRegisterDefaultsGroup *groups,
    uint32_t group_count, AgcRegisterDefaultSpace space)
{
    uint32_t i;
    uint32_t j;
    for (i = 0; i < group_count; ++i) {
        if (groups[i].space != space)
            continue;
        for (j = 0; j < groups[i].register_count; ++j) {
            AgcRegisterValue reg = {
                groups[i].registers[j].offset,
                groups[i].registers[j].value,
            };
            uint32_t *emitted = space == kAgcRegisterDefaultSpaceSh
                ? sceAgcCbSetShRegistersDirect(cb, &reg, 1u)
                : space == kAgcRegisterDefaultSpaceCx
                    ? sceAgcCbSetCxRegistersDirect(cb, &reg, 1u)
                    : sceAgcCbSetUcRegistersDirect(cb, &reg, 1u);
            if (!emitted)
                return false;
        }
    }
    return true;
}

int32_t PS5_SYSV_ABI agcGfx1013ApplyGraphicsDefaultsV8(
    SceAgcCb *cb, AgcGfx1013GraphicsDefaultStats *stats)
{
    const AgcRegisterDefaultsGroup *primary;
    const AgcRegisterDefaultsGroup *internal;
    AgcGfx1013GraphicsDefaultStats counts = {0};
    uint32_t primary_count;
    uint32_t internal_count;
    uint32_t required_dwords;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    primary = agcRegisterDefaultsV8GetPrimaryGroups(&primary_count);
    internal = agcRegisterDefaultsV8GetInternalGroups(&internal_count);
    if (!primary || !internal)
        return AGC_ERROR_INTERNAL;

    counts.sh_register_count =
        agcGfx1013CountDefaults(primary, primary_count,
            kAgcRegisterDefaultSpaceSh) +
        agcGfx1013CountDefaults(internal, internal_count,
            kAgcRegisterDefaultSpaceSh);
    counts.cx_register_count =
        agcGfx1013CountDefaults(primary, primary_count,
            kAgcRegisterDefaultSpaceCx) +
        agcGfx1013CountDefaults(internal, internal_count,
            kAgcRegisterDefaultSpaceCx);
    counts.uc_register_count =
        agcGfx1013CountDefaults(primary, primary_count,
            kAgcRegisterDefaultSpaceUc) +
        agcGfx1013CountDefaults(internal, internal_count,
            kAgcRegisterDefaultSpaceUc);
    required_dwords = 3u * (counts.sh_register_count +
        counts.cx_register_count + counts.uc_register_count);
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    if (!agcGfx1013EmitDefaults(cb, primary, primary_count,
            kAgcRegisterDefaultSpaceSh) ||
        !agcGfx1013EmitDefaults(cb, internal, internal_count,
            kAgcRegisterDefaultSpaceSh) ||
        !agcGfx1013EmitDefaults(cb, primary, primary_count,
            kAgcRegisterDefaultSpaceCx) ||
        !agcGfx1013EmitDefaults(cb, internal, internal_count,
            kAgcRegisterDefaultSpaceCx) ||
        !agcGfx1013EmitDefaults(cb, primary, primary_count,
            kAgcRegisterDefaultSpaceUc) ||
        !agcGfx1013EmitDefaults(cb, internal, internal_count,
            kAgcRegisterDefaultSpaceUc))
        return AGC_ERROR_INTERNAL;
    if (stats)
        *stats = counts;
    return AGC_OK;
}

static bool agcGfx1013FindComputeRegister(
    const AgcGfx1013ComputeState *state, uint32_t offset, uint32_t *value)
{
    uint32_t i;

    for (i = 0; i < state->num_sh_registers; ++i) {
        if (state->sh_registers[i].offset == offset) {
            *value = state->sh_registers[i].value;
            return true;
        }
    }
    return false;
}

static bool agcGfx1013EmitComputeRegisters(
    SceAgcCb *cb, uint32_t first_offset, const uint32_t *values,
    uint32_t value_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, value_count + 2u);
    uint32_t i;

    if (!cmd)
        return false;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG, value_count + 2u) | 1u;
    cmd[1] = first_offset & 0xffffu;
    for (i = 0; i < value_count; ++i)
        cmd[2u + i] = values[i];
    return true;
}

int32_t PS5_SYSV_ABI agcGfx1013SetContextControl(
    SceAgcCb *cb, uint32_t load_control, uint32_t shadow_control)
{
    uint32_t *cmd;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < 3u)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    cmd = agcCbAllocDwords(cb, 3u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_CONTEXT_CONTROL, 3u);
    cmd[1] = load_control;
    cmd[2] = shadow_control;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013ValidateCompute(
    const AgcGfx1013ComputeState *state)
{
    uint32_t value;
    uint64_t local_invocations;

    if (!state || !state->record || !state->sh_registers ||
        state->num_sh_registers == 0u ||
        state->num_sh_registers != state->record->num_sh_registers ||
        (state->num_user_data != 0u && !state->user_data) ||
        state->num_user_data > 16u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (state->record->magic != AGC_SHADER_RECORD_MAGIC ||
        state->record->version != AGC_SHADER_RECORD_VERSION_GEN5)
        return AGC_ERROR_SHADER_INVALID;
    if (state->record->shader_type != kAgcShaderTypeCs)
        return AGC_ERROR_SHADER_INVALID_TYPE;
    if (!agcGfx1013AddressIsProgramCompatible(state->code_address))
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (state->local_size_x == 0u || state->local_size_y == 0u ||
        state->local_size_z == 0u || state->group_count_x == 0u ||
        state->group_count_y == 0u || state->group_count_z == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    local_invocations = (uint64_t)state->local_size_x *
        state->local_size_y * state->local_size_z;
    if (local_invocations > 1024u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcGfx1013FindComputeRegister(
            state, AGC_REG_COMPUTE_PGM_RSRC1, &value) ||
        !agcGfx1013FindComputeRegister(
            state, AGC_REG_COMPUTE_PGM_RSRC2, &value) ||
        !agcGfx1013FindComputeRegister(
            state, AGC_REG_COMPUTE_PGM_RSRC3, &value))
        return AGC_ERROR_SHADER_INVALID;
    return AGC_OK;
}

static uint32_t agcGfx1013ComputeDefaultsDwords(
    const AgcRegisterDefaultsGroup *groups, uint32_t group_count,
    AgcGfx1013ComputeDefaultStats *stats)
{
    uint32_t dwords = 0u;
    uint32_t i;

    for (i = 0; i < group_count; ++i) {
        if (groups[i].space != kAgcRegisterDefaultSpaceSh ||
            groups[i].register_count == 0u)
            continue;
        dwords += groups[i].register_count + 2u;
        stats->sh_register_count += groups[i].register_count;
        stats->packet_count++;
    }
    return dwords;
}

static bool agcGfx1013EmitComputeDefaults(
    SceAgcCb *cb, const AgcRegisterDefaultsGroup *groups,
    uint32_t group_count)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < group_count; ++i) {
        uint32_t values[64];
        if (groups[i].space != kAgcRegisterDefaultSpaceSh ||
            groups[i].register_count == 0u)
            continue;
        if (groups[i].register_count > 64u)
            return false;
        for (j = 0; j < groups[i].register_count; ++j)
            values[j] = groups[i].registers[j].value;
        if (!agcGfx1013EmitComputeRegisters(cb,
                groups[i].registers[0].offset, values,
                groups[i].register_count))
            return false;
    }
    return true;
}

int32_t PS5_SYSV_ABI agcGfx1013ApplyComputeDefaultsV8(
    SceAgcCb *cb, AgcGfx1013ComputeDefaultStats *stats)
{
    const AgcRegisterDefaultsGroup *primary;
    const AgcRegisterDefaultsGroup *internal;
    AgcGfx1013ComputeDefaultStats counts = {0};
    uint32_t primary_count;
    uint32_t internal_count;
    uint32_t required_dwords;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    primary = agcRegisterDefaultsV8GetPrimaryGroups(&primary_count);
    internal = agcRegisterDefaultsV8GetInternalGroups(&internal_count);
    if (!primary || !internal)
        return AGC_ERROR_INTERNAL;
    required_dwords = agcGfx1013ComputeDefaultsDwords(
        primary, primary_count, &counts);
    required_dwords += agcGfx1013ComputeDefaultsDwords(
        internal, internal_count, &counts);
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    if (!agcGfx1013EmitComputeDefaults(cb, primary, primary_count) ||
        !agcGfx1013EmitComputeDefaults(cb, internal, internal_count))
        return AGC_ERROR_INTERNAL;
    if (stats)
        *stats = counts;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013DispatchCompute(
    SceAgcCb *cb, const AgcGfx1013ComputeState *state)
{
    uint32_t limits0[3] = {0x3fffffffu, 0xffffffffu, 0xffffffffu};
    uint32_t limits1[2] = {0xffffffffu, 0xffffffffu};
    uint32_t threads[6];
    uint32_t program[2];
    uint32_t resources[2];
    uint32_t resource3;
    uint32_t *dispatch;
    int32_t result;
    uint32_t required_dwords;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGfx1013ValidateCompute(state);
    if (result != AGC_OK)
        return result;
    required_dwords = 38u + state->num_user_data;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    threads[0] = 0u;
    threads[1] = 0u;
    threads[2] = 0u;
    threads[3] = state->local_size_x;
    threads[4] = state->local_size_y;
    threads[5] = state->local_size_z;
    program[0] = (uint32_t)(state->code_address >> 8u);
    program[1] = (uint32_t)(state->code_address >> 40u) & 0xffu;
    (void)agcGfx1013FindComputeRegister(
        state, AGC_REG_COMPUTE_PGM_RSRC1, &resources[0]);
    (void)agcGfx1013FindComputeRegister(
        state, AGC_REG_COMPUTE_PGM_RSRC2, &resources[1]);
    (void)agcGfx1013FindComputeRegister(
        state, AGC_REG_COMPUTE_PGM_RSRC3, &resource3);

    if (agcGfx1013SetContextControl(
            cb, 0x80000000u, 0x80000000u) != AGC_OK ||
        !agcGfx1013EmitComputeRegisters(
            cb, AGC_REG_COMPUTE_RESOURCE_LIMITS, limits0, 3u) ||
        !agcGfx1013EmitComputeRegisters(cb, 0x219u, limits1, 2u) ||
        !agcGfx1013EmitComputeRegisters(
            cb, AGC_REG_COMPUTE_START_X, threads, 6u) ||
        !agcGfx1013EmitComputeRegisters(
            cb, AGC_REG_COMPUTE_PGM_LO, program, 2u) ||
        !agcGfx1013EmitComputeRegisters(
            cb, AGC_REG_COMPUTE_PGM_RSRC1, resources, 2u) ||
        !agcGfx1013EmitComputeRegisters(
            cb, AGC_REG_COMPUTE_PGM_RSRC3, &resource3, 1u) ||
        (state->num_user_data != 0u &&
            !agcGfx1013EmitComputeRegisters(cb,
                AGC_REG_COMPUTE_USER_DATA_0, state->user_data,
                state->num_user_data)))
        return AGC_ERROR_INTERNAL;
    dispatch = agcCbAllocDwords(cb, 5u);
    if (!dispatch)
        return AGC_ERROR_INTERNAL;
    dispatch[0] = agcPm4Header3(AGC_PM4_OP_DISPATCH_DIRECT, 5u) | 1u;
    dispatch[1] = state->group_count_x;
    dispatch[2] = state->group_count_y;
    dispatch[3] = state->group_count_z;
    dispatch[4] = (state->modifier & 0xa038u) | 0x41u;
    return AGC_OK;
}

static bool agcGfx1013IsResourcePlaceholder(uint32_t value)
{
    return value == OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER ||
        (value & 0xffffff00u) == OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u);
}

static int32_t agcGfx1013FindResourceTable(
    const AgcGfx1013ResourceTableBinding *tables, uint32_t table_count,
    uint32_t placeholder, uint64_t *address)
{
    uint32_t i;
    bool found = false;

    for (i = 0; i < table_count; ++i) {
        if (tables[i].placeholder != placeholder)
            continue;
        if (found)
            return AGC_ERROR_INVALID_ARGUMENT;
        found = true;
        *address = tables[i].address;
    }
    return found ? AGC_OK : AGC_ERROR_RESOURCE_NOT_BOUND;
}

int32_t PS5_SYSV_ABI agcGfx1013ValidateResourceTables(
    const AgcGfx1013ShaderBinding *shader,
    const AgcGfx1013ResourceTableBinding *tables, uint32_t table_count,
    uint32_t *binding_count)
{
    uint32_t resource_count = 0u;
    uint32_t i;

    if (!shader || !shader->record || !shader->sh_registers ||
        !tables || table_count == 0u || table_count > 256u ||
        shader->num_sh_registers == 0u ||
        shader->num_sh_registers != shader->record->num_sh_registers)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (shader->record->magic != AGC_SHADER_RECORD_MAGIC ||
        shader->record->version != AGC_SHADER_RECORD_VERSION_GEN5)
        return AGC_ERROR_SHADER_INVALID;

    for (i = 0; i < table_count; ++i) {
        uint32_t j;
        if (!agcGfx1013IsResourcePlaceholder(tables[i].placeholder))
            return AGC_ERROR_INVALID_ARGUMENT;
        if (tables[i].address == 0u || (tables[i].address & 0xfu) != 0u ||
            (uint32_t)(tables[i].address >> 32u) !=
                AGC_GFX1013_ADDRESS32_HIGH)
            return AGC_ERROR_INVALID_ALIGNMENT;
        for (j = i + 1u; j < table_count; ++j) {
            if (tables[i].placeholder == tables[j].placeholder)
                return AGC_ERROR_INVALID_ARGUMENT;
        }
    }

    for (i = 0; i < shader->num_sh_registers; ++i) {
        uint64_t address;
        int32_t result;
        if (!agcGfx1013IsResourcePlaceholder(
                shader->sh_registers[i].value))
            continue;
        result = agcGfx1013FindResourceTable(tables, table_count,
            shader->sh_registers[i].value, &address);
        if (result != AGC_OK)
            return result;
        resource_count++;
    }
    if (binding_count)
        *binding_count = resource_count;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013BindResourceTables(
    SceAgcCb *cb, const AgcGfx1013ShaderBinding *shader,
    const AgcGfx1013ResourceTableBinding *tables, uint32_t table_count)
{
    uint32_t resource_count;
    uint32_t i;
    bool compute;
    int32_t result;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGfx1013ValidateResourceTables(
        shader, tables, table_count, &resource_count);
    if (result != AGC_OK)
        return result;
    if (resource_count == 0u)
        return AGC_OK;
    if (agcCbRemainingDwords(cb) < resource_count * 3u)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    compute = shader->record->shader_type == kAgcShaderTypeCs;

    for (i = 0; i < shader->num_sh_registers; ++i) {
        uint64_t address;
        uint32_t value;
        if (!agcGfx1013IsResourcePlaceholder(
                shader->sh_registers[i].value))
            continue;
        if (agcGfx1013FindResourceTable(tables, table_count,
                shader->sh_registers[i].value, &address) != AGC_OK)
            return AGC_ERROR_INTERNAL;
        value = (uint32_t)address;
        if (compute) {
            if (!agcGfx1013EmitComputeRegisters(cb,
                    shader->sh_registers[i].offset, &value, 1u))
                return AGC_ERROR_INTERNAL;
        } else {
            AgcRegisterValue reg = {
                shader->sh_registers[i].offset,
                value,
            };
            if (!sceAgcCbSetShRegistersDirect(cb, &reg, 1u))
                return AGC_ERROR_INTERNAL;
        }
    }
    return AGC_OK;
}

static int32_t agcGfx1013ValidateTessellationState(
    const AgcGfx1013TessellationState *state)
{
    if (!state || state->offchip_ring_address == 0u ||
        state->factor_ring_address == 0u ||
        state->offchip_ring_size == 0u || state->factor_ring_size == 0u ||
        (state->offchip_ring_size & 3u) != 0u ||
        (state->factor_ring_size & 3u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((state->offchip_ring_address & 0xffu) != 0u ||
        (state->factor_ring_address & 0xffu) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if ((state->offchip_ring_address >> 48u) != 0u ||
        (state->factor_ring_address >> 48u) != 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    return AGC_OK;
}

static void agcGfx1013BuildRawRingDescriptor(
    uint32_t words[4], uint64_t address, uint32_t size)
{
    words[0] = (uint32_t)address;
    words[1] = (uint32_t)(address >> 32u);
    words[2] = size;
    words[3] = AGC_GFX1013_RAW_R32_DESCRIPTOR_WORD3;
}

int32_t PS5_SYSV_ABI agcGfx1013BuildTessellationRingTable(
    AgcGfx1013TessellationRingTable *table,
    const AgcGfx1013TessellationState *state)
{
    AgcGfx1013TessellationRingTable encoded;
    int32_t result;

    if (!table)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGfx1013ValidateTessellationState(state);
    if (result != AGC_OK)
        return result;
    memset(&encoded, 0, sizeof(encoded));
    agcGfx1013BuildRawRingDescriptor(
        &encoded.words[AGC_GFX1013_TESS_FACTOR_RING_SLOT *
            AGC_GFX1013_TESS_RING_DESCRIPTOR_DWORDS],
        state->factor_ring_address, state->factor_ring_size);
    agcGfx1013BuildRawRingDescriptor(
        &encoded.words[AGC_GFX1013_TESS_OFFCHIP_RING_SLOT *
            AGC_GFX1013_TESS_RING_DESCRIPTOR_DWORDS],
        state->offchip_ring_address, state->offchip_ring_size);
    *table = encoded;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetTessellationRings(
    SceAgcCb *cb, const AgcGfx1013TessellationState *state)
{
    AgcRegisterValue registers[4];
    uint32_t i;
    int32_t result;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGfx1013ValidateTessellationState(state);
    if (result != AGC_OK)
        return result;
    if (agcCbRemainingDwords(cb) < 12u)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    registers[0] = (AgcRegisterValue){
        AGC_REG_VGT_TF_RING_SIZE, state->factor_ring_size / 4u,
    };
    registers[1] = (AgcRegisterValue){
        AGC_REG_VGT_HS_OFFCHIP_PARAM, state->offchip_param,
    };
    registers[2] = (AgcRegisterValue){
        AGC_REG_VGT_TF_MEMORY_BASE,
        (uint32_t)(state->factor_ring_address >> 8u),
    };
    registers[3] = (AgcRegisterValue){
        AGC_REG_VGT_TF_MEMORY_BASE_HI,
        (uint32_t)(state->factor_ring_address >> 40u),
    };
    for (i = 0; i < 4u; ++i) {
        if (!sceAgcCbSetUcRegistersDirect(cb, &registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetTessellationContext(
    SceAgcCb *cb, const AgcGfx1013TessellationState *state)
{
    AgcRegisterValue registers[5];
    uint32_t i;
    int32_t result;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGfx1013ValidateTessellationState(state);
    if (result != AGC_OK)
        return result;
    if (agcCbRemainingDwords(cb) < 15u)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    registers[0] = (AgcRegisterValue){
        AGC_REG_VGT_HOS_MAX_TESS_LEVEL, state->max_tess_level,
    };
    registers[1] = (AgcRegisterValue){
        AGC_REG_VGT_HOS_MIN_TESS_LEVEL, state->min_tess_level,
    };
    registers[2] = (AgcRegisterValue){
        AGC_REG_VGT_ESGS_RING_ITEMSIZE, state->esgs_ring_itemsize,
    };
    registers[3] = (AgcRegisterValue){
        AGC_REG_VGT_TESS_DISTRIBUTION, state->distribution,
    };
    registers[4] = (AgcRegisterValue){
        AGC_REG_VGT_TF_PARAM, state->tf_param,
    };
    for (i = 0; i < 5u; ++i) {
        if (!sceAgcCbSetCxRegistersDirect(cb, &registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
}
