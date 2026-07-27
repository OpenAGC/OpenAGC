/*
 * openagc capability-query tests
 */

#include "test.h"

#include "agc_capabilities.h"
#include "agc_error.h"

static void test_capabilities_invalid_argument(void) {
    TEST_ASSERT_EQ(agcGfx1013GetCapabilities(NULL), AGC_ERROR_INVALID_ARGUMENT,
        "NULL capability output is rejected");
}

static void test_capabilities_baseline(void) {
    AgcGfx1013Capabilities capabilities;
    uint64_t color_mask =
        (1ull << AGC_GFX1013_RT_FORMAT_COUNT) - 1ull;
    uint32_t depth_mask =
        (1u << AGC_GFX1013_DEPTH_FORMAT_COUNT) - 1u;

    memset(&capabilities, 0xff, sizeof(capabilities));
    TEST_ASSERT_EQ(agcGfx1013GetCapabilities(&capabilities), AGC_OK,
        "gfx1013 capabilities query succeeds");
    TEST_ASSERT_EQ(capabilities.version, AGC_GFX1013_CAPABILITIES_VERSION,
        "capability version is stable");
    TEST_ASSERT_EQ(capabilities.max_image_dimension_2d, 16384u,
        "gfx1013 2D image limit");
    TEST_ASSERT_EQ(capabilities.max_color_targets,
        AGC_GFX1013_MAX_COLOR_TARGETS, "gfx1013 MRT limit");
    TEST_ASSERT_EQ(capabilities.subgroup_size, 32u, "gfx1013 Wave32 baseline");
    TEST_ASSERT_EQ(capabilities.color_target_format_mask, color_mask,
        "all public render-target formats are qualified");
    TEST_ASSERT_EQ(capabilities.depth_stencil_format_mask, depth_mask,
        "all public depth/stencil formats are qualified");
    TEST_ASSERT_EQ(capabilities.memory_profile_count,
        AGC_GFX1013_MEMORY_PROFILE_COUNT, "both PS5 memory profiles are exposed");
    TEST_ASSERT(capabilities.memory_profiles[0].property_flags &
        AGC_GFX1013_MEMORY_HOST_CACHED_BIT, "flexible memory is host cached");
    TEST_ASSERT(capabilities.memory_profiles[1].property_flags &
        AGC_GFX1013_MEMORY_WRITE_COMBINED_BIT, "direct memory is write combined");
    TEST_ASSERT_EQ(capabilities.memory_profiles[1].minimum_alignment,
        2ull * 1024ull * 1024ull, "direct-memory alignment is 2 MiB");
}

void test_suite_capabilities(void) {
    TEST_SUITE("gfx1013 capabilities");
    TEST_RUN(test_capabilities_invalid_argument);
    TEST_RUN(test_capabilities_baseline);
}
