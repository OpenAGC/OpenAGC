/*
 * ps5_video_out.h — PS5 VideoOut API constants and declarations
 *
 * Adapted from the sibling ps5-openagc project's video_out.h.
 * The ps5-payload-sdk provides libSceVideoOut.so stub for linking.
 */

#ifndef _PS5_VIDEO_OUT_H
#define _PS5_VIDEO_OUT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bus types */
#define SCE_VIDEO_OUT_BUS_TYPE_MAIN                   0
#define SCE_VIDEO_OUT_BUS_TYPE_AUX_SOCIAL_SCREEN      5
#define SCE_VIDEO_OUT_BUS_TYPE_AUX_GAME_LIVE_STREAMING 6

/* Refresh rates */
#define SCE_VIDEO_OUT_REFRESH_RATE_UNKNOWN     0
#define SCE_VIDEO_OUT_REFRESH_RATE_23_98HZ     1
#define SCE_VIDEO_OUT_REFRESH_RATE_50HZ        2
#define SCE_VIDEO_OUT_REFRESH_RATE_59_94HZ     3
#define SCE_VIDEO_OUT_REFRESH_RATE_119_88HZ    13
#define SCE_VIDEO_OUT_REFRESH_RATE_89_91HZ     35

/* Pixel formats */
#define SCE_VIDEO_OUT_PIXEL_FORMAT_A8R8G8B8_SRGB         0x80000000
#define SCE_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB         0x80002200
#define SCE_VIDEO_OUT_PIXEL_FORMAT_A2R10G10B10           0x88060000
#define SCE_VIDEO_OUT_PIXEL_FORMAT_A2R10G10B10_SRGB      0x88000000
#define SCE_VIDEO_OUT_PIXEL_FORMAT_A2R10G10B10_BT2020_PQ 0x88740000
#define SCE_VIDEO_OUT_PIXEL_FORMAT_A16R16G16B16_FLOAT    0xC1060000

/* Tiling modes */
#define SCE_VIDEO_OUT_TILING_MODE_TILE   0
#define SCE_VIDEO_OUT_TILING_MODE_LINEAR 1

/* Aspect ratios */
#define SCE_VIDEO_OUT_ASPECT_RATIO_16_9  0

/* Flip modes */
#define SCE_VIDEO_OUT_FLIP_MODE_HSYNC  0
#define SCE_VIDEO_OUT_FLIP_MODE_VSYNC  1
#define SCE_VIDEO_OUT_FLIP_MODE_BLOCK  2

/* Event IDs */
#define SCE_VIDEO_OUT_EVENT_ID_FLIP       0
#define SCE_VIDEO_OUT_EVENT_ID_VBLANK     1
#define SCE_VIDEO_OUT_EVENT_ID_PRE_VBLANK_START  2

/* Resolution status */
typedef struct SceVideoOutResolutionStatus {
    int32_t  full_width;
    int32_t  full_height;
    int32_t  pane_width;
    int32_t  pane_height;
    uint64_t refresh_rate;
    float    screen_size_in_inch;
    uint16_t flags;
    uint16_t reserved0;
    uint32_t reserved1[3];
} SceVideoOutResolutionStatus;

/* Buffer attribute */
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

/* Flip status */
typedef struct SceVideoOutFlipStatus {
    uint64_t count;
    uint64_t process_time;
    uint64_t tsc;
    int64_t  flip_arg;
    uint64_t submit_tsc;
    uint64_t reserved0;
    int32_t  gc_queue_num;
    int32_t  flip_pending_num;
    int32_t  current_buffer;
    uint32_t reserved1;
} SceVideoOutFlipStatus;

/* --- Open/Close --- */
int32_t sceVideoOutOpen(int32_t userId, int32_t busType, int32_t index,
                        const void *param);
int32_t sceVideoOutClose(int32_t handle);

/* --- Buffer management --- */
void sceVideoOutSetBufferAttribute(SceVideoOutBufferAttribute *attribute,
                                   uint32_t pixelFormat, uint32_t tilingMode,
                                   uint32_t aspectRatio, uint32_t width,
                                   uint32_t height, uint32_t pitchInPixel);

/* PS5 extended buffer attribute (0x50 bytes) */
typedef struct SceVideoOutBufferAttribute2 {
    uint32_t reserved0;        /* 0x00 */
    uint32_t tiling_mode;      /* 0x04 */
    uint32_t reserved1;        /* 0x08 */
    uint32_t width;            /* 0x0C */
    uint32_t height;           /* 0x10 */
    uint32_t reserved2;        /* 0x14 */
    uint64_t option;           /* 0x18 */
    uint64_t pixel_format;     /* 0x20 */
    uint64_t dcc_clear_color;  /* 0x28 */
    uint32_t dcc_control;      /* 0x30 */
    uint32_t reserved3[7];     /* 0x34 - 0x4F */
} SceVideoOutBufferAttribute2;

/* sceVideoOutSetBufferAttribute2 — PS5 extended attribute setter.
 * NID: PjS5uASwcV8. Takes extra option, dccControl, dccClearColor params. */
void sceVideoOutSetBufferAttribute2(SceVideoOutBufferAttribute2 *attribute,
                                    uint64_t pixelFormat, uint32_t tilingMode,
                                    uint32_t width, uint32_t height,
                                    uint64_t option, uint32_t dccControl,
                                    uint64_t dccClearColor);

int32_t sceVideoOutRegisterBuffers(int32_t handle, int32_t startIndex,
                                   void *const *addresses, int32_t bufferNum,
                                   const SceVideoOutBufferAttribute *attribute);
int32_t sceVideoOutUnregisterBuffers(int32_t handle, int32_t setIndex);

/* sceVideoOutRegisterBuffers2 — PS5 extended register.
 * NID: rKBUtgRrtbk. Buffers are 0x20-byte entries {void* addr; uint64_t pad;}.
 * category and option must be 0. */
int32_t sceVideoOutRegisterBuffers2(int32_t handle, int32_t setIndex,
                                    int32_t bufferIndexStart,
                                    const void *buffers, int32_t bufferNum,
                                    const void *attribute,
                                    uint64_t category, uint64_t option);

/* --- Flip --- */
int32_t sceVideoOutSubmitFlip(int32_t handle, int32_t bufferIndex,
                              int32_t flipMode, int64_t flipArg);
int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t rate);

/* --- Status --- */
int32_t sceVideoOutGetResolutionStatus(int32_t handle,
                                       SceVideoOutResolutionStatus *status);

/* --- Events --- */
int32_t sceVideoOutAddFlipEvent(void *equeue, int32_t handle, void *data);
int32_t sceVideoOutDeleteFlipEvent(void *equeue, int32_t handle);

#ifdef __cplusplus
}
#endif

#endif /* _PS5_VIDEO_OUT_H */
