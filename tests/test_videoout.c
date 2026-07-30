#include "test.h"
#include "agc_error.h"
#include "agc_videoout.h"
#include "videoout_patch.h"

#include <string.h>

static void test_linear_patch_profiles(void)
{
    static const uint8_t fw550_bytes[] =
        {0x0f, 0x84, 0x15, 0x02, 0x00, 0x00};
    static const uint8_t fw1160_bytes[] =
        {0x0f, 0x84, 0x3c, 0x02, 0x00, 0x00};
    const AgcVideoOutLinearPatch *patch;

    patch = agcVideoOutFindLinearPatch(0x05500008u);
    TEST_ASSERT(patch != NULL, "VideoOut finds FW 5.50 linear patch");
    TEST_ASSERT_EQ(patch->firmware_abi_key, 0x0550u,
        "VideoOut normalizes full FW 5.50 version");
    TEST_ASSERT_EQ(patch->text_offset, 0x7e61u,
        "VideoOut FW 5.50 patch offset");
    TEST_ASSERT(memcmp(patch->original, fw550_bytes, sizeof(fw550_bytes)) == 0,
        "VideoOut FW 5.50 patch signature");

    patch = agcVideoOutFindLinearPatch(0x11600005u);
    TEST_ASSERT(patch != NULL, "VideoOut finds FW 11.60 linear patch");
    TEST_ASSERT_EQ(patch->firmware_abi_key, 0x1160u,
        "VideoOut normalizes full FW 11.60 version");
    TEST_ASSERT_EQ(patch->text_offset, 0x9922u,
        "VideoOut FW 11.60 patch offset");
    TEST_ASSERT(memcmp(patch->original, fw1160_bytes,
        sizeof(fw1160_bytes)) == 0,
        "VideoOut FW 11.60 patch signature");

    TEST_ASSERT(agcVideoOutFindLinearPatch(0x05510000u) == NULL,
        "VideoOut rejects unevidenced firmware patch");
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
    agcVideoOutClose(video_out);
    agcVideoOutClose(NULL);

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
