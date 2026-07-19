/*
 * agc_init.c — PS5 AGC initialization + NOP submit test
 *
 * Adapted from freegnm-examples/triangle/src/main.c for PS5 AGC.
 * Tests the native /dev/gc backend:
 *   1. sce_agc_initialize() — open /dev/gc + CONTEXT_QUERY ioctl + mmap
 *   2. sceAgcDriverGetPaDebugInterfaceVersion() — PA debug query
 *   3. Build a NOP command buffer and submit via sceAgcDriverSubmitDcb()
 *   4. Create + destroy a user special queue
 *
 * This is the GPU command submission validation step. Run after
 * videoout_linear confirms the display pipeline works.
 *
 * Deploy: make agc_init.elf && make deploy_agc
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "agcdriver.h"
#include "agc_cb.h"
#include "agc_error.h"

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

/* Command buffer: 4 KB = 1024 dwords, page-aligned for GPU access */
static uint32_t cb_buffer[1024] __attribute__((aligned(4096)));

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
    default:                            return "UNKNOWN";
    }
}

int main(void) {
    int32_t err;
    int32_t dcb_err = -1;  /* result of DCB submit (step 5) */
    int32_t cred_err = -1; /* result of credential bypass (step 0) */
    uint32_t version;
    SceAgcCb cb;
    AgcCommandBufferSubmit submit;

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

        /* Try mmap at the fixed GPU register address used by the SPRX */
        if (ioret == 0 && (ctx_query & 0xFFFF) == 0) {
            void *mmio = mmap((void*)0xfe0200000ULL, 0x4000,
                              PROT_READ | PROT_WRITE, MAP_SHARED, test_fd, 0);
            if (mmio == MAP_FAILED) {
                printf("    DIAG: mmap 0xfe0200000 failed (errno=%d)\n", errno);
            } else {
                printf("    DIAG: mmap 0xfe0200000 OK (ptr=%p)\n", mmio);
                munmap(mmio, 0x4000);
            }
        }

        /* Try FRAME_OPEN — may still fail with EINVAL if args are wrong,
         * or may succeed now that credentials are set. */
        uint32_t frame_arg[2] = {0, 0};
        ioret = ioctl(test_fd, 0xC0088100u, frame_arg);
        printf("    DIAG: FRAME_OPEN 0xC0088100 returned %d (errno=%d)\n",
               ioret, errno);

        close(test_fd);
    }

    err = sce_agc_initialize();
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("    FATAL: cannot initialize AGC\n");
        return 1;
    }
    printf("    /dev/gc opened, CONTEXT_QUERY succeeded\n");

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
    if (err != AGC_OK)
        printf("    WARNING: default state notification failed\n");
    else
        printf("    Default states notified\n");

    /* --- Step 4: Query PA debug interface version --- */
    printf("[4] sceAgcDriverGetPaDebugInterfaceVersion()...\n");
    version = sceAgcDriverGetPaDebugInterfaceVersion();
    printf("    version: 0x%08X\n", version);
    if (version == 0)
        printf("    WARNING: PADEBUG_4 returned 0 (may not be supported)\n");
    else
        printf("    PA debug interface queried OK\n");

    /* --- Step 5: Build and submit a NOP command buffer --- */
    printf("[5] sceAgcDriverSubmitDcb() with NOP packet...\n");

    /* Init command buffer cursor */
    agcCbInit(&cb, cb_buffer, sizeof(cb_buffer));

    /* Add a NOP packet (minimum 2 dwords for type-3 header) */
    uint32_t *nop = sceAgcCbNop(&cb, 2);
    if (!nop) {
        printf("    ERROR: failed to build NOP packet\n");
        return 1;
    }
    nop[0] = 0;  /* NOP data */

    uint32_t used_dwords = agcCbUsedDwords(&cb);
    printf("    CB built: %u dwords at %p\n", used_dwords, (void *)cb_buffer);

    /* Submit it.
     * NOTE: On the prospero backend, command_address must be a GPU VA, not a
     * CPU address. This sample passes a CPU static array which works on the
     * generic backend but will FAIL on real hardware. To fix, the buffer
     * must be allocated via sceKernelAllocateDirectMemory + MapDirectMemory +
     * makesysmap ioctl, and the GPU VA passed here. */
    submit.command_address = (uintptr_t)cb_buffer;
    submit.dword_count = used_dwords;
    submit.reserved = 0;

    err = sceAgcDriverSubmitDcb(&submit);
    dcb_err = err;
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("    WARNING: DCB submit failed\n");
        printf("    Check: CB header opcode, VMID, alignment\n");
    } else {
        printf("    NOP packet submitted to GPU!\n");
    }

    /* --- Step 6: Setup async graphics (needed before queue create) --- */
    printf("[6] sceAgcDriverSetupAsyncGraphics(1)...\n");
    err = sceAgcDriverSetupAsyncGraphics(1);
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
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
    if (queue_handle < 0)
        printf("    WARNING: queue creation failed (err=0x%08X)\n",
               (unsigned)queue_handle);
    else
        printf("    Compute queue created (handle=%d)\n", queue_handle);

    /* --- Step 8: Submit a suspend point (while queue is active) --- */
    printf("[8] sceAgcDriverSuspendPointSubmitDirect()...\n");

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
        err = sceAgcDriverSuspendPointSubmitDirect(
            0xaf1e80b7u, 0x8b4cdd90u, 0x99f68d6cu, 0u);
        printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
        if (err != AGC_OK)
            printf("    WARNING: suspend point submit failed\n");
        else
            printf("    Suspend point submitted\n");

        printf("[8b] sceAgcDriverIsSuspendPointInFlightDirect()...\n");
        bool in_flight = sceAgcDriverIsSuspendPointInFlightDirect(0u);
        printf("    in flight: %s\n", in_flight ? "yes" : "no");
    } else {
        printf("    skipped — no queue for suspend point\n");
    }

    /* --- Step 9: Destroy the queue (only if created) --- */
    if (queue_handle >= 0) {
        printf("[9] _sceAgcDriverDestroyUserSpecialQueue()...\n");
        err = _sceAgcDriverDestroyUserSpecialQueue();
        printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
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
        if (err == AGC_OK)
            printf("    Workload ended\n");
        else
            printf("    WARNING: EndWorkload failed\n");
    } else {
        printf("    WARNING: BeginWorkload failed\n");
    }

    /* --- Summary --- */
    printf("\n=== Summary ===\n");
    printf("  GPU credentials:   %s\n",
           cred_err == 0 ? "set (cr_sceAuthId)" : "FAILED");
    printf("  AGC init:          OK\n");
    printf("  Internal memory:   OK\n");
    printf("  Default states:    notified\n");
    printf("  PA debug version:  0x%08X\n", version);
    printf("  DCB submit (NOP):  %s\n",
           dcb_err == AGC_OK ? "OK" : "check step 5");
    printf("  Async graphics:    %s\n",
           "check step 6");
    printf("  Queue create/destroy: %s\n",
           queue_handle >= 0 ? "OK" : "FAILED");
    printf("  Suspend point:     check step 8\n");
    printf("=== Done ===\n");

    return 0;
}
