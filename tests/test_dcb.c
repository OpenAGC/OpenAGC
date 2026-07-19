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

    /* RE: opcode 0x3F (INDIRECT_BUFFER), 5 params */
    uint32_t *cmd = sceAgcDcbJump(&cb, 0, 0, 0x12345678ULL, 0);
    TEST_ASSERT(cmd != NULL, "Jump should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INDIRECT_BUFFER, "Jump opcode 0x3F");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "Jump length");
    TEST_ASSERT_EQ(cmd[1], 0x12345678u, "Jump target lo (& ~3)");
    TEST_ASSERT_EQ(cmd[2], 0u, "Jump target hi");
    TEST_ASSERT_EQ(cmd[3] & 0x0F200000u, 0x0F200000u, "Jump cmd[3] has constant bits");
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

    /* RE: 2 dwords (not 3), clamps to max(count, 1) */
    uint32_t *cmd = sceAgcDcbSetIndexCount(&cb, 1024);
    TEST_ASSERT(cmd != NULL, "SetIndexCount should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INDEX_BUFFER_SIZE, "SetIndexCount opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2, "SetIndexCount length 2 dwords");
    TEST_ASSERT_EQ(cmd[1], 1024u, "SetIndexCount value");

    /* Test clamp: count=0 → 1 */
    agcCbInit(&cb, buffer, sizeof(buffer));
    cmd = sceAgcDcbSetIndexCount(&cb, 0);
    TEST_ASSERT(cmd != NULL, "SetIndexCount(0) should return non-NULL");
    TEST_ASSERT_EQ(cmd[1], 1u, "SetIndexCount clamps 0 to 1");
}

