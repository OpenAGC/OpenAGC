/* openagc — SPDX-License-Identifier: Apache-2.0 */

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "agc_types.h"

#ifndef AGC_FIRMWARE_PROBE_LOG_PATH
#define AGC_FIRMWARE_PROBE_LOG_PATH \
    "/data/homebrew/openagc_portability/preflight.log"
#endif

typedef struct AgcFirmwareProbeVersion {
    uint64_t reserved0;
    char version_string[0x1c];
    uint32_t version;
    uint64_t reserved1;
} AgcFirmwareProbeVersion;

_Static_assert(sizeof(AgcFirmwareProbeVersion) == 0x30,
    "PS5 system software version ABI size");
_Static_assert(offsetof(AgcFirmwareProbeVersion, version) == 0x24,
    "PS5 system software numeric version ABI offset");

extern int PS5_SYSV_ABI sceKernelGetProsperoSystemSwVersion(
    AgcFirmwareProbeVersion *version);

int main(void)
{
    AgcFirmwareProbeVersion version;
    int result;

    if (!freopen(AGC_FIRMWARE_PROBE_LOG_PATH, "w", stdout))
        return 1;
    setbuf(stdout, NULL);
    memset(&version, 0, sizeof(version));
    result = sceKernelGetProsperoSystemSwVersion(&version);
    if (result == 0) {
        printf("Firmware preflight raw=0x%08x key=0x%04x string=%s\n",
            (unsigned)version.version,
            (unsigned)(version.version >> 16), version.version_string);
        printf("Firmware preflight result: PASS\n");
    } else {
        printf("Firmware preflight query=0x%08x\n", (unsigned)result);
        printf("Firmware preflight result: FAIL\n");
    }
    fflush(stdout);
    kill(getpid(), SIGKILL);
    return result == 0 ? 0 : 1;
}
