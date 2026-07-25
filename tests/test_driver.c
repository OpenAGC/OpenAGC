#include "test.h"
#include "agc_driver_debug.h"
#include "agcdriver.h"
#include "agc_types.h"
#include "agc_context.h"
#include "driver_ops.h"

/* Debug helpers for queue state — defined in driver_generic.c, not public API.
 * Declared here as extern so tests can inspect internal queue tracking without
 * polluting the public header surface. */
extern bool agcDriverDebugIsQueueInUse(uint32_t index);
extern uint32_t agcDriverDebugGetQueueCount(void);
extern bool agcDriverDebugIsAsyncSetup(void);

/* Must match AGC_GENERIC_MAX_QUEUES in driver_generic.c. */
#define TEST_MAX_QUEUES 32

static uint32_t g_fake_submit_count;

static int32_t PS5_SYSV_ABI fake_submit_dcb(
    const AgcCommandBufferSubmit *packet)
{
    if (!packet)
        return AGC_ERROR_INVALID_ARGUMENT;
    g_fake_submit_count++;
    return 1234;
}

static void test_internal_operations_dispatch(void) {
    const AgcDriverOps fake_ops = {
        .name = "test-fake",
        .submit_dcb = fake_submit_dcb,
    };
    uint32_t command = 0;
    AgcCommandBufferSubmit packet = {
        .command_address = (uintptr_t)&command,
        .dword_count = 1,
    };

    TEST_ASSERT(strcmp(agcDriverGetOps()->name, "generic") == 0,
        "generic operations table selected by default");
    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(NULL),
        AGC_ERROR_INVALID_ARGUMENT, "reject NULL operations table");
    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&fake_ops), AGC_OK,
        "install fake operations table");
    TEST_ASSERT(agcDriverGetOps() == &fake_ops,
        "active operations table is the fake table");
    TEST_ASSERT_EQ(sceAgcDriverSubmitDcb(&packet), 1234,
        "public submit dispatches through fake callback");
    TEST_ASSERT_EQ(g_fake_submit_count, 1u,
        "fake submit callback invoked exactly once");
    TEST_ASSERT_EQ(sce_agc_initialize(), AGC_ERROR_NOT_SUPPORTED,
        "missing callback fails safely");

    agcDriverResetOpsForTesting();
    TEST_ASSERT(strcmp(agcDriverGetOps()->name, "generic") == 0,
        "reset restores generic operations table");
}

static void test_submit_packet_layout(void) {
    TEST_ASSERT_EQ(offsetof(AgcCommandBufferSubmit, command_address), 0, "submit address offset");
    TEST_ASSERT_EQ(offsetof(AgcCommandBufferSubmit, dword_count), 8, "submit dword count offset");
    TEST_ASSERT_EQ(sizeof(AgcCommandBufferSubmit), 0x10, "submit packet size");
}

static void test_pa_debug_permission_stub(void) {
    TEST_ASSERT_EQ(sceAgcDriverGetPaDebugInterfaceVersion(),
        AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT,
        "FW 5.50 PA-debug interface is a permission stub");
}

static void test_submit_dcb_validation(void) {
    AgcCommandBufferSubmit bad = {0};
    TEST_ASSERT(sceAgcDriverSubmitDcb(&bad) < 0, "empty DCB submit should fail");

    uint32_t command[2] = {0};
    AgcCommandBufferSubmit packet = {
        .command_address = (uintptr_t)command,
        .dword_count = 2,
        .reserved = 0,
    };

    TEST_ASSERT_EQ(sce_agc_initialize(), AGC_OK, "initialize generic driver");
    TEST_ASSERT_EQ(sceAgcDriverSubmitDcb(&packet), AGC_OK, "valid DCB submit succeeds");

    const AgcCommandBufferSubmit* last = agcDriverDebugLastDcbSubmit();
    TEST_ASSERT_EQ(last->command_address, packet.command_address, "last DCB address");
    TEST_ASSERT_EQ(last->dword_count, packet.dword_count, "last DCB dwords");
}

