/*
 * emu_agc_init.c — AGC init test for emulator builds
 *
 * Same as agc_init.c but with kernel exploit functions stubbed out.
 * Emulators don't need credential bypass — ioctls are HLE'd.
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

/* Stubs — emulators don't need kernel credential bypass */
static int set_gpu_credentials(void) {
    printf("    GPU cred: skipped (emulator mode)\n");
    return 0;
}

static const char *errstr(int32_t err) {
    switch (err) {
    case AGC_OK:                        return "OK";
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

static uint32_t cb_buffer[4096] __attribute__((aligned(64)));

int main(void) {
    int32_t err;
    int32_t dcb_err = -1;
    int32_t cred_err = -1;
    uint32_t version;
    SceAgcCb cb;
    AgcCommandBufferSubmit submit;

    printf("=== openagc AGC init + NOP submit test (emu) ===\n");

    /* --- Step 0: Set GPU process credentials --- */
    printf("[0] set_gpu_credentials()...\n");
    cred_err = set_gpu_credentials();

    /* --- Step 1: Initialize AGC context --- */
    printf("[1] sce_agc_initialize()...\n");

    /* Pre-init diagnostics: check /dev/gc open + CONTEXT_QUERY */
    int test_fd = open("/dev/gc", O_RDWR);
    if (test_fd < 0) {
        printf("    DIAG: /dev/gc open failed (errno=%d)\n", errno);
    } else {
        printf("    DIAG: /dev/gc opened OK (fd=%d)\n", test_fd);

        uint32_t ctx_query = 0;
        int ioret = ioctl(test_fd, 0xC004812Eu, &ctx_query);
        printf("    DIAG: CONTEXT_QUERY returned %d (cap=0x%X)\n",
               ioret, ctx_query);

        if (ioret == 0 && (ctx_query & 0xFFFF) == 0) {
            void *mmio = mmap((void*)0xfe0200000ULL, 0x4000,
                              PROT_READ | PROT_WRITE, MAP_SHARED, test_fd, 0);
            if (mmio == MAP_FAILED) {
                printf("    DIAG: mmap 0xfe0200000 failed (errno=%d)\n", errno);
            } else {
                printf("    DIAG: mmap 0xfe0200000 OK (ptr=%p)\n", mmio);
            }
        }

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

    /* --- Step 4: Verify the official FW 5.50 PA-debug permission stub --- */
    printf("[4] sceAgcDriverGetPaDebugInterfaceVersion()...\n");
    version = sceAgcDriverGetPaDebugInterfaceVersion();
    printf("    version: 0x%08X\n", version);
    if (version == AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT)
        printf("    FW 5.50 permission stub: PASS\n");
    else
        printf("    WARNING: unexpected PA debug result\n");

    /* --- Step 5: Build and submit a NOP command buffer --- */
    printf("[5] sceAgcDriverSubmitDcb() with NOP packet...\n");

    agcCbInit(&cb, cb_buffer, sizeof(cb_buffer));

    uint32_t *nop = sceAgcCbNop(&cb, 2);
    if (!nop) {
        printf("    ERROR: failed to build NOP packet\n");
        return 1;
    }
    nop[0] = 0;

    uint32_t used_dwords = agcCbUsedDwords(&cb);
    printf("    CB built: %u dwords at %p\n", used_dwords, (void *)cb_buffer);

    submit.command_address = (uintptr_t)cb_buffer;
    submit.dword_count = used_dwords;
    submit.reserved = 0;

    err = sceAgcDriverSubmitDcb(&submit);
    dcb_err = err;
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("    WARNING: DCB submit failed\n");
    } else {
        printf("    NOP packet submitted to GPU!\n");
    }

    /* --- Step 6: Setup async graphics --- */
    printf("[6] sceAgcDriverSetupAsyncGraphics(1)...\n");
    err = sceAgcDriverSetupAsyncGraphics(1);
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK)
        printf("    WARNING: async graphics setup failed\n");
    else
        printf("    Async graphics set up\n");

    /* --- Step 7: Create a user special queue --- */
    printf("[7] _sceAgcDriverCreateUserSpecialQueue()...\n");
    int32_t queue_handle = _sceAgcDriverCreateUserSpecialQueue();
    printf("    result: %d (handle)\n", queue_handle);
    if (queue_handle < 0)
        printf("    WARNING: queue creation failed (err=0x%08X)\n",
               (unsigned)queue_handle);
    else
        printf("    Compute queue created (handle=%d)\n", queue_handle);

    /* --- Step 8: Submit a suspend point --- */
    printf("[8] private primary suspend carrier...\n");
    if (queue_handle >= 0) {
        err = sce_agc_internal_suspend_point_submit_primary(
            0xaf1e80b7u, 0x8b4cdd90u, 0x99f68d6cu, 0u);
        printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
        if (err != AGC_OK)
            printf("    WARNING: suspend point submit failed\n");
        else
            printf("    Suspend point submitted\n");
    } else {
        printf("    skipped — no queue for suspend point\n");
    }

    /* --- Step 9: Destroy the queue --- */
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
    }

    /* --- Summary --- */
    printf("\n=== Summary ===\n");
    printf("  GPU credentials:   %s\n",
           cred_err == 0 ? "skipped (emu)" : "FAILED");
    printf("  AGC init:          OK\n");
    printf("  Internal memory:   OK\n");
    printf("  Default states:    notified\n");
    printf("  PA debug version:  0x%08X\n", version);
    printf("  DCB submit (NOP):  %s\n",
           dcb_err == AGC_OK ? "OK" : "check step 5");
    printf("  Queue create/destroy: %s\n",
           queue_handle >= 0 ? "OK" : "FAILED");
    printf("=== Done ===\n");

    return 0;
}
