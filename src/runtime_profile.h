#ifndef OPENAGC_RUNTIME_PROFILE_H
#define OPENAGC_RUNTIME_PROFILE_H

#include "agc_error.h"
#include "agc_graphics.h"

static inline uint32_t agcRuntimeDepthClipControl(int depth_clip_disabled)
{
    return depth_clip_disabled ?
        AGC_GFX1013_DEPTH_CLIP_DISABLE_CONTROL :
        AGC_GFX1013_VULKAN_CLIP_CONTROL;
}

/* Address32 shader ABI pointers carry only their low dword in user SGPRs.
 * The qualified Prospero compiler contract reconstructs the high dword as 2,
 * so accepting any other allocation would silently point shaders elsewhere. */
static inline int32_t agcRuntimeValidateAddress32(uint64_t address)
{
    return (uint32_t)(address >> 32u) == AGC_GFX1013_ADDRESS32_HIGH ?
        AGC_OK : AGC_ERROR_NOT_SUPPORTED;
}

#endif