static void test_multi_dcb_submission(void) {
    uint32_t dcb0[2] = {0};
    uint32_t dcb1[3] = {0};
    void *dcbs[2] = {dcb0, dcb1};
    uint32_t sizes[2] = {2, 3};

    TEST_ASSERT_EQ(sce_agc_initialize(), AGC_OK,
        "initialize generic driver for multi DCB");
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiDcbs(dcbs, sizes, 2), AGC_OK,
        "SubmitMultiDcbs accepts a descriptor array");
    TEST_ASSERT_EQ(
        sceAgcDriverSubmitMultiCommandBuffers(3, dcbs, sizes, 2), AGC_OK,
        "SubmitMultiCommandBuffers accepts a descriptor array");
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiDcbs(NULL, sizes, 2),
        AGC_ERROR_INVALID_ARGUMENT, "SubmitMultiDcbs rejects NULL addresses");

    sizes[1] = 0;
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiDcbs(dcbs, sizes, 2),
        AGC_ERROR_CB_INVALID_SIZE, "SubmitMultiDcbs rejects empty DCB");
    sizes[1] = UINT32_MAX;
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiDcbs(dcbs, sizes, 2),
        AGC_ERROR_CB_INVALID_SIZE, "SubmitMultiDcbs rejects byte overflow");
}

static void test_submit_acb_validation(void) {
    sce_agc_initialize();
    uint32_t command[2] = {0};
    AgcCommandBufferSubmit packet = {
        .command_address = (uintptr_t)command,
        .dword_count = 2,
        .reserved = 0,
    };
    uint32_t owner = 0;

    /* ACB submit now requires a valid in-use queue. Create one first. */
    int32_t handle = _sceAgcDriverCreateUserSpecialQueue();
    TEST_ASSERT(handle >= 0, "create user special queue for ACB submit");

    TEST_ASSERT_EQ(sceAgcDriverSubmitAcb((uint32_t)handle, &packet), AGC_OK, "valid ACB submit succeeds");

    const AgcCommandBufferSubmit* last = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT_EQ(owner, (uint32_t)handle, "last ACB owner");
    TEST_ASSERT_EQ(last->command_address, packet.command_address, "last ACB address");
    TEST_ASSERT_EQ(last->dword_count, packet.dword_count, "last ACB dwords");

    _sceAgcDriverDestroyUserSpecialQueue();
}

static void test_queue_create_destroy(void) {
    sce_agc_initialize();
    int32_t handle = _sceAgcDriverCreateUserSpecialQueue();
    TEST_ASSERT(handle >= 0, "create queue returns valid handle");
    TEST_ASSERT(agcDriverDebugIsQueueInUse((uint32_t)handle), "queue is marked in use");
    TEST_ASSERT_EQ(agcDriverDebugGetQueueCount(), 1u, "one queue in use after create");

    int32_t ret = _sceAgcDriverDestroyUserSpecialQueue();
    TEST_ASSERT_EQ(ret, AGC_OK, "destroy queue succeeds");
    TEST_ASSERT(!agcDriverDebugIsQueueInUse((uint32_t)handle), "queue is freed after destroy");
    TEST_ASSERT_EQ(agcDriverDebugGetQueueCount(), 0u, "zero queues in use after destroy");
}

static void test_queue_destroy_no_queues(void) {
    sce_agc_initialize();  /* resets queue state */
    TEST_ASSERT_EQ(agcDriverDebugGetQueueCount(), 0u, "no queues in use before destroy test");

    int32_t ret = _sceAgcDriverDestroyUserSpecialQueue();
    TEST_ASSERT_EQ(ret, AGC_ERROR_CB_INVALID_QUEUE, "destroy with no queues returns error");
}

static void test_queue_create_max(void) {
    sce_agc_initialize();
    int32_t handles[TEST_MAX_QUEUES];
    for (int i = 0; i < TEST_MAX_QUEUES; i++) {
        handles[i] = _sceAgcDriverCreateUserSpecialQueue();
        TEST_ASSERT(handles[i] >= 0, "create queue within max");
    }
    TEST_ASSERT_EQ(agcDriverDebugGetQueueCount(), (uint32_t)TEST_MAX_QUEUES, "all queues in use");

    int32_t overflow = _sceAgcDriverCreateUserSpecialQueue();
    TEST_ASSERT_EQ(overflow, AGC_ERROR_CB_INVALID_QUEUE, "33rd queue fails with invalid queue error");

    /* Destroy all queues — destroy removes the first in-use queue each call. */
    for (int i = 0; i < TEST_MAX_QUEUES; i++) {
        int32_t ret = _sceAgcDriverDestroyUserSpecialQueue();
        TEST_ASSERT_EQ(ret, AGC_OK, "destroy queue during cleanup");
    }
    TEST_ASSERT_EQ(agcDriverDebugGetQueueCount(), 0u, "all queues freed after cleanup");
}

