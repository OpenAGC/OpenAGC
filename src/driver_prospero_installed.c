/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Installed-Sony-driver-only Prospero profile support. This translation unit
 * intentionally contains no direct /dev/gc backend or fallback.
 */

#include "driver_ops.h"
#include "driver_registry.h"

#include "agc_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

typedef int (PS5_SYSV_ABI *AgcKernelHasTrinityModeFn)(void);

extern int PS5_SYSV_ABI sceKernelDlsym(int handle, const char *name,
    void *symbol_out);
extern int PS5_SYSV_ABI sceKernelGetModuleList(int *modules,
    size_t module_capacity, size_t *module_count);

static AgcProsperoRuntimeProfile g_installed_profile;

static int32_t agcProsperoQueryTrinityMode(bool *is_trinity)
{
    static const char *const symbols[] = {
        "sceKernelHasTrinityMode",
        "yu17wG8L5FI"
    };
    static const int handles[] = {0x2001, 1};
    AgcKernelHasTrinityModeFn query = NULL;
    void *symbol = NULL;
    int modules[128];
    uint32_t main_socid = 0;
    size_t main_socid_size = sizeof(main_socid);
    size_t module_count = 0;
    size_t symbol_index;
    size_t handle_index;

    if (!is_trinity)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (symbol_index = 0;
         symbol_index < sizeof(symbols) / sizeof(symbols[0]);
         ++symbol_index) {
        for (handle_index = 0;
             handle_index < sizeof(handles) / sizeof(handles[0]);
             ++handle_index) {
            symbol = NULL;
            if (sceKernelDlsym(handles[handle_index], symbols[symbol_index],
                    &symbol) == 0 && symbol)
                break;
        }
        if (symbol)
            break;
    }
    if (!symbol && sceKernelGetModuleList(modules,
            sizeof(modules) / sizeof(modules[0]), &module_count) == 0) {
        if (module_count > sizeof(modules) / sizeof(modules[0]))
            module_count = sizeof(modules) / sizeof(modules[0]);
        for (handle_index = 0; handle_index < module_count; ++handle_index) {
            for (symbol_index = 0;
                 symbol_index < sizeof(symbols) / sizeof(symbols[0]);
                 ++symbol_index) {
                symbol = NULL;
                if (sceKernelDlsym(modules[handle_index],
                        symbols[symbol_index], &symbol) == 0 && symbol)
                    break;
            }
            if (symbol)
                break;
        }
    }
    if (symbol) {
        if (sizeof(query) != sizeof(symbol))
            return AGC_ERROR_NOT_SUPPORTED;
        memcpy(&query, &symbol, sizeof(query));
        *is_trinity = query() != 0;
        return AGC_OK;
    }
    if (sysctlbyname("hw.sce_main_socid", &main_socid, &main_socid_size,
            NULL, 0) != 0 || main_socid_size != sizeof(main_socid))
        return AGC_ERROR_NOT_SUPPORTED;
    *is_trinity = (main_socid & ~0x1fu) == 0x00840fc0u;
    return AGC_OK;
}

int32_t agcProsperoConfigureRuntimeProfile(uint32_t raw_version)
{
    bool is_trinity = false;

    if (agcProsperoFirmwareUsesTrinityPredicate(raw_version)) {
        int32_t result = agcProsperoQueryTrinityMode(&is_trinity);

        if (result != AGC_OK)
            return result;
    }
    if (!agcProsperoBuildRuntimeProfile(raw_version, is_trinity,
            &g_installed_profile))
        return AGC_ERROR_NOT_SUPPORTED;
    printf("[openagc] installed profile fw=0x%08X family=%s model=%s "
           "gpu_info=0x%X cwsr_work=0x%X cwsr_size=0x%X\n",
           raw_version, agcProsperoAbiFamilyName(g_installed_profile.family),
           g_installed_profile.is_trinity ? "trinity" : "standard-ps5",
           g_installed_profile.gpu_info_span,
           g_installed_profile.cwsr_work_offset,
           g_installed_profile.cwsr_size);
    return AGC_OK;
}

int32_t agcProsperoGetRuntimeProfile(
    AgcProsperoRuntimeProfile *profile_out)
{
    if (!profile_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (g_installed_profile.family == AGC_PROSPERO_ABI_UNSUPPORTED)
        return AGC_ERROR_NOT_INITIALIZED;
    *profile_out = g_installed_profile;
    return AGC_OK;
}
