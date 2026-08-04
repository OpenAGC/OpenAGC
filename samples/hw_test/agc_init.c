/*
 * agc_init.c — PS5 AGC initialization + NOP submit test
 *
 * Adapted from freegnm-examples/triangle/src/main.c for PS5 AGC.
 * Tests the selected native driver backend:
 *   1. sce_agc_initialize() — open /dev/gc + CONTEXT_QUERY ioctl + mmap
 *   2. sceAgcDriverGetPaDebugInterfaceVersion() — PA debug query
 *   3. Submit two marker DCBs as one descriptor-array frame
 *   4. Create + destroy a user special queue
 *
 * This is the GPU command submission validation step. Run after
 * videoout_linear confirms the display pipeline works.
 *
 * Deploy: make agc_init.elf && make deploy_agc
 */

#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <machine/cpufunc.h>

#include <ps5/kernel.h>

#include "agcdriver.h"
#include "agc_cb.h"
#include "agc_error.h"
#include "agc_pm4.h"
#include "agc_runtime_diag.h"
#include "agc_test_defaults.h"

#ifndef AGC_EXPECT_FIRMWARE_ABI_KEY
#define AGC_EXPECT_FIRMWARE_ABI_KEY 0x0550u
#endif

#ifndef AGC_EXPECT_TRINITY
#define AGC_EXPECT_TRINITY 0
#endif

#ifndef AGC_EXPECT_DEFAULT_STATES
#define AGC_EXPECT_DEFAULT_STATES 1
#endif

#ifndef AGC_EXPECT_WORKLOAD
#define AGC_EXPECT_WORKLOAD 1
#endif

#ifndef AGC_SELF_TERMINATE
#define AGC_SELF_TERMINATE 0
#endif

/* Private hardware-test diagnostic; not part of the installed public ABI. */
extern const char *agcDriverDebugBackendName(void);

int sceKernelMapNamedSystemFlexibleMemory(
    void **virtualAddress, size_t length, int protection, int flags,
    const char *name);

/*
 * GPU process credential bypass.
 *
 * The kernel GPU ioctl handlers check the thread's cached ucred:
 *
 *   gs:[0]                       -> curthread
 *   [curthread + 0x140]          -> td_ucred
 *   [td_ucred + 0x58]            -> cr_sceAuthId
 *
 * Two credential check functions are used:
 *
 *   0xd8e70ac0: returns 1 if authid == 0x4800000000000009
 *                    or authid == 0x480100000000002c (exact match)
 *               Used in the main ioctl dispatch to set ebx.
 *               Queue create checks: cmp ebx, 1; je error
 *               So ebx must be 0 (authid must NOT be an exact match).
 *
 *   0xd8e70400: returns 1 if (authid & 0xff0f000000000000) == 0x4801000000000000
 *               Used inside the queue create handler.
 *               If it returns 0, error 0x804c000b is returned.
 *
 * Solution: set authid to 0x4801000000000000
 *   - 0xd8e70ac0 returns 0 (not an exact match) → ebx = 0 → passes
 *   - 0xd8e70400 returns 1 (masked match) → credential check passes
 *
 * Homebrew payloads have authid 0x480000001000000e (from elfldr), which
 * fails both checks. We set cr_sceAuthId to 0x4801000000000000 via
 * kernel_set_ucred_authid (provided by the ps5-payload-sdk CRT).
 *
 * NOTE: proc+0x58 is NOT the credential field -- it is a kernel pointer.
 * Writing to it causes a kernel panic. The credential is in ucred, not
 * proc. The ps5-payload-sdk offset KERNEL_OFFSET_UCRED_CR_SCEAUTHID = 0x58
 * confirms this is ucred+0x58, not proc+0x58.
 */
#define GPU_AUTHID_REQUIRED  0x4801000000000000ULL
#define GPU_AUTHID_MASK      0xff0f000000000000ULL

