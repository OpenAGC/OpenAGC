/* eboot_patch.c — Color bar payload to embed in a game's eboot.bin.
 *
 * This code runs in the game's own thread context, so it has full VideoOut
 * access. It opens its own VideoOut handle, registers linear buffers, fills
 * them with SMPTE color bars, and flips forever.
 */

#include <stdint.h>
#include <stddef.h>

/* Hardcoded library bases for PPSA02453 runtime on the target firmware.
 * These are stable in the memory maps we've observed. */
#define VO_BASE  0x811924000ULL
#define K_BASE   0x800000000ULL

/* Function offsets in libSceVideoOut.sprx */
#define VO_OPEN           0x14f80
#define VO_REGISTER       0x15140
#define VO_SUBMIT_FLIP    0x11a10
#define VO_ADD_FLIP_EVENT 0x110d0
#define VO_GET_FLIP_STATUS 0x12fd0

/* Linear tiling bypass in libSceVideoOut.sprx */
#define VO_PATCH_OFFSET   0x7e61
#define VO_PATCH_LEN      6

/* Function offsets in libkernel.sprx */
#define K_ALLOC_DIRECT   0x3de0
#define K_MAP_DIRECT     0xdac0
#define K_CREATE_EQUEUE  0x1ae60
#define K_WAIT_EQUEUE    0x9510
#define K_USLEEP         0x27770

#define FB_WIDTH   1920
#define FB_HEIGHT  1080
#define NUM_COLORS 8

static const uint32_t COLORS[NUM_COLORS] = {
    0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
    0xFFFF00FF, 0xFFFF0000, 0xFF0000FF, 0xFF000000,
};

typedef int  (*sceVideoOutOpen_t)(int userId, int type, int index, void *param);
typedef int  (*sceVideoOutRegisterBuffers_t)(int handle, int startIndex,
        void **addresses, int count, const void *attr);
typedef int  (*sceVideoOutSubmitFlip_t)(int handle, int bufferIndex,
        int flipMode, int64_t flipArg);
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

/* Attribute layout expected by sceVideoOutRegisterBuffers (PS5 SDK
 * SceVideoOutBufferAttribute is 0x20 bytes). */
typedef struct {
    uint32_t pixel_format;
    uint32_t tiling_mode;
    uint32_t aspect_ratio;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_in_pixel;
    uint32_t option;
    uint32_t reserved;
} SceVideoOutBufferAttribute;

