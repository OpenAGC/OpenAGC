#include "test.h"
#include "agc_error.h"
#include "agc_videoout.h"
#include "videoout_patch.h"

#include <string.h>

static void test_linear_patch_profiles(void)
{
    typedef struct ExpectedPatch {
        uint32_t key;
        uintptr_t offset;
        uint8_t displacement_low;
        uint8_t displacement_high;
    } ExpectedPatch;
    static const ExpectedPatch expected[] = {
        {0x0320u, 0x81b3u, 0x9f, 0x04},
        {0x0400u, 0x8409u, 0x25, 0x05},
        {0x0403u, 0x8409u, 0x25, 0x05},
        {0x0450u, 0x8409u, 0x25, 0x05},
        {0x0451u, 0x8409u, 0x25, 0x05},
        {0x0502u, 0x7e61u, 0x15, 0x02},
        {0x0510u, 0x7e61u, 0x15, 0x02},
        {0x0550u, 0x7e61u, 0x15, 0x02},
        {0x0600u, 0x8515u, 0x46, 0x02},
        {0x0602u, 0x8515u, 0x46, 0x02},
        {0x0650u, 0x8515u, 0x46, 0x02},
        {0x0701u, 0x8cb3u, 0x49, 0x02},
        {0x0720u, 0x8cb3u, 0x49, 0x02},
        {0x0740u, 0x8cb3u, 0x49, 0x02},
        {0x0760u, 0x8cb3u, 0x49, 0x02},
        {0x0761u, 0x8cb3u, 0x49, 0x02},
        {0x0800u, 0x8fc4u, 0x38, 0x02},
        {0x0820u, 0x8fc4u, 0x38, 0x02},
        {0x0840u, 0x8fc4u, 0x38, 0x02},
        {0x0860u, 0x8fc4u, 0x38, 0x02},
        {0x0900u, 0x97e3u, 0x49, 0x02},
        {0x0905u, 0x97e3u, 0x49, 0x02},
        {0x0920u, 0x97e3u, 0x49, 0x02},
        {0x0940u, 0x97e3u, 0x49, 0x02},
        {0x0960u, 0x97e3u, 0x49, 0x02},
        {0x1001u, 0x97ebu, 0x33, 0x02},
        {0x1020u, 0x97ebu, 0x33, 0x02},
        {0x1040u, 0x97ebu, 0x33, 0x02},
        {0x1060u, 0x97ebu, 0x33, 0x02},
        {0x1100u, 0x9922u, 0x3c, 0x02},
        {0x1120u, 0x9922u, 0x3c, 0x02},
        {0x1140u, 0x9922u, 0x3c, 0x02},
        {0x1160u, 0x9922u, 0x3c, 0x02},
        {0x1200u, 0x9f2eu, 0x2e, 0x03},
        {0x1202u, 0x9f2eu, 0x2e, 0x03},
        {0x1220u, 0x9f2eu, 0x2e, 0x03},
        {0x1240u, 0x9f2eu, 0x2e, 0x03},
        {0x1260u, 0x9f2eu, 0x2e, 0x03},
        {0x1270u, 0x9f2eu, 0x2e, 0x03},
    };

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        const uint8_t signature[] = {
            0x0f, 0x84,
            expected[i].displacement_low,
            expected[i].displacement_high,
            0x00, 0x00,
        };
        const AgcVideoOutLinearPatch *patch =
            agcVideoOutFindLinearPatch((expected[i].key << 16) | 0x0005u);

        TEST_ASSERT(patch != NULL,
            "VideoOut finds every active exact firmware profile");
        if (patch == NULL)
            continue;
        TEST_ASSERT_EQ(patch->firmware_abi_key, expected[i].key,
            "VideoOut normalizes the full firmware version");
        TEST_ASSERT_EQ(patch->text_offset, expected[i].offset,
            "VideoOut selects the exact firmware patch offset");
        TEST_ASSERT(memcmp(patch->original, signature, sizeof(signature)) == 0,
            "VideoOut selects the exact firmware patch signature");
    }

    TEST_ASSERT(agcVideoOutFindLinearPatch(0x05510000u) == NULL,
        "VideoOut rejects unevidenced firmware patch");
    TEST_ASSERT(agcVideoOutFindLinearPatch(0x03190000u) == NULL,
        "VideoOut rejects firmware below the active support floor");
    TEST_ASSERT(agcVideoOutFindLinearPatch(0x12800000u) == NULL,
        "VideoOut rejects firmware beyond the evidenced table");
}

static void test_default_mode(void)
{
    AgcVideoOutMode mode = {0};
    TEST_ASSERT_EQ(agcVideoOutGetDefaultMode(NULL), AGC_ERROR_INVALID_ARGUMENT,
        "VideoOut rejects NULL mode output");
    TEST_ASSERT_EQ(agcVideoOutGetDefaultMode(&mode), AGC_OK,
        "VideoOut reports its qualified default mode");
    TEST_ASSERT_EQ(mode.width, 1920u, "VideoOut default width");
    TEST_ASSERT_EQ(mode.height, 1080u, "VideoOut default height");
}

static void test_lifecycle(void)
{
    uint32_t pixels[3] = {0};
    void *buffers[3] = {&pixels[0], &pixels[1], &pixels[2]};
    AgcVideoOutCreateInfo info = {
        .width = 1920,
        .height = 1080,
        .pitch_pixels = 1920,
        .buffer_count = 3,
        .buffers = buffers,
        .format = AGC_VIDEO_OUT_FORMAT_BGRA8_SRGB,
    };
    AgcVideoOut *video_out = NULL;

    TEST_ASSERT_EQ(agcVideoOutOpen(NULL, &video_out),
        AGC_ERROR_INVALID_ARGUMENT, "VideoOut rejects NULL create info");
    TEST_ASSERT_EQ(agcVideoOutOpen(&info, &video_out), AGC_OK,
        "VideoOut opens with three caller-owned buffers");
    TEST_ASSERT(video_out != NULL, "VideoOut returns a lifecycle object");
    TEST_ASSERT_EQ(agcVideoOutPresent(video_out, 3, 1, 1000),
        AGC_ERROR_INVALID_ARGUMENT, "VideoOut rejects invalid buffer index");
    TEST_ASSERT_EQ(agcVideoOutPresent(video_out, 0, 1, 0),
        AGC_ERROR_TIMEOUT, "VideoOut zero timeout is bounded");
    TEST_ASSERT_EQ(agcVideoOutPresent(video_out, 0, 1, 1000), AGC_OK,
        "VideoOut presents a registered buffer");
    TEST_ASSERT_EQ(agcVideoOutCloseChecked(video_out), AGC_OK,
        "VideoOut checked close releases display ownership");
    TEST_ASSERT_EQ(agcVideoOutCloseChecked(NULL), AGC_OK,
        "VideoOut checked close accepts NULL");

    info.width = 1280;
    TEST_ASSERT_EQ(agcVideoOutOpen(&info, &video_out),
        AGC_ERROR_NOT_SUPPORTED, "VideoOut rejects unqualified modes");
}

void test_suite_videoout(void)
{
    TEST_SUITE("videoout");
    test_linear_patch_profiles();
    test_default_mode();
    test_lifecycle();
}
