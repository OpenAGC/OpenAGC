#include "test.h"
#include "agc_driver_debug.h"
#include "agcdriver.h"

static void test_submit_packet_layout(void) {
    TEST_ASSERT_EQ(offsetof(AgcCommandBufferSubmit, command_address), 0, "submit address offset");
    TEST_ASSERT_EQ(offsetof(AgcCommandBufferSubmit, dword_count), 8, "submit dword count offset");
    TEST_ASSERT_EQ(sizeof(AgcCommandBufferSubmit), 0x10, "submit packet size");
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

static void test_submit_acb_validation(void) {
    uint32_t command[2] = {0};
    AgcCommandBufferSubmit packet = {
        .command_address = (uintptr_t)command,
        .dword_count = 2,
        .reserved = 0,
    };
    uint32_t owner = 0;

    TEST_ASSERT_EQ(sceAgcDriverSubmitAcb(0x2A, &packet), AGC_OK, "valid ACB submit succeeds");

    const AgcCommandBufferSubmit* last = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT_EQ(owner, 0x2A, "last ACB owner");
    TEST_ASSERT_EQ(last->command_address, packet.command_address, "last ACB address");
    TEST_ASSERT_EQ(last->dword_count, packet.dword_count, "last ACB dwords");
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

void test_suite_driver(void) {
    TEST_SUITE("Driver Submit RE");
    TEST_RUN(test_submit_packet_layout);
    TEST_RUN(test_submit_dcb_validation);
    TEST_RUN(test_submit_acb_validation);
    TEST_RUN(test_notify_default_states);
    TEST_RUN(test_suspend_point_and_check_status);
}
