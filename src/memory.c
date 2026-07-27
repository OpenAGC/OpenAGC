/* openagc — SPDX-License-Identifier: Apache-2.0 */
#include "agc_memory.h"
#include "agc_error.h"

#include <stdlib.h>
#include <string.h>

#define AGC_FLEXIBLE_PAGE_SIZE 0x4000u
#define AGC_CACHE_LINE_SIZE 64u

#if defined(OPENAGC_PROSPERO)
extern int32_t sceKernelMapNamedSystemFlexibleMemory(
    void **addr, size_t size, int type, int flags, const char *name);
extern int32_t sceKernelMunmap(void *addr, size_t len);
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
