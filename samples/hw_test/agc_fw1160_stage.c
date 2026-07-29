/* SPDX-License-Identifier: Apache-2.0 */
/* Narrow FW 11.60 qualification stages. Add only one isolated gate at a time. */

#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <machine/cpufunc.h>

#include "agc_cb.h"
#include "agcdriver.h"
#include "agc_error.h"
#include "agc_runtime_diag.h"
#include "agc_types.h"
#include "gpu_credentials.h"

#ifndef AGC_FW1160_STAGE
#define AGC_FW1160_STAGE 0
#endif

typedef struct AgcKernelSwVersion {
    uint64_t reserved0;
    char version_string[0x1c];
    uint32_t version;
    uint64_t reserved1;
} AgcKernelSwVersion;

_Static_assert(sizeof(AgcKernelSwVersion) == 0x30,
    "PS5 system software version ABI size");

extern int PS5_SYSV_ABI sceKernelGetProsperoSystemSwVersion(
    AgcKernelSwVersion *version);
extern int32_t agcProsperoConfigureRuntimeProfile(uint32_t raw_version);
extern int32_t PS5_SYSV_ABI agcProsperoInitialize(void);
extern int32_t PS5_SYSV_ABI agcProsperoInitializeInternalMemory(void);
extern int32_t PS5_SYSV_ABI agcProsperoSubmitDcb(
    const AgcCommandBufferSubmit *packet);
extern int32_t PS5_SYSV_ABI agcProsperoSetupAsyncGraphics(uint32_t pipe_id);
extern int32_t PS5_SYSV_ABI agcProsperoCreateUserSpecialQueue(void);
extern int32_t PS5_SYSV_ABI agcProsperoDestroyUserSpecialQueue(void);
extern int32_t PS5_SYSV_ABI agcProsperoSuspendPointSubmitDirect(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3);
extern int32_t PS5_SYSV_ABI agcProsperoShutdown(void);
extern int32_t agcProsperoGetRuntimeProfile(
    AgcProsperoRuntimeProfile *profile_out);
extern int sceKernelMapNamedSystemFlexibleMemory(
    void **virtual_address, size_t length, int protection, int flags,
    const char *name);
extern int sceKernelReleaseFlexibleMemory(void *address, size_t length);

