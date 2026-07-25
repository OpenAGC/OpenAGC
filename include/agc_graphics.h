#ifndef _AGC_GRAPHICS_H_
#define _AGC_GRAPHICS_H_

#include <stdint.h>

#include "agcdriver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN    0x00008000u
#define AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN 0x00400000u

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

int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32VsPs(
    const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindWave32VsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_GRAPHICS_H_ */
