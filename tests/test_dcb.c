#include "test.h"
#include "agcdriver.h"
#include "agc_pm4.h"
#include "agc_cb.h"

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

static void test_dcb_eop_flip_null(void) {
    int32_t r = sceAgcDcbSetEopFlip(NULL, 0, 0, 0x100000020ULL, 0x1234);
    TEST_ASSERT(r < 0, "NULL dcb should fail for EopFlip");
}

static void test_dcb_eop_flip_packet(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    int32_t r = sceAgcDcbSetEopFlip(&cb, 0x05, 0x03, 0x100000020ULL, 0xDEADBEEF);
    TEST_ASSERT_EQ(r, 8, "EopFlip should write 8 dwords");
    TEST_ASSERT_EQ(agcPm4Type(buffer[0]), AGC_PM4_TYPE3, "EopFlip should emit PM4 type 3");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[0]), AGC_PM4_OP_RELEASE_MEM, "EopFlip opcode 0x49");
    TEST_ASSERT_EQ(agcPm4Length(buffer[0]), 8, "EopFlip packet length 8");
    TEST_ASSERT_EQ(buffer[0], 0xC0064900u, "EopFlip header matches SPRX 0xc0064900");
    TEST_ASSERT_EQ(buffer[1], 0x0305u, "EopFlip event_control (type=5, index=3)");
    TEST_ASSERT_EQ(buffer[2], 0x20u, "EopFlip dst_addr lo");
    TEST_ASSERT_EQ(buffer[3], 0x1u, "EopFlip dst_addr hi");
    TEST_ASSERT_EQ(buffer[4], 0xDEADBEEFu, "EopFlip data");
    TEST_ASSERT_EQ(buffer[5], 0x0u, "EopFlip reserved 5");
    TEST_ASSERT_EQ(buffer[6], 0x0u, "EopFlip reserved 6");
    TEST_ASSERT_EQ(buffer[7], 0x0u, "EopFlip reserved 7");
}

static void test_dcb_eop_flip_cursor_advance(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0, "cursor starts at 0");
    int32_t r = sceAgcDcbSetEopFlip(&cb, 0, 0, 0, 0);
    TEST_ASSERT_EQ(r, 8, "EopFlip returns 8 dwords");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 8, "EopFlip advances cursor by 8");
    TEST_ASSERT_EQ(agcCbRemainingDwords(&cb), 8, "EopFlip leaves 8 remaining");
}

static void test_dcb_eop_flip_overflow(void) {
    uint32_t buffer[7];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    int32_t r = sceAgcDcbSetEopFlip(&cb, 0, 0, 0, 0);
    TEST_ASSERT(r < 0, "EopFlip should fail on insufficient buffer");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0, "EopFlip overflow does not advance cursor");
}

/* === Game compat tests (from Joe & Mac analysis) === */

static void test_game_compat_dcb_acquire_mem(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbAcquireMem(&cb, 1, 0x1234, 0x5678, 0xDEAD0000ULL);
    TEST_ASSERT(cmd != NULL, "AcquireMem should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_ACQUIRE_MEM, "AcquireMem opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8, "AcquireMem length");
    TEST_ASSERT_EQ(cmd[1], 0x1234u, "AcquireMem coher_cntl");
    TEST_ASSERT_EQ(cmd[2], 0x5678u, "AcquireMem coher_size");
    TEST_ASSERT_EQ(cmd[3], 0xDEAD0000u, "AcquireMem coher_base lo");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 8, "AcquireMem advances cursor");
}

static void test_game_compat_dcb_copy_data(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbCopyData(&cb, 1, 2, 0x1000, 0x2000, 256);
    TEST_ASSERT(cmd != NULL, "CopyData should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_COPY_DATA, "CopyData opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "CopyData length");
    TEST_ASSERT_EQ(cmd[2], 0x1000u, "CopyData src lo");
    TEST_ASSERT_EQ(cmd[4], 0x2000u, "CopyData dst lo");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 6, "CopyData advances cursor");
}

static void test_game_compat_dcb_jump(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbJump(&cb, 0x12345678ULL);
    TEST_ASSERT(cmd != NULL, "Jump should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INDIRECT_BUFFER_CNST, "Jump opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "Jump length");
    TEST_ASSERT_EQ(cmd[1], 0x12345678u, "Jump target lo");
}

