/*
 * openagc standalone gfx1013 buffer-copy hardware gate.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_graphics.h"
#include "agc_memory.h"
#include "agc_runtime_diag.h"
#include "agcdriver.h"
#include "gpu_credentials.h"

#ifndef AGC_EXPECT_FIRMWARE_ABI_KEY
#define AGC_EXPECT_FIRMWARE_ABI_KEY 0x0550u
#endif

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

#define COPY_BYTE_COUNT UINT32_C(8294400)
#define COPY_ALIGNMENT UINT32_C(0x4000)
#define COMMAND_BYTES UINT32_C(0x10000)
#define COMMAND_CAPACITY UINT32_C(0x8000)
#define FENCE_OFFSET UINT32_C(0x8000)
#define FENCE_VALUE UINT32_C(0xc0febabe)

static bool initialize_driver(bool *driver_initialized)
{
    if (!driver_initialized)
        return false;
    int32_t result = sce_agc_initialize();
    if (result != AGC_OK) {
        printf("sce_agc_initialize: 0x%08x\n", (unsigned)result);
        return false;
    }
    *driver_initialized = true;

    AgcDriverRuntimeDiagnostics diagnostics;
    result = agcDriverDebugRuntimeProfile(&diagnostics);
    const bool profile_ok = result == AGC_OK &&
        (diagnostics.firmware_version >> 16u) ==
            AGC_EXPECT_FIRMWARE_ABI_KEY &&
        diagnostics.profile.family == AGC_PROSPERO_ABI_STANDARD &&
        !diagnostics.profile.is_trinity;
    printf("Runtime profile FW ABI 0x%04X: %s\n",
           AGC_EXPECT_FIRMWARE_ABI_KEY, profile_ok ? "PASS" : "FAIL");
    if (!profile_ok)
        return false;

    result = sce_agc_initialize_internal_memory();
    if (result != AGC_OK) {
        printf("sce_agc_initialize_internal_memory: 0x%08x\n",
               (unsigned)result);
        return false;
    }
    result = sceAgcDriverNotifyDefaultStates(0u);
    if (result != AGC_OK) {
        printf("sceAgcDriverNotifyDefaultStates: 0x%08x\n",
               (unsigned)result);
        return false;
    }
    result = sceAgcDriverSetupAsyncGraphics(1u);
    if (result != AGC_OK) {
        printf("sceAgcDriverSetupAsyncGraphics: 0x%08x\n",
               (unsigned)result);
        return false;
    }
    return true;
}

static uint64_t hash_bytes(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0u; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int main(void)
{
    AgcGpuMemory command = {0};
    AgcGpuMemory source = {0};
    AgcGpuMemory destination = {0};
    bool driver_initialized = false;
    bool passed = false;

    if (set_gpu_credentials() != 0) {
        printf("GPU credentials: FAIL\n");
        goto finish;
    }
#ifdef AGC_RESULT_LOG_PATH
    if (!freopen(AGC_RESULT_LOG_PATH, "w", stdout))
        goto finish;
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("Result log: %s\n", AGC_RESULT_LOG_PATH);
#endif
    printf("=== openagc standalone gfx1013 buffer copy ===\n");

    if (!initialize_driver(&driver_initialized))
        goto finish;

    if (agcGpuMemoryAllocateFlexible(
            &command, COMMAND_BYTES, COPY_ALIGNMENT, "OpenAgcCopyCommand") !=
            AGC_OK ||
        agcGpuMemoryAllocateFlexible(
            &source, COPY_BYTE_COUNT, COPY_ALIGNMENT, "OpenAgcCopySource") !=
            AGC_OK ||
        agcGpuMemoryAllocateFlexible(
            &destination, COPY_BYTE_COUNT, COPY_ALIGNMENT,
            "OpenAgcCopyDestination") != AGC_OK) {
        printf("Copy allocations: FAIL\n");
        goto finish;
    }

    uint32_t *source_words = (uint32_t *)source.cpu_address;
    uint32_t *destination_words = (uint32_t *)destination.cpu_address;
    const uint32_t word_count = COPY_BYTE_COUNT / sizeof(uint32_t);
    for (uint32_t i = 0u; i < word_count; ++i) {
        source_words[i] = UINT32_C(0xa5a55a5a) ^
            i * UINT32_C(0x9e3779b9);
        destination_words[i] = UINT32_C(0xcdcdcdcd);
    }
    memset(command.cpu_address, 0, COMMAND_BYTES);

    if (agcGpuMemoryFlush(&source, 0u, COPY_BYTE_COUNT) != AGC_OK ||
        agcGpuMemoryFlush(&destination, 0u, COPY_BYTE_COUNT) != AGC_OK ||
        agcGpuMemoryFlush(&command, 0u, COMMAND_BYTES) != AGC_OK) {
        printf("CPU-to-GPU cache publication: FAIL\n");
        goto finish;
    }

    SceAgcCb cb;
    agcCbInit(&cb, command.cpu_address, COMMAND_CAPACITY);
    int32_t result = agcGfx1013CopyBuffer(
        &cb, source.gpu_address, destination.gpu_address, COPY_BYTE_COUNT);
    if (result != AGC_OK) {
        printf("agcGfx1013CopyBuffer: 0x%08x\n", (unsigned)result);
        goto finish;
    }

    const AgcGfx1013ResourceTransition completion = {
        .before = AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION,
        .after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ,
        .completion_address = command.gpu_address + FENCE_OFFSET,
        .completion_value = FENCE_VALUE,
    };
    result = agcGfx1013TransitionResource(&cb, &completion);
    if (result != AGC_OK) {
        printf("copy-to-host transition: 0x%08x\n", (unsigned)result);
        goto finish;
    }

    const uint32_t used_dwords = agcCbUsedDwords(&cb);
    if (agcGpuMemoryFlush(
            &command, 0u, used_dwords * sizeof(uint32_t)) != AGC_OK) {
        printf("Command publication: FAIL\n");
        goto finish;
    }

    const AgcCommandBufferSubmit submit = {
        .command_address = command.gpu_address,
        .dword_count = used_dwords,
        .reserved = 0u,
    };
    result = sceAgcDriverSubmitDcb(&submit);
    printf("Copy submit: 0x%08x dwords=%u dma-packets=4 bytes=%u\n",
           (unsigned)result, used_dwords, COPY_BYTE_COUNT);
    if (result != AGC_OK)
        goto finish;

    result = agcGpuMemoryWait32(
        &command, FENCE_OFFSET, FENCE_VALUE, UINT32_C(200000));
    printf("Copy completion fence: %s (0x%08x)\n",
           result == AGC_OK ? "PASS" : "FAIL", (unsigned)result);
    if (result != AGC_OK)
        goto finish;

    if (agcGpuMemoryInvalidate(&destination, 0u, COPY_BYTE_COUNT) != AGC_OK) {
        printf("Destination invalidate: FAIL\n");
        goto finish;
    }

    uint32_t mismatch_count = 0u;
    uint32_t first_mismatch = UINT32_MAX;
    for (uint32_t i = 0u; i < word_count; ++i) {
        if (destination_words[i] != source_words[i]) {
            if (first_mismatch == UINT32_MAX)
                first_mismatch = i;
            ++mismatch_count;
        }
    }
    const uint64_t source_hash = hash_bytes(source.cpu_address, COPY_BYTE_COUNT);
    const uint64_t destination_hash =
        hash_bytes(destination.cpu_address, COPY_BYTE_COUNT);
    printf("Copy compare: mismatches=%u first=%u source-fnv64=0x%016llx "
           "destination-fnv64=0x%016llx: %s\n",
           mismatch_count, first_mismatch,
           (unsigned long long)source_hash,
           (unsigned long long)destination_hash,
           mismatch_count == 0u && source_hash == destination_hash ?
               "PASS" : "FAIL");
    passed = mismatch_count == 0u && source_hash == destination_hash;

finish:
    agcGpuMemoryFreeFlexible(&destination);
    agcGpuMemoryFreeFlexible(&source);
    agcGpuMemoryFreeFlexible(&command);
    if (driver_initialized) {
        const int32_t shutdown_result = agcDriverShutdown();
        printf("Driver shutdown: %s (0x%08x)\n",
               shutdown_result == AGC_OK ? "PASS" : "FAIL",
               (unsigned)shutdown_result);
        passed &= shutdown_result == AGC_OK;
    }
    printf("Copy result: %s\n", passed ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);
#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
#endif
    return passed ? 0 : 1;
}