static void test_acb_submit_invalid_queue(void) {
    sce_agc_initialize();
    uint32_t command[2] = {0};
    AgcCommandBufferSubmit packet = {
        .command_address = (uintptr_t)command,
        .dword_count = 2,
        .reserved = 0,
    };

    /* Out-of-range handle (>= max queues). */
    int32_t ret = sceAgcDriverSubmitAcb(0x2A, &packet);
    TEST_ASSERT_EQ(ret, AGC_ERROR_CB_INVALID_QUEUE, "ACB submit with out-of-range handle fails");

    /* In-range but non-existent queue (slot 5 is free after init). */
    ret = sceAgcDriverSubmitAcb(5, &packet);
    TEST_ASSERT_EQ(ret, AGC_ERROR_CB_INVALID_QUEUE, "ACB submit to non-existent queue fails");
}

static void test_acb_submit_to_created_queue(void) {
    sce_agc_initialize();
    uint32_t command[2] = {0};
    AgcCommandBufferSubmit packet = {
        .command_address = (uintptr_t)command,
        .dword_count = 2,
        .reserved = 0,
    };

    /* Full flow: create queue -> submit ACB -> verify -> destroy -> submit fails. */
    int32_t handle = _sceAgcDriverCreateUserSpecialQueue();
    TEST_ASSERT(handle >= 0, "create queue for ACB flow");

    int32_t ret = sceAgcDriverSubmitAcb((uint32_t)handle, &packet);
    TEST_ASSERT_EQ(ret, AGC_OK, "ACB submit to created queue succeeds");

    ret = _sceAgcDriverDestroyUserSpecialQueue();
    TEST_ASSERT_EQ(ret, AGC_OK, "destroy queue after ACB submit");

    /* Submit to the destroyed queue should now fail. */
    ret = sceAgcDriverSubmitAcb((uint32_t)handle, &packet);
    TEST_ASSERT_EQ(ret, AGC_ERROR_CB_INVALID_QUEUE, "ACB submit to destroyed queue fails");
}

static void test_async_graphics_setup(void) {
    sce_agc_initialize();
    int32_t ret = sceAgcDriverSetupAsyncGraphics(0xC);
    TEST_ASSERT_EQ(ret, AGC_OK, "SetupAsyncGraphics returns OK");
    TEST_ASSERT(agcDriverDebugIsAsyncSetup(), "async setup flag is set after call");
}

static void test_notify_default_states(void) {
    TEST_ASSERT_EQ(sceAgcDriverNotifyDefaultStates(0), AGC_OK, "NotifyDefaultStates returns OK on generic backend");
}

static void test_suspend_point_and_check_status(void) {
    /*
     * On the generic backend IsSuspendPointInFlightDirect always returns false,
     * so SuspendPointAndCheckStatus returns AGC_OK (not in flight / done).
     */
    int32_t r = sceAgcSuspendPointAndCheckStatus(0x12345678u);
    TEST_ASSERT_EQ(r, AGC_OK, "SuspendPointAndCheckStatus returns OK on generic backend");
}

static void test_submit_eop_flip_generic_stub(void) {
    /*
     * On the generic backend, sceAgcDriverSubmitEopFlip is a stub that
     * returns AGC_ERROR_NOT_SUPPORTED — the EOP flip path requires the
     * prospero /dev/gc backend and sceVideoOutSubmitEopFlip.
     */
    int32_t r = sceAgcDriverSubmitEopFlip((void *)1, 0, 0, NULL);
    TEST_ASSERT_EQ(r, AGC_ERROR_NOT_SUPPORTED, "SubmitEopFlip returns NOT_SUPPORTED on generic backend");
}

static void test_workload_begin_end(void) {
    sce_agc_initialize();
    /* Valid workload ID */
    int32_t r = sceAgcDriverSetWorkloadsActive(42);
    TEST_ASSERT_EQ(r, AGC_OK, "BeginWorkload with valid ID returns OK");

    r = sceAgcDriverSetWorkloadComplete(42);
    TEST_ASSERT_EQ(r, AGC_OK, "EndWorkload with matching ID returns OK");
}

static void test_workload_begin_invalid_id(void) {
    sce_agc_initialize();
    /* workload_id == 0 is invalid */
    int32_t r = sceAgcDriverSetWorkloadsActive(0);
    TEST_ASSERT_EQ(r, AGC_ERROR_INVALID_ARGUMENT, "BeginWorkload with ID=0 returns error");
}

static void test_workload_end_invalid_id(void) {
    sce_agc_initialize();
    /* workload_id == 0 is invalid */
    int32_t r = sceAgcDriverSetWorkloadComplete(0);
    TEST_ASSERT_EQ(r, AGC_ERROR_INVALID_ARGUMENT, "EndWorkload with ID=0 returns error");
}

