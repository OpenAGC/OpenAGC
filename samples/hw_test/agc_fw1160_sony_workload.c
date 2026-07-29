/* SPDX-License-Identifier: Apache-2.0 */
/*
 * FW 11.60 installed-driver workload oracle.
 *
 * This payload deliberately never initializes OpenAGC's direct /dev/gc
 * backend. It patches credentials before dlopen(), lets the console's matching
 * libSceAgcDriver initialize its private state, and requires an installed-
 * driver preflight marker before attempting the workload builders.
 */

#include <dlfcn.h>
#include <sys/types.h>
#include <machine/cpufunc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_types.h"
#include "agcdriver.h"
#include "gpu_credentials.h"

enum {
    ORACLE_MEMORY_SIZE = 0x4000,
    ORACLE_DCB_SIZE = 0x1000,
    ORACLE_STREAM_ID = 1,
    ORACLE_WORKLOAD_ID = 1,
    ORACLE_ACTIVE_DWORDS = 18,
    ORACLE_COMPLETE_DWORDS = 12,
};

typedef struct AgcKernelSwVersion {
    uint64_t reserved0;
    char version_string[0x1c];
    uint32_t version;
    uint64_t reserved1;
} AgcKernelSwVersion;

_Static_assert(sizeof(AgcKernelSwVersion) == 0x30,
    "PS5 system software version ABI size");

typedef int32_t (PS5_SYSV_ABI *SonySubmitDcbFn)(
    const AgcCommandBufferSubmit *packet);
typedef int32_t (PS5_SYSV_ABI *SonySetupAsyncGraphicsFn)(uint32_t pipe_id);
typedef int32_t (PS5_SYSV_ABI *SonyRegisterWorkloadStreamFn)(
    uint32_t stream_id, const void *descriptor);
typedef int32_t (PS5_SYSV_ABI *SonyUnregisterWorkloadStreamFn)(
    uint32_t stream_id);
typedef uint32_t (PS5_SYSV_ABI *SonyGetPacketSizeFn)(void);
typedef int32_t (PS5_SYSV_ABI *SonySetWorkloadsActiveFn)(
    uint32_t *packet, uint32_t control, uint32_t stream_id,
    const uint32_t *workload_ids, uint32_t workload_count);
typedef int32_t (PS5_SYSV_ABI *SonySetWorkloadCompleteFn)(
    uint32_t *packet, uint32_t control, uint32_t stream_id,
    uint32_t workload_id);

extern int PS5_SYSV_ABI sceKernelGetProsperoSystemSwVersion(
    AgcKernelSwVersion *version);
extern int sceKernelMapNamedSystemFlexibleMemory(
    void **virtual_address, size_t length, int protection, int flags,
    const char *name);
extern int sceKernelReleaseFlexibleMemory(void *address, size_t length);

static int load_symbol(void *module, const char *name,
    void *function_storage, size_t function_size)
{
    void *address = dlsym(module, name);

    if (!address || function_size != sizeof(address)) {
        const char *error = dlerror();
        printf("resolve %s: FAIL (%s)\n", name,
            error ? error : "missing symbol");
        return -1;
    }
    memcpy(function_storage, &address, sizeof(address));
    printf("resolve %s: PASS\n", name);
    return 0;
}

static uint32_t wait_for_markers(volatile uint32_t *first,
    uint32_t first_expected, volatile uint32_t *second,
    uint32_t second_expected)
{
    uint32_t waited_ms = 0;

    while (waited_ms < 5000u) {
        clflush((u_long)(uintptr_t)first);
        if (second)
            clflush((u_long)(uintptr_t)second);
        mfence();
        if (*first == first_expected &&
            (!second || *second == second_expected))
            break;
        usleep(50000u);
        waited_ms += 50u;
    }
    clflush((u_long)(uintptr_t)first);
    if (second)
        clflush((u_long)(uintptr_t)second);
    mfence();
    return waited_ms;
}

