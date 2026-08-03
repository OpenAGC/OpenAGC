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
 * The command-resource arena captures the high dword from its first flexible
 * allocation. Every later address32 pointer must stay in that same 4 GiB
 * window; otherwise its low dword would name unrelated memory in the shader. */
static inline int32_t agcRuntimeValidateAddress32(uint64_t address,
    uint32_t address32_hi)
{
    return (uint32_t)(address >> 32u) == address32_hi ? AGC_OK :
        AGC_ERROR_NOT_SUPPORTED;
}

static inline int32_t agcRuntimeValidateAddress32Range(uint64_t address,
    uint64_t size, uint32_t address32_hi)
{
    uint64_t last;

    if (size == 0u || size - 1u > UINT64_MAX - address)
        return AGC_ERROR_INVALID_ARGUMENT;
    last = address + size - 1u;
    if (agcRuntimeValidateAddress32(address, address32_hi) != AGC_OK ||
        agcRuntimeValidateAddress32(last, address32_hi) != AGC_OK)
        return AGC_ERROR_NOT_SUPPORTED;
    return AGC_OK;
}

#endif