static void test_workload_end_without_begin(void) {
    sce_agc_initialize();
    /* EndWorkload without a prior BeginWorkload */
    int32_t r = sceAgcDriverSetWorkloadComplete(99);
    TEST_ASSERT_EQ(r, AGC_ERROR_INVALID_STATE, "EndWorkload without BeginWorkload returns error");
}

static void test_workload_end_mismatched_id(void) {
    sce_agc_initialize();
    /* Begin with one ID, end with a different ID */
    int32_t r = sceAgcDriverSetWorkloadsActive(10);
    TEST_ASSERT_EQ(r, AGC_OK, "BeginWorkload 10 returns OK");

    r = sceAgcDriverSetWorkloadComplete(20);
    TEST_ASSERT_EQ(r, AGC_ERROR_INVALID_STATE, "EndWorkload with mismatched ID returns error");

    /* Clean up with the correct ID */
    r = sceAgcDriverSetWorkloadComplete(10);
    TEST_ASSERT_EQ(r, AGC_OK, "EndWorkload with matching ID after mismatch returns OK");
}

static void test_workload_begin_already_active(void) {
    sce_agc_initialize();
    /* BeginWorkload should reject if a workload is already active */
    int32_t r = sceAgcDriverSetWorkloadsActive(42);
    TEST_ASSERT_EQ(r, AGC_OK, "BeginWorkload 42 returns OK");

    r = sceAgcDriverSetWorkloadsActive(99);
    TEST_ASSERT_EQ(r, AGC_ERROR_INVALID_STATE, "BeginWorkload while already active returns error");

    /* Original workload is still active — EndWorkload with original ID works */
    r = sceAgcDriverSetWorkloadComplete(42);
    TEST_ASSERT_EQ(r, AGC_OK, "EndWorkload 42 after rejected double-begin returns OK");
}

/* sceAgcGetDefaultState should return a populated context state (not all zeros) */
static void test_default_state_populated(void) {
    AgcContextState state;
    int32_t r = sceAgcGetDefaultState(&state);
    TEST_ASSERT_EQ(r, AGC_OK, "GetDefaultState returns OK");

    /* Verify it's not all zeros (register defaults should populate some fields) */
    uint32_t nonzero_count = 0;
    for (uint32_t i = 0; i < 512; i++) {
        if (state.data[i] != 0)
            nonzero_count++;
    }
    TEST_ASSERT(nonzero_count > 0, "GetDefaultState has non-zero register values");

    /* Verify consistency: second call returns same data */
    AgcContextState state2;
    r = sceAgcGetDefaultState(&state2);
    TEST_ASSERT_EQ(r, AGC_OK, "GetDefaultState second call returns OK");
    TEST_ASSERT(memcmp(&state, &state2, sizeof(state)) == 0, "GetDefaultState consistent");

    /* GetDefaultCxStateFlat should also work */
    uint32_t flat[64];
    r = sceAgcGetDefaultCxStateFlat(flat, sizeof(flat));
    TEST_ASSERT_EQ(r, AGC_OK, "GetDefaultCxStateFlat returns OK");

    /* NULL check */
    r = sceAgcGetDefaultState(NULL);
    TEST_ASSERT(r != AGC_OK, "GetDefaultState rejects NULL");
}

void test_suite_driver(void) {
    TEST_SUITE("Driver Submit RE");
    TEST_RUN(test_internal_operations_dispatch);
    TEST_RUN(test_submit_packet_layout);
    TEST_RUN(test_pa_debug_permission_stub);
    TEST_RUN(test_submit_dcb_validation);
    TEST_RUN(test_multi_dcb_submission);
    TEST_RUN(test_submit_acb_validation);
    TEST_RUN(test_queue_create_destroy);
    TEST_RUN(test_queue_destroy_no_queues);
    TEST_RUN(test_queue_create_max);
    TEST_RUN(test_acb_submit_invalid_queue);
    TEST_RUN(test_acb_submit_to_created_queue);
    TEST_RUN(test_async_graphics_setup);
    TEST_RUN(test_notify_default_states);
    TEST_RUN(test_suspend_point_and_check_status);
    TEST_RUN(test_submit_eop_flip_generic_stub);
    TEST_RUN(test_workload_begin_end);
    TEST_RUN(test_workload_begin_invalid_id);
    TEST_RUN(test_workload_end_invalid_id);
    TEST_RUN(test_workload_end_without_begin);
    TEST_RUN(test_workload_end_mismatched_id);
    TEST_RUN(test_workload_begin_already_active);
    TEST_RUN(test_default_state_populated);
}