void colorbars_payload(volatile uint64_t *trace) {
    /* Local stack storage */
    SceVideoOutBufferAttribute attr;
    uint64_t queue;
    uint8_t event[128] __attribute__((aligned(16)));
    int out;
    int64_t phys;
    void *cpu_addr;
    void *addresses[2];

#define TRACE(v) do { if (trace) { *trace++ = (v); } } while (0)
    TRACE(0x100);

    sceVideoOutOpen_t                 vo_open           = (sceVideoOutOpen_t)                 (VO_BASE + VO_OPEN);
    sceVideoOutRegisterBuffers_t      vo_register       = (sceVideoOutRegisterBuffers_t)      (VO_BASE + VO_REGISTER);
    sceVideoOutSubmitFlip_t           vo_submit_flip    = (sceVideoOutSubmitFlip_t)           (VO_BASE + VO_SUBMIT_FLIP);
    sceVideoOutAddFlipEvent_t         vo_add_flip_event = (sceVideoOutAddFlipEvent_t)         (VO_BASE + VO_ADD_FLIP_EVENT);
    sceVideoOutGetFlipStatus_t        vo_get_flip_status = (sceVideoOutGetFlipStatus_t)        (VO_BASE + VO_GET_FLIP_STATUS);
    sceKernelAllocateDirectMemory_t   k_alloc_direct    = (sceKernelAllocateDirectMemory_t)     (K_BASE + K_ALLOC_DIRECT);
    sceKernelMapDirectMemory_t        k_map_direct      = (sceKernelMapDirectMemory_t)          (K_BASE + K_MAP_DIRECT);
    sceKernelCreateEqueue_t           k_create_equeue   = (sceKernelCreateEqueue_t)             (K_BASE + K_CREATE_EQUEUE);
    sceKernelWaitEqueue_t             k_wait_equeue     = (sceKernelWaitEqueue_t)               (K_BASE + K_WAIT_EQUEUE);

    /* 0. Patch libSceVideoOut linear tiling check. The caller must make the
     * libSceVideoOut code page writable before invoking this payload. */
    {
        uint8_t *p = (uint8_t *)(VO_BASE + VO_PATCH_OFFSET);
        for (int i = 0; i < VO_PATCH_LEN; i++) p[i] = 0x90;  /* NOP */
    }
    TRACE(0x101);

    /* 1. Open VideoOut. If the game already has a handle (typical), open fails;
     * scan 0..255 for an existing handle via sceVideoOutGetFlipStatus. */
    int handle = vo_open(0xff, 0, 0, NULL);
    TRACE(0x200 | (uint64_t)(uint32_t)handle);
    if (handle < 0) {
        uint8_t status[64] __attribute__((aligned(16)));
        handle = -1;
        for (int h = 0; h < 256; h++) {
            uint32_t ret = (uint32_t)vo_get_flip_status(h, status);
            /* 0 = valid; 0x80290401 may mean valid handle with no flip yet. */
            if (ret == 0 || ret == 0x80290401U) {
                handle = h;
                TRACE(0x300 | (uint64_t)(uint32_t)h);
                break;
            }
            if (h < 4) TRACE(0x400 | ((uint64_t)(uint32_t)h << 32) | (uint64_t)ret);
        }
        if (handle < 0) { TRACE(0x1FF); return; }
    }

    /* 2. Allocate direct memory (4MB for 2 buffers) */
    int ret = k_alloc_direct(0, 0x300000000ULL, 0x400000, 0x200000, 3, &phys);
    TRACE(0x500 | (uint64_t)(uint32_t)ret);
    if (ret != 0) return;

    /* 3. Map direct memory */
    ret = k_map_direct(&cpu_addr, 0x400000, 3, 0, phys, 0x200000);
    TRACE(0x600 | (uint64_t)(uint32_t)ret);
    if (ret != 0) return;
    TRACE(0x700 | (uint64_t)(uintptr_t)cpu_addr);

    /* 4. Fill attribute manually (sceVideoOutSetBufferAttribute is a stub in this game) */
    attr.pixel_format   = 0x80000000;  /* A8R8G8B8_SRGB */
    attr.tiling_mode    = 1;            /* LINEAR */
    attr.aspect_ratio   = 0;            /* 16:9 */
    attr.width          = FB_WIDTH;
    attr.height         = FB_HEIGHT;
    attr.pitch_in_pixel = FB_WIDTH;
    attr.option         = 0;
    attr.reserved       = 0;

    /* 5. Register buffers at a high index to avoid the game's own buffers. */
    addresses[0] = cpu_addr;
    addresses[1] = (uint8_t *)cpu_addr + 0x200000;
    int base_index = 16;
    ret = vo_register(handle, base_index, addresses, 2, &attr);
    TRACE(0x800 | (uint64_t)(uint32_t)ret);
    if (ret != 0) {
        base_index = 0;
        ret = vo_register(handle, base_index, addresses, 2, &attr);
        TRACE(0x900 | (uint64_t)(uint32_t)ret);
        if (ret != 0) return;
    }

    /* 6. Create event queue and add flip event */
    ret = k_create_equeue(&queue, "colorbars");
    TRACE(0xA00 | (uint64_t)(uint32_t)ret);
    if (ret != 0) return;
    ret = vo_add_flip_event(handle, &queue, NULL);
    TRACE(0xB00 | (uint64_t)(uint32_t)ret);
    if (ret != 0) return;
    TRACE(0xC00);

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

    /* 8. Flip loop forever */
    for (int64_t frame = 0; ; frame++) {
        int idx = base_index + (int)(frame % 2);
        int ret = vo_submit_flip(handle, idx, 1, frame);
        if (ret != 0) {
            idx = (int)(frame % 2);
            ret = vo_submit_flip(handle, idx, 1, frame);
            if (ret != 0) continue;
        }
        k_wait_equeue(&queue, event, 1, &out, NULL);
    }
}
