/* colorbars_thread.c — Color bar payload for injection into a running game.
 *
 * This code is compiled and its .text is extracted and written into the game's
 * memory. It's called via CMD_PROC_CALL with a pointer to a function table.
 *
 * The function never returns — it runs an infinite flip loop displaying
 * SMPTE color bars. The calling thread is hijacked by the debugger, but
 * the game's other threads continue running.
 */

#include <stdint.h>
#include <stddef.h>

/* Function pointer types */
typedef int  (*sceVideoOutOpen_t)(int userId, int type, int index, void *param);
typedef void (*sceVideoOutSetBufferAttribute_t)(void *attr, int pixelFormat,
        int tilingMode, int aspectRatio, int width, int height, int pitchInPixels);
typedef int  (*sceVideoOutRegisterBuffers_t)(int handle, int startIndex,
        void **addresses, int count, void *attr);
typedef int  (*sceVideoOutSubmitFlip_t)(int handle, int bufferIndex,
        int flipMode, int64_t flipArg);
typedef int  (*sceVideoOutGetResolutionStatus_t)(int handle, void *status);
typedef int  (*sceVideoOutAddFlipEvent_t)(int handle, void *queue, void *param);
typedef int  (*sceVideoOutGetFlipStatus_t)(int handle, void *status);
typedef int  (*sceKernelAllocateDirectMemory_t)(int64_t searchStart,
        int64_t searchEnd, size_t len, size_t alignment, int memoryType,
        int64_t *directMemoryStart);
typedef int  (*sceKernelMapDirectMemory_t)(void **addr, size_t len, int prot,
        int flags, int64_t directMemoryStart, size_t alignment);
typedef int  (*sceKernelCreateEqueue_t)(void *eq, const char *name);
typedef int  (*sceKernelWaitEqueue_t)(void *eq, void *event, int count,
        int *out, void *timeout);
typedef int  (*sceKernelUsleep_t)(unsigned int us);

struct func_table {
    sceVideoOutOpen_t                 open;
    sceVideoOutSetBufferAttribute_t   set_attr;
    sceVideoOutRegisterBuffers_t      register_bufs;
    sceVideoOutSubmitFlip_t           submit_flip;
    sceVideoOutAddFlipEvent_t         add_flip_event;
    sceKernelAllocateDirectMemory_t   alloc_direct;
    sceKernelMapDirectMemory_t        map_direct;
    sceKernelCreateEqueue_t           create_equeue;
    sceKernelWaitEqueue_t             wait_equeue;
    sceKernelUsleep_t                 usleep;
    sceVideoOutGetFlipStatus_t        get_flip_status;
};

/* Debug trace buffer — written to a known address for debugging */
volatile uint64_t trace[16];
volatile uint64_t trace_idx = 0;

static inline void TRACE(uint64_t val) {
    if (trace_idx < 16) trace[trace_idx++] = val;
}

/* SMPTE color bars (ARGB) */
static const uint32_t COLORS[] = {
    0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
    0xFFFF00FF, 0xFFFF0000, 0xFF0000FF, 0xFF000000,
};
#define NUM_COLORS 8
#define FB_WIDTH   1920
#define FB_HEIGHT  1080
#define BUFFER_SIZE (FB_WIDTH * FB_HEIGHT * 4)
#define ATTR_SIZE  0x50

