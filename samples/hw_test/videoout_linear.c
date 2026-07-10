/*
 * videoout_linear.c — PS5 VideoOut linear smoke test
 *
 * Adapted from freegnm-examples/videoout-linear/src/main.c for PS5.
 * Tests the display pipeline without any GPU commands:
 *   1. Open VideoOut
 *   2. Allocate garlic direct memory for framebuffers
 *   3. Register linear A8B8G8R8_SRGB buffers
 *   4. CPU-fill color bars + submit flips in a loop
 *
 * This is the first hardware validation step — if this works, the display
 * pipeline is functional and we can move on to AGC GPU command submission.
 *
 * Deploy: make videoout_linear.elf && make deploy_videoout
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "ps5_video_out.h"

/* PS5 kernel memory constants (same as PS4, different names) */
#ifndef SCE_KERNEL_PROT_CPU_READ
#define SCE_KERNEL_PROT_CPU_READ  0x01
#endif
#ifndef SCE_KERNEL_PROT_CPU_RW
#define SCE_KERNEL_PROT_CPU_RW    0x02
#endif
#ifndef SCE_KERNEL_PROT_GPU_READ
#define SCE_KERNEL_PROT_GPU_READ  0x10
#endif
#ifndef SCE_KERNEL_PROT_GPU_WRITE
#define SCE_KERNEL_PROT_GPU_WRITE 0x20
#endif

/* WC Garlic memory type (same as PS4) */
#define SCE_KERNEL_WC_GARLIC 3

/* Kernel functions from libkernel.so stub */
int sceKernelAllocateDirectMemory(
    off_t searchStart, off_t searchEnd, size_t len, size_t alignment,
    int memoryType, off_t *directMemoryStart);
int sceKernelMapDirectMemory(
    void **virtualAddress, size_t len, int prot, int flags,
    off_t directMemoryStart, size_t alignment);
int sceKernelReleaseDirectMemory(off_t directMemoryStart, size_t len);
off_t sceKernelGetDirectMemorySize(void);

/* Event queue (same as PS4) */
typedef int SceKernelEqueue;
typedef struct { char _opaque[64]; } SceKernelEvent;
int sceKernelCreateEqueue(SceKernelEqueue *equeue, const char *name);
int sceKernelDeleteEqueue(SceKernelEqueue equeue);
int sceKernelWaitEqueue(SceKernelEqueue equeue, SceKernelEvent *events,
                        int numEvents, int *out, void *timeout);

enum {
    BUFFER_COUNT = 2,
    BYTES_PER_PIXEL = 4,
    DIRECT_MEMORY_ALIGNMENT = 64 * 1024,
};

typedef struct {
    int handle;
    SceKernelEqueue flipqueue;
    off_t direct_memory;
    void *mapped;
    size_t mapped_size;
    uint8_t *buffers[BUFFER_COUNT];
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    size_t buffer_stride;
} VideoOutTest;

static size_t align_up(size_t value, size_t alignment) {
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

static uint32_t pack_a8b8g8r8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
           ((uint32_t)a << 24);
}

static uint32_t color_bar(unsigned index, uint64_t frame) {
    static const uint32_t colors[] = {
        0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff,
        0xff00ffff, 0xffffff00, 0xffff00ff, 0xff000000,
    };
    const unsigned count = sizeof(colors) / sizeof(colors[0]);
    return colors[(index + (unsigned)(frame / 60)) % count];
}

static void fill_pattern(uint8_t *buffer, uint32_t width, uint32_t height,
                         uint32_t pitch_pixels, uint64_t frame) {
    const unsigned bar_count = 8;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(buffer +
            (size_t)y * pitch_pixels * BYTES_PER_PIXEL);
        for (uint32_t x = 0; x < width; x++) {
            const unsigned bar = (unsigned)(((uint64_t)x * bar_count) / width);
            uint32_t color = color_bar(bar, frame);
            if (y < height / 12) {
                const uint8_t phase = (uint8_t)((x + frame * 8) & 0xff);
                color = pack_a8b8g8r8(phase, 255 - phase, 0x40, 0xff);
            }
            row[x] = color;
        }
    }
}

