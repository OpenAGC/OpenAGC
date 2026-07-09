#include "test.h"
#include "agcdriver.h"
#include "agc_pm4.h"

static void test_dcb_init_null(void) {
    int32_t r = sceAgcVshDcbInitializeDefaultHardwareState_pre0090(NULL, 100);
    TEST_ASSERT(r < 0, "NULL dcb should fail");
}

static void test_dcb_init_ok(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbInitializeDefaultHardwareState_pre0090(buf, 64);
    TEST_ASSERT(r > 0, "Init should return positive dword count");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 2, "Init NOP should be two dwords");
}

static void test_dcb_clear_state(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbClearState(buf, 64);
    TEST_ASSERT_EQ(r, 2, "ClearState should write 2 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_CLEAR_STATE, "ClearState opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 2, "ClearState length");
    TEST_ASSERT_EQ(buf[1], 0x0u, "ClearState flags");
}

static void test_dcb_mem_semaphore(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbMemSemaphore(buf, 64);
    TEST_ASSERT_EQ(r, 4, "MemSemaphore should write 4 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_MEM_SEMAPHORE, "MemSemaphore opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 4, "MemSemaphore length");
}

static void test_dcb_flip(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbSetFlip(buf, 64, 0, 0);
    TEST_ASSERT_EQ(r, 6, "SetFlip should write SharpEmu-sized packet");
    TEST_ASSERT_EQ(agcPm4Type(buf[0]), AGC_PM4_TYPE3, "SetFlip should emit PM4 type 3");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_NOP, "SetFlip uses NOP wrapper");
    TEST_ASSERT_EQ(agcPm4Subcommand(buf[0]), AGC_PM4_SUB_FLIP, "SetFlip subcommand");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 6, "SetFlip packet length");
}

static void test_dcb_set_workload_complete(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbSetWorkloadComplete(buf, 64, 0xAABBCCDD11223344u);
    TEST_ASSERT_EQ(r, 8, "SetWorkloadComplete should write 8 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_WORKLOAD, "SetWorkloadComplete opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 8, "SetWorkloadComplete length");
    TEST_ASSERT_EQ(buf[1], 0x11223344u, "SetWorkloadComplete workload lo");
}

static void test_dcb_set_workload_stream_inactive(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbSetWorkloadStreamInactive(buf, 64, 0xAABBCCDD11223344u);
    TEST_ASSERT_EQ(r, 3, "SetWorkloadStreamInactive should write 3 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_UCONFIG_REG, "SetWorkloadStreamInactive opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 3, "SetWorkloadStreamInactive length");
    TEST_ASSERT_EQ(buf[1], 0x00000342u, "SetWorkloadStreamInactive control");
    TEST_ASSERT_EQ(buf[2], 0x11223344u, "SetWorkloadStreamInactive workload lo");
}

static void test_dcb_set_workloads_active(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbSetWorkloadsActive(buf, 64, 0x12345678u);
    TEST_ASSERT_EQ(r, 8, "SetWorkloadsActive should write 8 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_WORKLOAD, "SetWorkloadsActive opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 8, "SetWorkloadsActive length");
    TEST_ASSERT_EQ(buf[1], 0x12345678u, "SetWorkloadsActive flags");
}

static void test_dcb_atomic_gds(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbAtomicGds(buf, 64, 0x7u, 0x1234u, 0xAABBCCDDu, 0x1u);
    TEST_ASSERT_EQ(r, 10, "AtomicGds should write 10 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_ATOMIC_GDS, "AtomicGds opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 10, "AtomicGds length");
    TEST_ASSERT_EQ(buf[1], 0x12340007u, "AtomicGds control");
    TEST_ASSERT_EQ(buf[2], 0xAABBCCDDu, "AtomicGds data");
    TEST_ASSERT_EQ(buf[3], 0x1u, "AtomicGds src");
    TEST_ASSERT_EQ(buf[9], 0x0u, "AtomicGds reserved 9");
}

static void test_dcb_context_state_op(void) {
    uint32_t buf[64];
    uint32_t data[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    int32_t r = sceAgcVshDcbContextStateOp(buf, 64, 0, 0, 0x200u, 4, data);
    TEST_ASSERT_EQ(r, 6, "ContextStateOp should write 6 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_CONTEXT_REG, "ContextStateOp opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 6, "ContextStateOp length");
    TEST_ASSERT_EQ(buf[1], 0x200u, "ContextStateOp reg_offset");
    TEST_ASSERT_EQ(buf[2], 0x11111111u, "ContextStateOp data[0]");
    TEST_ASSERT_EQ(buf[3], 0x22222222u, "ContextStateOp data[1]");
    TEST_ASSERT_EQ(buf[4], 0x33333333u, "ContextStateOp data[2]");
    TEST_ASSERT_EQ(buf[5], 0x44444444u, "ContextStateOp data[3]");
}

static void test_dcb_reset_queue(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbResetQueue(buf, 64, 0x5u);
    TEST_ASSERT_EQ(r, 3, "ResetQueue should write 3 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_UCONFIG_REG, "ResetQueue opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 3, "ResetQueue length");
    TEST_ASSERT_EQ(buf[1], 0x00000342u, "ResetQueue control");
    TEST_ASSERT_EQ(buf[2], 0x5u, "ResetQueue queue_id");
}

static void test_dcb_set_preemption(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbSetPreemption(buf, 64, 0x2u);
    TEST_ASSERT_EQ(r, AGC_ERROR_INVALID_STATE, "SetPreemption is not allowed from agc vsh");
}

static void test_dcb_wait_until_safe(void) {
    uint32_t buf[64];
    int32_t r = sceAgcVshDcbWaitUntilSafeForRendering(buf, 64);
    TEST_ASSERT_EQ(r, 7, "WaitUntilSafeForRendering should write 7 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_NOP, "WaitUntilSafeForRendering opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 7, "WaitUntilSafeForRendering length");
    TEST_ASSERT_EQ(agcPm4Subcommand(buf[0]), AGC_PM4_SUB_WAIT_FLIP_DONE, "WaitUntilSafeForRendering subcommand");
}

void test_suite_dcb(void) {
    TEST_SUITE("DCB Commands");
    TEST_RUN(test_dcb_init_null);
    TEST_RUN(test_dcb_init_ok);
    TEST_RUN(test_dcb_clear_state);
    TEST_RUN(test_dcb_mem_semaphore);
    TEST_RUN(test_dcb_flip);
    TEST_RUN(test_dcb_set_workload_complete);
    TEST_RUN(test_dcb_set_workload_stream_inactive);
    TEST_RUN(test_dcb_set_workloads_active);
    TEST_RUN(test_dcb_atomic_gds);
    TEST_RUN(test_dcb_context_state_op);
    TEST_RUN(test_dcb_reset_queue);
    TEST_RUN(test_dcb_set_preemption);
    TEST_RUN(test_dcb_wait_until_safe);
}
