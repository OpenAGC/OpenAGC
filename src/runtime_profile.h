#ifndef OPENAGC_RUNTIME_PROFILE_H
#define OPENAGC_RUNTIME_PROFILE_H

#include "agc_graphics.h"
#include "agc_registers.h"

#include <stdint.h>

static inline uint32_t agcRuntimeDepthClipControl(
    uint16_t firmware_abi_key, int depth_clip_disabled)
{
    uint32_t value = depth_clip_disabled ?
        AGC_GFX1013_DEPTH_CLIP_DISABLE_CONTROL :
        AGC_GFX1013_VULKAN_CLIP_CONTROL;

    /* Exact Zink replays established a firmware-profile distinction for
     * explicit Z clipping on standard gfx1013.  FW 5.50 requires linear
     * attribute clipping enabled, while FW 11.60 produces zero coverage with
     * that bit set and requires the otherwise identical control. */
    if (!depth_clip_disabled && firmware_abi_key == 0x1160u)
        value &= ~(1u <<
            AGC_REG_PA_CL_CLIP_CNTL_DX_LINEAR_ATTR_CLIP_ENA_SHIFT);
    return value;
}

#endif