int main(void)
{
    AgcKernelSwVersion firmware;
    uint32_t socid = 0;
    size_t socid_size = sizeof(socid);

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("FW 11.60 staged probe: stage=%d\n", AGC_FW1160_STAGE);

    memset(&firmware, 0, sizeof(firmware));
    if (sceKernelGetProsperoSystemSwVersion(&firmware) != 0) {
        printf("stage %d: firmware query FAIL\n", AGC_FW1160_STAGE);
        return 1;
    }
    printf("firmware raw=0x%08X string=%s\n",
        firmware.version, firmware.version_string);
    if ((firmware.version >> 16) != 0x1160u) {
        printf("stage %d: wrong firmware FAIL\n", AGC_FW1160_STAGE);
        return 1;
    }

    if (sysctlbyname("hw.sce_main_socid", &socid, &socid_size, NULL, 0) != 0 ||
        socid_size != sizeof(socid)) {
        printf("stage %d: SoC query FAIL\n", AGC_FW1160_STAGE);
        return 1;
    }
    printf("socid=0x%08X model=%s\n", socid,
        (socid & ~0x1fu) == 0x00840fc0u ? "trinity" : "standard-ps5");
    if ((socid & ~0x1fu) == 0x00840fc0u) {
        printf("stage %d: unexpected Trinity hardware FAIL\n",
            AGC_FW1160_STAGE);
        return 1;
    }

#if AGC_FW1160_STAGE >= 1
    AgcProsperoRuntimeProfile profile;
    int32_t result;

    if (set_gpu_credentials() != 0) {
        printf("stage 1: credentials FAIL\n");
        return 1;
    }
    result = agcProsperoConfigureRuntimeProfile(firmware.version);
    printf("profile configure=0x%08X\n", (unsigned)result);
    if (result != AGC_OK ||
        agcProsperoGetRuntimeProfile(&profile) != AGC_OK ||
        profile.is_trinity) {
        printf("stage 1: profile FAIL\n");
        return 1;
    }
    result = agcProsperoInitialize();
    printf("direct initialize=0x%08X\n", (unsigned)result);
    if (result != AGC_OK) {
        printf("stage 1: initialize FAIL\n");
        return 1;
    }
#if AGC_FW1160_STAGE >= 2
    result = agcProsperoInitializeInternalMemory();
    printf("internal memory=0x%08X\n", (unsigned)result);
    if (result != AGC_OK) {
        printf("stage 2: internal memory FAIL\n");
        (void)agcProsperoShutdown();
        return 1;
    }
#endif
#if AGC_FW1160_STAGE == 3
    void *submit_memory = (void *)(uintptr_t)0xf02000000ULL;
    const uint32_t expected_marker = 0x1160CAFEu;
    volatile uint32_t *marker;
    SceAgcCb cb;
    AgcCommandBufferSubmit submit;
    uint32_t waited_ms = 0u;

    result = sceKernelMapNamedSystemFlexibleMemory(&submit_memory, 0x4000u,
        0x33, 0, "OpenAgcFw1160Submit");
    printf("submit memory result=%d address=%p\n", result, submit_memory);
    if (result != 0 || !submit_memory) {
        printf("stage 3: submit memory FAIL\n");
        (void)agcProsperoShutdown();
        return 1;
    }

    marker = (volatile uint32_t *)((uint8_t *)submit_memory + 0x1000u);
    *marker = 0u;
    agcCbInit(&cb, submit_memory, 0x1000u);
    if (!sceAgcDcbWriteData(&cb, 2u, 0u,
            (uint64_t)(uintptr_t)marker, &expected_marker, 1u, 1u, 1u)) {
        printf("stage 3: WRITE_DATA build FAIL\n");
        (void)agcProsperoShutdown();
        (void)sceKernelReleaseFlexibleMemory(submit_memory, 0x4000u);
        return 1;
    }
    clflush((u_long)(uintptr_t)submit_memory);
    clflush((u_long)(uintptr_t)marker);
    mfence();

    submit.command_address = (uintptr_t)submit_memory;
    submit.dword_count = agcCbUsedDwords(&cb);
    /* The staged probe configures the direct backend explicitly because the
     * public runtime selector remains blocked until qualification completes. */
    result = agcProsperoSubmitDcb(&submit);
    printf("single DCB submit=0x%08X dwords=%u\n",
        (unsigned)result, submit.dword_count);
    while (result == AGC_OK && waited_ms < 5000u) {
        clflush((u_long)(uintptr_t)marker);
        mfence();
        if (*marker == expected_marker)
            break;
        usleep(50000u);
        waited_ms += 50u;
    }
    clflush((u_long)(uintptr_t)marker);
    mfence();
    printf("marker=0x%08X expected=0x%08X wait=%u ms\n",
        *marker, expected_marker, waited_ms);
    if (result != AGC_OK || *marker != expected_marker) {
        printf("stage 3: submission FAIL\n");
        (void)agcProsperoShutdown();
        return 1;
    }
    result = sceKernelReleaseFlexibleMemory(submit_memory, 0x4000u);
    printf("submit memory release=%d\n", result);
    if (result != 0) {
        printf("stage 3: submit memory release FAIL\n");
        (void)agcProsperoShutdown();
        return 1;
    }
#endif
#if AGC_FW1160_STAGE >= 4
    result = agcProsperoSetupAsyncGraphics(1u);
    printf("async graphics=0x%08X\n", (unsigned)result);
    if (result != AGC_OK) {
        printf("stage 4: async graphics FAIL\n");
        (void)agcProsperoShutdown();
        return 1;
    }
#endif
#if AGC_FW1160_STAGE >= 5
    int32_t queue_handle = agcProsperoCreateUserSpecialQueue();
    printf("queue create=%d (0x%08X)\n",
        queue_handle, (unsigned)queue_handle);
    if (queue_handle < 0) {
        printf("stage 5: queue create FAIL\n");
        (void)agcProsperoShutdown();
        return 1;
    }
#if AGC_FW1160_STAGE >= 6
    result = agcProsperoSuspendPointSubmitDirect(
        0xaf1e80b7u, 0x8b4cdd90u, 0x99f68d6cu, 0u);
    printf("primary suspend=0x%08X\n", (unsigned)result);
    if (result != AGC_OK) {
        printf("stage 6: primary suspend FAIL\n");
        (void)agcProsperoDestroyUserSpecialQueue();
        (void)agcProsperoShutdown();
        return 1;
    }
#endif
    result = agcProsperoDestroyUserSpecialQueue();
    printf("queue destroy=0x%08X\n", (unsigned)result);
    if (result != AGC_OK) {
        printf("stage %d: queue destroy FAIL\n", AGC_FW1160_STAGE);
        (void)agcProsperoShutdown();
        return 1;
    }
#endif
    result = agcProsperoShutdown();
    printf("direct shutdown=0x%08X\n", (unsigned)result);
    if (result != AGC_OK) {
        printf("stage %d: shutdown FAIL\n", AGC_FW1160_STAGE);
        return 1;
    }
#endif

    printf("stage %d: PASS\n", AGC_FW1160_STAGE);
    fflush(NULL);
    (void)kill(getpid(), SIGKILL);
    for (;;)
        pause();
}
