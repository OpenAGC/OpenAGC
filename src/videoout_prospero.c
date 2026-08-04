/* openagc — SPDX-License-Identifier: Apache-2.0 */

#include "agc_videoout.h"
#include "agc_error.h"
#include "videoout_patch.h"

#include <ps5/kernel.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SCE_VIDEO_OUT_BUS_TYPE_MAIN = 0,
    SCE_VIDEO_OUT_TILING_MODE_LINEAR = 1,
    SCE_VIDEO_OUT_ASPECT_RATIO_16_9 = 0,
    SCE_VIDEO_OUT_FLIP_MODE_VSYNC = 1,
};

static const uint32_t SCE_VIDEO_OUT_PIXEL_FORMAT_A8R8G8B8_SRGB = 0x80000000u;
static const int32_t SCE_VIDEO_OUT_ERROR_RESOURCE_BUSY =
    (int32_t)0x80290009u;
static const int32_t SCE_KERNEL_ERROR_EBADF = (int32_t)0x80020009u;

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

_Static_assert(sizeof(SceVideoOutBufferAttribute) == 0x20u,
               "legacy VideoOut buffer attribute size");

int32_t sceVideoOutOpen(int32_t, int32_t, int32_t, const void *);
int32_t sceVideoOutClose(int32_t);
int32_t sceVideoOutRegisterBuffers(int32_t, int32_t, void *const *, int32_t,
                                   const SceVideoOutBufferAttribute *);
int32_t sceVideoOutUnregisterBuffers(int32_t, int32_t);
int32_t sceVideoOutSetFlipRate(int32_t, int32_t);
int32_t sceVideoOutSubmitFlip(int32_t, int32_t, int32_t, int64_t);
int32_t sceVideoOutAddFlipEvent(void *, int32_t, void *);
int32_t sceVideoOutDeleteFlipEvent(void *, int32_t);
int sceKernelCreateEqueue(SceKernelEqueue *, const char *);
int sceKernelDeleteEqueue(SceKernelEqueue);
int sceKernelWaitEqueue(SceKernelEqueue, SceKernelEvent *, int, int *, void *);

struct AgcVideoOut {
    int32_t handle;
    SceKernelEqueue flip_queue;
    uint32_t buffer_count;
    bool event_added;
    bool buffers_registered;
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
    static const uint8_t patched[AGC_VIDEOOUT_LINEAR_PATCH_SIZE] = {
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    };
    static const int text_protection = 0x4; /* execute-only, as originally mapped */
    const AgcVideoOutLinearPatch *patch =
        agcVideoOutFindLinearPatch(kernel_get_fw_version());
    uint32_t module = 0;

    if (!patch)
        return AGC_ERROR_NOT_SUPPORTED;
    if (kernel_dynlib_handle(-1, "libSceVideoOut.sprx", &module) != 0 ||
        module == 0)
        return AGC_ERROR_NOT_FOUND;
    intptr_t base = kernel_dynlib_mapbase_addr(-1, module);
    if (!base)
        return AGC_ERROR_NOT_FOUND;

    uint8_t *address = (uint8_t *)(base + patch->text_offset);
    const uint8_t *expected = enable ? patch->original : patched;
    const uint8_t *replacement = enable ? patched : patch->original;
    intptr_t page = (intptr_t)address & ~(intptr_t)0xfff;
    if (kernel_mprotect(-1, page, 0x2000, 0x7) != 0)
        return AGC_ERROR_INTERNAL;
    if (memcmp(address, expected, sizeof(patch->original)) != 0) {
        return kernel_mprotect(-1, page, 0x2000, text_protection) == 0 ?
            AGC_ERROR_NOT_SUPPORTED : AGC_ERROR_INTERNAL;
    }
    memcpy(address, replacement, sizeof(patch->original));
    __builtin___clear_cache((char *)address,
                            (char *)address + sizeof(patch->original));
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

