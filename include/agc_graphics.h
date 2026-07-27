#ifndef _AGC_GRAPHICS_H_
#define _AGC_GRAPHICS_H_

#include <stdint.h>

#include "agcdriver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN    0x00008000u
#define AGC_GFX1013_VGT_SHADER_STAGES_EN_HS_W32_EN 0x00200000u
#define AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN 0x00400000u

#define AGC_GFX1013_COLOR_FORMAT_8_8_8_8       0x0Au
#define AGC_GFX1013_COLOR_FORMAT_16_16_16_16   0x0Cu
#define AGC_GFX1013_SURFACE_NUMBER_UNORM        0u
#define AGC_GFX1013_SURFACE_NUMBER_FLOAT        7u
#define AGC_GFX1013_SURFACE_SWAP_STD            0u
#define AGC_GFX1013_SURFACE_SWAP_ALT            1u
#define AGC_GFX1013_TARGET_MASK_RGBA0            0x0Fu

#define AGC_GFX1013_TESS_FACTOR_RING_SLOT         5u
#define AGC_GFX1013_TESS_OFFCHIP_RING_SLOT        6u
#define AGC_GFX1013_TESS_RING_DESCRIPTOR_DWORDS   4u
#define AGC_GFX1013_TESS_RING_TABLE_DWORDS       32u
#define AGC_GFX1013_TESS_OFFCHIP_RING_SIZE   0x8000u
#define AGC_GFX1013_TESS_FACTOR_RING_SIZE   0x10000u
#define AGC_GFX1013_TESS_OFFCHIP_PARAM            0u
#define AGC_GFX1013_TESS_OFFCHIP_LAYOUT   0x21042108u
#define AGC_GFX1013_RAW_R32_DESCRIPTOR_WORD3 0x31016FACu

#define AGC_GFX1013_EOP_FENCE_DWORDS             10u
#define AGC_GFX1013_EOP_CACHE_FLUSH_EVENT      0x14u
#define AGC_GFX1013_EOP_GCR_CONTROL            0x603u
#define AGC_GFX1013_EOP_CACHE_POLICY_LRU          3u

typedef struct AgcGfx1013ShaderBinding {
    const AgcShaderRecord *record;
    const AgcRegisterValue *sh_registers;
    uint32_t num_sh_registers;
    const AgcRegisterValue *cx_registers;
    uint32_t num_cx_registers;
    uint64_t code_address;
} AgcGfx1013ShaderBinding;

typedef struct AgcGfx1013Wave32VsPsState {
    AgcGfx1013ShaderBinding primitive;
    AgcGfx1013ShaderBinding pixel;
    uint64_t primitive_back_code_address;
    uint32_t primitive_type;
} AgcGfx1013Wave32VsPsState;

typedef struct AgcGfx1013Wave32TessVsPsState {
    AgcGfx1013ShaderBinding hull;
    AgcGfx1013ShaderBinding primitive;
    AgcGfx1013ShaderBinding pixel;
    uint64_t hull_back_code_address;
    uint64_t primitive_back_code_address;
    uint64_t ring_descriptor_address;
    uint32_t tcs_offchip_layout;
    uint32_t primitive_type;
} AgcGfx1013Wave32TessVsPsState;

typedef struct AgcGfx1013ColorTargetState {
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t color_format;
    uint32_t number_type;
    uint32_t component_swap;
} AgcGfx1013ColorTargetState;

typedef struct AgcGfx1013ViewportState {
    uint32_t width;
    uint32_t height;
} AgcGfx1013ViewportState;

typedef struct AgcGfx1013ScissorState {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
} AgcGfx1013ScissorState;

typedef struct AgcGfx1013GraphicsDefaultStats {
    uint32_t sh_register_count;
    uint32_t cx_register_count;
    uint32_t uc_register_count;
} AgcGfx1013GraphicsDefaultStats;

typedef struct AgcGfx1013ComputeDefaultStats {
    uint32_t sh_register_count;
    uint32_t packet_count;
} AgcGfx1013ComputeDefaultStats;

typedef struct AgcGfx1013ComputeState {
    const AgcShaderRecord *record;
    const AgcRegisterValue *sh_registers;
    uint32_t num_sh_registers;
    uint64_t code_address;
    const uint32_t *user_data;
    uint32_t num_user_data;
    uint32_t local_size_x;
    uint32_t local_size_y;
    uint32_t local_size_z;
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
    uint32_t modifier;
} AgcGfx1013ComputeState;

typedef struct AgcGfx1013EopFenceState {
    uint64_t address;
    uint32_t value;
} AgcGfx1013EopFenceState;

#define AGC_GFX1013_ADDRESS32_HIGH 0x00000002u

typedef struct AgcGfx1013ResourceTableBinding {
    uint32_t placeholder;
    uint64_t address;
} AgcGfx1013ResourceTableBinding;

typedef struct AgcGfx1013TessellationRingTable {
    uint32_t words[AGC_GFX1013_TESS_RING_TABLE_DWORDS];
} AgcGfx1013TessellationRingTable;

