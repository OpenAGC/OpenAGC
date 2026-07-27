#ifndef OPENAGC_CUBE_PS5_PLATFORM_H
#define OPENAGC_CUBE_PS5_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PS5_CUBE_BUFFER_COUNT 3u
#define PS5_CUBE_WIDTH 1920u
#define PS5_CUBE_HEIGHT 1080u

typedef int SceKernelEqueue;
typedef struct SceKernelEvent {
    char opaque[64];
} SceKernelEvent;

typedef struct Ps5CubePlatform {
    int32_t video_handle;
    SceKernelEqueue flip_queue;
    off_t direct_memory;
    void *display_mapping;
    size_t display_mapping_size;
    size_t display_stride;
    void *display_buffers[PS5_CUBE_BUFFER_COUNT];
    uint64_t original_authid;
    uintptr_t videoout_patch_address;
    uint8_t videoout_patch_bytes[6];
    uint32_t authid_changed;
    uint32_t videoout_patched;
    uint32_t display_registered;
    uint32_t flip_queue_created;
} Ps5CubePlatform;

int ps5CubePlatformInitialize(Ps5CubePlatform *platform);
void ps5CubePlatformShutdown(Ps5CubePlatform *platform);
int ps5CubePlatformMapFlexible(void **address, size_t size, const char *name);
void ps5CubePlatformUnmapFlexible(void *address, size_t size);
int ps5CubePlatformPresent(
    Ps5CubePlatform *platform, uint32_t buffer_index, uint64_t frame_number);
int ps5CubePlatformWaitFence(
    volatile const uint32_t *fence, uint32_t value, uint32_t timeout_us);

#endif

