/* openagc — SPDX-License-Identifier: Apache-2.0 */

#include "agc_videoout.h"
#include "agc_error.h"

#include <stdbool.h>
#include <stdlib.h>

struct AgcVideoOut {
    uint32_t buffer_count;
    uint64_t last_frame_id;
    bool presented;
};

static int32_t next_close_result = AGC_OK;
static int32_t next_open_result = AGC_OK;

void agcVideoOutDebugSetNextCloseResult(int32_t result)
{
    next_close_result = result;
}

void agcVideoOutDebugSetNextOpenResult(int32_t result)
{
    next_open_result = result;
}

static int32_t validate_create_info(const AgcVideoOutCreateInfo *info)
{
    if (!info || !info->buffers || info->buffer_count == 0 ||
        info->buffer_count > AGC_VIDEO_OUT_MAX_BUFFERS)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (info->width != 1920 || info->height != 1080 ||
        info->pitch_pixels < info->width ||
        info->format != AGC_VIDEO_OUT_FORMAT_BGRA8_SRGB)
        return AGC_ERROR_NOT_SUPPORTED;
    for (uint32_t i = 0; i < info->buffer_count; ++i) {
        if (!info->buffers[i])
            return AGC_ERROR_INVALID_ARGUMENT;
    }
    return AGC_OK;
}

int32_t agcVideoOutGetDefaultMode(AgcVideoOutMode *mode)
{
    if (!mode)
        return AGC_ERROR_INVALID_ARGUMENT;
    mode->width = 1920;
    mode->height = 1080;
    mode->refresh_millihz = 60000;
    return AGC_OK;
}

int32_t agcVideoOutOpen(const AgcVideoOutCreateInfo *info,
                        AgcVideoOut **video_out)
{
    if (!video_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *video_out = NULL;
    int32_t err = validate_create_info(info);
    if (err != AGC_OK)
        return err;

    AgcVideoOut *result = calloc(1, sizeof(*result));
    if (!result)
        return AGC_ERROR_OUT_OF_MEMORY;
    result->buffer_count = info->buffer_count;
    if (next_open_result != AGC_OK) {
        const int32_t setup_result = next_open_result;
        next_open_result = AGC_OK;
        const int32_t close_result = agcVideoOutCloseChecked(result);
        if (close_result != AGC_OK) {
            *video_out = result;
            return close_result;
        }
        return setup_result;
    }
    *video_out = result;
    return AGC_OK;
}

int32_t agcVideoOutPresent(AgcVideoOut *video_out, uint32_t buffer_index,
                           uint64_t frame_id, uint64_t timeout_us)
{
    if (!video_out || buffer_index >= video_out->buffer_count)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (timeout_us == 0)
        return AGC_ERROR_TIMEOUT;
    video_out->last_frame_id = frame_id;
    video_out->presented = true;
    return AGC_OK;
}

int32_t agcVideoOutCloseChecked(AgcVideoOut *video_out)
{
    if (next_close_result != AGC_OK) {
        const int32_t result = next_close_result;
        next_close_result = AGC_OK;
        return result;
    }
    free(video_out);
    return AGC_OK;
}

void agcVideoOutClose(AgcVideoOut *video_out)
{
    if (agcVideoOutCloseChecked(video_out) != AGC_OK)
        abort();
}
