#include "ps5_platform.h"

#include <ps5/kernel.h>

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define GPU_AUTHID_REQUIRED 0x4801000000000000ull
#define DIRECT_MEMORY_SEARCH_END 0x300000000ull
#define DIRECT_MEMORY_ALIGNMENT 0x200000u
#define SCE_KERNEL_WC_GARLIC 3
#define SCE_KERNEL_PROT_CPU_READ 0x01
#define SCE_KERNEL_PROT_CPU_RW 0x02
#define SCE_KERNEL_PROT_GPU_READ 0x10
#define SCE_KERNEL_PROT_GPU_WRITE 0x20
#define SCE_VIDEO_OUT_BUS_TYPE_MAIN 0
#define SCE_VIDEO_OUT_FLIP_MODE_VSYNC 1

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

int32_t sceVideoOutOpen(
    int32_t user_id, int32_t bus_type, int32_t index, const void *param);
int32_t sceVideoOutClose(int32_t handle);
int32_t sceVideoOutRegisterBuffers(
    int32_t handle, int32_t start_index, void *const *addresses,
    int32_t buffer_count, const SceVideoOutBufferAttribute *attribute);
int32_t sceVideoOutSubmitFlip(
    int32_t handle, int32_t buffer_index, int32_t flip_mode,
    int64_t flip_argument);
int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
int32_t sceVideoOutAddFlipEvent(
    void *event_queue, int32_t handle, void *data);

int sceKernelAllocateDirectMemory(
    off_t search_start, off_t search_end, size_t length, size_t alignment,
    int memory_type, off_t *direct_memory_start);
int sceKernelMapDirectMemory(
    void **virtual_address, size_t length, int protection, int flags,
    off_t direct_memory_start, size_t alignment);
int sceKernelReleaseDirectMemory(off_t direct_memory_start, size_t length);
int sceKernelMapNamedSystemFlexibleMemory(
    void **virtual_address, size_t length, int protection, int flags,
    const char *name);
int sceKernelCreateEqueue(SceKernelEqueue *event_queue, const char *name);
int sceKernelWaitEqueue(
    SceKernelEqueue event_queue, SceKernelEvent *events, int count,
    void *timeout, void *result);
int sceKernelDeleteEqueue(SceKernelEqueue event_queue);
int sceKernelUsleep(unsigned int microseconds);

int kernel_dynlib_handle(int pid, const char *name, uint32_t *handle);
intptr_t kernel_dynlib_mapbase_addr(int pid, uint32_t handle);
int kernel_mprotect(int pid, intptr_t address, size_t size, int protection);

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1u) / alignment * alignment;
}

static int patch_videoout(Ps5CubePlatform *platform)
{
    uint32_t handle = 0u;
    intptr_t base;
    volatile uint8_t *patch;

    if (kernel_dynlib_handle(-1, "libSceVideoOut.sprx", &handle) != 0 ||
        handle == 0u)
        return -1;
    base = kernel_dynlib_mapbase_addr(-1, handle);
    if (base == 0)
        return -1;
    platform->videoout_patch_address = (uintptr_t)(base + 0x7e61);
    patch = (volatile uint8_t *)platform->videoout_patch_address;
    if (kernel_mprotect(-1,
            (intptr_t)(platform->videoout_patch_address & ~(uintptr_t)0xfffu),
            0x2000u, SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW | 0x4) != 0)
        return -1;
    memcpy(platform->videoout_patch_bytes, (const void *)patch,
        sizeof(platform->videoout_patch_bytes));
    for (size_t i = 0u; i < sizeof(platform->videoout_patch_bytes); ++i)
        patch[i] = 0x90u;
    kernel_mprotect(-1,
        (intptr_t)(platform->videoout_patch_address & ~(uintptr_t)0xfffu),
        0x2000u, SCE_KERNEL_PROT_CPU_READ | 0x4);
    platform->videoout_patched = 1u;
    return 0;
}

static void restore_videoout(Ps5CubePlatform *platform)
{
    volatile uint8_t *patch;

    if (!platform->videoout_patched)
        return;
    patch = (volatile uint8_t *)platform->videoout_patch_address;
    if (kernel_mprotect(-1,
            (intptr_t)(platform->videoout_patch_address & ~(uintptr_t)0xfffu),
            0x2000u, SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW | 0x4) == 0) {
        memcpy((void *)patch, platform->videoout_patch_bytes,
            sizeof(platform->videoout_patch_bytes));
        kernel_mprotect(-1,
            (intptr_t)(platform->videoout_patch_address & ~(uintptr_t)0xfffu),
            0x2000u, SCE_KERNEL_PROT_CPU_READ | 0x4);
    }
    platform->videoout_patched = 0u;
}

