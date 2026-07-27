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
static int32_t agcGfx1013ValidateFrameState(
    const AgcGfx1013FrameState *state);

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
    if (state->frame) {
        error = agcGfx1013ValidateFrameState(state->frame);
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
    required_dwords = 29u + input_count * 3u +
        (state->shaders.primitive.num_sh_registers +
         state->shaders.primitive.num_cx_registers +
         state->shaders.pixel.num_sh_registers +
         state->shaders.pixel.num_cx_registers +
         primitive_resource_count + pixel_resource_count +
         state->num_post_bind_sh_registers +
         state->num_post_bind_cx_registers +
         state->num_post_bind_uc_registers) * 3u;
    if (state->frame)
        required_dwords += AGC_GFX1013_FRAME_POST_BIND_DWORDS;
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
    if (state->frame &&
        agcGfx1013ApplyFramePostBind(cb, state->frame) != AGC_OK)
        return AGC_ERROR_INTERNAL;
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
    if (state->frame) {
        error = agcGfx1013ValidateFrameState(state->frame);
        if (error != AGC_OK)
            return error;
    }
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
    if (state->frame)
        required_dwords += AGC_GFX1013_FRAME_POST_BIND_DWORDS;
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
    if (state->frame &&
        agcGfx1013ApplyFramePostBind(cb, state->frame) != AGC_OK)
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

static bool agcGfx1013UsageWrites(AgcGfx1013ResourceUsage usage)
{
    return usage == AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET ||
        usage == AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE ||
        usage == AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION ||
        usage == AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE;
}

static bool agcGfx1013UsageWritesDepth(AgcGfx1013ResourceUsage usage)
{
    return usage == AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE;
}

static int32_t agcGfx1013ValidateTransition(
    const AgcGfx1013ResourceTransition *transition, bool *release,
    bool *acquire)
{
    if (!transition || !release || !acquire)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (transition->before >= AGC_GFX1013_RESOURCE_USAGE_COUNT ||
        transition->after == AGC_GFX1013_RESOURCE_USAGE_UNDEFINED ||
        transition->after >= AGC_GFX1013_RESOURCE_USAGE_COUNT)
        return AGC_ERROR_INVALID_ARGUMENT;

    *release = transition->before != transition->after &&
        agcGfx1013UsageWrites(transition->before);
    *acquire = transition->before != transition->after &&
        transition->before != AGC_GFX1013_RESOURCE_USAGE_UNDEFINED &&
        transition->after != AGC_GFX1013_RESOURCE_USAGE_PRESENT &&
        transition->after != AGC_GFX1013_RESOURCE_USAGE_HOST_READ &&
        (*release ||
         transition->before == AGC_GFX1013_RESOURCE_USAGE_PRESENT);

    if (transition->completion_address == 0u) {
        if (transition->completion_value != 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
    } else if (!*release) {
        return AGC_ERROR_INVALID_ARGUMENT;
    } else if ((transition->completion_address & 3u) != 0u) {
        return AGC_ERROR_INVALID_ALIGNMENT;
    } else if ((transition->completion_address >> 48u) != 0u) {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013GetResourceTransitionDwords(
    const AgcGfx1013ResourceTransition *transition, uint32_t *dword_count)
{
    bool release;
    bool acquire;
    int32_t error;

    if (!dword_count)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013ValidateTransition(transition, &release, &acquire);
    if (error != AGC_OK)
        return error;
    *dword_count =
        (release && agcGfx1013UsageWritesDepth(transition->before) ?
            AGC_GFX1013_DB_META_FLUSH_DWORDS : 0u) +
        (release ? AGC_GFX1013_EOP_FENCE_DWORDS : 0u) +
        (acquire ? AGC_GFX1013_ACQUIRE_MEM_DWORDS : 0u);
    return AGC_OK;
}

static bool agcGfx1013EmitAcquireAll(SceAgcCb *cb)
{
    uint32_t *cmd = agcCbAllocDwords(cb, AGC_GFX1013_ACQUIRE_MEM_DWORDS);

    if (!cmd)
        return false;
    cmd[0] = agcPm4Header3(
        AGC_PM4_OP_ACQUIRE_MEM, AGC_GFX1013_ACQUIRE_MEM_DWORDS);
    cmd[1] = 0u;
    cmd[2] = 0xffffffffu;
    cmd[3] = 0x00ffffffu;
    cmd[4] = 0u;
    cmd[5] = 0u;
    cmd[6] = AGC_GFX1013_ACQUIRE_POLL_INTERVAL;
    cmd[7] = AGC_GFX1013_ACQUIRE_GCR_ALL;
    return true;
}

int32_t PS5_SYSV_ABI agcGfx1013TransitionResource(
    SceAgcCb *cb, const AgcGfx1013ResourceTransition *transition)
{
    uint32_t dword_count;
    bool release;
    bool acquire;
    int32_t error;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013ValidateTransition(transition, &release, &acquire);
    if (error != AGC_OK)
        return error;
    dword_count =
        (release && agcGfx1013UsageWritesDepth(transition->before) ?
            AGC_GFX1013_DB_META_FLUSH_DWORDS : 0u) +
        (release ? AGC_GFX1013_EOP_FENCE_DWORDS : 0u) +
        (acquire ? AGC_GFX1013_ACQUIRE_MEM_DWORDS : 0u);
    if (agcCbRemainingDwords(cb) < dword_count)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    if (release && agcGfx1013UsageWritesDepth(transition->before) &&
        !sceAgcDcbEventWrite(
            cb, AGC_GFX1013_DB_META_FLUSH_EVENT, 0u))
        return AGC_ERROR_INTERNAL;
    if (release &&
        (!sceAgcCbReleaseMem(
            cb, agcGfx1013UsageWritesDepth(transition->before) ?
                AGC_GFX1013_DB_DATA_FLUSH_EVENT :
                AGC_GFX1013_EOP_CACHE_FLUSH_EVENT,
            AGC_GFX1013_EOP_GCR_CONTROL, 0u,
            AGC_GFX1013_EOP_CACHE_POLICY_LRU,
            transition->completion_address,
            transition->completion_address != 0u ? 1u : 0u,
            transition->completion_value, 0u, 0u, 0u, 0u) ||
         !sceAgcCbNop(cb, 2u)))
        return AGC_ERROR_INTERNAL;
    if (acquire && !agcGfx1013EmitAcquireAll(cb))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SignalEopFence(
    SceAgcCb *cb, const AgcGfx1013EopFenceState *state)
{
    AgcGfx1013ResourceTransition transition;

    if (!state || state->address == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    transition.before = AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE;
    transition.after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ;
    transition.completion_address = state->address;
    transition.completion_value = state->value;
    return agcGfx1013TransitionResource(cb, &transition);
}

static uint32_t agcGfx1013FloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

typedef struct AgcGfx1013ColorTargetFormatEntry {
    AgcGfx1013ColorTargetFormat format;
    AgcGfx1013ColorTargetFormatInfo info;
} AgcGfx1013ColorTargetFormatEntry;

static const AgcGfx1013ColorTargetFormatEntry
    kAgcGfx1013ColorTargetFormats[] = {
        {AGC_GFX1013_RT_FORMAT_R8_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_8, AGC_GFX1013_SURFACE_NUMBER_UNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 1u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RG8_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_8_8, AGC_GFX1013_SURFACE_NUMBER_UNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 2u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGBA8_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
          AGC_GFX1013_SURFACE_NUMBER_UNORM, AGC_GFX1013_SURFACE_SWAP_STD,
          4u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_BGRA8_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
          AGC_GFX1013_SURFACE_NUMBER_UNORM, AGC_GFX1013_SURFACE_SWAP_ALT,
          4u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_10_10_10_2,
          AGC_GFX1013_SURFACE_NUMBER_UNORM, AGC_GFX1013_SURFACE_SWAP_STD,
          4u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_R16_FLOAT,
         {AGC_GFX1013_COLOR_FORMAT_16, AGC_GFX1013_SURFACE_NUMBER_FLOAT,
          AGC_GFX1013_SURFACE_SWAP_STD, 2u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RG16_FLOAT,
         {AGC_GFX1013_COLOR_FORMAT_16_16,
          AGC_GFX1013_SURFACE_NUMBER_FLOAT, AGC_GFX1013_SURFACE_SWAP_STD,
          4u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT,
         {AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
          AGC_GFX1013_SURFACE_NUMBER_FLOAT, AGC_GFX1013_SURFACE_SWAP_STD,
          8u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_R32_FLOAT,
         {AGC_GFX1013_COLOR_FORMAT_32, AGC_GFX1013_SURFACE_NUMBER_FLOAT,
          AGC_GFX1013_SURFACE_SWAP_STD, 4u,
          AGC_GFX1013_SPI_EXPORT_32_R}},
        {AGC_GFX1013_RT_FORMAT_RG32_FLOAT,
         {AGC_GFX1013_COLOR_FORMAT_32_32,
          AGC_GFX1013_SURFACE_NUMBER_FLOAT, AGC_GFX1013_SURFACE_SWAP_STD,
          8u, AGC_GFX1013_SPI_EXPORT_32_GR}},
        {AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT,
         {AGC_GFX1013_COLOR_FORMAT_32_32_32_32,
          AGC_GFX1013_SURFACE_NUMBER_FLOAT, AGC_GFX1013_SURFACE_SWAP_STD,
          16u, AGC_GFX1013_SPI_EXPORT_32_ABGR}},
};

int32_t PS5_SYSV_ABI agcGfx1013GetColorTargetFormatInfo(
    AgcGfx1013ColorTargetFormat format,
    AgcGfx1013ColorTargetFormatInfo *info)
{
    uint32_t i;

    if (!info)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (i = 0u; i < (uint32_t)AGC_GFX1013_RT_FORMAT_COUNT; ++i) {
        if (kAgcGfx1013ColorTargetFormats[i].format == format) {
            *info = kAgcGfx1013ColorTargetFormats[i].info;
            return AGC_OK;
        }
    }
    return AGC_ERROR_NOT_SUPPORTED;
}

int32_t PS5_SYSV_ABI agcGfx1013InitColorTarget(
    AgcGfx1013ColorTargetState *state, uint64_t address, uint32_t width,
    uint32_t height, AgcGfx1013ColorTargetFormat format)
{
    AgcGfx1013ColorTargetFormatInfo info;
    int32_t error;

    if (!state)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013GetColorTargetFormatInfo(format, &info);
    if (error != AGC_OK)
        return error;
    state->address = address;
    state->width = width;
    state->height = height;
    state->color_format = info.color_format;
    state->number_type = info.number_type;
    state->component_swap = info.component_swap;
    state->sample_count = 1u;
    state->fragment_count = 1u;
    state->swizzle_mode = 0u;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013GetColorSurfaceLayout(
    const AgcGfx1013ColorSurfaceLayoutInput *input,
    AgcGfx1013ColorSurfaceLayout *layout)
{
    AgcGfx1013ColorTargetFormatInfo info;
    AgcGfx1013ColorSurfaceLayout result = {0};
    uint32_t element_log2;
    uint32_t sample_log2;
    uint32_t element_count_log2;
    uint32_t width_log2;
    uint64_t slice_size;

    if (!input || !layout || input->width == 0u || input->height == 0u ||
        input->width > 0x4000u || input->height > 0x4000u ||
        input->layer_count == 0u || input->layer_count > 0x2000u ||
        input->mip_level_count != 1u || input->sample_count != 4u ||
        input->swizzle_mode != AGC_GFX1013_SWIZZLE_64KB_R_X)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcGfx1013GetColorTargetFormatInfo(input->format, &info) != AGC_OK)
        return AGC_ERROR_NOT_SUPPORTED;
    switch (info.bytes_per_pixel) {
    case 1u: element_log2 = 0u; break;
    case 2u: element_log2 = 1u; break;
    case 4u: element_log2 = 2u; break;
    case 8u: element_log2 = 3u; break;
    case 16u: element_log2 = 4u; break;
    default: return AGC_ERROR_NOT_SUPPORTED;
    }
    sample_log2 = 2u;
    element_count_log2 = 16u - element_log2 - sample_log2;
    width_log2 = (element_count_log2 + 1u) / 2u;
    result.block_width = 1u << width_log2;
    result.block_height = 1u << (element_count_log2 - width_log2);
    result.pitch = (input->width + result.block_width - 1u) &
        ~(result.block_width - 1u);
    result.padded_height = (input->height + result.block_height - 1u) &
        ~(result.block_height - 1u);
    result.alignment = AGC_GFX1013_64KB_SURFACE_ALIGNMENT;
    result.first_mip_in_tail = 1u;
    slice_size = (uint64_t)result.pitch * result.padded_height *
        info.bytes_per_pixel * input->sample_count;
    if (slice_size == 0u || input->layer_count > UINT64_MAX / slice_size)
        return AGC_ERROR_INVALID_ARGUMENT;
    result.slice_size = slice_size;
    result.allocation_size = slice_size * input->layer_count;
    *layout = result;
    return AGC_OK;
}

static bool agcGfx1013FindColorTargetFormat(
    const AgcGfx1013ColorTargetState *state,
    AgcGfx1013ColorTargetFormatInfo *info)
{
    uint32_t i;

    for (i = 0u; i < (uint32_t)AGC_GFX1013_RT_FORMAT_COUNT; ++i) {
        const AgcGfx1013ColorTargetFormatInfo *candidate =
            &kAgcGfx1013ColorTargetFormats[i].info;
        if (candidate->color_format == state->color_format &&
            candidate->number_type == state->number_type &&
            candidate->component_swap == state->component_swap) {
            *info = *candidate;
            return true;
        }
    }
    return false;
}

int32_t PS5_SYSV_ABI agcGfx1013SetColorTarget(
    SceAgcCb *cb, const AgcGfx1013ColorTargetState *state)
{
    uint32_t *cmd;
    uint32_t regs[14] = {0};
    uint32_t tiles_per_row;
    uint64_t tile_count;
    AgcGfx1013ColorTargetFormatInfo format_info;
    uint32_t sample_count;
    uint32_t fragment_count;
    uint32_t sample_log2;
    uint32_t fragment_log2;
    uint32_t padded_height;

    if (!cb || !state || state->address == 0u || state->width == 0u ||
        state->height == 0u || state->width > 0x4000u ||
        state->height > 0x4000u || (state->width & 7u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((state->address & 0xffu) != 0u || (state->address >> 48) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;

    if (!agcGfx1013FindColorTargetFormat(state, &format_info))
        return AGC_ERROR_NOT_SUPPORTED;
    sample_count = state->sample_count == 0u ? 1u : state->sample_count;
    fragment_count = state->fragment_count == 0u ? 1u : state->fragment_count;
    if (sample_count == 1u && fragment_count == 1u &&
        state->swizzle_mode == 0u) {
        sample_log2 = 0u;
        fragment_log2 = 0u;
        padded_height = state->height;
    } else if (sample_count == 4u && fragment_count == 4u &&
               state->swizzle_mode == AGC_GFX1013_SWIZZLE_64KB_R_X) {
        AgcGfx1013ColorSurfaceLayoutInput input = {
            state->width, state->height, 1u, 1u, 4u,
            AGC_GFX1013_RT_FORMAT_COUNT,
            AGC_GFX1013_SWIZZLE_64KB_R_X,
        };
        AgcGfx1013ColorSurfaceLayout layout;
        uint32_t i;
        for (i = 0u; i < (uint32_t)AGC_GFX1013_RT_FORMAT_COUNT; ++i) {
            const AgcGfx1013ColorTargetFormatInfo *candidate =
                &kAgcGfx1013ColorTargetFormats[i].info;
            if (candidate->color_format == state->color_format &&
                candidate->number_type == state->number_type &&
                candidate->component_swap == state->component_swap) {
                input.format = kAgcGfx1013ColorTargetFormats[i].format;
                break;
            }
        }
        if (input.format == AGC_GFX1013_RT_FORMAT_COUNT ||
            agcGfx1013GetColorSurfaceLayout(&input, &layout) != AGC_OK)
            return AGC_ERROR_NOT_SUPPORTED;
        sample_log2 = 2u;
        fragment_log2 = 2u;
        padded_height = layout.padded_height;
    } else {
        return AGC_ERROR_NOT_SUPPORTED;
    }
    if (((uint64_t)state->width * format_info.bytes_per_pixel & 0xffu) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;

    tiles_per_row = state->width / 8u;
    tile_count = (uint64_t)tiles_per_row * padded_height * sample_count;
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
    regs[5] =
        (sample_log2 << AGC_REG_CB_COLOR0_ATTRIB_NUM_SAMPLES_SHIFT) |
        (fragment_log2 << AGC_REG_CB_COLOR0_ATTRIB_NUM_FRAGMENTS_SHIFT);

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
            cb, AGC_REG_CB_COLOR0_ATTRIB3,
            0x09000001u | (state->swizzle_mode <<
                AGC_REG_CB_COLOR0_ATTRIB3_COLOR_SW_MODE_SHIFT)) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR_CONTROL, 0x00cc0010u))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetSampleState(
    SceAgcCb *cb, const AgcGfx1013SampleState *state)
{
    uint32_t sample_log2;
    uint32_t ps_iter_log2;
    uint32_t max_distance;
    uint32_t sample_locations;
    uint64_t centroid_priority;
    uint32_t mask;

    if (!cb || !state || (state->sample_count != 1u &&
        state->sample_count != 4u) ||
        (state->pixel_shader_sample_count != 1u &&
         state->pixel_shader_sample_count != state->sample_count) ||
        (state->sample_mask & ~((1u << state->sample_count) - 1u)) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_SAMPLE_STATE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    sample_log2 = state->sample_count == 4u ? 2u : 0u;
    ps_iter_log2 = state->pixel_shader_sample_count == 4u ? 2u : 0u;
    max_distance = state->sample_count == 4u ? 6u : 0u;
    /* Standard DX 4x positions: (-2,-6), (2,6), (-6,2), (6,-2). */
    sample_locations = state->sample_count == 4u ? 0xE62A62AEu : 0u;
    centroid_priority = state->sample_count == 4u ?
        UINT64_C(0x3210321032103210) : 0u;
    mask = (state->sample_mask & 0xffffu) |
        ((state->sample_mask & 0xffffu) << 16u);

    if (!agcGfx1013EmitCx(cb, AGC_REG_PA_SC_AA_CONFIG,
            (sample_log2 << AGC_REG_PA_SC_AA_CONFIG_MSAA_NUM_SAMPLES_SHIFT) |
            (max_distance << AGC_REG_PA_SC_AA_CONFIG_MAX_SAMPLE_DIST_SHIFT) |
            (sample_log2 << AGC_REG_PA_SC_AA_CONFIG_MSAA_EXPOSED_SAMPLES_SHIFT) |
            (state->sample_count == 4u ? (1u <<
                AGC_REG_PA_SC_AA_CONFIG_COVERED_CENTROID_IS_CENTER_SHIFT) : 0u)) ||
        !agcGfx1013EmitCx(cb, AGC_REG_DB_EQAA,
            (sample_log2 << AGC_REG_DB_EQAA_MAX_ANCHOR_SAMPLES_SHIFT) |
            (ps_iter_log2 << AGC_REG_DB_EQAA_PS_ITER_SAMPLES_SHIFT) |
            (sample_log2 << AGC_REG_DB_EQAA_MASK_EXPORT_NUM_SAMPLES_SHIFT) |
            (sample_log2 << AGC_REG_DB_EQAA_ALPHA_TO_MASK_NUM_SAMPLES_SHIFT)) ||
        !agcGfx1013EmitCx(cb, AGC_REG_PA_SC_MODE_CNTL_0,
            (state->sample_count == 4u ? 1u : 0u) |
            (1u << AGC_REG_PA_SC_MODE_CNTL_0_VPORT_SCISSOR_ENABLE_SHIFT)))
        return AGC_ERROR_INTERNAL;
    {
        uint32_t *cmd = agcCbAllocDwords(cb, 4u);
        if (!cmd) return AGC_ERROR_INTERNAL;
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
        cmd[1] = AGC_REG_PA_SC_CENTROID_PRIORITY_0;
        cmd[2] = (uint32_t)centroid_priority;
        cmd[3] = (uint32_t)(centroid_priority >> 32u);
    }
    if (!agcGfx1013EmitCx(cb, AGC_REG_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0,
            sample_locations) ||
        !agcGfx1013EmitCx(cb, AGC_REG_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0,
            sample_locations) ||
        !agcGfx1013EmitCx(cb, AGC_REG_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0,
            sample_locations) ||
        !agcGfx1013EmitCx(cb, AGC_REG_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0,
            sample_locations))
        return AGC_ERROR_INTERNAL;
    {
        uint32_t *cmd = agcCbAllocDwords(cb, 4u);
        if (!cmd) return AGC_ERROR_INTERNAL;
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
        cmd[1] = AGC_REG_PA_SC_AA_MASK_X0Y0_X1Y0;
        cmd[2] = mask;
        cmd[3] = mask;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013ResolveColor4x(
    SceAgcCb *cb, const AgcGfx1013ColorResolveState *state)
{
    AgcGfx1013ResourceTransition transition = {
        AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET,
        AGC_GFX1013_RESOURCE_USAGE_SHADER_READ, 0u, 0u,
    };
    AgcGfx1013SampleState samples = {1u, 1u, 1u};
    AgcGfx1013GraphicsDefaultStats stats;
    SceAgcCb probe;
    int32_t error;

    if (!cb || !state || !state->source || !state->draw ||
        !state->draw->frame || state->source->sample_count != 4u ||
        state->source->fragment_count != 4u ||
        state->source->swizzle_mode != AGC_GFX1013_SWIZZLE_64KB_R_X ||
        (state->draw->frame->color_target.sample_count != 0u &&
         state->draw->frame->color_target.sample_count != 1u))
        return AGC_ERROR_INVALID_ARGUMENT;
    probe = *cb;
    error = agcGfx1013TransitionResource(&probe, &transition);
    if (error == AGC_OK)
        error = agcGfx1013BuildFramePrologue(
            &probe, state->draw->frame, &stats);
    if (error == AGC_OK)
        error = agcGfx1013SetSampleState(&probe, &samples);
    if (error == AGC_OK)
        error = agcGfx1013DrawBaselineIndexAuto(&probe, state->draw);
    if (error != AGC_OK)
        return error;
    error = agcGfx1013TransitionResource(cb, &transition);
    if (error == AGC_OK)
        error = agcGfx1013BuildFramePrologue(
            cb, state->draw->frame, &stats);
    if (error == AGC_OK)
        error = agcGfx1013SetSampleState(cb, &samples);
    if (error == AGC_OK)
        error = agcGfx1013DrawBaselineIndexAuto(cb, state->draw);
    return error;
}

static bool agcGfx1013DepthAddressValid(uint64_t address)
{
    return address != 0u && (address & 0xffu) == 0u &&
           (address >> 48) == 0u;
}

static bool agcGfx1013DepthFormatInfo(
    AgcGfx1013DepthSurfaceFormat format, uint32_t *z_format,
    uint32_t *stencil_format)
{
    switch (format) {
    case AGC_GFX1013_DEPTH_FORMAT_D16_UNORM:
        *z_format = 1u;
        *stencil_format = 0u;
        return true;
    case AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT:
        *z_format = 3u;
        *stencil_format = 0u;
        return true;
    case AGC_GFX1013_DEPTH_FORMAT_S8_UINT:
        *z_format = 0u;
        *stencil_format = 1u;
        return true;
    case AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT:
        *z_format = 1u;
        *stencil_format = 1u;
        return true;
    case AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT:
        *z_format = 3u;
        *stencil_format = 1u;
        return true;
    default:
        return false;
    }
}

static bool agcGfx1013DepthSampleLog2(
    uint32_t sample_count, uint32_t *sample_log2)
{
    switch (sample_count) {
    case 1u: *sample_log2 = 0u; return true;
    case 2u: *sample_log2 = 1u; return true;
    case 4u: *sample_log2 = 2u; return true;
    case 8u: *sample_log2 = 3u; return true;
    default: return false;
    }
}

static bool agcGfx1013MulU64(uint64_t a, uint64_t b, uint64_t *result)
{
    if (a != 0u && b > UINT64_MAX / a)
        return false;
    *result = a * b;
    return true;
}

static uint32_t agcGfx1013AlignPow2(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static bool agcGfx1013CalculateDepthPlaneLayout(
    const AgcGfx1013DepthSurfaceLayoutInput *input, uint32_t bytes_per_element,
    AgcGfx1013DepthPlaneLayout *layout)
{
    uint32_t element_log2 = bytes_per_element == 1u ? 0u :
                            bytes_per_element == 2u ? 1u : 2u;
    uint32_t sample_log2;
    uint32_t element_count_log2;
    uint32_t width_log2;
    uint32_t mip_width;
    uint32_t mip_height;
    uint32_t tail_width;
    uint32_t tail_height;
    uint64_t slice_size = 0u;
    uint64_t allocation_size;

    if (!agcGfx1013DepthSampleLog2(input->sample_count, &sample_log2))
        return false;
    element_count_log2 = 16u - element_log2 - sample_log2;
    width_log2 = (element_count_log2 +
        (((sample_log2 & 1u) == 0u) ? 1u : 0u)) / 2u;
    layout->block_width = 1u << width_log2;
    layout->block_height = 1u << (element_count_log2 - width_log2);
    layout->pitch = agcGfx1013AlignPow2(input->width, layout->block_width);
    layout->padded_height = agcGfx1013AlignPow2(
        input->height, layout->block_height);
    layout->alignment = AGC_GFX1013_64KB_SURFACE_ALIGNMENT;
    layout->first_mip_in_tail = input->mip_level_count;

    if (input->mip_level_count == 1u) {
        if (!agcGfx1013MulU64(layout->pitch, layout->padded_height,
                &slice_size) ||
            !agcGfx1013MulU64(slice_size, bytes_per_element, &slice_size) ||
            !agcGfx1013MulU64(slice_size, input->sample_count, &slice_size))
            return false;
    } else {
        uint32_t mip;

        tail_width = layout->block_width >> 1u;
        tail_height = layout->block_height;
        /* gfx10 dsMipmapHtileFix adjusts Z-order tails for 8/16-bit planes. */
        if (bytes_per_element == 1u) {
            tail_width >>= 1u;
            tail_height >>= 1u;
        } else if (bytes_per_element == 2u) {
            tail_width >>= 1u;
        }
        mip_width = input->width;
        mip_height = input->height;
        for (mip = 0u; mip < input->mip_level_count; ++mip) {
            uint32_t remaining = input->mip_level_count - mip;
            uint64_t mip_size;

            if (mip_width <= tail_width && mip_height <= tail_height &&
                remaining <= 12u) {
                layout->first_mip_in_tail = mip;
                if (slice_size > UINT64_MAX -
                        AGC_GFX1013_64KB_SURFACE_ALIGNMENT)
                    return false;
                slice_size += AGC_GFX1013_64KB_SURFACE_ALIGNMENT;
                break;
            }
            if (!agcGfx1013MulU64(
                    agcGfx1013AlignPow2(mip_width, layout->block_width),
                    agcGfx1013AlignPow2(mip_height, layout->block_height),
                    &mip_size) ||
                !agcGfx1013MulU64(mip_size, bytes_per_element, &mip_size) ||
                slice_size > UINT64_MAX - mip_size)
                return false;
            slice_size += mip_size;
            mip_width = mip_width > 1u ? mip_width >> 1u : 1u;
            mip_height = mip_height > 1u ? mip_height >> 1u : 1u;
        }
    }
    if (!agcGfx1013MulU64(slice_size, input->layer_count,
            &allocation_size))
        return false;
    layout->slice_size = slice_size;
    layout->allocation_size = allocation_size;
    return true;
}

int32_t PS5_SYSV_ABI agcGfx1013GetDepthSurfaceLayout(
    const AgcGfx1013DepthSurfaceLayoutInput *input,
    AgcGfx1013DepthSurfaceLayout *layout)
{
    AgcGfx1013DepthSurfaceLayout result = {0};
    uint32_t sample_log2;
    uint32_t max_dimension;
    uint32_t max_mips = 1u;
    uint32_t depth_bytes = 0u;
    uint32_t stencil_bytes = 0u;

    if (!input || !layout || input->width == 0u || input->height == 0u ||
        input->width > 0x4000u || input->height > 0x4000u ||
        input->layer_count == 0u || input->layer_count > 0x2000u ||
        input->mip_level_count == 0u || input->mip_level_count > 16u ||
        (input->sample_count > 1u && input->mip_level_count > 1u) ||
        !agcGfx1013DepthSampleLog2(input->sample_count, &sample_log2))
        return AGC_ERROR_INVALID_ARGUMENT;

    max_dimension = input->width > input->height ? input->width : input->height;
    while (max_dimension > 1u) {
        max_dimension >>= 1u;
        ++max_mips;
    }
    if (input->mip_level_count > max_mips)
        return AGC_ERROR_INVALID_ARGUMENT;
    switch (input->format) {
    case AGC_GFX1013_DEPTH_FORMAT_D16_UNORM: depth_bytes = 2u; break;
    case AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT: depth_bytes = 4u; break;
    case AGC_GFX1013_DEPTH_FORMAT_S8_UINT: stencil_bytes = 1u; break;
    case AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT:
        depth_bytes = 2u; stencil_bytes = 1u; break;
    case AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT:
        depth_bytes = 4u; stencil_bytes = 1u; break;
    default: return AGC_ERROR_NOT_SUPPORTED;
    }
    if ((depth_bytes != 0u &&
         input->depth_swizzle_mode != AGC_GFX1013_SWIZZLE_64KB_Z_X) ||
        (stencil_bytes != 0u &&
         input->stencil_swizzle_mode != AGC_GFX1013_SWIZZLE_64KB_Z_X))
        return AGC_ERROR_NOT_SUPPORTED;
    if ((depth_bytes != 0u && !agcGfx1013CalculateDepthPlaneLayout(
             input, depth_bytes, &result.depth)) ||
        (stencil_bytes != 0u && !agcGfx1013CalculateDepthPlaneLayout(
             input, stencil_bytes, &result.stencil)))
        return AGC_ERROR_INVALID_ARGUMENT;
    *layout = result;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013GetHtileLayout(
    const AgcGfx1013HtileLayoutInput *input,
    AgcGfx1013HtileLayout *layout)
{
    AgcGfx1013HtileLayout result = {0};
    uint32_t pipe_count;
    uint32_t pipe_log2 = 0u;
    uint32_t meta_pixels_log2;
    uint64_t slice_size = 0u;
    uint64_t allocation_size;

    if (!input || !layout || input->width == 0u || input->height == 0u ||
        input->width > 0x4000u || input->height > 0x4000u ||
        input->layer_count == 0u || input->layer_count > 0x2000u ||
        input->mip_level_count == 0u || input->mip_level_count > 16u ||
        input->first_mip_in_tail > input->mip_level_count ||
        input->pipe_count == 0u || input->pipe_count > 64u ||
        (input->pipe_count & (input->pipe_count - 1u)) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (input->swizzle_mode != AGC_GFX1013_SWIZZLE_64KB_Z_X)
        return AGC_ERROR_NOT_SUPPORTED;

    pipe_count = input->pipe_count;
    while (pipe_count > 1u) {
        pipe_count >>= 1u;
        ++pipe_log2;
    }

    /* gfx1013 is non-RB+ with 256-byte pipe interleave. AddrLib pads
     * pipe-aligned HTILE metadata blocks to 2 KiB per address pipe. */
    result.meta_block_size = 1u << (11u + pipe_log2);
    result.alignment = result.meta_block_size;
    meta_pixels_log2 = 15u + pipe_log2;
    result.meta_block_width = 1u <<
        ((meta_pixels_log2 >> 1u) + (meta_pixels_log2 & 1u));
    result.meta_block_height = 1u << (meta_pixels_log2 >> 1u);
    result.pitch = agcGfx1013AlignPow2(
        input->width, result.meta_block_width);
    result.padded_height = agcGfx1013AlignPow2(
        input->height, result.meta_block_height);

    if (input->mip_level_count == 1u) {
        uint64_t blocks;

        if (!agcGfx1013MulU64(
                result.pitch / result.meta_block_width,
                result.padded_height / result.meta_block_height, &blocks) ||
            !agcGfx1013MulU64(
                blocks, result.meta_block_size, &slice_size))
            return AGC_ERROR_INVALID_ARGUMENT;
    } else {
        uint32_t mip;

        if (input->first_mip_in_tail < input->mip_level_count)
            slice_size = result.meta_block_size;
        for (mip = 0u; mip < input->first_mip_in_tail; ++mip) {
            uint32_t mip_width = input->width >> mip;
            uint32_t mip_height = input->height >> mip;
            uint64_t blocks;
            uint64_t mip_size;

            if (mip_width == 0u)
                mip_width = 1u;
            if (mip_height == 0u)
                mip_height = 1u;
            if (!agcGfx1013MulU64(
                    agcGfx1013AlignPow2(mip_width,
                        result.meta_block_width) /
                        result.meta_block_width,
                    agcGfx1013AlignPow2(mip_height,
                        result.meta_block_height) /
                        result.meta_block_height,
                    &blocks) ||
                !agcGfx1013MulU64(
                    blocks, result.meta_block_size, &mip_size) ||
                slice_size > UINT64_MAX - mip_size)
                return AGC_ERROR_INVALID_ARGUMENT;
            slice_size += mip_size;
        }
    }

    if (!agcGfx1013MulU64(
            slice_size, input->layer_count, &allocation_size) ||
        slice_size / result.meta_block_size > UINT32_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    result.slice_size = slice_size;
    result.allocation_size = allocation_size;
    result.meta_blocks_per_slice =
        (uint32_t)(slice_size / result.meta_block_size);
    *layout = result;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetDepthSurface(
    SceAgcCb *cb, const AgcGfx1013DepthSurfaceState *state)
{
    uint32_t *cmd;
    uint32_t z_format;
    uint32_t stencil_format;
    uint32_t sample_log2;
    uint32_t depth_view;
    uint32_t depth_size;
    uint32_t z_info;
    uint32_t stencil_info;
    uint32_t bases[4];
    uint32_t bases_hi[5];

    if (!cb || !state || state->width == 0u || state->height == 0u ||
        state->width > 0x4000u || state->height > 0x4000u ||
        state->depth_swizzle_mode > 0x1fu ||
        state->stencil_swizzle_mode > 0x1fu ||
        state->mip_level_count == 0u || state->mip_level_count > 16u ||
        state->mip_level >= state->mip_level_count ||
        state->first_layer > state->last_layer ||
        state->last_layer > 0x1fffu || state->depth_read_only > 1u ||
        state->stencil_read_only > 1u || state->htile_enable > 1u ||
        state->allow_expclear > 1u || state->htile_stencil_disable > 1u ||
        (state->allow_expclear && !state->htile_enable) ||
        (state->htile_stencil_disable && !state->htile_enable) ||
        !agcGfx1013DepthSampleLog2(state->sample_count, &sample_log2))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcGfx1013DepthFormatInfo(
            state->format, &z_format, &stencil_format))
        return AGC_ERROR_NOT_SUPPORTED;

    if ((z_format != 0u &&
         (!state->depth_read_address || !state->depth_write_address)) ||
        (z_format == 0u &&
         (state->depth_read_address || state->depth_write_address ||
          state->depth_swizzle_mode || state->depth_read_only ||
          state->htile_enable)) ||
        (stencil_format != 0u &&
         (!state->stencil_read_address || !state->stencil_write_address)) ||
        (stencil_format == 0u &&
         (state->stencil_read_address || state->stencil_write_address ||
          state->stencil_swizzle_mode || state->stencil_read_only ||
          state->htile_stencil_disable)) ||
        (state->htile_enable && !state->htile_address) ||
        (!state->htile_enable && state->htile_address))
        return AGC_ERROR_INVALID_ARGUMENT;

    if ((state->depth_read_address &&
         !agcGfx1013DepthAddressValid(state->depth_read_address)) ||
        (state->depth_write_address &&
         !agcGfx1013DepthAddressValid(state->depth_write_address)) ||
        (state->stencil_read_address &&
         !agcGfx1013DepthAddressValid(state->stencil_read_address)) ||
        (state->stencil_write_address &&
         !agcGfx1013DepthAddressValid(state->stencil_write_address)) ||
        (state->htile_address &&
         !agcGfx1013DepthAddressValid(state->htile_address)))
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_DEPTH_SURFACE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    depth_view = AGC_REG_SET(DB_DEPTH_VIEW, SLICE_START,
            state->first_layer) |
        AGC_REG_SET(DB_DEPTH_VIEW, SLICE_START_HI,
            state->first_layer >> 11) |
        AGC_REG_SET(DB_DEPTH_VIEW, SLICE_MAX, state->last_layer) |
        AGC_REG_SET(DB_DEPTH_VIEW, Z_READ_ONLY, state->depth_read_only) |
        AGC_REG_SET(DB_DEPTH_VIEW, STENCIL_READ_ONLY,
            state->stencil_read_only) |
        AGC_REG_SET(DB_DEPTH_VIEW, MIPID, state->mip_level) |
        AGC_REG_SET(DB_DEPTH_VIEW, SLICE_MAX_HI,
            state->last_layer >> 11);
    depth_size = AGC_REG_SET(DB_DEPTH_SIZE_XY, X_MAX, state->width - 1u) |
        AGC_REG_SET(DB_DEPTH_SIZE_XY, Y_MAX, state->height - 1u);
    z_info = AGC_REG_SET(DB_Z_INFO, FORMAT, z_format) |
        AGC_REG_SET(DB_Z_INFO, NUM_SAMPLES, sample_log2) |
        AGC_REG_SET(DB_Z_INFO, SW_MODE, state->depth_swizzle_mode) |
        AGC_REG_SET(DB_Z_INFO, MAXMIP, state->mip_level_count - 1u) |
        AGC_REG_SET(DB_Z_INFO, ALLOW_EXPCLEAR, state->allow_expclear) |
        AGC_REG_SET(DB_Z_INFO, TILE_SURFACE_ENABLE, state->htile_enable);
    stencil_info = AGC_REG_SET(DB_STENCIL_INFO, FORMAT, stencil_format) |
        AGC_REG_SET(DB_STENCIL_INFO, SW_MODE, state->stencil_swizzle_mode) |
        AGC_REG_SET(DB_STENCIL_INFO, ALLOW_EXPCLEAR,
            state->allow_expclear && stencil_format != 0u &&
            !state->htile_stencil_disable) |
        AGC_REG_SET(DB_STENCIL_INFO, TILE_STENCIL_DISABLE,
            state->htile_stencil_disable);

    bases[0] = (uint32_t)(state->depth_read_address >> 8);
    bases[1] = (uint32_t)(state->stencil_read_address >> 8);
    bases[2] = (uint32_t)(state->depth_write_address >> 8);
    bases[3] = (uint32_t)(state->stencil_write_address >> 8);
    bases_hi[0] = (uint32_t)(state->depth_read_address >> 40);
    bases_hi[1] = (uint32_t)(state->stencil_read_address >> 40);
    bases_hi[2] = (uint32_t)(state->depth_write_address >> 40);
    bases_hi[3] = (uint32_t)(state->stencil_write_address >> 40);
    bases_hi[4] = (uint32_t)(state->htile_address >> 40);

    if (!agcGfx1013EmitCx(cb, AGC_REG_DB_DEPTH_VIEW, depth_view) ||
        !agcGfx1013EmitCx(cb, AGC_REG_DB_HTILE_DATA_BASE,
            (uint32_t)(state->htile_address >> 8)) ||
        !agcGfx1013EmitCx(cb, AGC_REG_DB_DEPTH_SIZE_XY, depth_size))
        return AGC_ERROR_INTERNAL;
    cmd = agcCbAllocDwords(cb, 8u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 8u);
    cmd[1] = AGC_REG_DB_Z_INFO;
    cmd[2] = z_info;
    cmd[3] = stencil_info;
    memcpy(&cmd[4], bases, sizeof(bases));

    cmd = agcCbAllocDwords(cb, 7u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 7u);
    cmd[1] = AGC_REG_DB_Z_READ_BASE_HI;
    memcpy(&cmd[2], bases_hi, sizeof(bases_hi));
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

static bool agcGfx1013StencilOpValid(AgcGfx1013StencilOp operation)
{
    return operation == AGC_GFX1013_STENCIL_KEEP ||
        operation == AGC_GFX1013_STENCIL_ZERO ||
        operation == AGC_GFX1013_STENCIL_REPLACE ||
        operation == AGC_GFX1013_STENCIL_INCREMENT_CLAMP ||
        operation == AGC_GFX1013_STENCIL_DECREMENT_CLAMP ||
        operation == AGC_GFX1013_STENCIL_INVERT ||
        operation == AGC_GFX1013_STENCIL_INCREMENT_WRAP ||
        operation == AGC_GFX1013_STENCIL_DECREMENT_WRAP;
}

int32_t PS5_SYSV_ABI agcGfx1013SetColorBlendState(
    SceAgcCb *cb, const AgcGfx1013ColorBlendState *state)
{
    uint32_t controls[AGC_GFX1013_MAX_COLOR_TARGETS] = {0};
    uint32_t target_mask = 0u;
    uint32_t *cmd;
    uint32_t i;

    if (!cb || !state || state->target_count == 0u ||
        state->target_count > AGC_GFX1013_MAX_COLOR_TARGETS)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (i = 0u; i < state->target_count; ++i) {
        const AgcGfx1013ColorBlendTargetState *target = &state->targets[i];
        if (target->enable > 1u || target->separate_alpha > 1u ||
            target->color_source >= AGC_GFX1013_BLEND_FACTOR_COUNT ||
            target->color_destination >= AGC_GFX1013_BLEND_FACTOR_COUNT ||
            target->alpha_source >= AGC_GFX1013_BLEND_FACTOR_COUNT ||
            target->alpha_destination >= AGC_GFX1013_BLEND_FACTOR_COUNT ||
            target->color_operation >= AGC_GFX1013_BLEND_OP_COUNT ||
            target->alpha_operation >= AGC_GFX1013_BLEND_OP_COUNT ||
            target->write_mask > 0x0fu)
            return AGC_ERROR_INVALID_ARGUMENT;
        controls[i] =
            ((uint32_t)target->color_source <<
                AGC_REG_CB_BLEND0_CONTROL_COLOR_SRCBLEND_SHIFT) |
            ((uint32_t)target->color_operation <<
                AGC_REG_CB_BLEND0_CONTROL_COLOR_COMB_FCN_SHIFT) |
            ((uint32_t)target->color_destination <<
                AGC_REG_CB_BLEND0_CONTROL_COLOR_DESTBLEND_SHIFT) |
            ((uint32_t)target->alpha_source <<
                AGC_REG_CB_BLEND0_CONTROL_ALPHA_SRCBLEND_SHIFT) |
            ((uint32_t)target->alpha_operation <<
                AGC_REG_CB_BLEND0_CONTROL_ALPHA_COMB_FCN_SHIFT) |
            ((uint32_t)target->alpha_destination <<
                AGC_REG_CB_BLEND0_CONTROL_ALPHA_DESTBLEND_SHIFT) |
            (target->separate_alpha <<
                AGC_REG_CB_BLEND0_CONTROL_SEPARATE_ALPHA_BLEND_SHIFT) |
            (target->enable << AGC_REG_CB_BLEND0_CONTROL_ENABLE_SHIFT);
        target_mask |= target->write_mask << (i * 4u);
    }
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_BLEND_STATE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    cmd = agcCbAllocDwords(cb, 10u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 10u);
    cmd[1] = AGC_REG_CB_BLEND0_CONTROL;
    memcpy(&cmd[2], controls, sizeof(controls));
    if (!agcGfx1013EmitCx(cb, AGC_REG_CB_TARGET_MASK, target_mask))
        return AGC_ERROR_INTERNAL;
    cmd = agcCbAllocDwords(cb, 6u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 6u);
    cmd[1] = AGC_REG_CB_BLEND_RED;
    for (i = 0u; i < 4u; ++i)
        cmd[i + 2u] = agcGfx1013FloatBits(state->constants[i]);
    return AGC_OK;
}

static bool agcGfx1013StencilFaceValid(
    const AgcGfx1013StencilFaceState *face)
{
    return face->compare_operation < AGC_GFX1013_COMPARE_COUNT &&
        agcGfx1013StencilOpValid(face->fail_operation) &&
        agcGfx1013StencilOpValid(face->depth_fail_operation) &&
        agcGfx1013StencilOpValid(face->pass_operation) &&
        face->reference <= 0xffu && face->compare_mask <= 0xffu &&
        face->write_mask <= 0xffu;
}

static uint32_t agcGfx1013StencilRefMask(
    const AgcGfx1013StencilFaceState *face)
{
    return face->reference |
        (face->compare_mask << AGC_REG_DB_STENCILREFMASK_COMPARE_SHIFT) |
        (face->write_mask << AGC_REG_DB_STENCILREFMASK_WRITE_SHIFT) |
        (face->reference << AGC_REG_DB_STENCILREFMASK_OPVAL_SHIFT);
}

int32_t PS5_SYSV_ABI agcGfx1013SetDepthStencilState(
    SceAgcCb *cb, const AgcGfx1013DepthStencilState *state)
{
    const AgcGfx1013StencilFaceState *back;
    uint32_t depth_control;
    uint32_t stencil_control;
    uint32_t *cmd;

    if (!cb || !state || state->depth_test_enable > 1u ||
        state->depth_write_enable > 1u || state->depth_bounds_enable > 1u ||
        state->stencil_test_enable > 1u || state->back_face_enable > 1u ||
        state->depth_compare_operation >= AGC_GFX1013_COMPARE_COUNT ||
        (state->depth_write_enable && !state->depth_test_enable) ||
        (state->depth_bounds_enable && !state->depth_test_enable) ||
        (state->back_face_enable && !state->stencil_test_enable) ||
        !(state->min_depth_bounds >= 0.0f &&
          state->max_depth_bounds <= 1.0f &&
          state->min_depth_bounds <= state->max_depth_bounds) ||
        !agcGfx1013StencilFaceValid(&state->front) ||
        !agcGfx1013StencilFaceValid(&state->back))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    back = state->back_face_enable ? &state->back : &state->front;
    depth_control = state->stencil_test_enable |
        (state->depth_test_enable << AGC_REG_DB_DEPTH_CONTROL_Z_ENABLE_SHIFT) |
        (state->depth_write_enable <<
            AGC_REG_DB_DEPTH_CONTROL_Z_WRITE_ENABLE_SHIFT) |
        (state->depth_bounds_enable <<
            AGC_REG_DB_DEPTH_CONTROL_DEPTH_BOUNDS_ENABLE_SHIFT) |
        ((uint32_t)state->depth_compare_operation <<
            AGC_REG_DB_DEPTH_CONTROL_ZFUNC_SHIFT) |
        (state->back_face_enable <<
            AGC_REG_DB_DEPTH_CONTROL_BACKFACE_ENABLE_SHIFT) |
        ((uint32_t)state->front.compare_operation <<
            AGC_REG_DB_DEPTH_CONTROL_STENCILFUNC_SHIFT) |
        ((uint32_t)back->compare_operation <<
            AGC_REG_DB_DEPTH_CONTROL_STENCILFUNC_BF_SHIFT);
    stencil_control = (uint32_t)state->front.fail_operation |
        ((uint32_t)state->front.pass_operation <<
            AGC_REG_DB_STENCIL_CONTROL_ZPASS_SHIFT) |
        ((uint32_t)state->front.depth_fail_operation <<
            AGC_REG_DB_STENCIL_CONTROL_ZFAIL_SHIFT) |
        ((uint32_t)back->fail_operation <<
            AGC_REG_DB_STENCIL_CONTROL_FAIL_BF_SHIFT) |
        ((uint32_t)back->pass_operation <<
            AGC_REG_DB_STENCIL_CONTROL_ZPASS_BF_SHIFT) |
        ((uint32_t)back->depth_fail_operation <<
            AGC_REG_DB_STENCIL_CONTROL_ZFAIL_BF_SHIFT);

    if (!agcGfx1013EmitCx(cb, AGC_REG_DB_DEPTH_CONTROL, depth_control) ||
        !agcGfx1013EmitCx(cb, AGC_REG_DB_STENCIL_CONTROL, stencil_control))
        return AGC_ERROR_INTERNAL;
    cmd = agcCbAllocDwords(cb, 4u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
    cmd[1] = AGC_REG_DB_STENCILREFMASK;
    cmd[2] = agcGfx1013StencilRefMask(&state->front);
    cmd[3] = agcGfx1013StencilRefMask(back);
    cmd = agcCbAllocDwords(cb, 4u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
    cmd[1] = AGC_REG_DB_DEPTH_BOUNDS_MIN;
    cmd[2] = agcGfx1013FloatBits(state->min_depth_bounds);
    cmd[3] = agcGfx1013FloatBits(state->max_depth_bounds);
    return AGC_OK;
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

static int32_t agcGfx1013ValidateFrameState(
    const AgcGfx1013FrameState *state)
{
    uint32_t scratch[28] = {0};
    SceAgcCb probe;
    int32_t error;

    if (!state || state->min_vertex_index > state->max_vertex_index ||
        state->instance_step_rate == 0u ||
        state->scissor.right > state->color_target.width ||
        state->scissor.bottom > state->color_target.height)
        return AGC_ERROR_INVALID_ARGUMENT;

    agcCbInit(&probe, scratch, sizeof(scratch));
    error = agcGfx1013SetColorTarget(&probe, &state->color_target);
    if (error != AGC_OK)
        return error;
    agcCbReset(&probe, scratch, sizeof(scratch));
    error = agcGfx1013SetViewport(&probe, &state->viewport);
    if (error != AGC_OK)
        return error;
    agcCbReset(&probe, scratch, sizeof(scratch));
    error = agcGfx1013SetScissor(&probe, &state->scissor);
    if (error != AGC_OK)
        return error;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013BuildFramePrologue(
    SceAgcCb *cb, const AgcGfx1013FrameState *state,
    AgcGfx1013GraphicsDefaultStats *stats)
{
    AgcGfx1013GraphicsDefaultStats counts = {0};
    const AgcRegisterValue vertex_bounds[3] = {
        {AGC_REG_GE_MIN_VTX_INDX, state ? state->min_vertex_index : 0u},
        {AGC_REG_GE_INDX_OFFSET, state ? state->vertex_index_offset : 0u},
        {AGC_REG_GE_MAX_VTX_INDX, state ? state->max_vertex_index : 0u},
    };
    const AgcRegisterValue launch_context[3] = {
        {AGC_REG_PA_SC_NGG_MODE_CNTL,
         state ? state->ngg_mode_control : 0u},
        {AGC_REG_VGT_VERTEX_REUSE_BLOCK_CNTL,
         state ? state->vertex_reuse_block_control : 0u},
        {AGC_REG_VGT_INSTANCE_STEP_RATE_0,
         state ? state->instance_step_rate : 0u},
    };
    uint32_t i;
    int32_t error;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013ValidateFrameState(state);
    if (error != AGC_OK)
        return error;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_FRAME_PROLOGUE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    if (agcGfx1013SetContextControl(
            cb, state->context_load_control,
            state->context_shadow_control) != AGC_OK ||
        !sceAgcDcbClearState(cb, state->clear_state_flags))
        return AGC_ERROR_INTERNAL;
    error = agcGfx1013ApplyGraphicsDefaultsV8(cb, &counts);
    if (error != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (agcGfx1013SetColorTarget(cb, &state->color_target) != AGC_OK ||
        agcGfx1013SetViewport(cb, &state->viewport) != AGC_OK ||
        agcGfx1013SetScissor(cb, &state->scissor) != AGC_OK ||
        agcGfx1013SetTargetMask(cb, state->target_mask) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    for (i = 0u; i < 3u; ++i) {
        if (!sceAgcCbSetUcRegistersDirect(cb, &vertex_bounds[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    for (i = 0u; i < 3u; ++i) {
        if (!sceAgcCbSetCxRegistersDirect(cb, &launch_context[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    if (stats)
        *stats = counts;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013ApplyFramePostBind(
    SceAgcCb *cb, const AgcGfx1013FrameState *state)
{
    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    int32_t error = agcGfx1013ValidateFrameState(state);
    if (error != AGC_OK)
        return error;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_FRAME_POST_BIND_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    if (agcGfx1013SetDepthDisabled(cb) != AGC_OK ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_PA_CL_CLIP_CNTL, state->clip_control) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_PA_SU_SC_MODE_CNTL, state->raster_mode_control))
        return AGC_ERROR_INTERNAL;
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
