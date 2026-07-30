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

static bool agcGfx1013IndirectModifier(
    uint32_t base_vertex_location, uint32_t start_instance_location,
    uint32_t draw_initiator, uint64_t *modifier)
{
    uint32_t register_base;
    uint64_t value;

    if (base_vertex_location >= 0x08cu &&
        base_vertex_location <= 0x0abu &&
        start_instance_location >= 0x08cu &&
        start_instance_location <= 0x0abu) {
        register_base = 0x08cu;
        value = 0u;
    } else if (base_vertex_location >= 0x10cu &&
               base_vertex_location <= 0x12bu &&
               start_instance_location >= 0x10cu &&
               start_instance_location <= 0x12bu) {
        register_base = 0x10cu;
        value = UINT64_C(3) << 29u;
    } else {
        return false;
    }
    if (draw_initiator != 2u && draw_initiator != 0x22u)
        return false;
    value |= UINT64_C(1) | UINT64_C(1) << 2u;
    value |= (uint64_t)(base_vertex_location - register_base) << 9u;
    value |= (uint64_t)(start_instance_location - register_base) << 19u;
    if (draw_initiator == 0x22u)
        value |= UINT64_C(1) << 8u;
    *modifier = value;
    return true;
}

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
    uint32_t hull_lds_size;
} AgcGfx1013RuntimePatches;

#define AGC_GFX1013_HS_LDS_SIZE_SHIFT 18u
#define AGC_GFX1013_HS_LDS_SIZE_MASK  (0x1ffu << 18u)
#define AGC_GFX1013_HS_LDS_ALLOCATION_GRANULARITY 1024u
#define AGC_GFX1013_HS_LDS_ENCODING_GRANULARITY 512u
#define AGC_GFX1013_HS_LDS_MAX_SIZE 65536u

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

