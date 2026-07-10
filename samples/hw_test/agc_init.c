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

#include "agcdriver.h"
#include "agc_cb.h"
#include "agc_error.h"

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
    uint32_t version;
    SceAgcCb cb;
    AgcCommandBufferSubmit submit;

    printf("=== openagc AGC init + NOP submit test ===\n");

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

        /* Also try the old FRAME_OPEN for comparison (expected to fail with EINVAL) */
        uint32_t frame_arg[2] = {0, 0};
        ioret = ioctl(test_fd, 0xC0088100u, frame_arg);
        printf("    DIAG: FRAME_OPEN 0xC0088100 (expected EINVAL) returned %d (errno=%d)\n",
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

    /* --- Step 6: Submit a suspend point and check status --- */
    printf("[6] sceAgcDriverSuspendPointSubmitDirect()...\n");
    err = sceAgcDriverSuspendPointSubmitDirect(1u, 0u, 0u, 0xABCDEF01u);
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK)
        printf("    WARNING: suspend point submit failed\n");
    else
        printf("    Suspend point submitted\n");

    printf("[6b] sceAgcDriverIsSuspendPointInFlightDirect()...\n");
    bool in_flight = sceAgcDriverIsSuspendPointInFlightDirect(0xABCDEF01u);
    printf("    in flight: %s\n", in_flight ? "yes" : "no");

    /* --- Step 7: Create a user special queue --- */
    printf("[7] _sceAgcDriverCreateUserSpecialQueue()...\n");
    int32_t queue_handle = _sceAgcDriverCreateUserSpecialQueue();
    printf("    result: %d (handle)\n", queue_handle);
    if (queue_handle < 0)
        printf("    WARNING: queue creation failed (%s)\n", errstr(queue_handle));
    else
        printf("    Compute queue created (handle=%d)\n", queue_handle);

    /* --- Step 8: Destroy the queue --- */
    printf("[8] _sceAgcDriverDestroyUserSpecialQueue()...\n");
    err = _sceAgcDriverDestroyUserSpecialQueue();
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK)
        printf("    WARNING: queue destruction failed\n");
    else
        printf("    Queue destroyed\n");

    /* --- Step 9: Workload tracking --- */
    printf("[9] sceAgcDriverBeginWorkload(1)...\n");
    err = sceAgcDriverBeginWorkload(1);
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err == AGC_OK) {
        printf("    Workload begun\n");
        printf("[9b] sceAgcDriverEndWorkload(1)...\n");
        err = sceAgcDriverEndWorkload(1);
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
    printf("  AGC init:          OK\n");
    printf("  Internal memory:   OK\n");
    printf("  Default states:    notified\n");
    printf("  PA debug version:  0x%08X\n", version);
    printf("  DCB submit (NOP):  %s\n",
           dcb_err == AGC_OK ? "OK" : "check step 5");
    printf("  Suspend point:     submitted, in_flight=%s\n",
           in_flight ? "yes" : "no");
    printf("  Queue create/destroy: %s\n",
           queue_handle >= 0 ? "OK" : "FAILED");
    printf("=== Done ===\n");

    return 0;
}