typedef struct AgcGfx1013TessellationState {
    uint64_t offchip_ring_address;
    uint64_t factor_ring_address;
    uint32_t offchip_ring_size;
    uint32_t factor_ring_size;
    uint32_t offchip_param;
    uint32_t max_tess_level;
    uint32_t min_tess_level;
    uint32_t esgs_ring_itemsize;
    uint32_t distribution;
    uint32_t tf_param;
} AgcGfx1013TessellationState;

_Static_assert(sizeof(AgcGfx1013TessellationRingTable) == 128,
    "gfx1013 tessellation ring table must be 128 bytes");

typedef struct AgcGfx1013BaselineDrawState {
    AgcGfx1013Wave32VsPsState shaders;
    const AgcGfx1013ResourceTableBinding *primitive_resource_tables;
    uint32_t num_primitive_resource_tables;
    const AgcGfx1013ResourceTableBinding *pixel_resource_tables;
    uint32_t num_pixel_resource_tables;
    const AgcRegisterValue *post_bind_sh_registers;
    uint32_t num_post_bind_sh_registers;
    const AgcRegisterValue *post_bind_cx_registers;
    uint32_t num_post_bind_cx_registers;
    const AgcRegisterValue *post_bind_uc_registers;
    uint32_t num_post_bind_uc_registers;
    uint32_t index_type;
    uint32_t index_swap;
    uint32_t instance_count;
    uint32_t vertex_count;
    uint64_t draw_modifier;
} AgcGfx1013BaselineDrawState;

typedef struct AgcGfx1013TessDrawState {
    AgcGfx1013Wave32TessVsPsState shaders;
    const AgcGfx1013TessellationState *tessellation;
    const AgcGfx1013ResourceTableBinding *hull_resource_tables;
    uint32_t num_hull_resource_tables;
    const AgcGfx1013ResourceTableBinding *primitive_resource_tables;
    uint32_t num_primitive_resource_tables;
    const AgcGfx1013ResourceTableBinding *pixel_resource_tables;
    uint32_t num_pixel_resource_tables;
    const AgcRegisterValue *post_bind_sh_registers;
    uint32_t num_post_bind_sh_registers;
    const AgcRegisterValue *post_bind_cx_registers;
    uint32_t num_post_bind_cx_registers;
    const AgcRegisterValue *post_bind_uc_registers;
    uint32_t num_post_bind_uc_registers;
    uint32_t instance_count;
    uint32_t vertex_count;
    uint64_t draw_modifier;
} AgcGfx1013TessDrawState;

int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32VsPs(
    const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindWave32VsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013ValidateVsPs(
    const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindVsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndexAuto(
    SceAgcCb *cb, const AgcGfx1013BaselineDrawState *state);
int32_t PS5_SYSV_ABI agcGfx1013DrawTessIndexAuto(
    SceAgcCb *cb, const AgcGfx1013TessDrawState *state);
int32_t PS5_SYSV_ABI agcGfx1013SignalEopFence(
    SceAgcCb *cb, const AgcGfx1013EopFenceState *state);
int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32TessVsPs(
    const AgcGfx1013Wave32TessVsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindWave32TessVsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32TessVsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetColorTarget(
    SceAgcCb *cb, const AgcGfx1013ColorTargetState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetViewport(
    SceAgcCb *cb, const AgcGfx1013ViewportState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetScissor(
    SceAgcCb *cb, const AgcGfx1013ScissorState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetTargetMask(
    SceAgcCb *cb, uint32_t mask);
int32_t PS5_SYSV_ABI agcGfx1013SetDepthDisabled(SceAgcCb *cb);
int32_t PS5_SYSV_ABI agcGfx1013SetContextControl(
    SceAgcCb *cb, uint32_t load_control, uint32_t shadow_control);
int32_t PS5_SYSV_ABI agcGfx1013ValidateCompute(
    const AgcGfx1013ComputeState *state);
int32_t PS5_SYSV_ABI agcGfx1013ApplyComputeDefaultsV8(
    SceAgcCb *cb, AgcGfx1013ComputeDefaultStats *stats);
int32_t PS5_SYSV_ABI agcGfx1013DispatchCompute(
    SceAgcCb *cb, const AgcGfx1013ComputeState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindResourceTables(
    SceAgcCb *cb, const AgcGfx1013ShaderBinding *shader,
    const AgcGfx1013ResourceTableBinding *tables, uint32_t table_count);
int32_t PS5_SYSV_ABI agcGfx1013ValidateResourceTables(
    const AgcGfx1013ShaderBinding *shader,
    const AgcGfx1013ResourceTableBinding *tables, uint32_t table_count,
    uint32_t *binding_count);
int32_t PS5_SYSV_ABI agcGfx1013BuildTessellationRingTable(
    AgcGfx1013TessellationRingTable *table,
    const AgcGfx1013TessellationState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetTessellationRings(
    SceAgcCb *cb, const AgcGfx1013TessellationState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetTessellationContext(
    SceAgcCb *cb, const AgcGfx1013TessellationState *state);
int32_t PS5_SYSV_ABI agcGfx1013ApplyGraphicsDefaultsV8(
    SceAgcCb *cb, AgcGfx1013GraphicsDefaultStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_GRAPHICS_H_ */