/*
 * The kernel GPU ioctl handlers read cr_sceAuthId from td_ucred
 * (curthread->td_ucred at thread+0x140), NOT from proc->p_ucred.
 * kernel_set_ucred_authid modifies p_ucred. In FreeBSD, td_ucred is
 * a crhold reference to the same ucred object as p_ucred, so modifying
 * p_ucred in-place should be visible through td_ucred. But if the
 * exploit/jailbreak changed p_ucred to point to a new ucred, td_ucred
 * may still point to the old one. We patch both to be safe.
 *
 * Kernel struct offsets (FW 5.50):
 *   proc + 0x40  = p_ucred (pointer to ucred)
 *   thread + 0x140 = td_ucred (pointer to ucred)
 *   ucred + 0x58 = cr_sceAuthId (uint64)
 *
 * To find td_ucred, we walk proc's thread list:
 *   proc + 0x08  = p_numthreads (or p_threads.le_first)
 *   proc + 0x10  = p_threads (list_head, first thread)
 *   thread + 0x00 = td_plist.le_next (next thread in proc)
 */
#define KERNEL_OFFSET_PROC_P_UCRED    0x40
#define KERNEL_OFFSET_UCRED_AUTHID    0x58
#define KERNEL_OFFSET_THREAD_UCRED    0x140

static int set_gpu_credentials(void) {
    pid_t pid = getpid();
    uint64_t authid = 0;

    /* Read current authid from p_ucred */
    authid = kernel_get_ucred_authid(pid);
    printf("    GPU cred: current authid = 0x%016llx\n",
           (unsigned long long)authid);

    if ((authid & GPU_AUTHID_MASK) == GPU_AUTHID_REQUIRED) {
        printf("    GPU cred: already set, skipping\n");
        /* Still need to check td_ucred — may differ */
    }

    /* Set authid on p_ucred */
    if (kernel_set_ucred_authid(pid, GPU_AUTHID_REQUIRED)) {
        printf("    GPU cred: kernel_set_ucred_authid failed\n");
        return -1;
    }

    /* Verify p_ucred was updated */
    authid = kernel_get_ucred_authid(pid);
    printf("    GPU cred: p_ucred authid = 0x%016llx (%s)\n",
           (unsigned long long)authid,
           (authid & GPU_AUTHID_MASK) == GPU_AUTHID_REQUIRED ? "OK" : "FAIL");

    /* Also patch td_ucred directly for each thread.
     * The kernel ioctl handler reads cr_sceAuthId from td_ucred
     * (curthread->td_ucred at thread+0x140), NOT from p_ucred.
     * kernel_set_ucred_authid modifies p_ucred. If td_ucred and
     * p_ucred point to the same ucred object (the normal case),
     * the modification is already visible. But if the exploit
     * changed p_ucred to point to a new ucred, td_ucred may still
     * point to the old one.
     *
     * Thread list traversal (from ps5-payload-sdk kernel_get_proc_thread):
     *   proc + 0x10  = first thread (p_threads.le_first)
     *   thread + 0x10 = next thread (td_plist.le_next)
     *   thread + 0x140 = td_ucred
     *   thread + 0x9c  = td_tid
     */
    intptr_t proc = kernel_get_proc(pid);
    if (proc) {
        uint64_t p_ucred = 0;
        kernel_copyout(proc + KERNEL_OFFSET_PROC_P_UCRED, &p_ucred, sizeof(p_ucred));
        printf("    GPU cred: p_ucred = 0x%lx\n", (unsigned long)p_ucred);

        /* Walk the thread list using correct offsets */
        uint64_t thread_ptr = kernel_getlong(proc + 0x10);

        int patched = 0;
        int threads = 0;
        for (int i = 0; i < 16 && thread_ptr != 0; i++) {
            threads++;
            uint64_t td_ucred = 0;
            kernel_copyout(thread_ptr + KERNEL_OFFSET_THREAD_UCRED,
                          &td_ucred, sizeof(td_ucred));

            uint64_t tid = kernel_getlong(thread_ptr + 0x9c);
            printf("    GPU cred: thread[%d] tid=%d td_ucred=0x%lx",
                   i, (int)tid, (unsigned long)td_ucred);

            if (td_ucred != 0 && td_ucred != p_ucred) {
                /* td_ucred differs from p_ucred — patch it */
                uint64_t cur = 0;
                kernel_copyout(td_ucred + KERNEL_OFFSET_UCRED_AUTHID,
                              &cur, sizeof(cur));
                printf(" authid=0x%016llx (DIFFERS from p_ucred)",
                       (unsigned long long)cur);
                if ((cur & GPU_AUTHID_MASK) != GPU_AUTHID_REQUIRED) {
                    uint64_t new_id = GPU_AUTHID_REQUIRED;
                    kernel_copyin(&new_id,
                                 td_ucred + KERNEL_OFFSET_UCRED_AUTHID,
                                 sizeof(new_id));
                    printf(" → PATCHED");
                    patched++;
                }
            } else if (td_ucred == p_ucred) {
                printf(" (matches p_ucred)");
            } else {
                printf(" (NULL!)");
            }
            printf("\n");

            /* Next thread: td_plist.le_next at thread+0x10 */
            thread_ptr = kernel_getlong(thread_ptr + 0x10);
        }

        printf("    GPU cred: found %d thread(s), patched %d\n",
               threads, patched);
    } else {
        printf("    GPU cred: WARNING — kernel_get_proc failed\n");
    }

    /* Final verify */
    authid = kernel_get_ucred_authid(pid);
    return (authid & GPU_AUTHID_MASK) == GPU_AUTHID_REQUIRED ? 0 : -1;
}

