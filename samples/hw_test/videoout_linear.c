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
#include <string.h>
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
/* WB Onion memory type (write-back coherent) */
#define SCE_KERNEL_WB_ONION  1

/* PS5-proven constants from PS5_DEV_HOMEBREW/examples/ps5_sdk/hello_square.c */
#define PS5_DIRECT_MEM_SEARCH_END  0x300000000ULL
#define PS5_DIRECT_MEM_ALIGNMENT   0x200000  /* 2 MB */

/* Kernel functions from libkernel.so stub */
int sceKernelUsleep(unsigned int microseconds);
void thr_exit(long *state);
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
    /* PS5 tiled buffers require 2MB alignment */
    DIRECT_MEMORY_ALIGNMENT = 0x200000,
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

int kernel_dynlib_handle(int pid, const char *name, uint32_t *handle);
intptr_t kernel_dynlib_mapbase_addr(int pid, uint32_t handle);
int kernel_mprotect(int pid, intptr_t addr, size_t size, int prot);

static bool patch_videoout_linear(void) {
    uint32_t handle = 0;
    intptr_t base;
    intptr_t address;
    volatile uint8_t *code;

    if (kernel_dynlib_handle(-1, "libSceVideoOut.sprx", &handle) != 0 ||
        handle == 0) {
        printf("libSceVideoOut handle lookup failed\n");
        return false;
    }
    base = kernel_dynlib_mapbase_addr(-1, handle);
    if (base == 0) {
        printf("libSceVideoOut base lookup failed\n");
        return false;
    }

    address = base + 0x7e61;
    if (kernel_mprotect(
            -1, address & ~(intptr_t)0xfff, 0x2000,
            SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW | 0x4) != 0) {
        printf("libSceVideoOut patch mprotect(RWX) failed\n");
        return false;
    }
    code = (volatile uint8_t *)address;
    for (uint32_t i = 0; i < 6u; ++i)
        code[i] = 0x90;
    if (kernel_mprotect(
            -1, address & ~(intptr_t)0xfff, 0x2000,
            SCE_KERNEL_PROT_CPU_READ | 0x4) != 0) {
        printf("libSceVideoOut patch mprotect(RX) failed\n");
        return false;
    }
    printf("Patched libSceVideoOut linear check at +0x7e61\n");
    return true;
}

