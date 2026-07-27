/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 */

#include "agc_capabilities.h"

#include <string.h>

#include "agc_error.h"

#define AGC_GIB (1024ull * 1024ull * 1024ull)
#define AGC_MIB (1024ull * 1024ull)

int32_t PS5_SYSV_ABI agcGfx1013GetCapabilities(
    AgcGfx1013Capabilities *capabilities) {
    uint32_t index;

    if (capabilities == NULL) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }

    memset(capabilities, 0, sizeof(*capabilities));
    capabilities->version = AGC_GFX1013_CAPABILITIES_VERSION;
    capabilities->max_image_dimension_1d = 16384u;
    capabilities->max_image_dimension_2d = 16384u;
    capabilities->max_image_dimension_3d = 2048u;
    capabilities->max_image_dimension_cube = 16384u;
    capabilities->max_image_array_layers = 2048u;
    capabilities->max_color_targets = AGC_GFX1013_MAX_COLOR_TARGETS;
    capabilities->subgroup_size = 32u;
    capabilities->max_compute_shared_memory_size = 65536u;
    capabilities->max_compute_workgroup_invocations = 1024u;
    capabilities->max_compute_workgroup_size[0] = 1024u;
    capabilities->max_compute_workgroup_size[1] = 1024u;
    capabilities->max_compute_workgroup_size[2] = 64u;
    capabilities->color_sample_counts = AGC_GFX1013_SAMPLE_COUNT_1_BIT |
        AGC_GFX1013_SAMPLE_COUNT_4_BIT;
    capabilities->depth_sample_counts = AGC_GFX1013_SAMPLE_COUNT_1_BIT;

    for (index = 0; index < AGC_GFX1013_RT_FORMAT_COUNT; ++index) {
        capabilities->color_target_format_mask |= 1ull << index;
    }
    capabilities->depth_stencil_format_mask =
        (1u << AGC_GFX1013_DEPTH_FORMAT_COUNT) - 1u;

    capabilities->memory_profile_count = AGC_GFX1013_MEMORY_PROFILE_COUNT;
    capabilities->memory_profiles[0].size = 4ull * AGC_GIB;
    capabilities->memory_profiles[0].minimum_alignment = 4096u;
    capabilities->memory_profiles[0].property_flags =
        AGC_GFX1013_MEMORY_DEVICE_LOCAL_BIT |
        AGC_GFX1013_MEMORY_HOST_VISIBLE_BIT |
        AGC_GFX1013_MEMORY_HOST_COHERENT_BIT |
        AGC_GFX1013_MEMORY_HOST_CACHED_BIT;
    capabilities->memory_profiles[1].size = 12ull * AGC_GIB;
    capabilities->memory_profiles[1].minimum_alignment = 2ull * AGC_MIB;
    capabilities->memory_profiles[1].property_flags =
        AGC_GFX1013_MEMORY_DEVICE_LOCAL_BIT |
        AGC_GFX1013_MEMORY_HOST_VISIBLE_BIT |
        AGC_GFX1013_MEMORY_HOST_COHERENT_BIT |
        AGC_GFX1013_MEMORY_WRITE_COMBINED_BIT;

    return AGC_OK;
}