static const char *errstr(int32_t err) {
    switch (err) {
    case AGC_OK:                        return "OK";
    case AGC_ERROR_NOT_INITIALIZED:     return "NOT_INITIALIZED";
    case AGC_ERROR_INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
    case AGC_ERROR_INVALID_STATE:       return "INVALID_STATE";
    case AGC_ERROR_SUBMIT_FAILED:       return "SUBMIT_FAILED";
    case AGC_ERROR_CB_INVALID_QUEUE:    return "CB_INVALID_QUEUE";
    case AGC_ERROR_INTERNAL:            return "INTERNAL";
    case AGC_ERROR_NOT_FOUND:           return "NOT_FOUND";
    case AGC_ERROR_OUT_OF_MEMORY:       return "OUT_OF_MEMORY";
    case AGC_ERROR_NOT_SUPPORTED:       return "NOT_SUPPORTED";
    default:                            return "UNKNOWN";
    }
}

int main(void) {
    int32_t err;
    int32_t dcb_err = -1;  /* result of DCB submit (step 5) */
    int32_t cred_err = -1; /* result of credential bypass (step 0) */
    uint32_t version;
    bool profile_ok = false;
    bool backend_ok = false;
    bool wait64_ok = false;
    bool defaults_ok = false;
    bool async_ok = false;
    bool queue_contract_ok = false;
    bool suspend_ok = false;
    bool workload_ok = false;
    bool submit_memory_release_ok = false;
    bool shutdown_ok = false;
    bool success;
    AgcDriverRuntimeDiagnostics runtime_diag;
    SceAgcCb cb;

    printf("=== openagc AGC init + NOP submit test ===\n");

    /* --- Step 0: Set GPU process credentials --- */
    printf("[0] set_gpu_credentials()...\n");
    cred_err = set_gpu_credentials();
    if (cred_err != 0) {
        printf("    WARNING: GPU credential bypass failed\n");
        printf("    GPU ioctls will likely return EPERM/EAGAIN\n");
    } else {
        printf("    GPU credentials set\n");
    }

    /* --- Step 1: Initialize AGC context --- */
    printf("[1] sce_agc_initialize()...\n");

    /* Pre-init diagnostics: check process identity and firmware */
    printf("    DIAG: pid=%d, uid=%d, euid=%d\n", getpid(), getuid(), geteuid());

    /* A Sony-first artifact must not mutate /dev/gc before selection. */
#ifndef AGC_EXPECT_SONY_DRIVER
    /* Check /dev/gc open + CONTEXT_QUERY with detailed diagnostics */
    int test_fd = open("/dev/gc", O_RDWR);
    if (test_fd < 0) {
        printf("    DIAG: /dev/gc open failed (errno=%d)\n", errno);
        struct stat st;
        if (stat("/dev/gc", &st) == 0)
            printf("    DIAG: /dev/gc exists (mode=0%o, type=%d)\n", st.st_mode & 0777, (int)(st.st_mode & S_IFMT));
        else
            printf("    DIAG: /dev/gc stat failed (errno=%d)\n", errno);
    } else {
        printf("    DIAG: /dev/gc opened OK (fd=%d)\n", test_fd);

        /* CONTEXT_QUERY ioctl: 0xC004812E = RW, size=4, type=0x81, nr=0x2e
         * This is the real init query used by libSceAgcDriver module_start.
         * Returns a 32-bit capability mask:
         *   bits [15:0]  = context initialized flag
         *   bits [31:16] = secondary capability flag */
        uint32_t ctx_query = 0;
        int ioret = ioctl(test_fd, 0xC004812Eu, &ctx_query);
        printf("    DIAG: CONTEXT_QUERY 0xC004812E returned %d (errno=%d, cap=0x%X)\n",
               ioret, errno, ctx_query);

        /* Do not map/unmap the GC aperture as a preflight. The real
         * initialization must own that mapping for the complete lifecycle. */

        close(test_fd);
    }
#else
    printf("    DIAG: direct /dev/gc preflight skipped for installed driver\n");
#endif

    err = sce_agc_initialize();
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("    FATAL: cannot initialize AGC\n");
        return 1;
    }
    const char *backend_name = agcDriverDebugBackendName();
    printf("    backend: %s\n", backend_name);