int main(void)
{
    const uint32_t preflight_value = 0x11605a01u;
    const uint32_t active_value = 0x11605a02u;
    const uint32_t complete_value = 0x11605a03u;
    const uint32_t workload_ids[] = {ORACLE_WORKLOAD_ID};
    uint8_t stream_descriptor[32] = {0};
    AgcKernelSwVersion firmware = {0};
    SonySubmitDcbFn submit_dcb = NULL;
    SonySetupAsyncGraphicsFn setup_async_graphics = NULL;
    SonyRegisterWorkloadStreamFn register_stream = NULL;
    SonyUnregisterWorkloadStreamFn unregister_stream = NULL;
    SonyGetPacketSizeFn get_active_size = NULL;
    SonyGetPacketSizeFn get_complete_size = NULL;
    SonySetWorkloadsActiveFn set_active = NULL;
    SonySetWorkloadCompleteFn set_complete = NULL;
    void *module = NULL;
    void *memory = NULL;
    volatile uint32_t *preflight_marker = NULL;
    volatile uint32_t *active_marker = NULL;
    volatile uint32_t *complete_marker = NULL;
    uint32_t socid = 0;
    size_t socid_size = sizeof(socid);
    SceAgcCb cb;
    AgcCommandBufferSubmit submit = {0};
    uint32_t waited_ms;
    int32_t result;
    int memory_mapped = 0;
    int stream_registered = 0;
    int submission_unresolved = 0;
    int passed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("FW 11.60 installed-driver workload oracle\n");
    printf("backend policy: Sony module only; /dev/gc fallback forbidden\n");

    if (sceKernelGetProsperoSystemSwVersion(&firmware) != 0 ||
        (firmware.version >> 16) != 0x1160u) {
        printf("firmware gate: FAIL raw=0x%08X string=%s\n",
            firmware.version, firmware.version_string);
        goto done;
    }
    printf("firmware gate: PASS raw=0x%08X string=%s\n",
        firmware.version, firmware.version_string);

    if (sysctlbyname("hw.sce_main_socid", &socid, &socid_size, NULL, 0) != 0 ||
        socid_size != sizeof(socid) ||
        (socid & ~0x1fu) == 0x00840fc0u) {
        printf("standard-PS5 gate: FAIL socid=0x%08X\n", socid);
        goto done;
    }
    printf("standard-PS5 gate: PASS socid=0x%08X\n", socid);

    if (set_gpu_credentials() != 0) {
        printf("credentials-before-dlopen: FAIL\n");
        goto done;
    }
    printf("credentials-before-dlopen: PASS\n");

    module = dlopen("libSceAgcDriver.sprx", RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        const char *error = dlerror();
        printf("installed driver dlopen: FAIL (%s)\n",
            error ? error : "unknown loader error");
        goto done;
    }
    printf("installed driver dlopen: PASS\n");

#define LOAD_SONY(storage, symbol) \
    if (load_symbol(module, symbol, &(storage), sizeof(storage)) != 0) \
        goto done
    LOAD_SONY(submit_dcb, "sceAgcDriverSubmitDcb");
    LOAD_SONY(setup_async_graphics, "sceAgcDriverSetupAsyncGraphics");
    LOAD_SONY(register_stream, "sceAgcDriverRegisterWorkloadStream");
    LOAD_SONY(unregister_stream, "sceAgcDriverUnregisterWorkloadStream");
    LOAD_SONY(get_active_size,
        "sceAgcDriverGetSetWorkloadsActivePacketSize");
    LOAD_SONY(get_complete_size,
        "sceAgcDriverGetSetWorkloadCompletePacketSize");
    LOAD_SONY(set_active, "sceAgcDriverSetWorkloadsActive");
    LOAD_SONY(set_complete, "sceAgcDriverSetWorkloadComplete");
#undef LOAD_SONY

    if (get_active_size() != ORACLE_ACTIVE_DWORDS ||
        get_complete_size() != ORACLE_COMPLETE_DWORDS) {
        printf("installed workload sizes: FAIL active=%u complete=%u\n",
            get_active_size(), get_complete_size());
        goto done;
    }
    printf("installed workload sizes: PASS active=%u complete=%u\n",
        get_active_size(), get_complete_size());

    result = setup_async_graphics(1u);
    printf("installed async setup=0x%08X\n", (unsigned)result);
    if (result != AGC_OK)
        goto done;

    result = sceKernelMapNamedSystemFlexibleMemory(&memory,
        ORACLE_MEMORY_SIZE, 0x33, 0, "OpenAgcFw1160SonyOracle");
    printf("oracle memory result=%d address=%p\n", result, memory);
    if (result != 0 || !memory)
        goto done;
    memory_mapped = 1;
    memset(memory, 0, ORACLE_MEMORY_SIZE);
    preflight_marker = (volatile uint32_t *)((uint8_t *)memory + 0x1000u);
    active_marker = preflight_marker + 1;
    complete_marker = preflight_marker + 2;

    agcCbInit(&cb, memory, ORACLE_DCB_SIZE);
    if (!sceAgcDcbWriteData(&cb, 2u, 0u,
            (uint64_t)(uintptr_t)preflight_marker, &preflight_value,
            1u, 1u, 1u)) {
        printf("installed preflight build: FAIL\n");
        goto done;
    }
    clflush((u_long)(uintptr_t)memory);
    clflush((u_long)(uintptr_t)preflight_marker);
    mfence();
    submit.command_address = (uintptr_t)memory;
    submit.dword_count = agcCbUsedDwords(&cb);
    result = submit_dcb(&submit);
    submission_unresolved = result == AGC_OK;
    waited_ms = result == AGC_OK ? wait_for_markers(preflight_marker,
        preflight_value, NULL, 0u) : 0u;
    printf("installed preflight submit=0x%08X marker=0x%08X wait=%u ms\n",
        (unsigned)result, *preflight_marker, waited_ms);
    if (result != AGC_OK || *preflight_marker != preflight_value) {
        printf("installed preflight execution: FAIL; workload not attempted\n");
        goto done;
    }
    submission_unresolved = 0;
    printf("installed preflight execution: PASS\n");

    (void)snprintf((char *)stream_descriptor, sizeof(stream_descriptor),
        "OpenAGC Sony oracle");
    result = register_stream(ORACLE_STREAM_ID, stream_descriptor);
    printf("installed workload register=0x%08X\n", (unsigned)result);
    if (result != AGC_OK)
        goto done;
    stream_registered = 1;

    *active_marker = 0u;
    *complete_marker = 0u;
    agcCbInit(&cb, memory, ORACLE_DCB_SIZE);
    uint32_t *packet = agcCbAllocDwords(&cb, ORACLE_ACTIVE_DWORDS);
    if (!packet || set_active(packet, 0u, ORACLE_STREAM_ID,
            workload_ids, 1u) != AGC_OK ||
        !sceAgcDcbWriteData(&cb, 2u, 0u,
            (uint64_t)(uintptr_t)active_marker, &active_value,
            1u, 1u, 1u)) {
        printf("installed active packet build: FAIL\n");
        goto done;
    }
    packet = agcCbAllocDwords(&cb, ORACLE_COMPLETE_DWORDS);
    if (!packet || set_complete(packet, 0u, ORACLE_STREAM_ID,
            ORACLE_WORKLOAD_ID) != AGC_OK ||
        !sceAgcDcbWriteData(&cb, 2u, 0u,
            (uint64_t)(uintptr_t)complete_marker, &complete_value,
            1u, 1u, 1u)) {
        printf("installed complete packet build: FAIL\n");
        goto done;
    }
    printf("installed inline workload DCB dwords=%u\n",
        agcCbUsedDwords(&cb));
    clflush((u_long)(uintptr_t)memory);
    clflush((u_long)(uintptr_t)active_marker);
    mfence();
    submit.command_address = (uintptr_t)memory;
    submit.dword_count = agcCbUsedDwords(&cb);
    submit.reserved = 0u;
    result = submit_dcb(&submit);
    submission_unresolved = result == AGC_OK;
    waited_ms = result == AGC_OK ? wait_for_markers(active_marker,
        active_value, complete_marker, complete_value) : 0u;
    printf("installed workload submit=0x%08X active=0x%08X "
           "complete=0x%08X wait=%u ms\n",
        (unsigned)result, *active_marker, *complete_marker, waited_ms);
    if (result != AGC_OK || *active_marker != active_value ||
        *complete_marker != complete_value)
        goto done;
    submission_unresolved = 0;
    passed = 1;

done:
    if (submission_unresolved) {
        printf("GPU submission unresolved; stream and memory retained for "
               "process teardown\n");
    } else if (stream_registered && unregister_stream) {
        result = unregister_stream(ORACLE_STREAM_ID);
        printf("installed workload unregister=0x%08X\n", (unsigned)result);
        if (result != AGC_OK)
            passed = 0;
    }
    if (memory_mapped && !submission_unresolved) {
        result = sceKernelReleaseFlexibleMemory(memory, ORACLE_MEMORY_SIZE);
        printf("oracle memory release=%d\n", result);
        if (result != 0)
            passed = 0;
    }
    printf("FW 11.60 installed-driver workload oracle: %s\n",
        passed ? "PASS" : "FAIL");
    printf("REBOOT REQUIRED before any direct /dev/gc test\n");
    fflush(NULL);
    (void)kill(getpid(), SIGKILL);
    for (;;)
        pause();
}