    /* Build the legacy attribute bytes directly. The installed setter writes
     * beyond the public 0x20-byte record on FW 5.50, which corrupts an
     * optimized caller's stack. The 0x40-byte zeroed carrier and first six
     * dwords match the hardware-qualified samples. */
    union {
        SceVideoOutBufferAttribute attribute;
        uint8_t storage[64];
    } attribute_storage;
    memset(&attribute_storage, 0, sizeof(attribute_storage));
    attribute_storage.attribute.pixel_format =
        SCE_VIDEO_OUT_PIXEL_FORMAT_A8R8G8B8_SRGB;
    attribute_storage.attribute.tiling_mode =
        SCE_VIDEO_OUT_TILING_MODE_LINEAR;
    attribute_storage.attribute.aspect_ratio =
        SCE_VIDEO_OUT_ASPECT_RATIO_16_9;
    attribute_storage.attribute.width = info->width;
    attribute_storage.attribute.height = info->height;
    attribute_storage.attribute.pitch_in_pixel = info->pitch_pixels;
    int32_t register_result = sceVideoOutRegisterBuffers(result->handle, 0,
        info->buffers, (int32_t)info->buffer_count,
        &attribute_storage.attribute);
    int32_t restore_result = set_linear_registration_patch(false);
    if (register_result != 0) {
        err = AGC_ERROR_NOT_SUPPORTED;
        goto fail;
    }
    result->buffers_registered = true;
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
    result->event_added = true;
    if (sceVideoOutSetFlipRate(result->handle, 0) != 0) {
        err = AGC_ERROR_NOT_SUPPORTED;
        goto fail;
    }
    result->buffer_count = info->buffer_count;
    *video_out = result;
    return AGC_OK;

fail:
    {
        const int32_t close_result = agcVideoOutCloseChecked(result);
        if (close_result != AGC_OK) {
            *video_out = result;
            return close_result;
        }
    }
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

static int32_t videoout_teardown_error(const char *stage, int32_t native_result)
{
    fprintf(stderr,
        "openagc: VideoOut teardown failed stage=%s native=0x%08x\n",
        stage, (unsigned)native_result);
    return AGC_ERROR_INTERNAL;
}

int32_t agcVideoOutCloseChecked(AgcVideoOut *video_out)
{
    if (!video_out)
        return AGC_OK;
    if (video_out->event_added) {
        const int32_t native_result = sceVideoOutDeleteFlipEvent(
            (void *)(uintptr_t)video_out->flip_queue, video_out->handle);
        if (native_result != 0)
            return videoout_teardown_error("delete-flip-event", native_result);
        video_out->event_added = false;
    }
    if (video_out->buffers_registered) {
        const int32_t native_result =
            sceVideoOutUnregisterBuffers(video_out->handle, 0);
        if (native_result == 0) {
            video_out->buffers_registered = false;
        } else if (native_result == SCE_VIDEO_OUT_ERROR_RESOURCE_BUSY) {
            fprintf(stderr,
                "openagc: VideoOut active scanout requires checked handle "
                "close before buffer release\n");
        } else {
            return videoout_teardown_error("unregister-buffers", native_result);
        }
    }
    if (video_out->handle >= 0) {
        const int32_t native_result = sceVideoOutClose(video_out->handle);
        if (native_result != 0)
            return videoout_teardown_error("close-handle", native_result);
        video_out->handle = -1;
        video_out->buffers_registered = false;
    }
    if (video_out->flip_queue) {
        const int32_t native_result =
            sceKernelDeleteEqueue(video_out->flip_queue);
        if (native_result == SCE_KERNEL_ERROR_EBADF) {
            fprintf(stderr,
                "openagc: VideoOut equeue already retired during checked "
                "teardown\n");
        } else if (native_result != 0) {
            return videoout_teardown_error("delete-equeue", native_result);
        }
        video_out->flip_queue = 0;
    }
    free(video_out);
    return AGC_OK;
}

void agcVideoOutClose(AgcVideoOut *video_out)
{
    const int32_t result = agcVideoOutCloseChecked(video_out);
    if (result != AGC_OK) {
        fprintf(stderr,
            "openagc: VideoOut teardown failed before buffer release: 0x%08x\n",
            (unsigned)result);
        abort();
    }
}