#ifdef AGC_EXPECT_SONY_DRIVER
    backend_ok = strcmp(backend_name, "sony-installed") == 0;
#else
    backend_ok = strcmp(backend_name, "prospero-gc-submit16") == 0;
#endif
    printf("    Expected backend selection: %s\n",
           backend_ok ? "PASS" : "FAIL");
    if (!backend_ok)
        return 1;
    err = agcDriverDebugRuntimeProfile(&runtime_diag);
    if (err == AGC_OK) {
        printf("    profile: fw=0x%08X family=%s model=%s\n",
               runtime_diag.firmware_version,
               agcProsperoAbiFamilyName(runtime_diag.profile.family),
               runtime_diag.profile.is_trinity ? "trinity" : "standard-ps5");
        printf("    capabilities: queue_auth=%u tf_ring=%u eop=0x%X\n",
               runtime_diag.profile.authenticated_special_queue ? 1u : 0u,
               runtime_diag.profile.supports_tf_ring ? 1u : 0u,
               runtime_diag.profile.eop_ring_offset);
        printf("    memory: gpu_info=0x%X cwsr_work=0x%X cwsr_size=0x%X\n",
               runtime_diag.profile.gpu_info_span,
               runtime_diag.profile.cwsr_work_offset,
               runtime_diag.profile.cwsr_size);
        profile_ok = (runtime_diag.firmware_version >> 16u) ==
                AGC_EXPECT_FIRMWARE_ABI_KEY &&
            runtime_diag.profile.family == AGC_PROSPERO_ABI_STANDARD &&
            runtime_diag.profile.is_trinity == (AGC_EXPECT_TRINITY != 0) &&
            runtime_diag.profile.authenticated_special_queue &&
            runtime_diag.profile.supports_tf_ring &&
            runtime_diag.profile.eop_ring_offset == 0x39000u &&
            runtime_diag.profile.gpu_info_span ==
                (AGC_EXPECT_TRINITY ? 0x180000u : 0x100000u) &&
            runtime_diag.profile.cwsr_work_offset ==
                (AGC_EXPECT_TRINITY ? 0x1000000u : 0xa00000u) &&
            runtime_diag.profile.cwsr_size ==
                (AGC_EXPECT_TRINITY ? 0x1600000u : 0x1000000u);
    }
    printf("    Runtime profile FW ABI 0x%04X: %s\n",
           AGC_EXPECT_FIRMWARE_ABI_KEY, profile_ok ? "PASS" : "FAIL");
    if (!profile_ok)
        return 1;

    err = sceAgcInit(agcTestDefaultsVersion(AGC_EXPECT_FIRMWARE_ABI_KEY));
    printf("    sceAgcInit defaults selection: 0x%08X (%s)\n",
           (unsigned)err, errstr(err));
    if (err != AGC_OK)
        return 1;

    /* --- Step 2: Initialize internal memory --- */
    printf("[2] sce_agc_initialize_internal_memory()...\n");
    err = sce_agc_initialize_internal_memory();
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("    FATAL: cannot allocate internal GPU memory\n");
        return 1;
    }
    printf("    Internal GPU memory allocated\n");

    /* --- Step 3: Notify default states --- */
    printf("[3] sceAgcDriverNotifyDefaultStates()...\n");
    err = sceAgcDriverNotifyDefaultStates(0);
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    defaults_ok = AGC_EXPECT_DEFAULT_STATES
        ? err == AGC_OK
        : err == AGC_ERROR_NOT_SUPPORTED;
    printf("    Default states contract: %s\n",
           defaults_ok ? "PASS" : "FAIL");


    /* --- Step 4: Verify the official FW 5.50 PA-debug permission stub --- */
    printf("[4] sceAgcDriverGetPaDebugInterfaceVersion()...\n");
    version = sceAgcDriverGetPaDebugInterfaceVersion();
    printf("    version: 0x%08X\n", version);
    if (version == AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT)
        printf("    FW 5.50 permission stub: PASS\n");
    else
        printf("    WARNING: unexpected PA debug result\n");

    /* --- Step 5: Submit two independent marker DCBs as one frame --- */
    printf("[5] sceAgcDriverSubmitMultiDcbs() with two DCBs...\n");
    static const uint32_t expected_markers[2] = {
        0xD001CAFEu, 0xD002CAFEu
    };
    uint32_t used_dwords[2] = {0, 0};
    void *submit_memory = NULL;
    int map_err = sceKernelMapNamedSystemFlexibleMemory(
        &submit_memory, 0x4000u, 0x33, 0, "agc_multi_submit");
    if (map_err != 0 || !submit_memory) {
        printf("    ERROR: flexible-memory allocation failed: %d\n", map_err);
        return 1;
    }
    uint32_t *cb_buffers[2] = {
        (uint32_t *)submit_memory,
        (uint32_t *)((uint8_t *)submit_memory + 0x1000u),
    };
    volatile uint32_t *submit_markers =
        (volatile uint32_t *)((uint8_t *)submit_memory + 0x2000u);
    submit_markers[0] = 0;
    submit_markers[1] = 0;
    clflush((u_long)(uintptr_t)submit_markers);
    mfence();

    for (uint32_t i = 0; i < 2; i++) {
        agcCbInit(&cb, cb_buffers[i], 0x1000u);
        uint64_t marker_addr = (uint64_t)(uintptr_t)&submit_markers[i];
        if (!sceAgcDcbWriteData(&cb, 2, 0, marker_addr,
                                &expected_markers[i], 1, 1, 1)) {
            printf("    ERROR: failed to allocate WRITE_DATA packet %u\n", i);
            return 1;
        }
        used_dwords[i] = agcCbUsedDwords(&cb);
        printf("    DCB[%u]: %u dwords at %p -> marker %p\n",
               i, used_dwords[i], (void *)cb_buffers[i],
               (const void *)&submit_markers[i]);
        clflush((u_long)(uintptr_t)cb_buffers[i]);
    }
    mfence();

    void *dcb_addresses[2] = { cb_buffers[0], cb_buffers[1] };
    dcb_err = AGC_OK;
    for (uint32_t run = 0; run < 3; run++) {
        uint32_t run_markers[2] = {
            expected_markers[0] + run * 0x00010000u,
            expected_markers[1] + run * 0x00010000u,
        };
        cb_buffers[0][4] = run_markers[0];
        cb_buffers[1][4] = run_markers[1];
        clflush((u_long)(uintptr_t)cb_buffers[0]);
        clflush((u_long)(uintptr_t)cb_buffers[1]);
        submit_markers[0] = 0;
        submit_markers[1] = 0;
        clflush((u_long)(uintptr_t)submit_markers);
        mfence();

        err = sceAgcDriverSubmitMultiDcbs(dcb_addresses, used_dwords, 2);
        printf("    run %u submit: 0x%08X (%s)\n",
               run + 1, (unsigned)err, errstr(err));

        uint32_t marker_wait_ms = 0;
        while (marker_wait_ms < 5000u) {
            clflush((u_long)(uintptr_t)submit_markers);
            mfence();
            if (submit_markers[0] == run_markers[0] &&
                submit_markers[1] == run_markers[1])
                break;
            usleep(50000);
            marker_wait_ms += 50u;
        }
        clflush((u_long)(uintptr_t)submit_markers);
        mfence();
        printf("    run %u markers after %u ms: [0]=0x%08X [1]=0x%08X\n",
               run + 1, marker_wait_ms,
               submit_markers[0], submit_markers[1]);
        if (err != AGC_OK ||
            submit_markers[0] != run_markers[0] ||
            submit_markers[1] != run_markers[1])
            dcb_err = AGC_ERROR_SUBMIT_FAILED;
    }
    printf("    Batched DCB execution: %s\n",
           dcb_err == AGC_OK ? "PASS" : "FAIL");

    /* --- Step 5b: Execute the SPRX-exact nine-dword wait packet --- */
    printf("[5b] 64-bit nine-dword WaitRegMem + marker...\n");
    volatile uint64_t *wait_value =
        (volatile uint64_t *)((uint8_t *)submit_memory + 0x3000u);
    volatile uint32_t *wait_marker =
        (volatile uint32_t *)((uint8_t *)submit_memory + 0x3010u);
    const uint64_t expected_wait_value = 0x1122334455667788ULL;
    const uint32_t expected_wait_marker = 0xD064CAFEu;
    *wait_value = 0u;
    *wait_marker = 0u;
    clflush((u_long)(uintptr_t)wait_value);
    clflush((u_long)(uintptr_t)wait_marker);
    mfence();

    agcCbInit(&cb, cb_buffers[0], 0x1000u);
    const uint32_t wait_value_words[2] = {
        (uint32_t)expected_wait_value,
        (uint32_t)(expected_wait_value >> 32u),
    };
    uint32_t *wait_publish = sceAgcDcbWriteData(
        &cb, 2u, 0u, (uint64_t)(uintptr_t)wait_value,
        wait_value_words, 2u, 0u, 1u);
    uint32_t *wait_packet = sceAgcDcbWaitRegMem(
        &cb, 1u, 3u, 0u, 0u, (uint64_t)(uintptr_t)wait_value,
        expected_wait_value, UINT64_MAX, UINT32_MAX);
    uint32_t *wait_write = sceAgcDcbWriteData(
        &cb, 2u, 0u, (uint64_t)(uintptr_t)wait_marker,
        &expected_wait_marker, 1u, 1u, 1u);
    uint32_t wait_used_dwords = agcCbUsedDwords(&cb);
    bool wait_shape_ok = wait_publish == cb_buffers[0] && wait_packet != NULL &&
        wait_write != NULL &&
        agcPm4Opcode(wait_packet[0]) == AGC_PM4_OP_WAIT_REG_MEM64 &&
        agcPm4Length(wait_packet[0]) == 9u &&
        wait_packet[8] == 0xFFFFu && wait_used_dwords == 20u;
    printf("    packet: header=0x%08X dwords=%u poll=0x%X shape=%s\n",
           wait_packet ? wait_packet[0] : 0u, wait_used_dwords,
           wait_packet ? wait_packet[8] : 0u,
           wait_shape_ok ? "PASS" : "FAIL");
    if (wait_shape_ok) {
        clflush((u_long)(uintptr_t)cb_buffers[0]);
        mfence();
        AgcCommandBufferSubmit wait_submit = {
            .command_address = (uintptr_t)cb_buffers[0],
            .dword_count = wait_used_dwords,
        };
        err = sceAgcDriverSubmitDcb(&wait_submit);
        uint32_t marker_wait_ms = 0u;
        while (marker_wait_ms < 5000u) {
            clflush((u_long)(uintptr_t)wait_marker);
            mfence();
            if (*wait_marker == expected_wait_marker)
                break;
            usleep(50000);
            marker_wait_ms += 50u;
        }
        clflush((u_long)(uintptr_t)wait_marker);
        clflush((u_long)(uintptr_t)wait_value);
        mfence();
        wait64_ok = err == AGC_OK && *wait_marker == expected_wait_marker;
        printf("    submit: 0x%08X value=0x%016llX marker after %u ms: 0x%08X (%s)\n",
               (unsigned)err, (unsigned long long)*wait_value,
               marker_wait_ms, *wait_marker,
               wait64_ok ? "PASS" : "FAIL");
    }

    /* --- Step 6: Setup async graphics (needed before queue create) --- */
    printf("[6] sceAgcDriverSetupAsyncGraphics(1)...\n");
    err = sceAgcDriverSetupAsyncGraphics(1);
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    async_ok = err == AGC_OK;
    if (err != AGC_OK)
        printf("    WARNING: async graphics setup failed\n");
    else
        printf("    Async graphics set up\n");

    /* --- Step 7: Create a user special queue --- */
    printf("[7] _sceAgcDriverCreateUserSpecialQueue()...\n");

    /* Verify authid right before the queue create */
    {
        pid_t mypid = getpid();
        intptr_t myproc = kernel_get_proc(mypid);
        if (myproc) {
            uint64_t pucred = 0;
            kernel_copyout(myproc + 0x40, &pucred, sizeof(pucred));
            uint64_t authid_val = 0;
            kernel_copyout(pucred + 0x58, &authid_val, sizeof(authid_val));
            printf("    DIAG: pre-ioctl authid = 0x%016llx (ucred=0x%lx)\n",
                   (unsigned long long)authid_val, (unsigned long)pucred);
        }
    }

    /* NOTE: Do NOT run a direct queue create ioctl test here — it would
     * occupy the queue slot and cause the library's call to fail with
     * 0x804C0012 (slot already in use). The kernel queue create handler
     * at 0xd8f66bb0 checks if the slot at ctx + offset is already non-zero
     * and returns 0x804C0012 if it is. */

    int32_t queue_handle = _sceAgcDriverCreateUserSpecialQueue();
    printf("    result: %d (handle)\n", queue_handle);
    queue_contract_ok =
