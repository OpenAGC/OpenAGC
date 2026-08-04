/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Credential preparation before the ps5-payload-sdk opens DT_NEEDED SPRXs.
 */

#include "driver_sony_exports.h"

#include <ps5/kernel.h>

#include <stdbool.h>
#include <stdint.h>

#define AGC_GPU_AUTHID_REQUIRED 0x4801000000000000ull
#define AGC_GPU_AUTHID_MASK 0xff0f000000000000ull

#define AGC_SONY_MAX_THREADS 64u

/* ps5-payload-sdk's crt1.o owns this private loader entry point. The Sony-first
 * target links with --wrap=__rtld_lib_open so this function runs after the CRT
 * has initialized kernel access but before it starts any DT_NEEDED SPRX. */
extern int __real___rtld_lib_open(void *library);

static bool g_sony_preload_attempted;
static int32_t g_sony_preload_result = AGC_ERROR_NOT_INITIALIZED;

static int32_t agcSonyPrepareThreadCredentials(
    int pid, uint64_t authid, const AgcSonyCredentialLayout *layout)
{
    intptr_t process = kernel_get_proc(pid);
    uint64_t process_ucred = 0u;
    uint64_t thread = 0u;
    uint32_t thread_count = 0u;

    if (!layout || process == 0 || kernel_copyout(
            process + layout->process_ucred_offset, &process_ucred,
            sizeof(process_ucred)) != 0 || process_ucred == 0u)
        return AGC_ERROR_NOT_INITIALIZED;
    if (kernel_copyout(process + layout->process_threads_offset, &thread,
            sizeof(thread)) != 0)
        return AGC_ERROR_NOT_INITIALIZED;
    while (thread != 0u && thread_count < AGC_SONY_MAX_THREADS) {
        uint64_t thread_ucred = 0u;
        uint64_t thread_authid = 0u;

        if (kernel_copyout(
                (intptr_t)thread + layout->thread_ucred_offset,
                &thread_ucred, sizeof(thread_ucred)) != 0 ||
            thread_ucred == 0u)
            return AGC_ERROR_NOT_INITIALIZED;
        if (thread_ucred != process_ucred && kernel_copyin(&authid,
                (intptr_t)thread_ucred + layout->ucred_authid_offset,
                sizeof(authid)) != 0)
            return AGC_ERROR_NOT_INITIALIZED;
        if (kernel_copyout(
                (intptr_t)thread_ucred + layout->ucred_authid_offset,
                &thread_authid, sizeof(thread_authid)) != 0 ||
            (thread_authid & AGC_GPU_AUTHID_MASK) != AGC_GPU_AUTHID_REQUIRED)
            return AGC_ERROR_NOT_INITIALIZED;
        if (kernel_copyout(
                (intptr_t)thread + layout->thread_next_offset, &thread,
                sizeof(thread)) != 0)
            return AGC_ERROR_NOT_INITIALIZED;
        ++thread_count;
    }
    if (thread_count == 0u || thread != 0u)
        return AGC_ERROR_NOT_INITIALIZED;
    return AGC_OK;
}

static int32_t agcSonyPrepareCredentialsBeforeDependencies(void)
{
    const uint32_t firmware_version = kernel_get_fw_version();
    const AgcSonyDriverProfile *profile =
        agcSonyDriverProfileForFirmware(firmware_version);
    const int pid = -1;
    uint64_t authid;

    if (!profile || !profile->credential_layout)
        return AGC_ERROR_NOT_SUPPORTED;
    if (kernel_set_ucred_authid(pid, AGC_GPU_AUTHID_REQUIRED) != 0)
        return AGC_ERROR_NOT_INITIALIZED;
    authid = kernel_get_ucred_authid(pid);
    if ((authid & AGC_GPU_AUTHID_MASK) != AGC_GPU_AUTHID_REQUIRED)
        return AGC_ERROR_NOT_INITIALIZED;
    return agcSonyPrepareThreadCredentials(
        pid, authid, profile->credential_layout);
}

int32_t agcSonyPreloadCredentialResult(void)
{
    return g_sony_preload_result;
}

int __wrap___rtld_lib_open(void *library)
{
    if (!g_sony_preload_attempted) {
        g_sony_preload_attempted = true;
        g_sony_preload_result =
            agcSonyPrepareCredentialsBeforeDependencies();
        if (g_sony_preload_result != AGC_OK)
            return -1;
    }
    return __real___rtld_lib_open(library);
}