void colorbars_main(struct func_table *funcs) {
    uint8_t attr[ATTR_SIZE] __attribute__((aligned(16)));
    uint64_t queue;
    uint8_t event[128] __attribute__((aligned(16)));
    int out;
    int64_t phys;
    void *cpu_addr;
    void *addresses[2];
    uint8_t flip_status[64] __attribute__((aligned(16)));

    /* 1. Try to open a new VideoOut handle. */
    int handle = funcs->open(0xff, 0, 0, NULL);
    TRACE(0x1000 | (uint64_t)handle);

    if (handle < 0) {
        /* Scan for existing handle — try values 1..256.
         * sceVideoOutGetFlipStatus returns 0 for a valid handle, even if no
         * flip has been submitted yet. For invalid handles it returns an error. */
        for (int h = 1; h <= 256; h++) {
            int ret = funcs->get_flip_status(h, flip_status);
            if (ret == 0) {
                handle = h;
                TRACE(0x1001 | (uint64_t)h);
                break;
            }
            if (h <= 4) {
                TRACE(0x1002 | ((uint64_t)h << 32) | (uint64_t)(uint32_t)ret);
            }
        }
        if (handle < 0) { TRACE(0x1FFF); return; }
    }

    /* 2. Allocate direct memory (4MB for 2 buffers) */
    int ret = funcs->alloc_direct(0, 0x300000000ULL, 0x400000, 0x200000, 3, &phys);
    TRACE(0x2000 | (uint64_t)ret);
    if (ret != 0) return;

    /* 3. Map direct memory */
    ret = funcs->map_direct(&cpu_addr, 0x400000, 3, 0, phys, 0x200000);
    TRACE(0x3000 | (uint64_t)ret);
    if (ret != 0) return;

    TRACE(0x3001 | (uint64_t)(uintptr_t)cpu_addr);

    /* 4. Set buffer attribute (linear tiling) */
    for (int i = 0; i < ATTR_SIZE; i++) attr[i] = 0;
    funcs->set_attr(attr, 0x80000000, 1, 0, FB_WIDTH, FB_HEIGHT, FB_WIDTH);

    /* 5. Register 2 buffers at a high index (to avoid conflicting with game's buffers) */
    addresses[0] = cpu_addr;
    addresses[1] = (uint8_t *)cpu_addr + 0x200000;
    ret = funcs->register_bufs(handle, 16, addresses, 2, attr);
    TRACE(0x4000 | (uint64_t)ret);
    if (ret != 0) {
        ret = funcs->register_bufs(handle, 0, addresses, 2, attr);
        TRACE(0x4001 | (uint64_t)ret);
        if (ret != 0) return;
    }

    /* 6. Create event queue and add flip event */
    ret = funcs->create_equeue(&queue, "colorbars");
    TRACE(0x5000 | (uint64_t)ret);
    if (ret != 0) return;

    ret = funcs->add_flip_event(handle, &queue, NULL);
    TRACE(0x6000 | (uint64_t)ret);
    if (ret != 0) return;

    /* 7. Fill both buffers with color bars */
    uint32_t *fb0 = (uint32_t *)cpu_addr;
    uint32_t *fb1 = (uint32_t *)((uint8_t *)cpu_addr + 0x200000);
    int bar_width = FB_WIDTH / NUM_COLORS;

    for (int y = 0; y < FB_HEIGHT; y++) {
        uint32_t *row0 = fb0 + y * FB_WIDTH;
        uint32_t *row1 = fb1 + y * FB_WIDTH;
        for (int x = 0; x < FB_WIDTH; x++) {
            int bar_idx = x / bar_width;
            if (bar_idx >= NUM_COLORS) bar_idx = NUM_COLORS - 1;
            row0[x] = COLORS[bar_idx];
            row1[x] = COLORS[bar_idx];
        }
    }

    TRACE(0x7000);

    /* 8. Flip loop */
    int base_idx = 16;
    for (int64_t frame = 0; ; frame++) {
        int idx = base_idx + (int)(frame % 2);
        ret = funcs->submit_flip(handle, idx, 1, frame);
        if (ret != 0) {
            TRACE(0x8000 | (uint64_t)ret);
            base_idx = 0;
            idx = (int)(frame % 2);
            ret = funcs->submit_flip(handle, idx, 1, frame);
            if (ret != 0) {
                TRACE(0x8001 | (uint64_t)ret);
                funcs->usleep(16000);
                continue;
            }
        }
        if (frame == 0) TRACE(0x9000);
        funcs->wait_equeue(&queue, event, 1, &out, NULL);
    }
}

int main(void) { return 0; }