#ifdef AGC_EXPECT_SONY_DRIVER
        queue_handle == AGC_ERROR_NOT_SUPPORTED;
#else
        queue_handle >= 0;
#endif
    if (queue_handle < 0)
        printf("    WARNING: queue creation failed (err=0x%08X)\n",
               (unsigned)queue_handle);
    else
        printf("    Compute queue created (handle=%d)\n", queue_handle);

    /* --- Step 8: Validate the public stub, then use the private carrier. --- */
    printf("[8] suspend-point public ABI + private primary carrier...\n");

    /* The suspend point ioctl (0xC010811C) handler at 0xd8f66ff0
     * requires a queue to already exist in the computed slot.
     *
     * With GPU credentials (cr_sceAuthId = 0x4801000000000000), the
     * credential check at 0xd8e70400 passes, so the handler falls through
     * to the magic-value checks. The magic triple (0xaf1e80b7,
     * 0x8b4cdd90, 0x99f68d6c) selects the SAME config table as the queue
     * create ioctl, mapping to slot (field0=2, field1=3, field2=5) at
     * ctx offset 0x158. Non-magic values like (1,0,0) would compute a
     * different slot (0x64) and fail with 0x804C0001 (no queue). */
    if (queue_handle >= 0) {
        int32_t direct_result = sceAgcDriverSuspendPointSubmitDirect(
            0xaf1e80b7u, 0x8b4cdd90u, 0x99f68d6cu, 0u);
        printf("    public Direct result: 0x%08X (%s)\n",
               (unsigned)direct_result,
               direct_result ==
                   (int32_t)AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT
                   ? "permission stub" : "unexpected");
        err = sce_agc_internal_suspend_point_submit_primary(
            0xaf1e80b7u, 0x8b4cdd90u, 0x99f68d6cu, 0u);
        printf("    private primary result: 0x%08X (%s)\n",
               (unsigned)err, errstr(err));
        suspend_ok = err == AGC_OK;
        if (err != AGC_OK)
            printf("    WARNING: suspend point submit failed\n");
        else
            printf("    Suspend point submitted\n");

        printf("[8b] sceAgcDriverIsSuspendPointInFlightDirect()...\n");
        int32_t query_result =
            sceAgcDriverIsSuspendPointInFlightDirect(0u);
        printf("    result: 0x%08X (%s)\n", (unsigned)query_result,
               query_result == (int32_t)AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT
                   ? "permission stub" : "unexpected");
    } else {
        printf("    skipped — no queue for suspend point\n");
#ifdef AGC_EXPECT_SONY_DRIVER
        suspend_ok = queue_handle == AGC_ERROR_NOT_SUPPORTED;
#endif
    }

    /* --- Step 9: Destroy the queue (only if created) --- */
    if (queue_handle >= 0) {
        printf("[9] _sceAgcDriverDestroyUserSpecialQueue()...\n");
        err = _sceAgcDriverDestroyUserSpecialQueue();
        printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
        queue_contract_ok = queue_contract_ok && err == AGC_OK;
        if (err != AGC_OK)
            printf("    WARNING: queue destruction failed\n");
        else
            printf("    Queue destroyed\n");
    } else {
        printf("[9] skipped — no queue to destroy\n");
    }

    /* --- Step 10: Workload tracking --- */
    printf("[10] sceAgcDriverSetWorkloadsActive(1)...\n");
    err = sceAgcDriverSetWorkloadsActive(1);
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err == AGC_OK) {
        printf("    Workload begun\n");
        printf("[10b] sceAgcDriverSetWorkloadComplete(1)...\n");
        err = sceAgcDriverSetWorkloadComplete(1);
        printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
        workload_ok = AGC_EXPECT_WORKLOAD && err == AGC_OK;
        if (err == AGC_OK)
            printf("    Workload ended\n");
        else
            printf("    WARNING: EndWorkload failed\n");

        if (workload_ok) {
            const uint32_t workload_marker = 0xD01DCAFEu;
            submit_markers[0] = 0u;
            cb_buffers[0][4] = workload_marker;
            clflush((u_long)(uintptr_t)submit_markers);
            clflush((u_long)(uintptr_t)cb_buffers[0]);
            mfence();

            AgcCommandBufferSubmit workload_probe = {
                .command_address = (uintptr_t)cb_buffers[0],
                .dword_count = used_dwords[0],
                .reserved = 0u,
            };
            err = sceAgcDriverSubmitDcb(&workload_probe);
            uint32_t workload_wait_ms = 0u;
            while (workload_wait_ms < 5000u) {
                clflush((u_long)(uintptr_t)submit_markers);
                mfence();
                if (submit_markers[0] == workload_marker)
                    break;
                usleep(50000u);
                workload_wait_ms += 50u;
            }
            printf("[10c] post-workload marker after %u ms: 0x%08X (%s)\n",
                   workload_wait_ms, submit_markers[0],
                   err == AGC_OK && submit_markers[0] == workload_marker ?
                       "PASS" : "FAIL");
            workload_ok = err == AGC_OK &&
                submit_markers[0] == workload_marker;
        }
    } else {
        workload_ok = !AGC_EXPECT_WORKLOAD && err == AGC_ERROR_NOT_SUPPORTED;
        printf("    Workload fail-closed contract: %s\n",
               workload_ok ? "PASS" : "FAIL");
    }

    /* --- Step 11: Release sample memory and shut down the public backend. --- */
    printf("[11] release submit memory...\n");
    submit_memory_release_ok = munmap(submit_memory, 0x4000u) == 0;
    printf("    result: %s\n", submit_memory_release_ok ? "PASS" : "FAIL");

    printf("[12] agcDriverShutdown()...\n");
    err = agcDriverShutdown();
    shutdown_ok = err == AGC_OK;
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));

    success = backend_ok && profile_ok && defaults_ok &&
        dcb_err == AGC_OK && wait64_ok &&
        async_ok && queue_contract_ok && suspend_ok && workload_ok &&
        submit_memory_release_ok && shutdown_ok;

    /* --- Summary --- */
    printf("\n=== Summary ===\n");
    printf("  GPU credentials:   %s\n",
           cred_err == 0 ? "set (cr_sceAuthId)" : "FAILED");
    printf("  AGC init:          OK\n");
    printf("  Backend selection: %s\n", backend_ok ? "PASS" : "FAILED");
    printf("  Runtime profile:   FW ABI 0x%04X %s\n",
           AGC_EXPECT_FIRMWARE_ABI_KEY, profile_ok ? "PASS" : "FAILED");
    printf("  Internal memory:   OK\n");
    printf("  Default states:    %s\n", defaults_ok ? "PASS" : "FAILED");
    printf("  PA debug version:  0x%08X\n", version);
    printf("  Batched DCBs:      %s\n",
           dcb_err == AGC_OK ? "OK" : "check step 5");
    printf("  9-dword wait64:    %s\n", wait64_ok ? "PASS" : "FAILED");
    printf("  Async graphics:    %s\n", async_ok ? "PASS" : "FAILED");
    printf("  Queue contract:    %s\n",
           queue_contract_ok ? "PASS" : "FAILED");
    printf("  Suspend point:     %s\n", suspend_ok ? "PASS" : "FAILED");
    printf("  Workload contract: %s\n", workload_ok ? "PASS" : "FAILED");
    printf("  Submit memory:     %s\n",
           submit_memory_release_ok ? "released" : "FAILED");
    printf("  Driver shutdown:   %s\n", shutdown_ok ? "PASS" : "FAILED");
    printf("=== Done ===\n");
    printf("Probe result: %s\n", success ? "PASS" : "FAIL");
    fflush(stdout);
    fflush(stderr);

#if AGC_SELF_TERMINATE
    kill(getpid(), SIGKILL);
    _exit(success ? 0 : 1);
#else
    return success ? 0 : 1;
#endif
}