static bool allocate_buffers(VideoOutTest *test) {
    const size_t buffer_size =
        (size_t)test->pitch_pixels * test->height * BYTES_PER_PIXEL;
    test->buffer_stride = align_up(buffer_size, DIRECT_MEMORY_ALIGNMENT);
    test->mapped_size = test->buffer_stride * BUFFER_COUNT;

    int res = sceKernelAllocateDirectMemory(
        0, sceKernelGetDirectMemorySize(), test->mapped_size,
        DIRECT_MEMORY_ALIGNMENT, SCE_KERNEL_WC_GARLIC, &test->direct_memory
    );
    if (res != 0) {
        printf("sceKernelAllocateDirectMemory failed: 0x%x\n", res);
        return false;
    }

    const int prot = SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW |
                     SCE_KERNEL_PROT_GPU_READ;
    res = sceKernelMapDirectMemory(
        &test->mapped, test->mapped_size, prot, 0,
        test->direct_memory, DIRECT_MEMORY_ALIGNMENT
    );
    if (res != 0) {
        printf("sceKernelMapDirectMemory failed: 0x%x\n", res);
        sceKernelReleaseDirectMemory(test->direct_memory, test->mapped_size);
        return false;
    }

    for (unsigned i = 0; i < BUFFER_COUNT; i++) {
        test->buffers[i] = (uint8_t *)test->mapped + i * test->buffer_stride;
    }
    return true;
}

static bool init_video(VideoOutTest *test) {
    test->handle = sceVideoOutOpen(0, SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, NULL);
    if (test->handle < 0) {
        printf("sceVideoOutOpen failed: 0x%x\n", test->handle);
        return false;
    }

    SceVideoOutResolutionStatus status = {0};
    int res = sceVideoOutGetResolutionStatus(test->handle, &status);
    if (res != 0) {
        printf("sceVideoOutGetResolutionStatus failed: 0x%x\n", res);
        return false;
    }
    test->width = status.full_width ? status.full_width : 1920;
    test->height = status.full_height ? status.full_height : 1080;
    test->pitch_pixels = test->width;

    if (!allocate_buffers(test)) {
        return false;
    }

    SceVideoOutBufferAttribute attr = {0};
    sceVideoOutSetBufferAttribute(
        &attr, SCE_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
        SCE_VIDEO_OUT_TILING_MODE_LINEAR, SCE_VIDEO_OUT_ASPECT_RATIO_16_9,
        test->width, test->height, test->pitch_pixels
    );

    void *addresses[BUFFER_COUNT] = {test->buffers[0], test->buffers[1]};
    res = sceVideoOutRegisterBuffers(
        test->handle, 0, addresses, BUFFER_COUNT, &attr);
    if (res < 0) {
        printf("sceVideoOutRegisterBuffers failed: 0x%x\n", res);
        return false;
    }

    res = sceKernelCreateEqueue(&test->flipqueue, "videoout linear flips");
    if (res != 0) {
        printf("sceKernelCreateEqueue failed: 0x%x\n", res);
        return false;
    }
    res = sceVideoOutAddFlipEvent((void *)(uintptr_t)test->flipqueue,
                                  test->handle, NULL);
    if (res != 0) {
        printf("sceVideoOutAddFlipEvent failed: 0x%x\n", res);
        return false;
    }

    sceVideoOutSetFlipRate(test->handle, 0);
    printf("VideoOut linear smoke: %ux%u pitch=%u stride=%zu\n",
           test->width, test->height, test->pitch_pixels, test->buffer_stride);
    return true;
}

static void shutdown_video(VideoOutTest *test) {
    if (test->handle >= 0) {
        sceVideoOutClose(test->handle);
    }
    if (test->flipqueue) {
        sceKernelDeleteEqueue(test->flipqueue);
    }
    if (test->direct_memory >= 0 && test->mapped_size != 0) {
        sceKernelReleaseDirectMemory(test->direct_memory, test->mapped_size);
    }
}

int main(void) {
    VideoOutTest test = {
        .handle = -1,
        .direct_memory = -1,
    };

    printf("=== openagc VideoOut linear smoke test ===\n");

    if (!init_video(&test)) {
        shutdown_video(&test);
        return 1;
    }

    printf("Display initialized, starting flip loop...\n");

    for (uint64_t frame = 0;; frame++) {
        const unsigned index = (unsigned)(frame % BUFFER_COUNT);
        fill_pattern(test.buffers[index], test.width, test.height,
                     test.pitch_pixels, frame);

        int res = sceVideoOutSubmitFlip(
            test.handle, (int)index, SCE_VIDEO_OUT_FLIP_MODE_VSYNC,
            (int64_t)frame
        );
        if (res != 0) {
            printf("sceVideoOutSubmitFlip failed: 0x%x\n", res);
            break;
        }

        SceKernelEvent event = {0};
        int out = 0;
        res = sceKernelWaitEqueue(test.flipqueue, &event, 1, &out, NULL);
        if (res != 0) {
            printf("sceKernelWaitEqueue failed: 0x%x\n", res);
            break;
        }

        if ((frame % 60) == 0) {
            printf("Frame %llu\n", (unsigned long long)frame);
        }
    }

    shutdown_video(&test);
    return 1;
}
