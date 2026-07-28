/* openagc — SPDX-License-Identifier: Apache-2.0 */

#include "agc_videoout.h"
#include "agc_error.h"

#include <ps5/kernel.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    SCE_VIDEO_OUT_BUS_TYPE_MAIN = 0,
    SCE_VIDEO_OUT_TILING_MODE_LINEAR = 1,
    SCE_VIDEO_OUT_ASPECT_RATIO_16_9 = 0,
    SCE_VIDEO_OUT_FLIP_MODE_VSYNC = 1,
};

static const uint32_t SCE_VIDEO_OUT_PIXEL_FORMAT_A8R8G8B8_SRGB = 0x80000000u;

typedef int SceKernelEqueue;
typedef struct SceKernelEvent { uint8_t opaque[64]; } SceKernelEvent;

typedef struct SceVideoOutBufferAttribute {
    uint32_t pixel_format;
    uint32_t tiling_mode;
    uint32_t aspect_ratio;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_in_pixel;
    uint32_t option;
    uint32_t reserved;
} SceVideoOutBufferAttribute;

int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void *);
int32_t sceVideoOutClose(int32_t);
void sceVideoOutSetBufferAttribute(SceVideoOutBufferAttribute *, uint32_t,
                                   uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t);
int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void *const *, int32_t,
                                   const SceVideoOutBufferAttribute *);
int32_t sceVideoOutSetFlipRate(int32_t, int32_t);
int32_t sceVideoOutSubmitFlip(int32_t, int32_t, int32_t, int64_t);
int32_t sceVideoOutAddFlipEvent(void *, int32_t, void *);
int sceKernelCreateEqueue(SceKernelEqueue *, const char *);
int sceKernelDeleteEqueue(SceKernelEqueue);
int sceKernelWaitEqueue(SceKernelEqueue, SceKernelEvent *, int, int *, void *);

struct AgcVideoOut {
    int32_t handle;
    SceKernelEqueue flip_queue;
    uint32_t buffer_count;
};

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

static int32_t set_linear_registration_patch(bool enable)
{
    static const uint8_t original[6] = {0x0f, 0x84, 0x15, 0x02, 0x00, 0x00};
    static const uint8_t patched[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    static const int text_protection = 0x4; /* execute-only, as originally mapped */
    uint32_t module = 0;
    if (kernel_dynlib_handle(-1, "libSceVideoOut.sprx", &module) != 0 ||
        module == 0)
        return AGC_ERROR_NOT_FOUND;
    intptr_t base = kernel_dynlib_mapbase_addr(-1, module);
    if (!base)
        return AGC_ERROR_NOT_FOUND;

    uint8_t *address = (uint8_t *)(base + 0x7e61);
    const uint8_t *expected = enable ? original : patched;
    const uint8_t *replacement = enable ? patched : original;
    intptr_t page = (intptr_t)address & ~(intptr_t)0xfff;
    if (kernel_mprotect(-1, page, 0x2000, 0x7) != 0)
        return AGC_ERROR_INTERNAL;
    if (memcmp(address, expected, sizeof(original)) != 0) {
        return kernel_mprotect(-1, page, 0x2000, text_protection) == 0 ?
            AGC_ERROR_NOT_SUPPORTED : AGC_ERROR_INTERNAL;
    }
    memcpy(address, replacement, sizeof(original));
    __builtin___clear_cache((char *)address, (char *)address + sizeof(original));
    if (kernel_mprotect(-1, page, 0x2000, text_protection) != 0)
        return AGC_ERROR_INTERNAL;
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
    result->handle = -1;

    const int32_t user_ids[] = {0xff, 0, 1, 2};
    for (uint32_t i = 0; i < sizeof(user_ids) / sizeof(user_ids[0]); ++i) {
        result->handle = sceVideoOutOpen(user_ids[i],
            SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, NULL);
        if (result->handle >= 0)
            break;
    }
    if (result->handle < 0) {
        err = AGC_ERROR_NOT_INITIALIZED;
        goto fail;
    }

    err = set_linear_registration_patch(true);
    if (err != AGC_OK)
        goto fail;

    SceVideoOutBufferAttribute attribute;
    memset(&attribute, 0, sizeof(attribute));
    sceVideoOutSetBufferAttribute(&attribute,
        SCE_VIDEO_OUT_PIXEL_FORMAT_A8R8G8B8_SRGB,
        SCE_VIDEO_OUT_TILING_MODE_LINEAR, SCE_VIDEO_OUT_ASPECT_RATIO_16_9,
        info->width, info->height, info->pitch_pixels);
    int32_t register_result = sceVideoOutRegisterBuffers(result->handle, 0,
        info->buffers, (int32_t)info->buffer_count, &attribute);
    int32_t restore_result = set_linear_registration_patch(false);
    if (register_result != 0) {
        err = AGC_ERROR_NOT_SUPPORTED;
        goto fail;
    }
    if (restore_result != AGC_OK) {
        err = restore_result;
        goto fail;
    }

    if (sceKernelCreateEqueue(&result->flip_queue, "openagc videoout") != 0) {
        err = AGC_ERROR_INTERNAL;
        goto fail;
    }
    if (sceVideoOutAddFlipEvent((void *)(uintptr_t)result->flip_queue,
                                result->handle, NULL) != 0) {
        err = AGC_ERROR_INTERNAL;
        goto fail;
    }
    if (sceVideoOutSetFlipRate(result->handle, 0) != 0) {
        err = AGC_ERROR_NOT_SUPPORTED;
        goto fail;
    }
    result->buffer_count = info->buffer_count;
    *video_out = result;
    return AGC_OK;

fail:
    agcVideoOutClose(result);
    return err;
}

int32_t agcVideoOutPresent(AgcVideoOut *video_out, uint32_t buffer_index,
                           uint64_t frame_id, uint64_t timeout_us)
{
    if (!video_out || buffer_index >= video_out->buffer_count)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (timeout_us == 0)
        return AGC_ERROR_TIMEOUT;
    if (sceVideoOutSubmitFlip(video_out->handle, (int32_t)buffer_index,
                              SCE_VIDEO_OUT_FLIP_MODE_VSYNC,
                              (int64_t)frame_id) != 0)
        return AGC_ERROR_INTERNAL;

    uint32_t timeout = timeout_us == AGC_VIDEO_OUT_INFINITE_TIMEOUT
        ? UINT32_MAX
        : (timeout_us > UINT32_MAX ? UINT32_MAX : (uint32_t)timeout_us);
    SceKernelEvent event;
    int event_count = 0;
    memset(&event, 0, sizeof(event));
    if (sceKernelWaitEqueue(video_out->flip_queue, &event, 1, &event_count,
                            &timeout) != 0)
        return AGC_ERROR_TIMEOUT;
    return event_count == 1 ? AGC_OK : AGC_ERROR_TIMEOUT;
}

void agcVideoOutClose(AgcVideoOut *video_out)
{
    if (!video_out)
        return;
    if (video_out->handle >= 0)
        sceVideoOutClose(video_out->handle);
    if (video_out->flip_queue)
        sceKernelDeleteEqueue(video_out->flip_queue);
    free(video_out);
}
