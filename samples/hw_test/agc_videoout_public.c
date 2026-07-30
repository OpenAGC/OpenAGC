/* openagc — SPDX-License-Identifier: Apache-2.0 */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <machine/cpufunc.h>

#include <ps5/kernel.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_runtime_diag.h"
#include "agc_videoout.h"
#include "agcdriver.h"
#include "gpu_credentials.h"
#include "agc_test_defaults.h"

#ifndef AGC_EXPECT_FIRMWARE_ABI_KEY
#define AGC_EXPECT_FIRMWARE_ABI_KEY 0x1160u
#endif

#ifndef AGC_RESULT_LOG_PATH
#define AGC_RESULT_LOG_PATH "/data/homebrew/openagc_fw1160_videoout/result.log"
#endif

enum {
    BUFFER_COUNT = 2,
    WIDTH = 1920,
    HEIGHT = 1080,
    PITCH_PIXELS = 1920,
    DIRECT_MEMORY_ALIGNMENT = 0x200000,
    SUBMIT_MEMORY_SIZE = 0x4000,
};

#define DIRECT_MEMORY_SEARCH_END 0x300000000ull
#define SCE_KERNEL_WC_GARLIC 3
#define EXPECTED_MARKER 0x1160cafeu

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
int sceKernelReleaseFlexibleMemory(void *address, size_t length);

static size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1u) / alignment * alignment;
}

static void fill_scanout(uint32_t *pixels, uint32_t color)
{
    const size_t pixel_count = (size_t)PITCH_PIXELS * HEIGHT;

    for (size_t i = 0; i < pixel_count; ++i)
        pixels[i] = color;
}

static bool submit_marker(void *submit_memory)
{
    volatile uint32_t *marker =
        (volatile uint32_t *)((uint8_t *)submit_memory + 0x1000u);
    const uint32_t expected = EXPECTED_MARKER;
    AgcCommandBufferSubmit submit;
    SceAgcCb cb;
    uint32_t waited_ms = 0u;
    int32_t result;

    *marker = 0u;
    agcCbInit(&cb, submit_memory, 0x1000u);
    if (!sceAgcDcbWriteData(&cb, 2u, 0u,
            (uint64_t)(uintptr_t)marker, &expected, 1u, 1u, 1u)) {
        printf("VideoOut GPU marker build: FAIL\n");
        return false;
    }
    clflush((u_long)(uintptr_t)submit_memory);
    clflush((u_long)(uintptr_t)marker);
    mfence();

    submit.command_address = (uintptr_t)submit_memory;
    submit.dword_count = agcCbUsedDwords(&cb);
    submit.reserved = 0u;
    result = sceAgcDriverSubmitDcb(&submit);
    while (result == AGC_OK && waited_ms < 5000u) {
        clflush((u_long)(uintptr_t)marker);
        mfence();
        if (*marker == expected)
            break;
        usleep(50000u);
        waited_ms += 50u;
    }
    clflush((u_long)(uintptr_t)marker);
    mfence();
    printf("VideoOut GPU marker: value=0x%08x wait=%u ms %s\n",
        *marker, waited_ms,
        result == AGC_OK && *marker == expected ? "PASS" : "FAIL");
    return result == AGC_OK && *marker == expected;
}

