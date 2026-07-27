/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Application-neutral gfx1013 capability discovery.
 */

#ifndef _AGC_CAPABILITIES_H_
#define _AGC_CAPABILITIES_H_

#include <stdint.h>

#include "agc_graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_GFX1013_CAPABILITIES_VERSION 1u
#define AGC_GFX1013_MEMORY_PROFILE_COUNT 2u

typedef enum AgcGfx1013MemoryPropertyFlagBits {
    AGC_GFX1013_MEMORY_DEVICE_LOCAL_BIT = 1u << 0,
    AGC_GFX1013_MEMORY_HOST_VISIBLE_BIT = 1u << 1,
    AGC_GFX1013_MEMORY_HOST_COHERENT_BIT = 1u << 2,
    AGC_GFX1013_MEMORY_HOST_CACHED_BIT = 1u << 3,
    AGC_GFX1013_MEMORY_WRITE_COMBINED_BIT = 1u << 4
} AgcGfx1013MemoryPropertyFlagBits;

typedef uint32_t AgcGfx1013MemoryPropertyFlags;

typedef enum AgcGfx1013SampleCountFlagBits {
    AGC_GFX1013_SAMPLE_COUNT_1_BIT = 1u,
    AGC_GFX1013_SAMPLE_COUNT_2_BIT = 2u,
    AGC_GFX1013_SAMPLE_COUNT_4_BIT = 4u,
    AGC_GFX1013_SAMPLE_COUNT_8_BIT = 8u
} AgcGfx1013SampleCountFlagBits;

typedef struct AgcGfx1013MemoryProfile {
    uint64_t size;
    uint64_t minimum_alignment;
    AgcGfx1013MemoryPropertyFlags property_flags;
} AgcGfx1013MemoryProfile;

typedef struct AgcGfx1013Capabilities {
    uint32_t version;
    uint32_t max_image_dimension_1d;
    uint32_t max_image_dimension_2d;
    uint32_t max_image_dimension_3d;
    uint32_t max_image_dimension_cube;
    uint32_t max_image_array_layers;
    uint32_t max_color_targets;
    uint32_t subgroup_size;
    uint32_t max_compute_shared_memory_size;
    uint32_t max_compute_workgroup_invocations;
    uint32_t max_compute_workgroup_size[3];
    uint32_t color_sample_counts;
    uint32_t depth_sample_counts;
    uint64_t color_target_format_mask;
    uint32_t depth_stencil_format_mask;
    uint32_t memory_profile_count;
    AgcGfx1013MemoryProfile memory_profiles[AGC_GFX1013_MEMORY_PROFILE_COUNT];
} AgcGfx1013Capabilities;

/* Returns the qualified baseline shared by OpenAGC clients on gfx1013. */
int32_t PS5_SYSV_ABI agcGfx1013GetCapabilities(
    AgcGfx1013Capabilities *capabilities);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_CAPABILITIES_H_ */