static void test_game_compat_dcb_set_index_size(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* RE: opcode 0x7A, cmd[1]=0x20000243, cmd[2]=(type&3)|(swap<<6)|0x400 */
    uint32_t *cmd = sceAgcDcbSetIndexSize(&cb, 1, 0);
    TEST_ASSERT(cmd != NULL, "SetIndexSize should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_INDEX_SIZE, "SetIndexSize opcode 0x7A");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "SetIndexSize length");
    TEST_ASSERT_EQ(cmd[1], 0x20000243u, "SetIndexSize constant cmd[1]");
    TEST_ASSERT_EQ(cmd[2], 0x401u, "SetIndexSize (1&3)|(0<<6)|0x400");

    /* Test swap=1 */
    agcCbInit(&cb, buffer, sizeof(buffer));
    cmd = sceAgcDcbSetIndexSize(&cb, 2, 1);
    TEST_ASSERT_EQ(cmd[2], 0x442u, "SetIndexSize (2&3)|(1<<6)|0x400");
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

    /* RE: cmd[1]=max(count,1), cmd[4]=count, cmd[5]=draw_initiator */
    uint32_t *cmd = sceAgcDcbDrawIndex(&cb, 100, 0x40000, 0x1234);
    TEST_ASSERT(cmd != NULL, "DrawIndex should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDEX_2, "DrawIndex opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "DrawIndex length");
    TEST_ASSERT_EQ(cmd[1], 100u, "DrawIndex cmd[1]=max(count,1)");
    TEST_ASSERT_EQ(cmd[2], 0x40000u, "DrawIndex base lo");
    TEST_ASSERT_EQ(cmd[4], 100u, "DrawIndex cmd[4]=index_count");
    TEST_ASSERT_EQ(cmd[5], 0x1234u, "DrawIndex cmd[5]=draw_initiator");

    /* Test clamp: count=0 → cmd[1]=1 */
    agcCbInit(&cb, buffer, sizeof(buffer));
    cmd = sceAgcDcbDrawIndex(&cb, 0, 0x40000, 0x1234);
    TEST_ASSERT_EQ(cmd[1], 1u, "DrawIndex clamps 0 to 1");
    TEST_ASSERT_EQ(cmd[4], 0u, "DrawIndex cmd[4] preserves original 0");
}

static void test_game_compat_dcb_stall(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* RE: opcode 0x42 (not NOP+sub) */
    uint32_t *cmd = sceAgcDcbStallCommandBufferParser(&cb);
    TEST_ASSERT(cmd != NULL, "Stall should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_STALL_PARSER, "Stall opcode 0x42");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2, "Stall length");
    TEST_ASSERT_EQ(cmd[1], 0u, "Stall cmd[1]=0");
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
    /* RE: sceAgcSetNop takes 1 param, patches byte at offset 1 to 0x10,
     * returns NULL */
    uint32_t cmd[4] = {0xC0036300, 0, 0, 0};  /* some non-NOP packet */
    uint32_t *ret = sceAgcSetNop(cmd);
    TEST_ASSERT(ret == NULL, "SetNop returns NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "SetNop patches opcode to NOP");
}

static void test_game_compat_patchers(void) {
    /* RE: Indirect register write packets use 5 dwords:
     *   [0] header (opcode 0x63/0x9F/0x64), [1] addr_lo, [2] addr_hi,
     *   [3] 0x80000000, [4] count (bits 13:0)
     * SetAddress patches cmd[1..2], preserving low 2 bits of cmd[1].
     * AddRegisters patches cmd[4] bits 13:0, preserving bits 31:14.
     * Wrong opcode → returns 0x8a6c000c. */

    /* Sh patcher (opcode 0x63) */
    uint32_t cmd[5] = {0};
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG_INDIRECT, 5);
    cmd[1] = 0x43u;  /* addr_lo with low 2 bits set */
    cmd[2] = 0x0u;
    cmd[3] = 0x80000000u;
    cmd[4] = 5u;

    int32_t r = sceAgcSetShRegIndirectPatchSetAddress(cmd, 0x12345678ABCDEF00ULL);
    TEST_ASSERT_EQ(r, AGC_OK, "Sh PatchSetAddress returns OK");
    TEST_ASSERT_EQ(cmd[1], 0xABCDEF03u, "Sh PatchSetAddress lo (preserves low 2 bits)");
    TEST_ASSERT_EQ(cmd[2], 0x12345678u, "Sh PatchSetAddress hi");

    r = sceAgcSetShRegIndirectPatchAddRegisters(cmd, 3);
    TEST_ASSERT_EQ(r, AGC_OK, "Sh PatchAddRegisters returns OK");
    TEST_ASSERT_EQ(cmd[4], 8u, "Sh PatchAddRegisters adds to cmd[4]");

    /* Wrong opcode → 0x8a6c000c */
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CX_REG_INDIRECT, 5);
    r = sceAgcSetShRegIndirectPatchSetAddress(cmd, 0xDEAD);
    TEST_ASSERT_EQ((uint32_t)r, 0x8a6c000cu, "Sh patcher rejects wrong opcode");

    /* Cx patcher (opcode 0x9F) */
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CX_REG_INDIRECT, 5);
    cmd[1] = 0; cmd[2] = 0; cmd[4] = 0;
    r = sceAgcSetCxRegIndirectPatchSetAddress(cmd, 0xDEAD);
    TEST_ASSERT_EQ(r, AGC_OK, "Cx PatchSetAddress returns OK");
    TEST_ASSERT_EQ(cmd[1], 0xDEACu, "Cx patch addr lo (aligned to 4)");

    r = sceAgcSetCxRegIndirectPatchAddRegisters(cmd, 10);
    TEST_ASSERT_EQ(r, AGC_OK, "Cx PatchAddRegisters returns OK");
    TEST_ASSERT_EQ(cmd[4], 10u, "Cx patch add regs to cmd[4]");

    /* Uc patcher (opcode 0x64) */
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UC_REG_INDIRECT, 5);
    cmd[1] = 0; cmd[2] = 0; cmd[4] = 0;
    r = sceAgcSetUcRegIndirectPatchSetAddress(cmd, 0xBEEF);
    TEST_ASSERT_EQ(r, AGC_OK, "Uc PatchSetAddress returns OK");
    TEST_ASSERT_EQ(cmd[1], 0xBEECu, "Uc patch addr lo (aligned to 4)");

    r = sceAgcSetUcRegIndirectPatchAddRegisters(cmd, 7);
    TEST_ASSERT_EQ(r, AGC_OK, "Uc PatchAddRegisters returns OK");
    TEST_ASSERT_EQ(cmd[4], 7u, "Uc patch add regs to cmd[4]");

    /* Wrong opcode → 0x8a6c000c */
    r = sceAgcSetUcRegIndirectPatchAddRegisters(cmd, 1);
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG_INDIRECT, 5);
    r = sceAgcSetUcRegIndirectPatchSetAddress(cmd, 0xCAFE);
    TEST_ASSERT_EQ((uint32_t)r, 0x8a6c000cu, "Uc patcher rejects wrong opcode");
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

    /* CreatePrimState with 5 params (out_state, out_state2, param3, param4, prim_type) */
    uint8_t state[64] = {0xFF};
    r = sceAgcCreatePrimState(state, NULL, NULL, NULL, 5);
    TEST_ASSERT_EQ(r, AGC_OK, "CreatePrimState returns OK");
    TEST_ASSERT_EQ(state[0], 0u, "CreatePrimState zeroes output");

    /* CreateShader with NULL fails */
    r = sceAgcCreateShader(NULL, 0);
    TEST_ASSERT(r < 0, "CreateShader NULL fails");
}

/* ===================================================================== */
/* Batch 2 tests — SPRX disassembly-derived packet builders              */
/* ===================================================================== */

static void test_batch2_dcb_clear_state(void) {
    SceAgcCb cb;
    uint32_t buf[16];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbClearState(&cb, 0x5);
    TEST_ASSERT(cmd != 0, "ClearState returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_CLEAR_STATE_AGC, "ClearState opcode 0x12");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2u, "ClearState length 2");
    TEST_ASSERT_EQ(cmd[1], 0x5u, "ClearState flags&0xf");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 2u, "ClearState cursor advance");
}