int main(void)
{
    const size_t frame_size = (size_t)PITCH_PIXELS * HEIGHT * sizeof(uint32_t);
    const size_t buffer_stride =
        align_up(frame_size, DIRECT_MEMORY_ALIGNMENT);
    const size_t display_size = buffer_stride * BUFFER_COUNT;
    AgcDriverRuntimeDiagnostics diagnostics;
    AgcVideoOutCreateInfo create_info;
    AgcVideoOut *video_out = NULL;
    off_t direct_memory = -1;
    void *display_mapping = NULL;
    void *submit_memory = NULL;
    void *buffers[BUFFER_COUNT] = {NULL, NULL};
    bool driver_initialized = false;
    bool runtime_ok = false;
    bool defaults_ok = false;
    bool async_ok = false;
    bool marker_ok = false;
    bool presentation_ok = false;
    bool success;
    int32_t shutdown_result = AGC_OK;
    int submit_release_result = 0;
    int unmap_result = 0;
    int direct_release_result = 0;
    int32_t result;

    setbuf(stdout, NULL);
    if (!freopen(AGC_RESULT_LOG_PATH, "w", stdout))
        return 1;
    setbuf(stdout, NULL);
    printf("Result log: %s\n", AGC_RESULT_LOG_PATH);
    printf("=== openagc public VideoOut integration gate ===\n");

    if (set_gpu_credentials() != 0) {
        printf("GPU credentials: FAIL\n");
        goto cleanup;
    }
    result = sce_agc_initialize();
    if (result != AGC_OK) {
        printf("Driver initialize: FAIL (0x%08x)\n", (unsigned)result);
        goto cleanup;
    }
    driver_initialized = true;
    result = agcDriverDebugRuntimeProfile(&diagnostics);
    runtime_ok = result == AGC_OK &&
        (diagnostics.firmware_version >> 16) == AGC_EXPECT_FIRMWARE_ABI_KEY;
    printf("Runtime profile FW ABI 0x%04x: %s\n",
        (unsigned)(diagnostics.firmware_version >> 16),
        runtime_ok ? "PASS" : "FAIL");
    if (!runtime_ok)
        goto cleanup;

    result = sceAgcInit(agcTestDefaultsVersion(
        (uint16_t)(diagnostics.firmware_version >> 16)));
    if (result != AGC_OK) {
        printf("Caller defaults selection: FAIL (0x%08x)\n", (unsigned)result);
        goto cleanup;
    }

    result = sce_agc_initialize_internal_memory();
    if (result != AGC_OK) {
        printf("Driver internal memory: FAIL (0x%08x)\n", (unsigned)result);
        goto cleanup;
    }
    defaults_ok = sceAgcDriverNotifyDefaultStates(0u) == AGC_OK;
    async_ok = sceAgcDriverSetupAsyncGraphics(1u) == AGC_OK;
    printf("VideoOut driver defaults/async: %s/%s\n",
        defaults_ok ? "PASS" : "FAIL", async_ok ? "PASS" : "FAIL");
    if (!defaults_ok || !async_ok)
        goto cleanup;

    if (sceKernelAllocateDirectMemory(0,
            (off_t)DIRECT_MEMORY_SEARCH_END, display_size,
            DIRECT_MEMORY_ALIGNMENT, SCE_KERNEL_WC_GARLIC,
            &direct_memory) != 0) {
        printf("VideoOut direct allocation: FAIL\n");
        goto cleanup;
    }
    if (sceKernelMapDirectMemory(&display_mapping, display_size, 0x33, 0,
            direct_memory, DIRECT_MEMORY_ALIGNMENT) != 0) {
        printf("VideoOut direct mapping: FAIL\n");
        goto cleanup;
    }
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i)
        buffers[i] = (uint8_t *)display_mapping + i * buffer_stride;
    fill_scanout((uint32_t *)buffers[0], 0xff20a040u);
    fill_scanout((uint32_t *)buffers[1], 0xffa04020u);

    memset(&create_info, 0, sizeof(create_info));
    create_info.width = WIDTH;
    create_info.height = HEIGHT;
    create_info.pitch_pixels = PITCH_PIXELS;
    create_info.buffer_count = BUFFER_COUNT;
    create_info.buffers = buffers;
    create_info.format = AGC_VIDEO_OUT_FORMAT_BGRA8_SRGB;
    result = agcVideoOutOpen(&create_info, &video_out);
    printf("Public VideoOut open/register/restore: 0x%08x %s\n",
        (unsigned)result, result == AGC_OK ? "PASS" : "FAIL");
    if (result != AGC_OK)
        goto cleanup;

    if (sceKernelMapNamedSystemFlexibleMemory(&submit_memory,
            SUBMIT_MEMORY_SIZE, 0x33, 0, "OpenAgcVideoOutSubmit") != 0 ||
        !submit_memory) {
        printf("VideoOut submit memory: FAIL\n");
        goto cleanup;
    }
    marker_ok = submit_marker(submit_memory);
    if (!marker_ok)
        goto cleanup;

    presentation_ok =
        agcVideoOutPresent(video_out, 0u, 1u, 2000000u) == AGC_OK &&
        agcVideoOutPresent(video_out, 1u, 2u, 2000000u) == AGC_OK;
    printf("Public VideoOut bounded flips: %s\n",
        presentation_ok ? "PASS" : "FAIL");

cleanup:
    agcVideoOutClose(video_out);
    if (driver_initialized)
        shutdown_result = agcDriverShutdown();
    if (submit_memory)
        submit_release_result = sceKernelReleaseFlexibleMemory(
            submit_memory, SUBMIT_MEMORY_SIZE);
    if (display_mapping)
        unmap_result = munmap(display_mapping, display_size);
    if (direct_memory >= 0)
        direct_release_result = sceKernelReleaseDirectMemory(
            direct_memory, display_size);
    printf("Public VideoOut cleanup: shutdown=0x%08x submit=0x%08x "
           "unmap=0x%08x direct=0x%08x\n",
        (unsigned)shutdown_result, (unsigned)submit_release_result,
        (unsigned)unmap_result, (unsigned)direct_release_result);

    success = runtime_ok && defaults_ok && async_ok && marker_ok &&
        presentation_ok && shutdown_result == AGC_OK &&
        submit_release_result == 0 && unmap_result == 0 &&
        direct_release_result == 0;
    printf("Public VideoOut result: %s\n", success ? "PASS" : "FAIL");
    fflush(stdout);
    kill(getpid(), SIGKILL);
    return success ? 0 : 1;
}
