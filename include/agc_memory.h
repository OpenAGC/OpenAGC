/* openagc — SPDX-License-Identifier: Apache-2.0 */
#ifndef OPENAGC_MEMORY_H
#define OPENAGC_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AgcGpuMemory {
    void *cpu_address;
    uint64_t gpu_address;
    size_t size;
    size_t mapped_size;
} AgcGpuMemory;

/* Allocates unified CPU/GPU-visible flexible memory.  The generic backend
 * provides aligned host memory with the same lifetime and address contract. */
int32_t PS5_SYSV_ABI agcGpuMemoryAllocateFlexible(
    AgcGpuMemory *memory, size_t size, size_t alignment, const char *name);
void PS5_SYSV_ABI agcGpuMemoryFreeFlexible(AgcGpuMemory *memory);

/* Publish CPU writes before GPU access, or discard stale CPU cache lines
 * before reading GPU writes. */
int32_t PS5_SYSV_ABI agcGpuMemoryFlush(
    const AgcGpuMemory *memory, size_t offset, size_t size);
int32_t PS5_SYSV_ABI agcGpuMemoryInvalidate(
    const AgcGpuMemory *memory, size_t offset, size_t size);

#ifdef __cplusplus
}
#endif

#endif
