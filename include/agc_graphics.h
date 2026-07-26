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

int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32VsPs(
    const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindWave32VsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013ValidateVsPs(
    const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindVsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state);
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
int32_t PS5_SYSV_ABI agcGfx1013ApplyGraphicsDefaultsV8(
    SceAgcCb *cb, AgcGfx1013GraphicsDefaultStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_GRAPHICS_H_ */