int ps5CubePlatformInitialize(Ps5CubePlatform *platform)
{
    const int32_t user_ids[] = {0xff, 0, 1, 2};
    const size_t frame_bytes =
        (size_t)PS5_CUBE_WIDTH * PS5_CUBE_HEIGHT * sizeof(uint32_t);
    SceVideoOutBufferAttribute attribute;
    int protection;
    int error;

    if (!platform)
        return -1;
    memset(platform, 0, sizeof(*platform));
    platform->video_handle = -1;
    platform->direct_memory = -1;
    printf("[Platform] applying GPU process credentials\n");
    platform->original_authid = kernel_get_ucred_authid(getpid());
    if (kernel_set_ucred_authid(getpid(), GPU_AUTHID_REQUIRED) != 0 ||
        kernel_get_ucred_authid(getpid()) != GPU_AUTHID_REQUIRED)
        goto fail;
    platform->authid_changed = 1u;

    printf("[Platform] opening VideoOut\n");
    for (size_t i = 0u; i < sizeof(user_ids) / sizeof(user_ids[0]); ++i) {
        platform->video_handle = sceVideoOutOpen(
            user_ids[i], SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, NULL);
        if (platform->video_handle >= 0)
            break;
    }
    if (platform->video_handle < 0)
        goto fail;
    printf("[Platform] enabling linear VideoOut registration\n");
    if (patch_videoout(platform) != 0)
        goto fail;

    printf("[Platform] allocating three garlic display buffers\n");
    platform->display_stride = align_up(frame_bytes, DIRECT_MEMORY_ALIGNMENT);
    platform->display_mapping_size =
        platform->display_stride * PS5_CUBE_BUFFER_COUNT;
    error = sceKernelAllocateDirectMemory(
        0, (off_t)DIRECT_MEMORY_SEARCH_END, platform->display_mapping_size,
        DIRECT_MEMORY_ALIGNMENT, SCE_KERNEL_WC_GARLIC,
        &platform->direct_memory);
    if (error != 0)
        goto fail;
    protection = SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW |
        SCE_KERNEL_PROT_GPU_READ | SCE_KERNEL_PROT_GPU_WRITE;
    error = sceKernelMapDirectMemory(
        &platform->display_mapping, platform->display_mapping_size,
        protection, 0, platform->direct_memory, DIRECT_MEMORY_ALIGNMENT);
    if (error != 0)
        goto fail;
    for (uint32_t i = 0u; i < PS5_CUBE_BUFFER_COUNT; ++i)
        platform->display_buffers[i] =
            (uint8_t *)platform->display_mapping + i * platform->display_stride;

    memset(&attribute, 0, sizeof(attribute));
    attribute.pixel_format = 0x80000000u;
    attribute.tiling_mode = 1u;
    attribute.width = PS5_CUBE_WIDTH;
    attribute.height = PS5_CUBE_HEIGHT;
    attribute.pitch_in_pixel = PS5_CUBE_WIDTH;
    printf("[Platform] registering VideoOut buffers\n");
    error = sceVideoOutRegisterBuffers(
        platform->video_handle, 0, platform->display_buffers,
        PS5_CUBE_BUFFER_COUNT, &attribute);
    if (error != 0)
        goto fail;
    platform->display_registered = 1u;

    printf("[Platform] setting VideoOut flip rate\n");
    if (sceVideoOutSetFlipRate(platform->video_handle, 0) != 0)
        goto fail;
    printf("[Platform] initialization complete\n");
    return 0;

fail:
    ps5CubePlatformShutdown(platform);
    return -1;
}

void ps5CubePlatformShutdown(Ps5CubePlatform *platform)
{
    if (!platform)
        return;
    printf("[Platform] shutdown begin\n");
    if (platform->flip_queue_created) {
        sceKernelDeleteEqueue(platform->flip_queue);
        platform->flip_queue_created = 0u;
    }
    if (platform->video_handle >= 0) {
        sceVideoOutClose(platform->video_handle);
        platform->video_handle = -1;
    }
    restore_videoout(platform);
    if (platform->display_mapping) {
        munmap(platform->display_mapping, platform->display_mapping_size);
        platform->display_mapping = NULL;
    }
    if (platform->direct_memory >= 0) {
        sceKernelReleaseDirectMemory(
            platform->direct_memory, platform->display_mapping_size);
        platform->direct_memory = -1;
    }
    if (platform->authid_changed) {
        kernel_set_ucred_authid(getpid(), platform->original_authid);
        platform->authid_changed = 0u;
    }
    printf("[Platform] shutdown complete\n");
}

int ps5CubePlatformMapFlexible(void **address, size_t size, const char *name)
{
    if (!address || !size)
        return -1;
    *address = NULL;
    return sceKernelMapNamedSystemFlexibleMemory(
        address, size,
        SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW |
            SCE_KERNEL_PROT_GPU_READ | SCE_KERNEL_PROT_GPU_WRITE,
        0, name);
}

void ps5CubePlatformUnmapFlexible(void *address, size_t size)
{
    if (address && size)
        munmap(address, size);
}

int ps5CubePlatformPresent(
    Ps5CubePlatform *platform, uint32_t buffer_index, uint64_t frame_number)
{
    int error;

    if (!platform || buffer_index >= PS5_CUBE_BUFFER_COUNT)
        return -1;
    if (frame_number == 0u)
        printf("[Platform] submitting first VideoOut flip\n");
    error = sceVideoOutSubmitFlip(
        platform->video_handle, (int32_t)buffer_index,
        SCE_VIDEO_OUT_FLIP_MODE_VSYNC, (int64_t)frame_number);
    if (error != 0)
        return error;
    if (frame_number == 0u)
        printf("[Platform] first flip accepted\n");
    error = sceKernelUsleep(17000u);
    if (frame_number == 0u)
        printf("[Platform] first flip interval result=0x%08x\n",
            (unsigned)error);
    return error;
}

int ps5CubePlatformWaitFence(
    volatile const uint32_t *fence, uint32_t value, uint32_t timeout_us)
{
    uint32_t waited = 0u;

    if (!fence)
        return -1;
    while (*fence != value && waited < timeout_us) {
        sceKernelUsleep(1000u);
        waited += 1000u;
    }
    return *fence == value ? 0 : -1;
}
