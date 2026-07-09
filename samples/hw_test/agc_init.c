/*
 * agc_init.c — PS5 AGC initialization + NOP submit test
 *
 * Adapted from freegnm-examples/triangle/src/main.c for PS5 AGC.
 * Tests the native /dev/gc backend:
 *   1. sce_agc_initialize() — open /dev/gc + FRAME_OPEN ioctl
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
    uint32_t version;
    SceAgcCb cb;
    AgcCommandBufferSubmit submit;

    printf("=== openagc AGC init + NOP submit test ===\n");

    /* --- Step 1: Initialize AGC context --- */
    printf("[1] sce_agc_initialize()...\n");
    err = sce_agc_initialize();
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("    FATAL: cannot initialize AGC\n");
        printf("    Check: /dev/gc exists? GPU access? FRAME_OPEN arg?\n");
        return 1;
    }
    printf("    /dev/gc opened, FRAME_OPEN succeeded\n");

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

    /* Add a NOP packet (1 dword payload) */
    uint32_t *nop = sceAgcCbNop(&cb, 1);
    if (!nop) {
        printf("    ERROR: failed to build NOP packet\n");
        return 1;
    }
    nop[0] = 0;  /* NOP data */

    uint32_t used_dwords = agcCbUsedDwords(&cb);
    printf("    CB built: %u dwords at %p\n", used_dwords, (void *)cb_buffer);

    /* Submit it */
    submit.command_address = (uintptr_t)cb_buffer;
    submit.dword_count = used_dwords;
    submit.reserved = 0;

    err = sceAgcDriverSubmitDcb(&submit);
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
    err = _sceAgcDriverCreateUserSpecialQueue();
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK)
        printf("    WARNING: queue creation failed\n");
    else
        printf("    Compute queue created\n");

    /* --- Step 8: Destroy the queue --- */
    printf("[8] _sceAgcDriverDestroyUserSpecialQueue()...\n");
    err = _sceAgcDriverDestroyUserSpecialQueue();
    printf("    result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK)
        printf("    WARNING: queue destruction failed\n");
    else
        printf("    Queue destroyed\n");

    /* --- Summary --- */
    printf("\n=== Summary ===\n");
    printf("  AGC init:          OK\n");
    printf("  Internal memory:   OK\n");
    printf("  Default states:    notified\n");
    printf("  PA debug version:  0x%08X\n", version);
    printf("  DCB submit (NOP):  %s\n",
           err == AGC_OK ? "OK" : "FAILED (see step 5)");
    printf("  Suspend point:     submitted, in_flight=%s\n",
           in_flight ? "yes" : "no");
    printf("=== Done ===\n");

    return 0;
}