static void test_batch2_dcb_rewind(void) {
    SceAgcCb cb;
    uint32_t buf[16];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbRewind(&cb, 1);
    TEST_ASSERT(cmd != 0, "Rewind returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_REWIND, "Rewind opcode 0x59");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2u, "Rewind length 2");
    TEST_ASSERT_EQ(cmd[1], 0x80000000u, "Rewind flags<<31");
}

static void test_batch2_dcb_cond_exec(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbCondExec(&cb, 0x1000, 42);
    TEST_ASSERT(cmd != 0, "CondExec returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_COND_EXEC, "CondExec opcode 0x22");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5u, "CondExec length 5");
    TEST_ASSERT_EQ(cmd[1], 0x1000u, "CondExec addr_lo aligned");
    TEST_ASSERT_EQ(cmd[4], 42u, "CondExec count&0x3fff");
}

static void test_batch2_dcb_set_index_indirect_args(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbSetIndexIndirectArgs(&cb, 0x12340, 0x100);
    TEST_ASSERT(cmd != 0, "SetIndexIndirectArgs returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_INDEX_INDIRECT_ARGS, "SetIndexIndirectArgs opcode 0x91");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4u, "SetIndexIndirectArgs length 4");
    TEST_ASSERT_EQ(cmd[1] & 0xFu, 0u, "SetIndexIndirectArgs addr aligned to 16");
    TEST_ASSERT_EQ(cmd[3], 0x100u, "SetIndexIndirectArgs offset&0xffff");
}

static void test_batch2_dcb_atomic_mem(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbAtomicMem(&cb, 1, 5, 3, 0x4000, 0xDEAD, 0xBEEF);
    TEST_ASSERT(cmd != 0, "AtomicMem returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_ATOMIC_MEM_AGC, "AtomicMem opcode 0x1E");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 9u, "AtomicMem length 9");
    TEST_ASSERT_EQ(cmd[2], 0x4000u, "AtomicMem addr_lo");
    TEST_ASSERT_EQ(cmd[4], 0xDEADu, "AtomicMem data_lo");
    TEST_ASSERT_EQ(cmd[6], 0xBEEFu, "AtomicMem cmp_lo");
}

static void test_batch2_dcb_atomic_gds(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbAtomicGds(&cb, 1, 0, 0x10, 0xAA, 0x100, 0x200, 3, 0xBEEF, 0xFF);
    TEST_ASSERT(cmd != 0, "AtomicGds returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_ATOMIC_GDS, "AtomicGds opcode 0x1D");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 11u, "AtomicGds length 11");
}

static void test_batch2_dcb_mem_semaphore(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbMemSemaphore(&cb, 0x1008, 1, 0, 2);
    TEST_ASSERT(cmd != 0, "MemSemaphore returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_MEM_SEMAPHORE, "MemSemaphore opcode 0x39");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4u, "MemSemaphore length 4");
    TEST_ASSERT_EQ(cmd[1] & 0x7u, 0u, "MemSemaphore addr aligned to 8");
    TEST_ASSERT_EQ((cmd[3] >> 29) & 0x7u, 2u, "MemSemaphore op in bits 31:29");
}

static void test_batch2_dcb_prime_utcl2(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbPrimeUtcl2(&cb, 2, 1, 0x4000, 0x100);
    TEST_ASSERT(cmd != 0, "PrimeUtcl2 returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_PRIME_UTCL2, "PrimeUtcl2 opcode 0x5D");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5u, "PrimeUtcl2 length 5");
    TEST_ASSERT_EQ(cmd[1] & 0x7u, 2u, "PrimeUtcl2 cache_policy");
}

static void test_batch2_dcb_reg_direct_setters(void) {
    SceAgcCb cb;
    uint32_t buf[64];
    agcCbInit(&cb, buf, sizeof(buf));

    /* CfRegisterDirect: opcode 0x68
     * 64-bit param: low 16 bits = reg_offset, high 32 bits = value */
    uint32_t *cmd = sceAgcDcbSetCfRegisterDirect(&cb, 0x55AA00001234ULL);
    TEST_ASSERT(cmd != 0, "CfRegDirect returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_CONFIG_REG, "CfRegDirect opcode 0x68");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3u, "CfRegDirect length 3");
    TEST_ASSERT_EQ(cmd[1], 0x1234u, "CfRegDirect offset");
    TEST_ASSERT_EQ(cmd[2], 0x55AAu, "CfRegDirect value");

    /* CxRegisterDirect: opcode 0x69 */
    cmd = sceAgcDcbSetCxRegisterDirect(&cb, 0xDEAD000000FFULL);
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_CONTEXT_REG, "CxRegDirect opcode 0x69");
    TEST_ASSERT_EQ(cmd[1], 0x00FFu, "CxRegDirect offset");

    /* ShRegisterDirect: opcode 0x76 */
    cmd = sceAgcDcbSetShRegisterDirect(&cb, 0x12340000ABCDULL);
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_SH_REG, "ShRegDirect opcode 0x76");
    TEST_ASSERT_EQ(cmd[1], 0xABCDu, "ShRegDirect offset");

    /* UcRegisterDirect: opcode 0x79 */
    cmd = sceAgcDcbSetUcRegisterDirect(&cb, 0xBEEF00000001ULL);
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_UCONFIG_REG, "UcRegDirect opcode 0x79");
    TEST_ASSERT_EQ(cmd[1], 0x0001u, "UcRegDirect offset");
}

static void test_batch2_dcb_cf_reg_range_direct(void) {
    SceAgcCb cb;
    uint32_t buf[64];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t values[] = {0x10, 0x20, 0x30};
    uint32_t *cmd = sceAgcDcbSetCfRegisterRangeDirect(&cb, 0x100, values, 3);
    TEST_ASSERT(cmd != 0, "CfRegRangeDirect returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_CONFIG_REG, "CfRegRangeDirect opcode 0x68");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5u, "CfRegRangeDirect length 2+3");
    TEST_ASSERT_EQ(cmd[1], 0x100u, "CfRegRangeDirect offset");
    TEST_ASSERT_EQ(cmd[2], 0x10u, "CfRegRangeDirect value[0]");
    TEST_ASSERT_EQ(cmd[4], 0x30u, "CfRegRangeDirect value[2]");
}

static void test_batch2_cb_uc_reg_range_direct(void) {
    SceAgcCb cb;
    uint32_t buf[64];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t values[] = {0xAA, 0xBB};
    uint32_t *cmd = sceAgcCbSetUcRegisterRangeDirect(&cb, 0x20, values, 2);
    TEST_ASSERT(cmd != 0, "UcRegRangeDirect returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_UCONFIG_REG, "UcRegRangeDirect opcode 0x79");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4u, "UcRegRangeDirect length 2+2");
    TEST_ASSERT_EQ(cmd[1], 0x20u, "UcRegRangeDirect offset");
    TEST_ASSERT_EQ(cmd[3], 0xBBu, "UcRegRangeDirect value[1]");
}

static void test_batch2_cb_branch(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    /* 12-arg signature per SPRX disassembly */
    uint32_t *cmd = sceAgcCbBranch(&cb, 1, 2, 0x1000, 0x2000, 0x3000,
                                    1, 0x4000, 256, 0, 0x5000, 128);
    TEST_ASSERT(cmd != 0, "CbBranch returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INDIRECT_BUFFER, "CbBranch opcode 0x3F");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 14u, "CbBranch length 14");
    TEST_ASSERT_EQ(cmd[1], ((2u & 7u) << 8) | (1u & 3u), "CbBranch control word");
    TEST_ASSERT_EQ(cmd[2], 0x1000u & ~7u, "CbBranch target_addr_lo & ~7");
    TEST_ASSERT_EQ(cmd[10] >> 28, 1u, "CbBranch dst_engine in bits 31:28");
    TEST_ASSERT_EQ(cmd[13] >> 28, 0u, "CbBranch src_engine in bits 31:28");
    TEST_ASSERT_EQ(cmd[10] & 0xFFFFFu, 256u, "CbBranch size1 in bits 19:0");
    TEST_ASSERT_EQ(cmd[13] & 0xFFFFFu, 128u, "CbBranch size2 in bits 19:0");
}

static void test_batch2_cb_cond_write(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    /* 8-arg signature per SPRX: (cb, cmp_func, write_enable, address, write_data, ref, mask, reserved) */
    uint32_t *cmd = sceAgcCbCondWrite(&cb, 2, 1, 0x4000, 0x5678, 0x1234, 0xFF, 0);
    TEST_ASSERT(cmd != 0, "CbCondWrite returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_COND_WRITE, "CbCondWrite opcode 0x45");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 9u, "CbCondWrite length 9");
    TEST_ASSERT_EQ(cmd[1] & 0x7u, 2u, "CbCondWrite compare_function");
    TEST_ASSERT_EQ((cmd[1] >> 8) & 0x3u, 1u, "CbCondWrite write_enable");
    TEST_ASSERT_EQ(cmd[1] & 0x10u, 0x10u, "CbCondWrite fixed 0x10 bit");
    TEST_ASSERT_EQ(cmd[2], 0x1234u, "CbCondWrite ref_lo");
    TEST_ASSERT_EQ(cmd[4], 0xFFu, "CbCondWrite mask");
    TEST_ASSERT_EQ(cmd[6], 0x4000u, "CbCondWrite address_lo");
    TEST_ASSERT_EQ(cmd[8], 0x5678u, "CbCondWrite write_data");
}

static void test_batch2_cb_mem_semaphore(void) {
    SceAgcCb cb;
    uint32_t buf[32];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcCbMemSemaphore(&cb, 0x2000, 1, 1, 0);
    TEST_ASSERT(cmd != 0, "CbMemSemaphore returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_MEM_SEMAPHORE, "CbMemSemaphore opcode 0x39");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4u, "CbMemSemaphore length 4");
}

static void test_batch2_wait_reg_mem_patchers(void) {
    /* The SPRX patchers require a 0x79 (SET_UCONFIG_REG) wrapper packet
     * followed by a real WAIT_REG_MEM (0x3C) packet. The patcher finds
     * the WAIT_REG_MEM by skipping past the 0x79 wrapper. */
    uint32_t buf[32];
    memset(buf, 0, sizeof(buf));

    /* Build a 0x79 wrapper (2 dwords) + WAIT_REG_MEM (7 dwords) = 9 dwords */
    uint32_t *cmd = buf;
    /* 0x79 wrapper: type-3, opcode 0x79, length 0 (2 dwords total) */
    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, 2);
    cmd[1] = 0;  /* wrapper data */

    /* Real WAIT_REG_MEM at cmd[2] (adjusted pointer) */
    uint32_t *wrm = &cmd[2];
    wrm[0] = agcPm4Header3(AGC_PM4_OP_WAIT_REG_MEM, 7);
    wrm[1] = 0;  /* compare_function | (operation << 8) */
    wrm[2] = 0x4000;  /* address_lo */
    wrm[3] = 0;        /* address_hi */
    wrm[4] = 0x1234;   /* reference */
    wrm[5] = 0xFF;     /* mask */
    wrm[6] = 0;        /* poll_cycles */

    /* Patch compare function (bits 2:0 of adjusted[1] = wrm[1]) */
    int32_t r = sceAgcWaitRegMemPatchCompareFunction(cmd, 5);
    TEST_ASSERT_EQ(r, AGC_OK, "PatchCmpFunc returns OK");
    TEST_ASSERT_EQ(wrm[1] & 0x7u, 5u, "PatchCmpFunc sets wrm[1] bits 2:0");

    /* Patch reference (adjusted[4] = wrm[4]) */
    r = sceAgcWaitRegMemPatchReference(cmd, 0xDEAD);
    TEST_ASSERT_EQ(r, AGC_OK, "PatchRef returns OK");
    TEST_ASSERT_EQ(wrm[4], 0xDEADu, "PatchRef sets wrm[4]");

    /* Patch mask (adjusted[5] = wrm[5] for 32-bit 0x3C) */
    r = sceAgcWaitRegMemPatchMask(cmd, 0xFFFF);
    TEST_ASSERT_EQ(r, AGC_OK, "PatchMask returns OK");
    TEST_ASSERT_EQ(wrm[5], 0xFFFFu, "PatchMask sets wrm[5] for 32-bit");

    /* Wrong opcode (not 0x79) → 0x8a6c000c */
    cmd[0] = agcPm4Header3(AGC_PM4_OP_NOP, 2);
    r = sceAgcWaitRegMemPatchCompareFunction(cmd, 0);
    TEST_ASSERT_EQ((uint32_t)r, 0x8a6c000cu, "PatchCmpFunc rejects non-0x79 opcode");

    /* NULL cmd → 0x8a6c000c */
    r = sceAgcWaitRegMemPatchCompareFunction(NULL, 0);
    TEST_ASSERT_EQ((uint32_t)r, 0x8a6c000cu, "PatchCmpFunc rejects NULL");
}

static void test_batch2_dcb_draw_index_multi_instanced(void) {
    SceAgcCb cb;
    uint32_t buf[64];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t inst_data[] = {0x10, 0x20, 0x30};
    uint32_t *cmd = sceAgcDcbDrawIndexMultiInstanced(
        &cb, 100, 0x4000, 3, 0xFF, inst_data, 3);
    TEST_ASSERT(cmd != 0, "DrawIndexMultiInstanced returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDEX_MULTI_INSTANCED, "DrawIndexMultiInstanced opcode 0x3A");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 12u, "DrawIndexMultiInstanced length 9+3");
    TEST_ASSERT_EQ(cmd[1], 100u, "DrawIndexMultiInstanced index_count");
    TEST_ASSERT_EQ(cmd[9], 0x10u, "DrawIndexMultiInstanced inst_data[0]");
}

static void test_batch2_dcb_set_marker(void) {
    SceAgcCb cb;
    uint32_t buf[64];
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcDcbSetMarker(&cb, "test", 0);
    TEST_ASSERT(cmd != 0, "SetMarker returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "SetMarker uses NOP wrapper");
}

static void test_batch2_dcb_context_state_op(void) {
    SceAgcCb cb;
    uint32_t buf[64];
    agcCbInit(&cb, buf, sizeof(buf));

    /* op 0: CLEAR_STATE */
    uint32_t *cmd = sceAgcDcbContextStateOp(&cb, 0, 0, 0, 0, NULL);
    TEST_ASSERT(cmd != 0, "ContextStateOp op=0 returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_CLEAR_STATE_AGC, "ContextStateOp op=0 uses CLEAR_STATE");

    /* op 1: SET_CONTEXT_REG */
    cmd = sceAgcDcbContextStateOp(&cb, 1, 0, 0x100, 0x42, NULL);
    TEST_ASSERT(cmd != 0, "ContextStateOp op=1 returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_CONTEXT_REG, "ContextStateOp op=1 uses SET_CONTEXT_REG");

    /* op 2: SET_CX_REG_INDIRECT */
    cmd = sceAgcDcbContextStateOp(&cb, 2, 0, 0x200, 4, NULL);
    TEST_ASSERT(cmd != 0, "ContextStateOp op=2 returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_CX_REG_INDIRECT, "ContextStateOp op=2 uses SET_CX_REG_INDIRECT");

    /* op 3: CLEAR_STATE + SET_CX_REG_INDIRECT */
    cmd = sceAgcDcbContextStateOp(&cb, 3, 0, 0x300, 8, NULL);
    TEST_ASSERT(cmd != 0, "ContextStateOp op=3 returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_CLEAR_STATE_AGC, "ContextStateOp op=3 starts with CLEAR_STATE");

    /* Invalid op */
    cmd = sceAgcDcbContextStateOp(&cb, 99, 0, 0, 0, NULL);
    TEST_ASSERT(cmd == 0, "ContextStateOp invalid op returns NULL");
}

static void test_batch2_dcb_workload_helpers(void) {
    SceAgcCb cb;
    uint32_t buf[64];
    agcCbInit(&cb, buf, sizeof(buf));

    /* SetWorkloadsActive */
    uint32_t *cmd = sceAgcDcbSetWorkloadsActive(&cb, 0x1234, NULL, 0);
    TEST_ASSERT(cmd != 0, "DcbSetWorkloadsActive returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_WORKLOAD, "DcbSetWorkloadsActive opcode 0x1E");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8u, "DcbSetWorkloadsActive length 8");

    /* SetWorkloadComplete */
    cmd = sceAgcDcbSetWorkloadComplete(&cb, 0x42, 0);
    TEST_ASSERT(cmd != 0, "DcbSetWorkloadComplete returns non-NULL");
    TEST_ASSERT_EQ(cmd[1], 0x42u, "DcbSetWorkloadComplete workload_id");

    /* SetWorkloadStreamInactive */
    cmd = sceAgcDcbSetWorkloadStreamInactive(&cb, 0x99);
    TEST_ASSERT(cmd != 0, "DcbSetWorkloadStreamInactive returns non-NULL");
    TEST_ASSERT_EQ(cmd[1], 0x99u, "DcbSetWorkloadStreamInactive workload_id");
}

/* Forward declarations for batch 3 tests (defined after test_suite_dcb). */
static void test_batch3_kytyps5_patchers(void);
static void test_batch3_kytyps5_getsize_helpers(void);

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
    TEST_RUN(test_batch2_dcb_clear_state);
    TEST_RUN(test_batch2_dcb_rewind);
    TEST_RUN(test_batch2_dcb_cond_exec);
    TEST_RUN(test_batch2_dcb_set_index_indirect_args);
    TEST_RUN(test_batch2_dcb_atomic_mem);
    TEST_RUN(test_batch2_dcb_atomic_gds);
    TEST_RUN(test_batch2_dcb_mem_semaphore);
    TEST_RUN(test_batch2_dcb_prime_utcl2);
    TEST_RUN(test_batch2_dcb_reg_direct_setters);
    TEST_RUN(test_batch2_dcb_cf_reg_range_direct);
    TEST_RUN(test_batch2_cb_uc_reg_range_direct);
    TEST_RUN(test_batch2_cb_branch);
    TEST_RUN(test_batch2_cb_cond_write);
    TEST_RUN(test_batch2_cb_mem_semaphore);
    TEST_RUN(test_batch2_wait_reg_mem_patchers);
    TEST_RUN(test_batch2_dcb_draw_index_multi_instanced);
    TEST_RUN(test_batch2_dcb_set_marker);
    TEST_RUN(test_batch2_dcb_context_state_op);
    TEST_RUN(test_batch2_dcb_workload_helpers);
    TEST_RUN(test_batch3_kytyps5_patchers);
    TEST_RUN(test_batch3_kytyps5_getsize_helpers);
}

/* ===================================================================== */
/* Batch 3: KytyPS5-confirmed patchers and helpers                      */
/* ===================================================================== */

static void test_batch3_kytyps5_patchers(void) {
    /* GetPacketSize: normal type-3 packet with length=5 -> 5 dwords */
    uint32_t pkt[8];
    pkt[0] = agcPm4Header3(AGC_PM4_OP_COND_EXEC, 5);
    TEST_ASSERT_EQ(sceAgcGetPacketSize(pkt), 5u, "GetPacketSize normal");

    /* GetPacketSize: special NOP filler (0x3FFF1000 mask) -> 1 dword */
    pkt[0] = 0xC0001000u | 0x3FFF0000u;  /* matches 0x3FFF1000 mask */
    TEST_ASSERT_EQ(sceAgcGetPacketSize(pkt), 1u, "GetPacketSize NOP filler");

    /* SetPacketPredication: set bit 0 */
    pkt[0] = agcPm4Header3(AGC_PM4_OP_NOP, 2);
    uint32_t orig = pkt[0];
    TEST_ASSERT_EQ(sceAgcSetPacketPredication(pkt, 1), AGC_OK, "SetPred OK");
    TEST_ASSERT_EQ(pkt[0], orig | 1u, "SetPred sets bit 0");

    /* SetPacketPredication: clear bit 0 */
    pkt[0] = orig | 1u;
    TEST_ASSERT_EQ(sceAgcSetPacketPredication(pkt, 0), AGC_OK, "ClearPred OK");
    TEST_ASSERT_EQ(pkt[0], orig & ~1u, "ClearPred clears bit 0");

    /* SetPacketPredication: NULL -> error */
    TEST_ASSERT_EQ(sceAgcSetPacketPredication(NULL, 1), AGC_ERROR_INVALID_ARGUMENT,
        "SetPred rejects NULL");

    /* SetRangePredication: set predication on a 2-packet range */
    uint32_t range[10];
    range[0] = agcPm4Header3(AGC_PM4_OP_NOP, 3);  /* 3 dwords */
    range[3] = agcPm4Header3(AGC_PM4_OP_NOP, 2);  /* 2 dwords */
    uint32_t *end = &range[5];
    TEST_ASSERT_EQ(sceAgcSetRangePredication(range, end, 1), AGC_OK, "SetRangePred OK");
    TEST_ASSERT_EQ(range[0] & 1u, 1u, "SetRangePred pkt0 bit0 set");
    TEST_ASSERT_EQ(range[3] & 1u, 1u, "SetRangePred pkt1 bit0 set");

    /* CondExecPatchSetEnd: patch cmd[4] with dword count */
    uint32_t condexec[10];
    condexec[0] = agcPm4Header3(AGC_PM4_OP_COND_EXEC, 5);
    condexec[4] = 0;
    uint32_t *ce_end = &condexec[8];  /* 3 dwords after packet end (cmd+5) */
    TEST_ASSERT_EQ(sceAgcCondExecPatchSetEnd(condexec, ce_end), AGC_OK, "CondExecPatchEnd OK");
    TEST_ASSERT_EQ(condexec[4], 3u, "CondExecPatchEnd sets 3 dwords");

    /* CondExecPatchSetEnd: wrong opcode -> error */
    condexec[0] = agcPm4Header3(AGC_PM4_OP_NOP, 5);
    TEST_ASSERT_EQ(sceAgcCondExecPatchSetEnd(condexec, ce_end), AGC_ERROR_INVALID_ARGUMENT,
        "CondExecPatchEnd rejects wrong opcode");

    /* CondExecPatchSetCommandAddress: patch cmd[1..2] */
    condexec[0] = agcPm4Header3(AGC_PM4_OP_COND_EXEC, 5);
    condexec[1] = 0x2;  /* preserve bits 1:0 */
    condexec[2] = 0;
    uint32_t target_cmd[4] __attribute__((aligned(4)));
    uint64_t target_va = (uint64_t)(uintptr_t)target_cmd;
    TEST_ASSERT_EQ(sceAgcCondExecPatchSetCommandAddress(condexec, target_cmd), AGC_OK,
        "CondExecPatchCmdAddr OK");
    TEST_ASSERT_EQ(condexec[1] & 0x3u, 0x2u, "CondExecPatchCmdAddr preserves bits 1:0");
    TEST_ASSERT_EQ((uint32_t)condexec[1] & 0xFFFFFFFCu, (uint32_t)target_va & 0xFFFFFFFCu,
        "CondExecPatchCmdAddr lo matches target");
    TEST_ASSERT_EQ(condexec[2], (uint32_t)(target_va >> 32), "CondExecPatchCmdAddr hi matches");

    /* WriteDataPatchSetAddressOrOffset: patch cmd[2..3] */
    uint32_t wd[8];
    wd[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 8);
    wd[2] = 0;
    wd[3] = 0;
    uint64_t wd_addr = 0x123456789ABCULL;
    TEST_ASSERT_EQ(sceAgcWriteDataPatchSetAddressOrOffset(wd, wd_addr), AGC_OK,
        "WriteDataPatchAddr OK");
    TEST_ASSERT_EQ(wd[2], (uint32_t)wd_addr, "WriteDataPatchAddr lo");
    TEST_ASSERT_EQ(wd[3], (uint32_t)(wd_addr >> 32), "WriteDataPatchAddr hi");

    /* WriteDataPatchSetAddressOrOffset: wrong opcode -> error */
    wd[0] = agcPm4Header3(AGC_PM4_OP_NOP, 8);
    TEST_ASSERT_EQ(sceAgcWriteDataPatchSetAddressOrOffset(wd, 0), AGC_ERROR_INVALID_ARGUMENT,
        "WriteDataPatchAddr rejects wrong opcode");

    /* JumpPatchSetTarget: patch cmd[1..3] for IT_INDIRECT_BUFFER */
    uint32_t jump[4];
    jump[0] = agcPm4Header3(AGC_PM4_OP_INDIRECT_BUFFER, 4);
    jump[1] = 0;
    jump[2] = 0xFFFF0000u;  /* preserve upper bits */
    jump[3] = 0xFFF00000u;  /* preserve upper bits */
    uint32_t jump_target[4] __attribute__((aligned(4)));
    uint64_t jt_va = (uint64_t)(uintptr_t)jump_target;
    TEST_ASSERT_EQ(sceAgcJumpPatchSetTarget(jump, jump_target, 256), AGC_OK, "JumpPatch OK");
    TEST_ASSERT_EQ(jump[1], (uint32_t)jt_va, "JumpPatch lo matches target");
    TEST_ASSERT_EQ(jump[2] & 0xFFFFu, (uint32_t)(jt_va >> 32) & 0xFFFFu, "JumpPatch hi matches");
    TEST_ASSERT_EQ(jump[3] & 0xFFFFFu, 256u, "JumpPatch size");

    /* JumpPatchSetTarget: wrong opcode -> error */
    jump[0] = agcPm4Header3(AGC_PM4_OP_NOP, 4);
    TEST_ASSERT_EQ(sceAgcJumpPatchSetTarget(jump, jump_target, 0), AGC_ERROR_INVALID_ARGUMENT,
        "JumpPatch rejects wrong opcode");

    /* SetNumRegisters patchers */
    uint32_t ind[5];
    ind[0] = agcPm4Header3(AGC_PM4_OP_SET_CX_REG_INDIRECT, 5);
    ind[4] = 0;
    TEST_ASSERT_EQ(sceAgcSetCxRegIndirectPatchSetNumRegisters(ind, 42), AGC_OK,
        "SetCxNumRegs OK");
    TEST_ASSERT_EQ(ind[4] & 0x3FFFu, 42u, "SetCxNumRegs value");

    ind[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG_INDIRECT, 5);
    TEST_ASSERT_EQ(sceAgcSetShRegIndirectPatchSetNumRegisters(ind, 7), AGC_OK,
        "SetShNumRegs OK");
    TEST_ASSERT_EQ(ind[4] & 0x3FFFu, 7u, "SetShNumRegs value");

    ind[0] = agcPm4Header3(AGC_PM4_OP_SET_UC_REG_INDIRECT, 5);
    TEST_ASSERT_EQ(sceAgcSetUcRegIndirectPatchSetNumRegisters(ind, 15), AGC_OK,
        "SetUcNumRegs OK");
    TEST_ASSERT_EQ(ind[4] & 0x3FFFu, 15u, "SetUcNumRegs value");

    /* SetNumRegisters: wrong opcode -> error */
    ind[0] = agcPm4Header3(AGC_PM4_OP_NOP, 5);
    TEST_ASSERT_EQ(sceAgcSetCxRegIndirectPatchSetNumRegisters(ind, 1), AGC_ERROR_INVALID_ARGUMENT,
        "SetCxNumRegs rejects wrong opcode");
}

static void test_batch3_kytyps5_getsize_helpers(void) {
    /* WriteDataGetSize: 4*num_dwords + 16 */
    TEST_ASSERT_EQ(sceAgcDcbWriteDataGetSize(0), 16u, "WriteDataGetSize(0)");
    TEST_ASSERT_EQ(sceAgcDcbWriteDataGetSize(4), 32u, "WriteDataGetSize(4)");
    TEST_ASSERT_EQ(sceAgcDcbWriteDataGetSize(16), 80u, "WriteDataGetSize(16)");

    /* JumpGetSize: 16 bytes (4 dwords) */
    TEST_ASSERT_EQ(sceAgcDcbJumpGetSize(), 16u, "JumpGetSize");

    /* RewindGetSize: 8 bytes (2 dwords) */
    TEST_ASSERT_EQ(sceAgcDcbRewindGetSize(), 8u, "RewindGetSize");

    /* CondExecGetSize: 20 bytes (5 dwords) */
    TEST_ASSERT_EQ(sceAgcDcbCondExecGetSize(), 20u, "CondExecGetSize");

    /* AcbCondExecGetSize: 20 bytes (5 dwords) */
    TEST_ASSERT_EQ(sceAgcAcbCondExecGetSize(), 20u, "AcbCondExecGetSize");

    /* WaitOnAddressGetSize: 14*4 (32-bit) or 16*4 (64-bit) */
    TEST_ASSERT_EQ(sceAgcDcbWaitOnAddressGetSize(0), 56u, "WaitOnAddressGetSize(0)");
    TEST_ASSERT_EQ(sceAgcDcbWaitOnAddressGetSize(1), 64u, "WaitOnAddressGetSize(1)");
    TEST_ASSERT_EQ(sceAgcDcbWaitOnAddressGetSize(2), 0u, "WaitOnAddressGetSize(invalid)");
}