static bool agcGfx1013BindingHasOffset(
    const AgcGfx1013ShaderBinding *binding, uint32_t offset)
{
    uint32_t i;

    for (i = 0; i < binding->num_sh_registers; ++i) {
        if (binding->sh_registers[i].offset == offset)
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
        else if (patches->hull_lds_size != 0u &&
                 reg.offset == AGC_REG_SPI_SHADER_PGM_RSRC2_HS) {
            /* GFX10.3 allocates LDS in 1 KiB blocks, while the HS register
             * field remains encoded in 512-byte units.  Consequently every
             * legal gfx1013 field value is even. */
            const uint32_t allocated =
                (patches->hull_lds_size +
                 AGC_GFX1013_HS_LDS_ALLOCATION_GRANULARITY - 1u) /
                AGC_GFX1013_HS_LDS_ALLOCATION_GRANULARITY *
                AGC_GFX1013_HS_LDS_ALLOCATION_GRANULARITY;
            const uint32_t encoded = allocated /
                AGC_GFX1013_HS_LDS_ENCODING_GRANULARITY;
            reg.value = (reg.value & ~AGC_GFX1013_HS_LDS_SIZE_MASK) |
                (encoded << AGC_GFX1013_HS_LDS_SIZE_SHIFT);
        }
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

static bool agcGfx1013StencilFaceValid(
    const AgcGfx1013StencilFaceState *face);

static int32_t agcGfx1013ValidateBaselineDrawState(
    const AgcGfx1013BaselineDrawState *state, uint32_t *required_dwords_out)
{
    uint32_t input_count;
    uint32_t required_dwords;
    uint32_t primitive_resource_count = 0u;
    uint32_t pixel_resource_count = 0u;
    int32_t error;

    if (!state || !required_dwords_out)
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
    if (state->depth_surface_state) {
        uint32_t storage[AGC_GFX1013_DEPTH_SURFACE_DWORDS];
        SceAgcCb validation_cb;
        agcCbInit(&validation_cb, storage, sizeof(storage));
        error = agcGfx1013SetDepthSurface(
            &validation_cb, state->depth_surface_state);
        if (error != AGC_OK)
            return error;
    }
    if (state->depth_stencil_state) {
        const AgcGfx1013DepthStencilState *depth =
            state->depth_stencil_state;
        if (depth->depth_test_enable > 1u ||
            depth->depth_write_enable > 1u ||
            depth->depth_bounds_enable > 1u ||
            depth->stencil_test_enable > 1u ||
            depth->back_face_enable > 1u ||
            depth->depth_compare_operation >= AGC_GFX1013_COMPARE_COUNT ||
            (depth->depth_write_enable && !depth->depth_test_enable) ||
            (depth->depth_bounds_enable && !depth->depth_test_enable) ||
            (depth->back_face_enable && !depth->stencil_test_enable) ||
            !(depth->min_depth_bounds >= 0.0f &&
              depth->max_depth_bounds <= 1.0f &&
              depth->min_depth_bounds <= depth->max_depth_bounds) ||
            !agcGfx1013StencilFaceValid(&depth->front) ||
            !agcGfx1013StencilFaceValid(&depth->back))
            return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (state->sample_state) {
        uint32_t storage[AGC_GFX1013_SAMPLE_STATE_DWORDS];
        SceAgcCb validation_cb;
        agcCbInit(&validation_cb, storage, sizeof(storage));
        error = agcGfx1013SetSampleState(
            &validation_cb, state->sample_state);
        if (error != AGC_OK)
            return error;
    }
    if (state->viewport_array_state) {
        uint32_t storage[AGC_GFX1013_VIEWPORT_ARRAY_MAX_DWORDS];
        SceAgcCb validation_cb;
        agcCbInit(&validation_cb, storage, sizeof(storage));
        error = agcGfx1013SetViewportArray(
            &validation_cb, state->viewport_array_state);
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
    if (state->depth_surface_state)
        required_dwords += AGC_GFX1013_DEPTH_SURFACE_DWORDS;
    if (state->depth_stencil_state)
        required_dwords += AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS;
    if (state->sample_state)
        required_dwords += AGC_GFX1013_SAMPLE_STATE_DWORDS;
    if (state->viewport_array_state)
        required_dwords += AGC_GFX1013_VIEWPORT_ARRAY_DWORDS(
            state->viewport_array_state->count);
    *required_dwords_out = required_dwords;
    return AGC_OK;
}

static int32_t agcGfx1013EmitBaselineDrawPrefix(
    SceAgcCb *cb, const AgcGfx1013BaselineDrawState *state)
{
    uint32_t i;
    int32_t error;

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
    if (state->depth_surface_state &&
        agcGfx1013SetDepthSurface(
            cb, state->depth_surface_state) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->depth_stencil_state &&
        agcGfx1013SetDepthStencilState(
            cb, state->depth_stencil_state) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->sample_state &&
        agcGfx1013SetSampleState(cb, state->sample_state) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->viewport_array_state &&
        agcGfx1013SetViewportArray(
            cb, state->viewport_array_state) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    for (i = 0; i < state->num_post_bind_sh_registers; ++i) {
        if (!sceAgcCbSetShRegistersDirect(
                cb, &state->post_bind_sh_registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    for (i = 0; i < state->num_post_bind_cx_registers; ++i) {
        if (!sceAgcCbSetCxRegistersDirect(
                cb, &state->post_bind_cx_registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    for (i = 0; i < state->num_post_bind_uc_registers; ++i) {
        if (!sceAgcCbSetUcRegistersDirect(
                cb, &state->post_bind_uc_registers[i], 1u))
            return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndexAuto(
    SceAgcCb *cb, const AgcGfx1013BaselineDrawState *state)
{
    uint32_t required_dwords;
    int32_t error;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013ValidateBaselineDrawState(state, &required_dwords);
    if (error != AGC_OK)
        return error;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    error = agcGfx1013EmitBaselineDrawPrefix(cb, state);
    if (error != AGC_OK)
        return error;
    if (!sceAgcDcbSetIndexSize(cb, state->index_type, state->index_swap) ||
        !sceAgcDcbSetNumInstances(cb, state->instance_count) ||
        !sceAgcDcbDrawIndexAuto(
            cb, state->vertex_count, state->draw_modifier)) {
        return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndexed(
    SceAgcCb *cb, const AgcGfx1013IndexedDrawState *state)
{
    AgcGfx1013BaselineDrawState validation;
    uint32_t required_dwords;
    uint32_t element_size;
    uint64_t byte_offset;
    uint64_t draw_address;
    int32_t error;

    if (!cb || !state || state->index_buffer_address == 0u ||
        state->index_buffer_count == 0u || state->index_count == 0u ||
        state->draw.instance_count == 0u ||
        state->draw.index_type > (uint32_t)kAgcIndexSize32 ||
        state->first_index >= state->index_buffer_count ||
        state->index_count > state->index_buffer_count - state->first_index)
        return AGC_ERROR_INVALID_ARGUMENT;
    element_size = state->draw.index_type == (uint32_t)kAgcIndexSize32 ?
        4u : 2u;
    if ((state->index_buffer_address & (element_size - 1u)) != 0u ||
        (state->index_buffer_address >> 48) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    byte_offset = (uint64_t)state->first_index * element_size;
    draw_address = state->index_buffer_address + byte_offset;
    if (draw_address < state->index_buffer_address ||
        (draw_address >> 48) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;

    validation = state->draw;
    validation.vertex_count = state->index_count;
    error = agcGfx1013ValidateBaselineDrawState(
        &validation, &required_dwords);
    if (error != AGC_OK)
        return error;
    required_dwords += 3u;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    error = agcGfx1013EmitBaselineDrawPrefix(cb, &state->draw);
    if (error != AGC_OK)
        return error;
    if (!sceAgcDcbSetIndexSize(
            cb, state->draw.index_type, state->draw.index_swap) ||
        !sceAgcDcbSetNumInstances(cb, state->draw.instance_count) ||
        !sceAgcDcbDrawIndex2(
            cb, state->index_buffer_count - state->first_index,
            draw_address, state->index_count, state->draw_initiator))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndirect(
    SceAgcCb *cb, const AgcGfx1013IndirectDrawState *state)
{
    AgcGfx1013BaselineDrawState validation;
    uint32_t required_dwords;
    uint32_t tail_dwords;
    uint32_t minimum_stride;
    uint32_t element_size;
    uint64_t modifier;
    int32_t error;

    if (!cb || !state || state->argument_buffer_address == 0u ||
        state->draw_count == 0u || state->indexed > 1u ||
        state->count_indirect > 1u ||
        state->draw_index_enable != 0u ||
        state->draw_index_location != 0u ||
        (state->argument_buffer_address & 7u) != 0u ||
        (state->argument_buffer_address >> 48) != 0u ||
        (state->argument_offset & 3u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((state->count_indirect != 0u &&
         (state->count_address == 0u ||
          (state->count_address & 3u) != 0u ||
          (state->count_address >> 48u) != 0u)) ||
        (state->count_indirect == 0u && state->count_address != 0u))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcGfx1013IndirectModifier(
            state->base_vertex_location, state->start_instance_location,
            state->draw_initiator, &modifier))
        return AGC_ERROR_INVALID_ARGUMENT;
    minimum_stride = state->indexed ? 20u : 16u;
    if (state->draw_count > 1u &&
        (state->stride < minimum_stride || (state->stride & 3u) != 0u))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (state->indexed) {
        if (state->index_buffer_address == 0u ||
            state->index_buffer_count == 0u ||
            state->draw.index_type > (uint32_t)kAgcIndexSize32)
            return AGC_ERROR_INVALID_ARGUMENT;
        element_size = state->draw.index_type == (uint32_t)kAgcIndexSize32 ?
            4u : 2u;
        if ((state->index_buffer_address & (element_size - 1u)) != 0u ||
            (state->index_buffer_address >> 48) != 0u)
            return AGC_ERROR_INVALID_ALIGNMENT;
    }

    validation = state->draw;
    validation.instance_count = 1u;
    validation.vertex_count = 1u;
    error = agcGfx1013ValidateBaselineDrawState(
        &validation, &required_dwords);
    if (error != AGC_OK)
        return error;
    /* The Sony multi builders emit ten dwords but require their full
     * 16-dword GetSize reservation to remain available at the call site. */
    tail_dwords = 4u + 16u;
    if (state->indexed)
        tail_dwords += 8u;
    required_dwords = required_dwords - 8u + tail_dwords;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    error = agcGfx1013EmitBaselineDrawPrefix(cb, &state->draw);
    if (error != AGC_OK)
        return error;
    if (state->indexed &&
        (!sceAgcDcbSetIndexSize(
             cb, state->draw.index_type, state->draw.index_swap) ||
         !sceAgcDcbSetIndexBuffer(
             cb, state->index_buffer_address, state->index_buffer_count)))
        return AGC_ERROR_INTERNAL;
    if (!sceAgcDcbSetBaseIndirectArgs(
            cb, 0u, state->argument_buffer_address))
        return AGC_ERROR_INTERNAL;
    if (!(state->indexed ? sceAgcDcbDrawIndexIndirectMulti(
              cb, state->argument_offset, state->count_indirect,
              state->draw_count,
              (const volatile void *)(uintptr_t)state->count_address,
              state->draw_count > 1u ? state->stride : minimum_stride,
              modifier) :
          sceAgcDcbDrawIndirectMulti(
              cb, state->argument_offset, state->count_indirect,
              state->draw_count,
              (const volatile void *)(uintptr_t)state->count_address,
              state->draw_count > 1u ? state->stride : minimum_stride,
              modifier)))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013CopyBuffer(
    SceAgcCb *cb, uint64_t source_address, uint64_t destination_address,
    uint64_t byte_count)
{
    const uint64_t address_limit = UINT64_C(1) << 48u;
    const uint64_t maximum_packet_bytes = UINT64_C(0x1ffffc);

    if (!cb || source_address == 0u || destination_address == 0u ||
        byte_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (((source_address | destination_address | byte_count) & 3u) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (source_address >= address_limit || destination_address >= address_limit ||
        byte_count > address_limit - source_address ||
        byte_count > address_limit - destination_address)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint64_t packet_count = byte_count / maximum_packet_bytes +
        (byte_count % maximum_packet_bytes != 0u);
    if (packet_count > UINT32_MAX / 7u ||
        agcCbRemainingDwords(cb) < (uint32_t)packet_count * 7u)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    while (byte_count != 0u) {
        uint32_t packet_bytes = byte_count > maximum_packet_bytes ?
            (uint32_t)maximum_packet_bytes : (uint32_t)byte_count;
        uint32_t *packet = agcCbAllocDwords(cb, 7u);
        if (!packet)
            return AGC_ERROR_INTERNAL;
        packet[0] = agcPm4Header3(AGC_PM4_OP_DMA_DATA, 7u);
        packet[1] = UINT32_C(0xe0300000); /* L2 source/destination, CP sync. */
        packet[2] = (uint32_t)source_address;
        packet[3] = (uint32_t)(source_address >> 32u);
        packet[4] = (uint32_t)destination_address;
        packet[5] = (uint32_t)(destination_address >> 32u);
        packet[6] = packet_bytes;
        source_address += packet_bytes;
        destination_address += packet_bytes;
        byte_count -= packet_bytes;
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
    if ((state->tcs_offchip_layout & 0x7fu) == 0u ||
        (state->tes_offchip_layout & 0x7fu) == 0u ||
        (state->tcs_offchip_layout & ~(0x1fu << 7u)) !=
            (state->tes_offchip_layout & ~(0x1fu << 7u)))
        return AGC_ERROR_VALIDATION_FAILED;
    if (state->hull_lds_size > AGC_GFX1013_HS_LDS_MAX_SIZE ||
        (state->hull_lds_size != 0u &&
         !agcGfx1013BindingHasOffset(
             &state->hull, AGC_REG_SPI_SHADER_PGM_RSRC2_HS)))
        return AGC_ERROR_VALIDATION_FAILED;
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
        .ring_descriptor_address = state->ring_descriptor_address,
        .next_stage_address = state->hull_back_code_address,
        .tcs_offchip_layout = state->tcs_offchip_layout,
        .hull_lds_size = state->hull_lds_size,
    };
    gs_patches = (AgcGfx1013RuntimePatches){
        .ring_descriptor_address = state->ring_descriptor_address,
        .next_stage_address = state->primitive_back_code_address,
        .tcs_offchip_layout = state->tes_offchip_layout,
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
    if (state->depth_surface_state) {
        uint32_t storage[AGC_GFX1013_DEPTH_SURFACE_DWORDS];
        SceAgcCb validation_cb;
        agcCbInit(&validation_cb, storage, sizeof(storage));
        error = agcGfx1013SetDepthSurface(
            &validation_cb, state->depth_surface_state);
        if (error != AGC_OK)
            return error;
    }
    if (state->depth_stencil_state) {
        const AgcGfx1013DepthStencilState *depth =
            state->depth_stencil_state;
        if (depth->depth_test_enable > 1u ||
            depth->depth_write_enable > 1u ||
            depth->depth_bounds_enable > 1u ||
            depth->stencil_test_enable > 1u ||
            depth->back_face_enable > 1u ||
            depth->depth_compare_operation >= AGC_GFX1013_COMPARE_COUNT ||
            (depth->depth_write_enable && !depth->depth_test_enable) ||
            (depth->depth_bounds_enable && !depth->depth_test_enable) ||
            (depth->back_face_enable && !depth->stencil_test_enable) ||
            !(depth->min_depth_bounds >= 0.0f &&
              depth->max_depth_bounds <= 1.0f &&
              depth->min_depth_bounds <= depth->max_depth_bounds) ||
            !agcGfx1013StencilFaceValid(&depth->front) ||
            !agcGfx1013StencilFaceValid(&depth->back))
            return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (state->sample_state) {
        uint32_t storage[AGC_GFX1013_SAMPLE_STATE_DWORDS];
        SceAgcCb validation_cb;
        agcCbInit(&validation_cb, storage, sizeof(storage));
        error = agcGfx1013SetSampleState(
            &validation_cb, state->sample_state);
        if (error != AGC_OK)
            return error;
    }
    if (state->viewport_array_state) {
        uint32_t storage[AGC_GFX1013_VIEWPORT_ARRAY_MAX_DWORDS];
        SceAgcCb validation_cb;
        agcCbInit(&validation_cb, storage, sizeof(storage));
        error = agcGfx1013SetViewportArray(
            &validation_cb, state->viewport_array_state);
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
    if (state->depth_surface_state)
        required_dwords += AGC_GFX1013_DEPTH_SURFACE_DWORDS;
    if (state->depth_stencil_state)
        required_dwords += AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS;
    if (state->sample_state)
        required_dwords += AGC_GFX1013_SAMPLE_STATE_DWORDS;
    if (state->viewport_array_state)
        required_dwords += AGC_GFX1013_VIEWPORT_ARRAY_DWORDS(
            state->viewport_array_state->count);
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
    if (state->depth_surface_state &&
        agcGfx1013SetDepthSurface(
            cb, state->depth_surface_state) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->depth_stencil_state &&
        agcGfx1013SetDepthStencilState(
            cb, state->depth_stencil_state) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->sample_state &&
        agcGfx1013SetSampleState(cb, state->sample_state) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    if (state->viewport_array_state &&
        agcGfx1013SetViewportArray(
            cb, state->viewport_array_state) != AGC_OK)
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

int32_t PS5_SYSV_ABI agcGfx1013WriteOcclusionSnapshot(
    SceAgcCb *cb, uint64_t address)
{
    uint32_t *cmd;

    if (!cb || address == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((address & 7u) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if ((address >> 48u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_OCCLUSION_SNAPSHOT_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    cmd = agcCbAllocDwords(cb, AGC_GFX1013_OCCLUSION_SNAPSHOT_DWORDS);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_EVENT_WRITE,
                           AGC_GFX1013_OCCLUSION_SNAPSHOT_DWORDS);
    cmd[1] = 0x15u | (1u << 8u); /* ZPASS_DONE, event index 1. */
    cmd[2] = (uint32_t)address;
    cmd[3] = (uint32_t)(address >> 32u);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013BeginOcclusionQuery(
    SceAgcCb *cb, uint64_t address, uint32_t precise)
{
    uint32_t count_control = 0x11000102u;

    if (!cb || address == 0u || (address >> 48u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((address & 7u) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_OCCLUSION_QUERY_OP_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    if (precise)
        count_control |= 1u << 2u;
    if (!sceAgcDcbSetCxRegisterDirect(cb,
            ((uint64_t)count_control << 32u) | AGC_REG_DB_COUNT_CONTROL) ||
        agcGfx1013WriteOcclusionSnapshot(cb, address) != AGC_OK)
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013EndOcclusionQuery(
    SceAgcCb *cb, uint64_t address)
{
    if (!cb || address == 0u || (address >> 48u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((address & 7u) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_OCCLUSION_QUERY_OP_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    if (agcGfx1013WriteOcclusionSnapshot(cb, address) != AGC_OK ||
        !sceAgcDcbSetCxRegisterDirect(cb,
            ((uint64_t)1u << 32u) | AGC_REG_DB_COUNT_CONTROL))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
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
        {AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT,
         {AGC_GFX1013_COLOR_FORMAT_10_11_11,
          AGC_GFX1013_SURFACE_NUMBER_FLOAT, AGC_GFX1013_SURFACE_SWAP_STD,
          4u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGBA8_SRGB,
         {AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
          AGC_GFX1013_SURFACE_NUMBER_SRGB, AGC_GFX1013_SURFACE_SWAP_STD,
          4u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_BGRA8_SRGB,
         {AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
          AGC_GFX1013_SURFACE_NUMBER_SRGB, AGC_GFX1013_SURFACE_SWAP_ALT,
          4u, AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_R16_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_16, AGC_GFX1013_SURFACE_NUMBER_UNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 2u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RG16_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_16_16,
          AGC_GFX1013_SURFACE_NUMBER_UNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 4u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGBA16_UNORM,
         {AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
          AGC_GFX1013_SURFACE_NUMBER_UNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 8u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_R16_SNORM,
         {AGC_GFX1013_COLOR_FORMAT_16, AGC_GFX1013_SURFACE_NUMBER_SNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 2u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RG16_SNORM,
         {AGC_GFX1013_COLOR_FORMAT_16_16,
          AGC_GFX1013_SURFACE_NUMBER_SNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 4u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGBA16_SNORM,
         {AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
          AGC_GFX1013_SURFACE_NUMBER_SNORM,
          AGC_GFX1013_SURFACE_SWAP_STD, 8u,
          AGC_GFX1013_SPI_EXPORT_FP16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_R16_UINT,
         {AGC_GFX1013_COLOR_FORMAT_16, AGC_GFX1013_SURFACE_NUMBER_UINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 2u,
          AGC_GFX1013_SPI_EXPORT_UINT16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RG16_UINT,
         {AGC_GFX1013_COLOR_FORMAT_16_16,
          AGC_GFX1013_SURFACE_NUMBER_UINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 4u,
          AGC_GFX1013_SPI_EXPORT_UINT16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGBA16_UINT,
         {AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
          AGC_GFX1013_SURFACE_NUMBER_UINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 8u,
          AGC_GFX1013_SPI_EXPORT_UINT16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_R16_SINT,
         {AGC_GFX1013_COLOR_FORMAT_16, AGC_GFX1013_SURFACE_NUMBER_SINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 2u,
          AGC_GFX1013_SPI_EXPORT_SINT16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RG16_SINT,
         {AGC_GFX1013_COLOR_FORMAT_16_16,
          AGC_GFX1013_SURFACE_NUMBER_SINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 4u,
          AGC_GFX1013_SPI_EXPORT_SINT16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_RGBA16_SINT,
         {AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
          AGC_GFX1013_SURFACE_NUMBER_SINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 8u,
          AGC_GFX1013_SPI_EXPORT_SINT16_ABGR}},
        {AGC_GFX1013_RT_FORMAT_R32_UINT,
         {AGC_GFX1013_COLOR_FORMAT_32, AGC_GFX1013_SURFACE_NUMBER_UINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 4u,
          AGC_GFX1013_SPI_EXPORT_32_R}},
        {AGC_GFX1013_RT_FORMAT_RG32_UINT,
         {AGC_GFX1013_COLOR_FORMAT_32_32, AGC_GFX1013_SURFACE_NUMBER_UINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 8u,
          AGC_GFX1013_SPI_EXPORT_32_GR}},
        {AGC_GFX1013_RT_FORMAT_RGBA32_UINT,
         {AGC_GFX1013_COLOR_FORMAT_32_32_32_32,
          AGC_GFX1013_SURFACE_NUMBER_UINT,
          AGC_GFX1013_SURFACE_SWAP_STD, 16u,
          AGC_GFX1013_SPI_EXPORT_32_ABGR}},
};

_Static_assert(sizeof(kAgcGfx1013ColorTargetFormats) /
        sizeof(kAgcGfx1013ColorTargetFormats[0]) ==
        AGC_GFX1013_RT_FORMAT_COUNT,
    "color target format table must cover every public format");

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

int32_t PS5_SYSV_ABI agcGfx1013SetColorTargetSlot(
    SceAgcCb *cb, uint32_t slot,
    const AgcGfx1013ColorTargetState *state)
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
    uint32_t surface_pitch;
    uint32_t padded_height;
    uint32_t blend_clamp;
    uint32_t blend_bypass;
    uint32_t round_mode;

    if (!cb || slot >= AGC_GFX1013_MAX_COLOR_TARGETS || !state ||
        state->address == 0u || state->width == 0u ||
        state->height == 0u || state->width > 0x4000u ||
        state->height > 0x4000u)
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
        surface_pitch = state->width;
        padded_height = state->height;
        if ((state->width & 7u) != 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        if (((uint64_t)surface_pitch * format_info.bytes_per_pixel &
             0xffu) != 0u)
            return AGC_ERROR_INVALID_ALIGNMENT;
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
        surface_pitch = layout.pitch;
        padded_height = layout.padded_height;
        if ((state->address &
             (AGC_GFX1013_64KB_SURFACE_ALIGNMENT - 1u)) != 0u)
            return AGC_ERROR_INVALID_ALIGNMENT;
    } else {
        return AGC_ERROR_NOT_SUPPORTED;
    }

    tiles_per_row = surface_pitch / 8u;
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
        (state->component_swap << AGC_REG_CB_COLOR0_INFO_COMP_SWAP_SHIFT);
    blend_clamp = state->number_type == AGC_GFX1013_SURFACE_NUMBER_UNORM ||
        state->number_type == AGC_GFX1013_SURFACE_NUMBER_SNORM ||
        state->number_type == AGC_GFX1013_SURFACE_NUMBER_SRGB;
    blend_bypass =
        state->number_type == AGC_GFX1013_SURFACE_NUMBER_UINT ||
        state->number_type == AGC_GFX1013_SURFACE_NUMBER_SINT;
    round_mode = !blend_clamp;
    regs[4] |=
        (blend_clamp << AGC_REG_CB_COLOR0_INFO_BLEND_CLAMP_SHIFT) |
        (blend_bypass << AGC_REG_CB_COLOR0_INFO_BLEND_BYPASS_SHIFT) |
        (1u << AGC_REG_CB_COLOR0_INFO_SIMPLE_FLOAT_SHIFT) |
        (round_mode << AGC_REG_CB_COLOR0_INFO_ROUND_MODE_SHIFT);
    regs[5] =
        (sample_log2 << AGC_REG_CB_COLOR0_ATTRIB_NUM_SAMPLES_SHIFT) |
        (fragment_log2 << AGC_REG_CB_COLOR0_ATTRIB_NUM_FRAGMENTS_SHIFT);

    cmd = agcCbAllocDwords(cb, 16u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 16u);
    cmd[1] = AGC_REG_CB_COLOR0_BASE + slot * 15u;
    memcpy(&cmd[2], regs, sizeof(regs));

    if (!agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR0_BASE_EXT + slot,
            (uint32_t)(state->address >> 40)) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR0_ATTRIB2 + slot,
            ((state->height - 1u) & 0x3fffu) |
            (((state->width - 1u) & 0x3fffu) << 14)) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR0_ATTRIB3 + slot,
            0x09000001u | (state->swizzle_mode <<
                AGC_REG_CB_COLOR0_ATTRIB3_COLOR_SW_MODE_SHIFT)) ||
        !agcGfx1013EmitCx(
            cb, AGC_REG_CB_COLOR_CONTROL, 0x00cc0010u))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetColorTarget(
    SceAgcCb *cb, const AgcGfx1013ColorTargetState *state)
{
    return agcGfx1013SetColorTargetSlot(cb, 0u, state);
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
         state->pixel_shader_sample_count != 2u &&
         state->pixel_shader_sample_count != state->sample_count) ||
        (state->pixel_shader_sample_count > state->sample_count) ||
        (state->sample_mask & ~((1u << state->sample_count) - 1u)) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_SAMPLE_STATE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    sample_log2 = state->sample_count == 4u ? 2u : 0u;
    ps_iter_log2 = state->pixel_shader_sample_count == 4u ? 2u :
        (state->pixel_shader_sample_count == 2u ? 1u : 0u);
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
            (1u << AGC_REG_PA_SC_MODE_CNTL_0_VPORT_SCISSOR_ENABLE_SHIFT)) ||
        !agcGfx1013EmitCx(cb, AGC_REG_PA_SC_MODE_CNTL_1,
            (state->pixel_shader_sample_count > 1u ? 1u : 0u) <<
                AGC_REG_PA_SC_MODE_CNTL_1_PS_ITER_SAMPLE_SHIFT))
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
    uint32_t source_scratch[28] = {0};
    SceAgcCb source_probe;
    uint32_t transition_dwords;
    uint32_t draw_dwords;
    uint64_t required_dwords;
    int32_t error;

    if (!cb || !state || !state->source || !state->draw ||
        !state->draw->frame || state->source->sample_count != 4u ||
        state->source->fragment_count != 4u ||
        state->source->swizzle_mode != AGC_GFX1013_SWIZZLE_64KB_R_X ||
        (state->draw->frame->color_target.sample_count != 0u &&
         state->draw->frame->color_target.sample_count != 1u) ||
        state->source->address ==
            state->draw->frame->color_target.address ||
        state->source->width !=
            state->draw->frame->color_target.width ||
        state->source->height !=
            state->draw->frame->color_target.height ||
        state->source->color_format !=
            state->draw->frame->color_target.color_format ||
        state->source->number_type !=
            state->draw->frame->color_target.number_type ||
        state->source->component_swap !=
            state->draw->frame->color_target.component_swap)
        return AGC_ERROR_INVALID_ARGUMENT;

    agcCbInit(&source_probe, source_scratch, sizeof(source_scratch));
    error = agcGfx1013SetColorTarget(&source_probe, state->source);
    if (error != AGC_OK)
        return error;
    error = agcGfx1013GetResourceTransitionDwords(
        &transition, &transition_dwords);
    if (error != AGC_OK)
        return error;
    error = agcGfx1013ValidateBaselineDrawState(
        state->draw, &draw_dwords);
    if (error != AGC_OK)
        return error;

    required_dwords = (uint64_t)transition_dwords +
        AGC_GFX1013_FRAME_PROLOGUE_DWORDS +
        AGC_GFX1013_SAMPLE_STATE_DWORDS + draw_dwords;
    if (required_dwords > agcCbRemainingDwords(cb))
        return AGC_ERROR_BUFFER_TOO_SMALL;

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
        uint32_t ending_mip;
        uint32_t mip0_width_blocks;
        uint32_t mip0_height_blocks;
        uint32_t mip1_blocks;
        uint32_t chain_pitch = layout->pitch;
        uint32_t chain_height = layout->padded_height;

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

            if (mip_width <= tail_width && mip_height <= tail_height &&
                remaining <= 12u) {
                layout->first_mip_in_tail = mip;
                break;
            }
            mip_width = mip_width > 1u ? mip_width >> 1u : 1u;
            mip_height = mip_height > 1u ? mip_height >> 1u : 1u;
        }

        ending_mip = layout->first_mip_in_tail < input->mip_level_count - 1u ?
            layout->first_mip_in_tail : input->mip_level_count - 1u;
        if (ending_mip == 0u) {
            slice_size = AGC_GFX1013_64KB_SURFACE_ALIGNMENT;
        } else {
            mip0_width_blocks = layout->pitch / layout->block_width;
            mip0_height_blocks =
                layout->padded_height / layout->block_height;
            if (mip0_width_blocks >= mip0_height_blocks) {
                mip1_blocks = (mip0_height_blocks + 1u) >> 1u;
                if (mip1_blocks == 1u && ending_mip > 2u)
                    ++mip1_blocks;
                chain_height += mip1_blocks * layout->block_height;
            } else {
                mip1_blocks = (mip0_width_blocks + 1u) >> 1u;
                if (mip1_blocks == 1u && ending_mip > 2u)
                    ++mip1_blocks;
                chain_pitch += mip1_blocks * layout->block_width;
            }
            if (!agcGfx1013MulU64(chain_pitch, chain_height, &slice_size) ||
                !agcGfx1013MulU64(
                    slice_size, bytes_per_element, &slice_size) ||
                !agcGfx1013MulU64(
                    slice_size, input->sample_count, &slice_size))
                return false;
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

int32_t PS5_SYSV_ABI agcGfx1013GetHtileSubresourceLayout(
    const AgcGfx1013HtileLayoutInput *input, uint32_t mip_level,
    uint32_t layer, AgcGfx1013HtileSubresourceLayout *layout)
{
    AgcGfx1013HtileLayout aggregate;
    AgcGfx1013HtileSubresourceLayout result = {0};
    uint64_t offset;
    uint64_t mip_size = 0u;
    uint32_t mip;
    int32_t error;

    if (!input || !layout || mip_level >= input->mip_level_count ||
        layer >= input->layer_count)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013GetHtileLayout(input, &aggregate);
    if (error != AGC_OK)
        return error;

    result.width = input->width >> mip_level;
    result.height = input->height >> mip_level;
    if (result.width == 0u)
        result.width = 1u;
    if (result.height == 0u)
        result.height = 1u;
    result.pitch = agcGfx1013AlignPow2(
        result.width, aggregate.meta_block_width);
    result.padded_height = agcGfx1013AlignPow2(
        result.height, aggregate.meta_block_height);
    result.in_mip_tail = mip_level >= input->first_mip_in_tail;

    offset = input->first_mip_in_tail < input->mip_level_count ?
        aggregate.meta_block_size : 0u;
    if (result.in_mip_tail) {
        result.offset = (uint64_t)layer * aggregate.slice_size;
        result.size = aggregate.meta_block_size;
        *layout = result;
        return AGC_OK;
    }

    for (mip = input->first_mip_in_tail; mip-- > 0u;) {
        uint32_t width = input->width >> mip;
        uint32_t height = input->height >> mip;
        uint64_t blocks;

        if (width == 0u)
            width = 1u;
        if (height == 0u)
            height = 1u;
        if (!agcGfx1013MulU64(
                agcGfx1013AlignPow2(width, aggregate.meta_block_width) /
                    aggregate.meta_block_width,
                agcGfx1013AlignPow2(height, aggregate.meta_block_height) /
                    aggregate.meta_block_height,
                &blocks) ||
            !agcGfx1013MulU64(
                blocks, aggregate.meta_block_size, &mip_size))
            return AGC_ERROR_INVALID_ARGUMENT;
        if (mip == mip_level) {
            result.offset = (uint64_t)layer * aggregate.slice_size + offset;
            result.size = mip_size;
            *layout = result;
            return AGC_OK;
        }
        if (offset > UINT64_MAX - mip_size)
            return AGC_ERROR_INVALID_ARGUMENT;
        offset += mip_size;
    }
    return AGC_ERROR_INTERNAL;
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
    uint32_t expclear_aspects;
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
        (state->expclear_aspects &
         ~(AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH |
           AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL)) != 0u ||
        (state->expclear_aspects != 0u && !state->allow_expclear) ||
        (state->allow_expclear && !state->htile_enable) ||
        (state->htile_stencil_disable && !state->htile_enable) ||
        !agcGfx1013DepthSampleLog2(state->sample_count, &sample_log2))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcGfx1013DepthFormatInfo(
            state->format, &z_format, &stencil_format))
        return AGC_ERROR_NOT_SUPPORTED;
    expclear_aspects = state->expclear_aspects;
    if (state->allow_expclear && expclear_aspects == 0u) {
        if (z_format != 0u)
            expclear_aspects |= AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH;
        if (stencil_format != 0u && !state->htile_stencil_disable)
            expclear_aspects |= AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL;
    }
    if (((expclear_aspects &
          AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH) != 0u && z_format == 0u) ||
        ((expclear_aspects &
          AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL) != 0u &&
         (stencil_format == 0u || state->htile_stencil_disable)))
        return AGC_ERROR_INVALID_ARGUMENT;

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
    if ((state->depth_swizzle_mode == AGC_GFX1013_SWIZZLE_64KB_Z_X &&
         ((state->depth_read_address |
           state->depth_write_address) &
          (AGC_GFX1013_64KB_SURFACE_ALIGNMENT - 1u)) != 0u) ||
        (state->stencil_swizzle_mode == AGC_GFX1013_SWIZZLE_64KB_Z_X &&
         ((state->stencil_read_address |
           state->stencil_write_address) &
          (AGC_GFX1013_64KB_SURFACE_ALIGNMENT - 1u)) != 0u))
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
        AGC_REG_SET(DB_Z_INFO, ZRANGE_PRECISION,
            state->htile_enable &&
            (state->format == AGC_GFX1013_DEPTH_FORMAT_D16_UNORM ||
             state->format ==
                 AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT)) |
        AGC_REG_SET(DB_Z_INFO, DECOMPRESS_ON_N_ZPLANES,
            (expclear_aspects &
             AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH) != 0u &&
            (state->format == AGC_GFX1013_DEPTH_FORMAT_D16_UNORM ||
             state->format ==
                 AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT) ? 15u : 0u) |
        AGC_REG_SET(DB_Z_INFO, ALLOW_EXPCLEAR,
            (expclear_aspects &
             AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH) != 0u) |
        AGC_REG_SET(DB_Z_INFO, TILE_SURFACE_ENABLE, state->htile_enable);
    stencil_info = AGC_REG_SET(DB_STENCIL_INFO, FORMAT, stencil_format) |
        AGC_REG_SET(DB_STENCIL_INFO, SW_MODE, state->stencil_swizzle_mode) |
        AGC_REG_SET(DB_STENCIL_INFO, ALLOW_EXPCLEAR,
            (expclear_aspects &
             AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL) != 0u) |
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
        !agcGfx1013EmitCx(cb, AGC_REG_DB_HTILE_SURFACE,
            AGC_REG_SET(DB_HTILE_SURFACE, PIPE_ALIGNED,
                state->htile_enable)) ||
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

int32_t PS5_SYSV_ABI agcGfx1013SetHtileOperation(
    SceAgcCb *cb, AgcGfx1013HtileOperation operation)
{
    uint32_t value = 0u;

    if (!cb || operation >= AGC_GFX1013_HTILE_OPERATION_COUNT)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_HTILE_OPERATION_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    switch (operation) {
    case AGC_GFX1013_HTILE_OPERATION_NONE:
        break;
    case AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH:
        value = AGC_REG_SET(DB_RENDER_CONTROL, DEPTH_COMPRESS_DISABLE, 1u) |
            AGC_REG_SET(DB_RENDER_CONTROL, DECOMPRESS_ENABLE, 1u);
        break;
    case AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH_STENCIL:
        value = AGC_REG_SET(DB_RENDER_CONTROL, STENCIL_COMPRESS_DISABLE, 1u) |
            AGC_REG_SET(DB_RENDER_CONTROL, DEPTH_COMPRESS_DISABLE, 1u) |
            AGC_REG_SET(DB_RENDER_CONTROL, DECOMPRESS_ENABLE, 1u);
        break;
    case AGC_GFX1013_HTILE_OPERATION_RESUMMARIZE_DEPTH:
        value = AGC_REG_SET(DB_RENDER_CONTROL, RESUMMARIZE_ENABLE, 1u);
        break;
    default:
        return AGC_ERROR_INVALID_ARGUMENT;
    }

    if (!agcGfx1013EmitCx(cb, AGC_REG_DB_RENDER_CONTROL, value))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetDepthExpclear(
    SceAgcCb *cb, const AgcGfx1013DepthExpclearState *state)
{
    uint32_t clear_bits;

    if (!cb || !state)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (state->clear_depth != 0.0f && state->clear_depth != 1.0f)
        return AGC_ERROR_NOT_SUPPORTED;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_DEPTH_EXPCLEAR_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    memcpy(&clear_bits, &state->clear_depth, sizeof(clear_bits));
    if (!agcGfx1013EmitCx(cb, AGC_REG_DB_DEPTH_CLEAR, clear_bits))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetDepthStencilExpclear(
    SceAgcCb *cb, const AgcGfx1013DepthStencilExpclearState *state)
{
    const uint32_t valid_aspects =
        AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH |
        AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL;
    uint32_t *cmd;
    uint32_t depth_bits;

    if (!cb || !state || state->aspects == 0u ||
        (state->aspects & ~valid_aspects) != 0u ||
        ((state->aspects & AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH) != 0u &&
         state->clear_depth != 0.0f && state->clear_depth != 1.0f) ||
        ((state->aspects & AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL) != 0u &&
         state->clear_stencil > 0xffu))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) <
        ((state->aspects == valid_aspects) ? 4u : 3u))
        return AGC_ERROR_BUFFER_TOO_SMALL;

    memcpy(&depth_bits, &state->clear_depth, sizeof(depth_bits));
    if (state->aspects == valid_aspects) {
        cmd = agcCbAllocDwords(cb, 4u);
        if (!cmd)
            return AGC_ERROR_INTERNAL;
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
        cmd[1] = AGC_REG_DB_STENCIL_CLEAR;
        cmd[2] = state->clear_stencil;
        cmd[3] = depth_bits;
    } else if (state->aspects ==
            AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH) {
        if (!agcGfx1013EmitCx(cb, AGC_REG_DB_DEPTH_CLEAR, depth_bits))
            return AGC_ERROR_INTERNAL;
    } else if (!agcGfx1013EmitCx(
            cb, AGC_REG_DB_STENCIL_CLEAR, state->clear_stencil)) {
        return AGC_ERROR_INTERNAL;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013BuildHtileExpclearPlan(
    const AgcGfx1013HtileExpclearPlanState *state,
    AgcGfx1013HtileExpclearPlan *plan)
{
    AgcGfx1013HtileExpclearPlan result = {0};
    const uint32_t valid_aspects =
        AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH |
        AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL;

    if (!state || !plan || state->aspects == 0u ||
        (state->aspects & ~valid_aspects) != 0u ||
        state->has_stencil > 1u ||
        ((state->aspects & AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL) != 0u &&
         (!state->has_stencil || state->clear_stencil > 0xffu)) ||
        ((state->aspects & AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH) != 0u &&
         state->clear_depth != 0.0f && state->clear_depth != 1.0f))
        return AGC_ERROR_INVALID_ARGUMENT;

    if (!state->has_stencil) {
        if (state->aspects != AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH)
            return AGC_ERROR_INVALID_ARGUMENT;
        result.write_value = state->clear_depth == 1.0f ?
            AGC_GFX1013_HTILE_CLEAR_DEPTH_ONE :
            AGC_GFX1013_HTILE_CLEAR_DEPTH_ZERO;
        result.write_mask = UINT32_MAX;
        result.hardware_enabled = 1u;
        *plan = result;
        return AGC_OK;
    }

    if ((state->aspects &
         AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH) != 0u) {
        result.write_mask |= AGC_GFX1013_HTILE_DEPTH_ASPECT_MASK;
        if (state->clear_depth == 1.0f)
            result.write_value |= 0xfffc0000u;
    }
    if ((state->aspects &
         AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL) != 0u) {
        result.write_mask |= AGC_GFX1013_HTILE_STENCIL_ASPECT_MASK;
        /* The actual S8 clear value lives in DB_STENCIL_CLEAR. HTILE records
         * the cleared pretest state, independent of that 8-bit value. */
        result.write_value |= 0x000000f0u;
    }
    result.requires_read_modify_write = result.write_mask != UINT32_MAX;
    result.hardware_enabled =
        AGC_GFX1013_COMBINED_HTILE_EXPCLEAR_ENABLED;
    *plan = result;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013ApplyPolygonMode(
    AgcGfx1013FrameState *state, AgcGfx1013PolygonMode mode)
{
    const uint32_t polygon_mask =
        (AGC_REG_PA_SU_SC_MODE_CNTL_POLY_MODE_MASK <<
            AGC_REG_PA_SU_SC_MODE_CNTL_POLY_MODE_SHIFT) |
        (AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_FRONT_PTYPE_MASK <<
            AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_FRONT_PTYPE_SHIFT) |
        (AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_BACK_PTYPE_MASK <<
            AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_BACK_PTYPE_SHIFT);
    uint32_t primitive_type;
    uint32_t polygon_mode;

    if (!state || mode >= AGC_GFX1013_POLYGON_MODE_COUNT)
        return AGC_ERROR_INVALID_ARGUMENT;
    polygon_mode = mode == AGC_GFX1013_POLYGON_MODE_FILL ? 0u : 1u;
    primitive_type = mode == AGC_GFX1013_POLYGON_MODE_FILL ? 2u :
        mode == AGC_GFX1013_POLYGON_MODE_LINE ? 1u : 0u;
    state->raster_mode_control =
        (state->raster_mode_control & ~polygon_mask) |
        AGC_REG_SET(PA_SU_SC_MODE_CNTL, POLY_MODE, polygon_mode) |
        AGC_REG_SET(PA_SU_SC_MODE_CNTL, POLYMODE_FRONT_PTYPE,
            primitive_type) |
        AGC_REG_SET(PA_SU_SC_MODE_CNTL, POLYMODE_BACK_PTYPE,
            primitive_type);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013GetPrimitiveType(
    AgcGfx1013PrimitiveTopology topology, uint32_t *primitive_type)
{
    static const uint8_t types[AGC_GFX1013_TOPOLOGY_COUNT] = {
        1u, 2u, 3u, 4u, 5u, 9u,
    };

    if (!primitive_type || (uint32_t)topology >= AGC_GFX1013_TOPOLOGY_COUNT)
        return AGC_ERROR_INVALID_ARGUMENT;
    *primitive_type = types[topology];
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetPrimitiveSizeState(
    SceAgcCb *cb, const AgcGfx1013PrimitiveSizeState *state)
{
    uint32_t point_size;
    uint32_t point_min;
    uint32_t point_max;
    uint32_t line_width;
    uint32_t *cmd;

    if (!cb || !state || !(state->point_size >= 0.125f) ||
        !(state->point_size <= 8191.875f) ||
        !(state->point_size_min >= 0.0f) ||
        !(state->point_size_min <= state->point_size_max) ||
        !(state->point_size_max <= 8191.875f) ||
        !(state->line_width >= 0.125f) ||
        !(state->line_width <= 8191.875f))
        return AGC_ERROR_INVALID_ARGUMENT;
    point_size = (uint32_t)(state->point_size * 8.0f + 0.5f);
    point_min = (uint32_t)(state->point_size_min * 8.0f + 0.5f);
    point_max = (uint32_t)(state->point_size_max * 8.0f + 0.5f);
    line_width = (uint32_t)(state->line_width * 8.0f + 0.5f);
    if (point_size > 0xffffu || point_min > 0xffffu ||
        point_max > 0xffffu || line_width > 0xffffu)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    cmd = agcCbAllocDwords(cb, AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG,
        AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS);
    cmd[1] = AGC_REG_PA_SU_POINT_SIZE;
    cmd[2] = point_size | (point_size << 16u);
    cmd[3] = point_min | (point_max << 16u);
    cmd[4] = line_width;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetViewport(
    SceAgcCb *cb, const AgcGfx1013ViewportState *state)
{
    uint32_t *cmd;
    uint32_t regs[6];

    if (!cb || !state || state->width == 0u || state->height == 0u ||
        state->depth_clip_space > AGC_GFX1013_CLIP_SPACE_ZERO_TO_ONE ||
        state->width > 0x7fffu || state->height > 0x7fffu)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcCbRemainingDwords(cb) < 15u)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    regs[0] = agcGfx1013FloatBits((float)state->width * 0.5f);
    regs[1] = agcGfx1013FloatBits((float)state->width * 0.5f);
    regs[2] = agcGfx1013FloatBits(-(float)state->height * 0.5f);
    regs[3] = agcGfx1013FloatBits((float)state->height * 0.5f);
    if (state->depth_clip_space == AGC_GFX1013_CLIP_SPACE_ZERO_TO_ONE) {
        regs[4] = agcGfx1013FloatBits(1.0f);
        regs[5] = agcGfx1013FloatBits(0.0f);
    } else {
        regs[4] = agcGfx1013FloatBits(0.5f);
        regs[5] = agcGfx1013FloatBits(0.5f);
    }

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

static bool agcGfx1013FiniteFloat(float value)
{
    return (agcGfx1013FloatBits(value) & 0x7f800000u) != 0x7f800000u;
}

int32_t PS5_SYSV_ABI agcGfx1013SetViewportArray(
    SceAgcCb *cb, const AgcGfx1013ViewportArrayState *state)
{
    uint32_t *cmd;
    uint32_t union_left = 0x7fffu;
    uint32_t union_top = 0x7fffu;
    uint32_t union_right = 0u;
    uint32_t union_bottom = 0u;
    uint32_t i;

    if (!cb || !state || state->count == 0u ||
        state->count > AGC_GFX1013_MAX_VIEWPORTS)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (i = 0u; i < state->count; ++i) {
        const AgcGfx1013Viewport *viewport = &state->viewports[i];
        const AgcGfx1013ScissorState *scissor = &state->scissors[i];
        if (!agcGfx1013FiniteFloat(viewport->x) ||
            !agcGfx1013FiniteFloat(viewport->y) ||
            !agcGfx1013FiniteFloat(viewport->width) ||
            !agcGfx1013FiniteFloat(viewport->height) ||
            !agcGfx1013FiniteFloat(viewport->min_depth) ||
            !agcGfx1013FiniteFloat(viewport->max_depth) ||
            !(viewport->width > 0.0f) || !(viewport->height > 0.0f) ||
            !(viewport->min_depth >= 0.0f) ||
            !(viewport->max_depth <= 1.0f) ||
            viewport->min_depth > viewport->max_depth ||
            scissor->left >= scissor->right ||
            scissor->top >= scissor->bottom ||
            scissor->right > 0x7fffu || scissor->bottom > 0x7fffu)
            return AGC_ERROR_INVALID_ARGUMENT;
        if (scissor->left < union_left)
            union_left = scissor->left;
        if (scissor->top < union_top)
            union_top = scissor->top;
        if (scissor->right > union_right)
            union_right = scissor->right;
        if (scissor->bottom > union_bottom)
            union_bottom = scissor->bottom;
    }
    if (agcCbRemainingDwords(cb) <
        AGC_GFX1013_VIEWPORT_ARRAY_DWORDS(state->count))
        return AGC_ERROR_BUFFER_TOO_SMALL;

    cmd = agcCbAllocDwords(cb, 2u + state->count * 6u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(
        AGC_PM4_OP_SET_CONTEXT_REG, 2u + state->count * 6u);
    cmd[1] = AGC_REG_PA_CL_VPORT_XSCALE;
    for (i = 0u; i < state->count; ++i) {
        const AgcGfx1013Viewport *viewport = &state->viewports[i];
        cmd[2u + i * 6u] = agcGfx1013FloatBits(viewport->width * 0.5f);
        cmd[3u + i * 6u] = agcGfx1013FloatBits(
            viewport->x + viewport->width * 0.5f);
        cmd[4u + i * 6u] = agcGfx1013FloatBits(viewport->height * 0.5f);
        cmd[5u + i * 6u] = agcGfx1013FloatBits(
            viewport->y + viewport->height * 0.5f);
        cmd[6u + i * 6u] = agcGfx1013FloatBits(
            viewport->max_depth - viewport->min_depth);
        cmd[7u + i * 6u] = agcGfx1013FloatBits(viewport->min_depth);
    }

    cmd = agcCbAllocDwords(cb, 2u + state->count * 2u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(
        AGC_PM4_OP_SET_CONTEXT_REG, 2u + state->count * 2u);
    cmd[1] = AGC_REG_PA_SC_VPORT_ZMIN_0;
    for (i = 0u; i < state->count; ++i) {
        cmd[2u + i * 2u] = agcGfx1013FloatBits(
            state->viewports[i].min_depth);
        cmd[3u + i * 2u] = agcGfx1013FloatBits(
            state->viewports[i].max_depth);
    }
    if (!agcGfx1013EmitCx(cb, AGC_REG_PA_CL_VTE_CNTL, 0x0000043fu))
        return AGC_ERROR_INTERNAL;

#define AGC_EMIT_SCISSOR_PAIR(offset, left, top, right, bottom) do { \
    cmd = agcCbAllocDwords(cb, 4u); \
    if (!cmd) \
        return AGC_ERROR_INTERNAL; \
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u); \
    cmd[1] = (offset); \
    cmd[2] = (left) | ((top) << 16u); \
    cmd[3] = (right) | ((bottom) << 16u); \
} while (0)
    AGC_EMIT_SCISSOR_PAIR(AGC_REG_PA_SC_SCREEN_SCISSOR_TL,
        union_left, union_top, union_right, union_bottom);
    AGC_EMIT_SCISSOR_PAIR(AGC_REG_PA_SC_WINDOW_SCISSOR_TL,
        union_left, union_top, union_right, union_bottom);
    AGC_EMIT_SCISSOR_PAIR(AGC_REG_PA_SC_GENERIC_SCISSOR_TL,
        union_left, union_top, union_right, union_bottom);
#undef AGC_EMIT_SCISSOR_PAIR

    cmd = agcCbAllocDwords(cb, 2u + state->count * 2u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(
        AGC_PM4_OP_SET_CONTEXT_REG, 2u + state->count * 2u);
    cmd[1] = AGC_REG_PA_SC_VPORT_SCISSOR_0_TL;
    for (i = 0u; i < state->count; ++i) {
        cmd[2u + i * 2u] = state->scissors[i].left |
            (state->scissors[i].top << 16u);
        cmd[3u + i * 2u] = state->scissors[i].right |
            (state->scissors[i].bottom << 16u);
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
    static const uint8_t rop3[AGC_GFX1013_LOGIC_OP_COUNT] = {
        0x00u, 0x88u, 0x44u, 0xccu,
        0x22u, 0xaau, 0x66u, 0xeeu,
        0x11u, 0x99u, 0x55u, 0xddu,
        0x33u, 0xbbu, 0x77u, 0xffu,
    };
    uint32_t controls[AGC_GFX1013_MAX_COLOR_TARGETS] = {0};
    uint32_t blend_optimizations[AGC_GFX1013_MAX_COLOR_TARGETS] = {0};
    uint32_t target_mask = 0u;
    uint32_t color_control;
    bool dual_source_blend = false;
    uint32_t *cmd;
    uint32_t i;
    static const uint8_t optimization_operations[AGC_GFX1013_BLEND_OP_COUNT] = {
        1u, 2u, 3u, 4u, 5u,
    };

    if (!cb || !state || state->target_count == 0u ||
        state->target_count > AGC_GFX1013_MAX_COLOR_TARGETS ||
        state->logic_enable > 1u ||
        state->logic_operation >= AGC_GFX1013_LOGIC_OP_COUNT)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (i = 0u; i < AGC_GFX1013_MAX_COLOR_TARGETS; ++i)
        blend_optimizations[i] = 0x06770677u;
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
        const bool target_uses_dual_source = target->enable &&
            ((target->color_source >= AGC_GFX1013_BLEND_SRC1_COLOR &&
              target->color_source <= AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA) ||
             (target->color_destination >= AGC_GFX1013_BLEND_SRC1_COLOR &&
              target->color_destination <= AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA) ||
             (target->alpha_source >= AGC_GFX1013_BLEND_SRC1_COLOR &&
              target->alpha_source <= AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA) ||
             (target->alpha_destination >= AGC_GFX1013_BLEND_SRC1_COLOR &&
              target->alpha_destination <= AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA));
        if (target_uses_dual_source && i != 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        dual_source_blend |= target_uses_dual_source;
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
        const uint32_t color_operation =
            target->enable && !state->logic_enable ?
            optimization_operations[target->color_operation] : 6u;
        const uint32_t alpha_operation =
            target->enable && !state->logic_enable ?
            optimization_operations[target->alpha_operation] : 6u;
        blend_optimizations[i] =
            (7u << AGC_REG_SX_MRT0_BLEND_OPT_COLOR_SRC_OPT_SHIFT) |
            (7u << AGC_REG_SX_MRT0_BLEND_OPT_COLOR_DST_OPT_SHIFT) |
            (color_operation <<
                AGC_REG_SX_MRT0_BLEND_OPT_COLOR_COMB_FCN_SHIFT) |
            (7u << AGC_REG_SX_MRT0_BLEND_OPT_ALPHA_SRC_OPT_SHIFT) |
            (7u << AGC_REG_SX_MRT0_BLEND_OPT_ALPHA_DST_OPT_SHIFT) |
            (alpha_operation <<
                AGC_REG_SX_MRT0_BLEND_OPT_ALPHA_COMB_FCN_SHIFT);
        target_mask |= target->write_mask << (i * 4u);
    }
    if (dual_source_blend)
        memset(blend_optimizations, 0, sizeof(blend_optimizations));
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_BLEND_STATE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    cmd = agcCbAllocDwords(cb, 10u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 10u);
    cmd[1] = AGC_REG_CB_BLEND0_CONTROL;
    memcpy(&cmd[2], controls, sizeof(controls));
    cmd = agcCbAllocDwords(cb, 10u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 10u);
    cmd[1] = AGC_REG_SX_MRT0_BLEND_OPT;
    memcpy(&cmd[2], blend_optimizations, sizeof(blend_optimizations));
    if (!agcGfx1013EmitCx(cb, AGC_REG_CB_TARGET_MASK, target_mask))
        return AGC_ERROR_INTERNAL;
    cmd = agcCbAllocDwords(cb, 6u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 6u);
    cmd[1] = AGC_REG_CB_BLEND_RED;
    for (i = 0u; i < 4u; ++i)
        cmd[i + 2u] = agcGfx1013FloatBits(state->constants[i]);
    color_control = (1u << AGC_REG_CB_COLOR_CONTROL_MODE_SHIFT) |
        ((uint32_t)(state->logic_enable ? rop3[state->logic_operation] :
            rop3[AGC_GFX1013_LOGIC_COPY]) <<
            AGC_REG_CB_COLOR_CONTROL_ROP3_SHIFT);
    if (dual_source_blend)
        color_control |=
            1u << AGC_REG_CB_COLOR_CONTROL_DISABLE_DUAL_QUAD_SHIFT;
    if (!agcGfx1013EmitCx(cb, AGC_REG_CB_COLOR_CONTROL, color_control))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013SetDepthBiasState(
    SceAgcCb *cb, const AgcGfx1013DepthBiasState *state)
{
    uint32_t format_control;
    uint32_t *cmd;

    if (!cb || !state)
        return AGC_ERROR_INVALID_ARGUMENT;
    switch (state->format) {
    case AGC_GFX1013_DEPTH_FORMAT_D16_UNORM:
    case AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT:
        format_control = (uint8_t)-16;
        break;
    case AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT:
    case AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT:
        format_control = (uint8_t)-23 | (1u << 8u);
        break;
    default:
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_DEPTH_BIAS_STATE_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    cmd = agcCbAllocDwords(cb, 3u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u);
    cmd[1] = AGC_REG_PA_SU_POLY_OFFSET_DB_FMT_CNTL;
    cmd[2] = format_control;
    cmd = agcCbAllocDwords(cb, 7u);
    if (!cmd)
        return AGC_ERROR_INTERNAL;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 7u);
    cmd[1] = AGC_REG_PA_SU_POLY_OFFSET_CLAMP;
    cmd[2] = agcGfx1013FloatBits(state->clamp);
    cmd[3] = agcGfx1013FloatBits(state->slope_factor * 16.0f);
    cmd[4] = agcGfx1013FloatBits(state->constant_factor);
    cmd[5] = cmd[3];
    cmd[6] = cmd[4];
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
    uint32_t target_count;
    uint32_t i;
    int32_t error;

    if (!state)
        return AGC_ERROR_INVALID_ARGUMENT;
    target_count = state->color_target_count == 0u ?
        1u : state->color_target_count;
    if (target_count > AGC_GFX1013_MAX_COLOR_TARGETS ||
        state->min_vertex_index > state->max_vertex_index ||
        state->instance_step_rate == 0u ||
        state->scissor.right > state->color_target.width ||
        state->scissor.bottom > state->color_target.height)
        return AGC_ERROR_INVALID_ARGUMENT;

    for (i = 0u; i < target_count; ++i) {
        const AgcGfx1013ColorTargetState *target = i == 0u ?
            &state->color_target : &state->additional_color_targets[i - 1u];
        if (target->width != state->color_target.width ||
            target->height != state->color_target.height ||
            state->scissor.right > target->width ||
            state->scissor.bottom > target->height)
            return AGC_ERROR_INVALID_ARGUMENT;
        agcCbInit(&probe, scratch, sizeof(scratch));
        error = agcGfx1013SetColorTargetSlot(&probe, i, target);
        if (error != AGC_OK)
            return error;
    }
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
    uint32_t target_count;
    uint32_t required_dwords;
    int32_t error;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    error = agcGfx1013ValidateFrameState(state);
    if (error != AGC_OK)
        return error;
    target_count = state->color_target_count == 0u ?
        1u : state->color_target_count;
    required_dwords = AGC_GFX1013_FRAME_PROLOGUE_BASE_DWORDS +
        (target_count - 1u) * 28u;
    if (agcCbRemainingDwords(cb) < required_dwords)
        return AGC_ERROR_BUFFER_TOO_SMALL;

    if (agcGfx1013SetContextControl(
            cb, state->context_load_control,
            state->context_shadow_control) != AGC_OK ||
        !sceAgcDcbClearState(cb, state->clear_state_flags))
        return AGC_ERROR_INTERNAL;
    error = agcGfx1013ApplyGraphicsDefaultsV8(cb, &counts);
    if (error != AGC_OK)
        return AGC_ERROR_INTERNAL;
    for (i = 0u; i < target_count; ++i) {
        const AgcGfx1013ColorTargetState *target = i == 0u ?
            &state->color_target : &state->additional_color_targets[i - 1u];
        if (agcGfx1013SetColorTargetSlot(cb, i, target) != AGC_OK)
            return AGC_ERROR_INTERNAL;
    }
    if (agcGfx1013SetViewport(cb, &state->viewport) != AGC_OK ||
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
    AgcGfx1013ColorTargetFormatInfo format_info;
    uint32_t target_count;
    uint32_t export_format = 0u;
    uint32_t i;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    int32_t error = agcGfx1013ValidateFrameState(state);
    if (error != AGC_OK)
        return error;
    target_count = state->color_target_count == 0u ?
        1u : state->color_target_count;
    for (i = 0u; i < target_count; ++i) {
        const AgcGfx1013ColorTargetState *target = i == 0u ?
            &state->color_target : &state->additional_color_targets[i - 1u];
        if (!agcGfx1013FindColorTargetFormat(target, &format_info))
            return AGC_ERROR_NOT_SUPPORTED;
        export_format |= format_info.spi_shader_export_format << (i * 4u);
    }
    if (agcCbRemainingDwords(cb) < AGC_GFX1013_FRAME_POST_BIND_DWORDS)
        return AGC_ERROR_BUFFER_TOO_SMALL;
    if (agcGfx1013SetDepthDisabled(cb) != AGC_OK ||
        !agcGfx1013EmitCx(cb, AGC_REG_SPI_SHADER_COL_FORMAT,
            export_format) ||
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

static bool agcGfx1013IsResourcePlaceholder(uint32_t value);

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
    AgcGfx1013ShaderBinding shader;
    uint32_t value;
    uint32_t i;
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
    shader.record = state->record;
    shader.sh_registers = state->sh_registers;
    shader.num_sh_registers = state->num_sh_registers;
    shader.cx_registers = NULL;
    shader.num_cx_registers = 0u;
    shader.code_address = state->code_address;
    if (state->num_resource_tables != 0u) {
        int32_t result;
        if (!state->resource_tables)
            return AGC_ERROR_INVALID_ARGUMENT;
        result = agcGfx1013ValidateResourceTables(&shader,
            state->resource_tables, state->num_resource_tables, NULL);
        if (result != AGC_OK)
            return result;
    } else {
        for (i = 0u; i < state->num_sh_registers; ++i) {
            if (agcGfx1013IsResourcePlaceholder(
                    state->sh_registers[i].value))
                return AGC_ERROR_RESOURCE_NOT_BOUND;
        }
    }
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
    AgcGfx1013ShaderBinding shader;
    uint32_t limits0[3] = {0x3fffffffu, 0xffffffffu, 0xffffffffu};
    uint32_t limits1[2] = {0xffffffffu, 0xffffffffu};
    uint32_t threads[6];
    uint32_t program[2];
    uint32_t resources[2];
    uint32_t resource3;
    uint32_t *dispatch;
    int32_t result;
    uint32_t resource_count = 0u;
    uint32_t required_dwords;

    if (!cb)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGfx1013ValidateCompute(state);
    if (result != AGC_OK)
        return result;
    shader.record = state->record;
    shader.sh_registers = state->sh_registers;
    shader.num_sh_registers = state->num_sh_registers;
    shader.cx_registers = NULL;
    shader.num_cx_registers = 0u;
    shader.code_address = state->code_address;
    if (state->num_resource_tables != 0u) {
        result = agcGfx1013ValidateResourceTables(&shader,
            state->resource_tables, state->num_resource_tables,
            &resource_count);
        if (result != AGC_OK)
            return result;
    }
    required_dwords = 38u + state->num_user_data + resource_count * 3u;
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
    if (state->num_resource_tables != 0u) {
        result = agcGfx1013BindResourceTables(cb, &shader,
            state->resource_tables, state->num_resource_tables);
        if (result != AGC_OK)
            return result;
    }
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

int32_t PS5_SYSV_ABI agcGfx1013RmwHtile(
    SceAgcCb *cb, const AgcGfx1013HtileRmwState *state)
{
    AgcGfx1013ResourceTransition before = {
        AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE,
        AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE, 0u, 0u,
    };
    AgcGfx1013ResourceTransition after = {
        AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE,
        AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE, 0u, 0u,
    };
    AgcGfx1013ComputeState compute = {0};
    uint32_t user_data[7];
    uint32_t before_dwords;
    uint32_t after_dwords;
    uint32_t rsrc2;
    uint32_t word_count;
    uint64_t end;
    uint64_t address;
    uint64_t required_dwords;
    int32_t error;

    if (!cb || !state || !state->record || !state->sh_registers ||
        !state->subresource || !state->plan ||
        state->htile_address == 0u || state->htile_allocation_size == 0u ||
        state->subresource->size == 0u || state->plan->write_mask == 0u ||
        state->plan->requires_read_modify_write > 1u ||
        state->plan->hardware_enabled > 1u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (state->subresource->in_mip_tail)
        return AGC_ERROR_NOT_SUPPORTED;
    if ((state->htile_address & 3u) != 0u ||
        (state->subresource->offset & 3u) != 0u ||
        (state->subresource->size & 3u) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (state->subresource->offset > state->htile_allocation_size ||
        state->subresource->size >
            state->htile_allocation_size - state->subresource->offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (state->htile_address > UINT64_MAX - state->subresource->offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    address = state->htile_address + state->subresource->offset;
    end = address + state->subresource->size;
    if (end < address || (address >> 48u) != 0u || (end >> 48u) != 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    if (state->subresource->size / sizeof(uint32_t) > UINT32_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    word_count = (uint32_t)(
        state->subresource->size / sizeof(uint32_t));

    compute.record = state->record;
    compute.sh_registers = state->sh_registers;
    compute.num_sh_registers = state->num_sh_registers;
    compute.code_address = state->code_address;
    compute.user_data = user_data;
    compute.num_user_data = 7u;
    compute.local_size_x = AGC_GFX1013_HTILE_RMW_LOCAL_SIZE;
    compute.local_size_y = 1u;
    compute.local_size_z = 1u;
    compute.group_count_x =
        (word_count + AGC_GFX1013_HTILE_RMW_LOCAL_SIZE - 1u) /
        AGC_GFX1013_HTILE_RMW_LOCAL_SIZE;
    compute.group_count_y = 1u;
    compute.group_count_z = 1u;

    error = agcGfx1013ValidateCompute(&compute);
    if (error != AGC_OK)
        return error;
    if (!agcGfx1013FindComputeRegister(
            &compute, AGC_REG_COMPUTE_PGM_RSRC2, &rsrc2) ||
        ((rsrc2 >> 1u) & 0x1fu) != 7u || (rsrc2 & 0x80u) == 0u)
        return AGC_ERROR_SHADER_INVALID;
    error = agcGfx1013GetResourceTransitionDwords(
        &before, &before_dwords);
    if (error != AGC_OK)
        return error;
    error = agcGfx1013GetResourceTransitionDwords(
        &after, &after_dwords);
    if (error != AGC_OK)
        return error;
    required_dwords = (uint64_t)before_dwords + 38u +
        compute.num_user_data + after_dwords;
    if (required_dwords != AGC_GFX1013_HTILE_RMW_DWORDS)
        return AGC_ERROR_INTERNAL;
    if (required_dwords > agcCbRemainingDwords(cb))
        return AGC_ERROR_BUFFER_TOO_SMALL;

    user_data[0] = 0u;
    user_data[1] = 0u;
    user_data[2] = (uint32_t)address;
    user_data[3] = (uint32_t)(address >> 32u);
    user_data[4] = word_count;
    user_data[5] = state->plan->write_value;
    user_data[6] = state->plan->write_mask;

    error = agcGfx1013TransitionResource(cb, &before);
    if (error == AGC_OK)
        error = agcGfx1013DispatchCompute(cb, &compute);
    if (error == AGC_OK)
        error = agcGfx1013TransitionResource(cb, &after);
    return error;
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
    uint32_t offchip_granularity;
    uint32_t offchip_buffers;
    uint64_t required_offchip_size;

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
    offchip_granularity =
        AGC_GFX1013_TESS_OFFCHIP_BUFFER_DWORDS >>
        ((state->offchip_param >> 10u) & 3u);
    offchip_buffers = (state->offchip_param & 0x3ffu) + 1u;
    required_offchip_size =
        (uint64_t)offchip_granularity * 4u * offchip_buffers;
    if ((state->offchip_param & ~0xfffu) != 0u ||
        state->offchip_ring_size < required_offchip_size)
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

int32_t PS5_SYSV_ABI agcGfx1013BuildTessellationOffchipLayouts(
    const AgcGfx1013TessellationLayoutState *state,
    uint32_t *tcs_offchip_layout, uint32_t *tes_offchip_layout)
{
    uint32_t attribute_stride;
    uint32_t common;

    if (!state || !tcs_offchip_layout || !tes_offchip_layout ||
        state->patch_count == 0u || state->patch_count > 0x7fu ||
        state->input_control_points == 0u ||
        state->input_control_points > 32u ||
        state->output_control_points == 0u ||
        state->output_control_points > 32u ||
        state->vertex_output_count > 0x3fu ||
        state->control_output_count > 0x3fu ||
        state->primitive_mode == 0u || state->primitive_mode > 3u ||
        state->tes_reads_tess_factors > 1u)
        return AGC_ERROR_INVALID_ARGUMENT;
    attribute_stride =
        (state->patch_count * state->output_control_points * 16u + 255u) /
        256u;
    if (attribute_stride == 0u || attribute_stride > 0x1fu)
        return AGC_ERROR_INVALID_ARGUMENT;
    common = state->patch_count |
        (attribute_stride << 12u) |
        (state->vertex_output_count << 17u) |
        (state->control_output_count << 23u) |
        (state->primitive_mode << 29u) |
        (state->tes_reads_tess_factors << 31u);
    *tcs_offchip_layout = common |
        ((state->input_control_points - 1u) << 7u);
    *tes_offchip_layout = common |
        ((state->output_control_points - 1u) << 7u);
    return AGC_OK;
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