static void test_game_compat_dcb_reset_queue(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbResetQueue(&cb, 0x42);
    TEST_ASSERT(cmd != NULL, "ResetQueue should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "ResetQueue length");
    TEST_ASSERT_EQ(cmd[1], 0x42u, "ResetQueue queue_id");
}

static void test_game_compat_dcb_set_index_count(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbSetIndexCount(&cb, 1024);
    TEST_ASSERT(cmd != NULL, "SetIndexCount should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INDEX_BUFFER_SIZE, "SetIndexCount opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "SetIndexCount length");
    TEST_ASSERT_EQ(cmd[1], 1024u, "SetIndexCount value");
}

static void test_game_compat_dcb_set_index_size(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbSetIndexSize(&cb, 1, 0);
    TEST_ASSERT(cmd != NULL, "SetIndexSize should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INDEX_TYPE, "SetIndexSize opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "SetIndexSize length");
    TEST_ASSERT_EQ(cmd[1], 1u, "SetIndexSize index_type");
}

static void test_game_compat_dcb_set_num_instances(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbSetNumInstances(&cb, 4);
    TEST_ASSERT(cmd != NULL, "SetNumInstances should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NUM_INSTANCES, "SetNumInstances opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2, "SetNumInstances length");
    TEST_ASSERT_EQ(cmd[1], 4u, "SetNumInstances value");
}

static void test_game_compat_dcb_draw_index(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbDrawIndex(&cb, 100, 0x40000, 0x1234);
    TEST_ASSERT(cmd != NULL, "DrawIndex should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDEX_2, "DrawIndex opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "DrawIndex length");
    TEST_ASSERT_EQ(cmd[1], 100u, "DrawIndex index_count");
    TEST_ASSERT_EQ(cmd[2], 0x40000u, "DrawIndex base lo");
}

static void test_game_compat_dcb_stall(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbStallCommandBufferParser(&cb);
    TEST_ASSERT(cmd != NULL, "Stall should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2, "Stall length");
}

static void test_game_compat_cb_set_sh_reg_range(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t values[] = {0x1111, 0x2222, 0x3333};
    uint32_t *cmd = sceAgcCbSetShRegisterRangeDirect(&cb, 0x10, values, 3);
    TEST_ASSERT(cmd != NULL, "SetShRegRange should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_SH_REG, "SetShRegRange opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5, "SetShRegRange length (2+3)");
    TEST_ASSERT_EQ(cmd[1], 0x10u, "SetShRegRange offset");
    TEST_ASSERT_EQ(cmd[2], 0x1111u, "SetShRegRange val0");
    TEST_ASSERT_EQ(cmd[4], 0x3333u, "SetShRegRange val2");
}

static void test_game_compat_set_nop(void) {
    uint32_t cmd[4] = {0};
    uint32_t *ret = sceAgcSetNop(cmd, 4);
    TEST_ASSERT(ret != NULL, "SetNop should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "SetNop opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "SetNop length");
}

static void test_game_compat_patchers(void) {
    /* Simulate an indirect register write packet:
     *   [0] header, [1] addr_lo, [2] addr_hi, [3] count */
    uint32_t cmd[4] = {0, 0, 0, 5};

    int32_t r = sceAgcSetShRegIndirectPatchSetAddress(cmd, 0x12345678ABCDEF00ULL);
    TEST_ASSERT_EQ(r, AGC_OK, "PatchSetAddress returns OK");
    TEST_ASSERT_EQ(cmd[1], 0xABCDEF00u, "PatchSetAddress lo");
    TEST_ASSERT_EQ(cmd[2], 0x12345678u, "PatchSetAddress hi");

    r = sceAgcSetShRegIndirectPatchAddRegisters(cmd, 3);
    TEST_ASSERT_EQ(r, AGC_OK, "PatchAddRegisters returns OK");
    TEST_ASSERT_EQ(cmd[3], 8u, "PatchAddRegisters adds to count");

    /* Test Cx and Uc variants */
    cmd[1] = cmd[2] = 0; cmd[3] = 5;
    sceAgcSetCxRegIndirectPatchSetAddress(cmd, 0xDEAD);
    TEST_ASSERT_EQ(cmd[1], 0xDEADu, "Cx patch addr lo");

    cmd[3] = 0;
    sceAgcSetCxRegIndirectPatchAddRegisters(cmd, 10);
    TEST_ASSERT_EQ(cmd[3], 10u, "Cx patch add regs");

    cmd[1] = cmd[2] = 0; cmd[3] = 0;
    sceAgcSetUcRegIndirectPatchSetAddress(cmd, 0xBEEF);
    TEST_ASSERT_EQ(cmd[1], 0xBEEFu, "Uc patch addr lo");

    sceAgcSetUcRegIndirectPatchAddRegisters(cmd, 7);
    TEST_ASSERT_EQ(cmd[3], 7u, "Uc patch add regs");
}

static void test_game_compat_driver_stubs(void) {
    /* RegisterOwner/Resource return 0x8a6c9018 per SPRX */
    int32_t r = sceAgcDriverRegisterOwner(NULL, NULL);
    TEST_ASSERT_EQ((uint32_t)r, 0x8a6c9018u, "RegisterOwner stub");

    r = sceAgcDriverRegisterResource(NULL, 0);
    TEST_ASSERT_EQ((uint32_t)r, 0x8a6c9018u, "RegisterResource stub");

    /* GetEqContextId returns 0 on generic backend */
    uint32_t ctx = sceAgcDriverGetEqContextId();
    TEST_ASSERT_EQ(ctx, 0u, "GetEqContextId returns 0 on generic");

    /* AGR submit returns 0x8a6d0003 */
    r = sceAgcDriverAgrSubmitDcb(NULL);
    TEST_ASSERT_EQ((uint32_t)r, 0x8a6d0003u, "AgrSubmitDcb not initialized");

    /* AddEqEvent returns NOT_SUPPORTED */
    r = sceAgcDriverAddEqEvent(NULL, 0, NULL);
    TEST_ASSERT(r < 0, "AddEqEvent returns error");

    /* DebugRaiseException returns OK on non-dev */
    r = sceAgcDebugRaiseException();
    TEST_ASSERT_EQ(r, AGC_OK, "DebugRaiseException returns OK");
}

static void test_game_compat_init(void) {
    /* sceAgcInit with valid init_level */
    uint32_t out_val = 0xDEAD;
    int32_t r = sceAgcInit(0, 0, &out_val);
    TEST_ASSERT_EQ(r, AGC_OK, "sceAgcInit returns OK");
    TEST_ASSERT_EQ(out_val, 0u, "sceAgcInit sets out_value to 0");

    /* sceAgcInit with invalid init_level */
    r = sceAgcInit(10, 0, NULL);
    TEST_ASSERT(r < 0, "sceAgcInit invalid level fails");

    /* sceAgcSuspendPoint returns OK on generic */
    r = sceAgcSuspendPoint(0, 0, 0, 0);
    TEST_ASSERT_EQ(r, AGC_OK, "sceAgcSuspendPoint returns OK");

    /* CreatePrimState with valid type */
    uint8_t state[64] = {0xFF};
    r = sceAgcCreatePrimState(state, 5);
    TEST_ASSERT_EQ(r, AGC_OK, "CreatePrimState returns OK");
    TEST_ASSERT_EQ(state[0], 0u, "CreatePrimState zeroes output");

    /* CreatePrimState with invalid type */
    r = sceAgcCreatePrimState(state, 11);
    TEST_ASSERT(r < 0, "CreatePrimState invalid type fails");

    /* CreateShader with NULL fails */
    r = sceAgcCreateShader(NULL, 0);
    TEST_ASSERT(r < 0, "CreateShader NULL fails");
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
    TEST_RUN(test_dcb_eop_flip_null);
    TEST_RUN(test_dcb_eop_flip_packet);
    TEST_RUN(test_dcb_eop_flip_cursor_advance);
    TEST_RUN(test_dcb_eop_flip_overflow);
    TEST_RUN(test_game_compat_dcb_acquire_mem);
    TEST_RUN(test_game_compat_dcb_copy_data);
    TEST_RUN(test_game_compat_dcb_jump);
    TEST_RUN(test_game_compat_dcb_reset_queue);
    TEST_RUN(test_game_compat_dcb_set_index_count);
    TEST_RUN(test_game_compat_dcb_set_index_size);
    TEST_RUN(test_game_compat_dcb_set_num_instances);
    TEST_RUN(test_game_compat_dcb_draw_index);
    TEST_RUN(test_game_compat_dcb_stall);
    TEST_RUN(test_game_compat_cb_set_sh_reg_range);
    TEST_RUN(test_game_compat_set_nop);
    TEST_RUN(test_game_compat_patchers);
    TEST_RUN(test_game_compat_driver_stubs);
    TEST_RUN(test_game_compat_init);
}
