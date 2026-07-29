/* SPDX-License-Identifier: Apache-2.0 */
/* Narrow FW 11.60 qualification stages. Never add GPU work to this file. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

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
extern int32_t PS5_SYSV_ABI agcProsperoShutdown(void);
extern int32_t agcProsperoGetRuntimeProfile(
    AgcProsperoRuntimeProfile *profile_out);

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
    result = agcProsperoShutdown();
    printf("direct shutdown=0x%08X\n", (unsigned)result);
    if (result != AGC_OK) {
        printf("stage 1: shutdown FAIL\n");
        return 1;
    }
#endif

    printf("stage %d: PASS\n", AGC_FW1160_STAGE);
    return 0;
}
