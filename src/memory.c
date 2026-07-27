/* openagc — SPDX-License-Identifier: Apache-2.0 */
#include "agc_memory.h"
#include "agc_error.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AGC_FLEXIBLE_PAGE_SIZE 0x4000u
#define AGC_DIRECT_ALIGNMENT 0x200000u
#define AGC_DIRECT_SEARCH_END 0x300000000ll
#define AGC_CACHE_LINE_SIZE 64u

#if defined(OPENAGC_PROSPERO)
extern int32_t sceKernelMapNamedSystemFlexibleMemory(
    void **addr, size_t size, int type, int flags, const char *name);
extern int32_t sceKernelMunmap(void *addr, size_t len);
extern int32_t sceKernelUsleep(uint32_t microseconds);
extern int32_t sceKernelAllocateDirectMemory(
    int64_t search_start, int64_t search_end, size_t length,
    uint64_t alignment, int memory_type, int64_t *physical_address);
extern int32_t sceKernelMapDirectMemory(
    void **virtual_address, size_t length, int protection, int flags,
    int64_t physical_address, uint64_t alignment);
extern int32_t sceKernelReleaseDirectMemory(
    int64_t physical_address, size_t length);
#endif

static int32_t agcGpuMemoryValidateRange(
    const AgcGpuMemory *memory, size_t offset, size_t size)
{
    if (!memory || !memory->cpu_address || memory->size == 0u || size == 0u ||
        offset > memory->size || size > memory->size - offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGpuMemoryAllocateFlexible(
    AgcGpuMemory *memory, size_t size, size_t alignment, const char *name)
{
    size_t mapped_size;
    void *address = NULL;

    if (!memory || size == 0u || alignment == 0u ||
        (alignment & (alignment - 1u)) != 0u ||
        alignment > AGC_FLEXIBLE_PAGE_SIZE)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset(memory, 0, sizeof(*memory));
    if (size > SIZE_MAX - (AGC_FLEXIBLE_PAGE_SIZE - 1u))
        return AGC_ERROR_OUT_OF_MEMORY;
    mapped_size = (size + AGC_FLEXIBLE_PAGE_SIZE - 1u) &
        ~(size_t)(AGC_FLEXIBLE_PAGE_SIZE - 1u);

#if defined(OPENAGC_PROSPERO)
    if (sceKernelMapNamedSystemFlexibleMemory(
            &address, mapped_size, 0x33, 0,
            name ? name : "openagc_gpu") != 0 || !address)
        return AGC_ERROR_OUT_OF_MEMORY;
#else
    (void)name;
    if (posix_memalign(&address, AGC_FLEXIBLE_PAGE_SIZE, mapped_size) != 0)
        return AGC_ERROR_OUT_OF_MEMORY;
#endif
    if (((uintptr_t)address & (alignment - 1u)) != 0u) {
#if defined(OPENAGC_PROSPERO)
        sceKernelMunmap(address, mapped_size);
#else
        free(address);
#endif
        return AGC_ERROR_INVALID_ALIGNMENT;
    }
    memory->cpu_address = address;
    memory->gpu_address = (uint64_t)(uintptr_t)address;
    memory->size = size;
    memory->mapped_size = mapped_size;
    memory->type = AGC_GPU_MEMORY_TYPE_FLEXIBLE;
    return AGC_OK;
}

void PS5_SYSV_ABI agcGpuMemoryFreeFlexible(AgcGpuMemory *memory)
{
    if (!memory) return;
    if (memory->cpu_address && memory->mapped_size) {
#if defined(OPENAGC_PROSPERO)
        sceKernelMunmap(memory->cpu_address, memory->mapped_size);
#else
        free(memory->cpu_address);
#endif
    }
    memset(memory, 0, sizeof(*memory));
}

int32_t PS5_SYSV_ABI agcGpuMemoryAllocateDirectWriteCombined(
    AgcGpuMemory *memory, size_t size, size_t alignment)
{
    size_t mapped_size;
    void *address = NULL;
    int64_t physical = 0;

    if (!memory || size == 0u || alignment == 0u ||
        (alignment & (alignment - 1u)) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (alignment < AGC_DIRECT_ALIGNMENT) alignment = AGC_DIRECT_ALIGNMENT;
    memset(memory, 0, sizeof(*memory));
    if (size > SIZE_MAX - (alignment - 1u))
        return AGC_ERROR_OUT_OF_MEMORY;
    mapped_size = (size + alignment - 1u) & ~(alignment - 1u);

#if defined(OPENAGC_PROSPERO)
    if (sceKernelAllocateDirectMemory(
            0, AGC_DIRECT_SEARCH_END, mapped_size, alignment, 3,
            &physical) != 0)
        return AGC_ERROR_OUT_OF_MEMORY;
    if (sceKernelMapDirectMemory(
            &address, mapped_size, 0x33, 0, physical, alignment) != 0 ||
        !address) {
        sceKernelReleaseDirectMemory(physical, mapped_size);
        return AGC_ERROR_OUT_OF_MEMORY;
    }
#else
    if (posix_memalign(&address, alignment, mapped_size) != 0)
        return AGC_ERROR_OUT_OF_MEMORY;
#endif
    memory->cpu_address = address;
    memory->gpu_address = (uint64_t)(uintptr_t)address;
    memory->size = size;
    memory->mapped_size = mapped_size;
    memory->physical_offset = physical;
    memory->type = AGC_GPU_MEMORY_TYPE_DIRECT_WRITE_COMBINED;
    return AGC_OK;
}

void PS5_SYSV_ABI agcGpuMemoryFreeDirect(AgcGpuMemory *memory)
{
    if (!memory) return;
    if (memory->cpu_address && memory->mapped_size) {
#if defined(OPENAGC_PROSPERO)
        sceKernelMunmap(memory->cpu_address, memory->mapped_size);
        sceKernelReleaseDirectMemory(
            memory->physical_offset, memory->mapped_size);
#else
        free(memory->cpu_address);
#endif
    }
    memset(memory, 0, sizeof(*memory));
}

static int32_t agcGpuMemoryCacheOperation(
    const AgcGpuMemory *memory, size_t offset, size_t size)
{
    int32_t result = agcGpuMemoryValidateRange(memory, offset, size);
    if (result != AGC_OK) return result;
#if defined(OPENAGC_PROSPERO) && defined(__x86_64__)
    uintptr_t begin = ((uintptr_t)memory->cpu_address + offset) &
        ~(uintptr_t)(AGC_CACHE_LINE_SIZE - 1u);
    uintptr_t end = (uintptr_t)memory->cpu_address + offset + size;
    for (uintptr_t line = begin; line < end; line += AGC_CACHE_LINE_SIZE)
        __asm__ volatile("clflush (%0)" : : "r"((const void *)line) : "memory");
    __asm__ volatile("mfence" : : : "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGpuMemoryFlush(
    const AgcGpuMemory *memory, size_t offset, size_t size)
{
    return agcGpuMemoryCacheOperation(memory, offset, size);
}

int32_t PS5_SYSV_ABI agcGpuMemoryInvalidate(
    const AgcGpuMemory *memory, size_t offset, size_t size)
{
    return agcGpuMemoryCacheOperation(memory, offset, size);
}

int32_t PS5_SYSV_ABI agcGpuMemoryWait32(
    const AgcGpuMemory *memory, size_t offset, uint32_t value,
    uint32_t timeout_microseconds)
{
    const uint32_t interval = 50u;
    uint32_t elapsed = 0u;

    if ((offset & 3u) != 0u ||
        agcGpuMemoryValidateRange(memory, offset, sizeof(uint32_t)) != AGC_OK)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (;;) {
        if (agcGpuMemoryInvalidate(
                memory, offset, sizeof(uint32_t)) != AGC_OK)
            return AGC_ERROR_INTERNAL;
        if (*(const volatile uint32_t *)
                ((const uint8_t *)memory->cpu_address + offset) == value)
            return AGC_OK;
        if (elapsed >= timeout_microseconds)
            return AGC_ERROR_TIMEOUT;
        uint32_t delay = timeout_microseconds - elapsed;
        if (delay > interval) delay = interval;
#if defined(OPENAGC_PROSPERO)
        sceKernelUsleep(delay);
#else
        const struct timespec sleep_time = {
            .tv_sec = 0,
            .tv_nsec = (long)delay * 1000l,
        };
        nanosleep(&sleep_time, NULL);
#endif
        elapsed += delay;
    }
}