static size_t align_up(size_t value, size_t alignment) {
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

static void fill_pattern(uint8_t *buffer, uint32_t width, uint32_t height,
                         uint32_t pitch_pixels, uint64_t frame) {
    /* Fill the entire buffer with a single solid color.
     * In tiled mode, a uniform fill displays correctly regardless of
     * the tile layout because every pixel has the same value.
     * Cycle through red, green, blue, white every 60 frames. */
    static const uint32_t solid_colors[] = {
        0xff0000ff,  /* red */
        0xff00ff00,  /* green */
        0xffff0000,  /* blue */
        0xffffffff,  /* white */
    };
    const unsigned ci = (unsigned)(frame / 60) % 4;
    const uint32_t color = solid_colors[ci];
    const size_t total = (size_t)pitch_pixels * height * BYTES_PER_PIXEL;
    /* Fast memset-style fill using 32-bit writes */
    uint32_t *p = (uint32_t *)buffer;
    const size_t count = total / 4;
    (void)width;
    for (size_t i = 0; i < count; i++)
        p[i] = color;
}

static bool allocate_buffers(VideoOutTest *test) {
    const size_t buffer_size =
        (size_t)test->pitch_pixels * test->height * BYTES_PER_PIXEL;
    test->buffer_stride = align_up(buffer_size, DIRECT_MEMORY_ALIGNMENT);
    test->mapped_size = test->buffer_stride * BUFFER_COUNT;

    /* GARLIC (type=3, WC) — same as PS5_DEV_HOMEBREW examples */
    int res = sceKernelAllocateDirectMemory(
        0, (off_t)PS5_DIRECT_MEM_SEARCH_END, test->mapped_size,
        DIRECT_MEMORY_ALIGNMENT, SCE_KERNEL_WC_GARLIC, &test->direct_memory
    );
    if (res != 0) {
        printf("sceKernelAllocateDirectMemory failed: 0x%x\n", res);
        return false;
    }

    /* prot 0x33 = CPU_RW | GPU_RW — same as PS5_DEV_HOMEBREW examples */
    const int prot = 0x33;
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
    /* PS5 uses userId=0xFF (system/user default), not 0 like PS4.
     * Try multiple userIds like the PS5_DEV_HOMEBREW takeover_open. */
    int32_t user_ids[] = { 0xFF, 0, 1, 2 };
    test->handle = -1;
    for (int i = 0; i < 4; i++) {
        test->handle = sceVideoOutOpen(user_ids[i], SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, NULL);
        if (test->handle >= 0) {
            printf("sceVideoOutOpen(userId=0x%x) = %d\n", user_ids[i], test->handle);
            break;
        }
    }
    if (test->handle < 0) {
        printf("sceVideoOutOpen failed for all userIds: 0x%x\n", test->handle);
        return false;
    }

    SceVideoOutResolutionStatus status = {0};
    int res = sceVideoOutGetResolutionStatus(test->handle, &status);
    if (res != 0) {
        printf("sceVideoOutGetResolutionStatus failed: 0x%x\n", res);
        return false;
    }
    printf("Resolution: full=%dx%d pane=%dx%d\n",
           status.full_width, status.full_height,
           status.pane_width, status.pane_height);
    /* Use 1920x1080 like the working PS5_DEV_HOMEBREW examples */
    test->width = 1920;
    test->height = 1080;
    test->pitch_pixels = test->width;

    if (!allocate_buffers(test)) {
        return false;
    }

    /* Use linear tiling mode (mode 1) with pixel format 0x80000000.
     * This matches the pattern used by all working PS5_DEV_HOMEBREW examples.
     * The buffer attribute struct is written as raw bytes to match the
     * exact layout the kernel expects:
     *   offset 0:  pixel_format (0x80000000 = A8B8G8R8_SRGB)
     *   offset 4:  tiling_mode (1 = linear)
     *   offset 8:  aspect_ratio (0 = 16:9)
     *   offset 12: width
     *   offset 16: height
     *   offset 20: pitch_in_pixel
     *
     * FW 5.50 requires either the debug setting or the same runtime branch
     * patch used by the GPU validation samples. Keep this smoke test
     * self-contained so websrv qualification needs no external debugger. */
    if (!patch_videoout_linear())
        return false;

    uint8_t attr_raw[64];
    memset(attr_raw, 0, sizeof(attr_raw));
    *(uint32_t *)(attr_raw + 0)  = 0x80000000;  /* pixel format */
    *(uint32_t *)(attr_raw + 4)  = 1;           /* tiling mode = linear */
    *(uint32_t *)(attr_raw + 8)  = 0;           /* aspect ratio = 16:9 */
    *(uint32_t *)(attr_raw + 12) = test->width;
    *(uint32_t *)(attr_raw + 16) = test->height;
    *(uint32_t *)(attr_raw + 20) = test->pitch_pixels;

    void *addresses[BUFFER_COUNT] = {test->buffers[0], test->buffers[1]};
    res = sceVideoOutRegisterBuffers(
        test->handle, 0, addresses, BUFFER_COUNT,
        (const SceVideoOutBufferAttribute *)attr_raw);
    printf("sceVideoOutRegisterBuffers (linear): 0x%x\n", res);

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

    printf("Display initialized, starting 600-frame flip loop...\n");

    for (uint64_t frame = 0; frame < 600u; frame++) {
        const unsigned index = (unsigned)(frame % BUFFER_COUNT);
        fill_pattern(test.buffers[index], test.width, test.height,
                     test.pitch_pixels, frame);

        int res = sceVideoOutSubmitFlip(
            test.handle, (int)index, SCE_VIDEO_OUT_FLIP_MODE_VSYNC,
            (int64_t)frame
        );
        if (res != 0) {
            printf("sceVideoOutSubmitFlip failed: 0x%x\n", res);
            shutdown_video(&test);
            return 1;
        }

        SceKernelEvent event = {0};
        int out = 0;
        res = sceKernelWaitEqueue(test.flipqueue, &event, 1, &out, NULL);
        if (res != 0) {
            printf("sceKernelWaitEqueue failed: 0x%x\n", res);
            shutdown_video(&test);
            return 1;
        }

        if ((frame % 60) == 0)
            printf("Frame %llu\n", (unsigned long long)frame);
    }

    printf("VideoOut sustained smoke: 600/600 flips completed\n");
    shutdown_video(&test);
    return 0;
}
