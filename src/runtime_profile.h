#ifndef OPENAGC_RUNTIME_PROFILE_H
#define OPENAGC_RUNTIME_PROFILE_H

#include "agc_graphics.h"

static inline uint32_t agcRuntimeDepthClipControl(int depth_clip_disabled)
{
    return depth_clip_disabled ?
        AGC_GFX1013_DEPTH_CLIP_DISABLE_CONTROL :
        AGC_GFX1013_VULKAN_CLIP_CONTROL;
}

#endif
