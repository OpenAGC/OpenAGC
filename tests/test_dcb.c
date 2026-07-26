#include "test.h"
#include "agcdriver.h"
#include "agc_pm4.h"
#include "agc_cb.h"

static void test_dcb_init_null(void) {
    int32_t r = sceAgcDcbInitializeDefaultHardwareState(NULL, 100);
    TEST_ASSERT(r < 0, "NULL dcb should fail");
}

static void test_dcb_init_ok(void) {
    uint32_t buf[64];
    int32_t r = sceAgcDcbInitializeDefaultHardwareState(buf, 64);
    TEST_ASSERT(r > 0, "Init should return positive dword count");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 2, "Init NOP should be two dwords");
}

static void test_dcb_clear_state(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbClearState(&cb, 0x3u);
    TEST_ASSERT(cmd != NULL, "ClearState should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_CLEAR_STATE_AGC, "ClearState opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2, "ClearState length");
    TEST_ASSERT_EQ(cmd[1], 0x3u, "ClearState flags");
}

static void test_dcb_mem_semaphore(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbMemSemaphore(&cb, 0x1000ULL, 0x1u, 0x1u, 0x0u);
    TEST_ASSERT(cmd != NULL, "MemSemaphore should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_MEM_SEMAPHORE, "MemSemaphore opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "MemSemaphore length");
    TEST_ASSERT_EQ(cmd[1], 0x1000u, "MemSemaphore addr lo (& ~7)");
    TEST_ASSERT_EQ(cmd[2], 0x0u, "MemSemaphore addr hi");
    TEST_ASSERT_EQ(cmd[3], 0x00110000u, "MemSemaphore control (op<<29|signal<<20|wait<<16)");
}

static void test_dcb_flip(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbSetFlip(&cb, 0, 0, 0, 0);
    TEST_ASSERT(cmd != NULL, "SetFlip should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Type(cmd[0]), AGC_PM4_TYPE3, "SetFlip should emit PM4 type 3");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "SetFlip uses NOP wrapper");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_FLIP, "SetFlip subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "SetFlip packet length");
}

static void test_dcb_set_workload_complete(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbSetWorkloadComplete(&cb, 0x11223344u, 0x0u);
    TEST_ASSERT(cmd != NULL, "SetWorkloadComplete should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_WORKLOAD, "SetWorkloadComplete opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8, "SetWorkloadComplete length");
    TEST_ASSERT_EQ(cmd[1], 0x11223344u, "SetWorkloadComplete workload_id");
    TEST_ASSERT_EQ(cmd[2], 0x0u, "SetWorkloadComplete flags");
}

static void test_dcb_set_workload_stream_inactive(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbSetWorkloadStreamInactive(&cb, 0x11223344u);
    TEST_ASSERT(cmd != NULL, "SetWorkloadStreamInactive should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_WORKLOAD, "SetWorkloadStreamInactive opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8, "SetWorkloadStreamInactive length");
    TEST_ASSERT_EQ(cmd[1], 0x11223344u, "SetWorkloadStreamInactive workload_id");
}

static void test_dcb_set_workloads_active(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbSetWorkloadsActive(&cb, 0x12345678u, NULL, 0);
    TEST_ASSERT(cmd != NULL, "SetWorkloadsActive should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_WORKLOAD, "SetWorkloadsActive opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8, "SetWorkloadsActive length");
    TEST_ASSERT_EQ(cmd[1], 0x12345678u, "SetWorkloadsActive flags");
}

static void test_dcb_atomic_gds(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* sceAgcDcbAtomicGds(cb, op, gds_op, src, data, offset, index,
     *                    loop_count, cmp_data, mask) */
    uint32_t *cmd = sceAgcDcbAtomicGds(&cb, 0x1u, 0x0u, 0x7u, 0xAABBCCDDu,
                                       0x1234, 0x0, 0x1u, 0x0ULL, 0xFFu);
    TEST_ASSERT(cmd != NULL, "AtomicGds should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_ATOMIC_GDS, "AtomicGds opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 11, "AtomicGds length");
    /* cmd[1] = (op<<30)|(gds_op<<28)|(src&0x7f)|(loop_count&1)<<7|
     *          (data&1)<<16|0x40000000 */
    TEST_ASSERT_EQ(cmd[1], 0x40010087u, "AtomicGds control word");
    /* cmd[2] = (offset<<20)|index; 0x1234<<20 overflows uint32 to 0x23400000 */
    TEST_ASSERT_EQ(cmd[2], 0x23400000u, "AtomicGds offset/index");
    TEST_ASSERT_EQ(cmd[9], 0xAABBCCDDu, "AtomicGds data");
    TEST_ASSERT_EQ(cmd[10], 0x1u, "AtomicGds loop_count");
}

static void test_dcb_context_state_op(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t data[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    /* op=0 → CLEAR_STATE_AGC, 2 dwords, cmd[1]=reg_offset & 0xF */
    uint32_t *cmd = sceAgcDcbContextStateOp(&cb, 0, 0, 0x200u, 4, data);
    TEST_ASSERT(cmd != NULL, "ContextStateOp should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_CLEAR_STATE_AGC, "ContextStateOp op=0 opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2, "ContextStateOp op=0 length");
    TEST_ASSERT_EQ(cmd[1], 0x0u, "ContextStateOp op=0 reg_offset & 0xF");
}

static void test_dcb_reset_queue(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbResetQueue(&cb, 0x5u);
    TEST_ASSERT(cmd != NULL, "ResetQueue should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_UCONFIG_REG, "ResetQueue opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "ResetQueue length");
    TEST_ASSERT_EQ(cmd[1], 0x5u, "ResetQueue queue_id");
    TEST_ASSERT_EQ(cmd[2], 0x0u, "ResetQueue reserved");
}

static void test_dcb_set_preemption(void) {
    uint32_t buf[64];
    int32_t r = sceAgcDcbSetPreemption(buf, 64, 0x2u);
    TEST_ASSERT_EQ(r, AGC_ERROR_INVALID_STATE, "SetPreemption is not allowed from agc vsh");
}

static void test_dcb_wait_until_safe(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcDcbWaitUntilSafeForRendering(&cb, 0, 0);
    TEST_ASSERT(cmd != NULL, "WaitUntilSafeForRendering should return non-NULL");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "WaitUntilSafeForRendering opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 7, "WaitUntilSafeForRendering length");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_WAIT_FLIP_DONE, "WaitUntilSafeForRendering subcommand");
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
    char owner_name[9] = "sentinel";
    uint64_t address = UINT64_C(0x1111222233334444);
    uint64_t size = UINT64_C(0x5555666677778888);
    uint64_t name = UINT64_C(0x9999aaaabbbbcccc);
    uint64_t user_data = UINT64_C(0xddddeeeeffff0000);
    uint32_t type = UINT32_C(0x12345678);
    const int32_t resource_error =
        (int32_t)AGC_DRIVER_ERROR_RESOURCE_REGISTRATION_UNAVAILABLE;
    const int32_t capture_error = (int32_t)AGC_DRIVER_ERROR_DEBUG_UNAVAILABLE;

    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetOwnerName(
                       7u, owner_name, sizeof(owner_name)),
                   (uint32_t)resource_error, "GetOwnerName status stub");
    TEST_ASSERT_EQ((uint32_t)owner_name[0], (uint32_t)'s',
                   "GetOwnerName preserves output");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetResourceBaseAddressAndSizeInBytes(
                       UINT64_C(0x100), &address, &size),
                   (uint32_t)resource_error, "GetResourceAddress status stub");
    TEST_ASSERT_EQ(address, UINT64_C(0x1111222233334444),
                   "GetResourceAddress preserves address");
    TEST_ASSERT_EQ(size, UINT64_C(0x5555666677778888),
                   "GetResourceAddress preserves size");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetResourceName(
                       UINT64_C(0x200), &name),
                   (uint32_t)resource_error, "GetResourceName status stub");
    TEST_ASSERT_EQ(name, UINT64_C(0x9999aaaabbbbcccc),
                   "GetResourceName preserves output");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetResourceShaderGuid(1u, 2u, 3u, 4u),
                   (uint32_t)resource_error, "GetResourceShaderGuid status stub");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetResourceType(
                       UINT64_C(0x300), &type),
                   (uint32_t)resource_error, "GetResourceType status stub");
    TEST_ASSERT_EQ(type, UINT32_C(0x12345678),
                   "GetResourceType preserves output");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetResourceUserData(
                       UINT64_C(0x400), &user_data),
                   (uint32_t)resource_error, "GetResourceUserData status stub");
    TEST_ASSERT_EQ(user_data, UINT64_C(0xddddeeeeffff0000),
                   "GetResourceUserData preserves output");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverSetResourceUserData(
                       UINT64_C(0x500), 9u),
                   (uint32_t)resource_error, "SetResourceUserData status stub");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverRegisterGdsResource(1u, 2u, 3u, 4u, 5u),
                   (uint32_t)resource_error, "RegisterGdsResource status stub");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverRequestCaptureStart(),
                   (uint32_t)capture_error, "RequestCaptureStart status stub");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverRequestCaptureStop(),
                   (uint32_t)capture_error, "RequestCaptureStop status stub");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverTriggerCapture(),
                   (uint32_t)capture_error, "TriggerCapture status stub");

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

    out_val = 0xDEAD;
    r = sceAgcInit_0090(0, 0, &out_val);
    TEST_ASSERT_EQ(r, AGC_OK, "sceAgcInit_0090 forwards to current ABI");
    TEST_ASSERT_EQ(out_val, 0u, "sceAgcInit_0090 preserves output ABI");

    /* sceAgcInit with invalid init_level */
    r = sceAgcInit(10, 0, NULL);
    TEST_ASSERT(r < 0, "sceAgcInit invalid level fails");

    /* sceAgcSuspendPoint returns OK on generic */
    r = sceAgcSuspendPoint(0, 0, 0, 0);
    TEST_ASSERT_EQ(r, AGC_OK, "sceAgcSuspendPoint returns OK");

    /* CreatePrimState permits a no-output no-op, matching the SPRX. */
    r = sceAgcCreatePrimState(NULL, NULL, NULL, NULL, 5);
    TEST_ASSERT_EQ(r, AGC_OK, "CreatePrimState no-output returns OK");

    /* CreateShader with NULL fails */
    r = sceAgcCreateShader(NULL, 0);
    TEST_ASSERT(r < 0, "CreateShader NULL fails");

    uint32_t packet[3] = {0xC0001000u, 0x12345678u, 0u};
    uint64_t payload = 0;
    r = sceAgcGetDataPacketPayloadAddress_0090(&payload, packet, 0);
    TEST_ASSERT_EQ(r, AGC_OK,
        "GetDataPacketPayloadAddress_0090 returns AGC_OK");
    TEST_ASSERT_EQ(payload, (uint64_t)(uintptr_t)&packet[1],
        "GetDataPacketPayloadAddress_0090 preserves payload address ABI");

    SceAgcMemoryRange payload_range = {0};
    packet[0] = agcPm4Header3(AGC_PM4_OP_NOP, 3);
    r = sceAgcGetDataPacketPayloadRange(&payload_range, packet, 0);
    TEST_ASSERT_EQ(r, AGC_OK, "GetDataPacketPayloadRange type 0");
    TEST_ASSERT(payload_range.base == &packet[1],
        "GetDataPacketPayloadRange type 0 base");
    TEST_ASSERT_EQ(payload_range.size, 8u,
        "GetDataPacketPayloadRange type 0 size");

    r = sceAgcGetDataPacketPayloadRange(&payload_range, packet, 1);
    TEST_ASSERT_EQ(r, AGC_OK, "GetDataPacketPayloadRange type 1");
    TEST_ASSERT(payload_range.base == &packet[2],
        "GetDataPacketPayloadRange type 1 base");
    TEST_ASSERT_EQ(payload_range.size, 4u,
        "GetDataPacketPayloadRange type 1 size");

    packet[0] = 0xFFFF1000u;
    r = sceAgcGetDataPacketPayloadRange(&payload_range, packet, 0);
    TEST_ASSERT_EQ(r, AGC_OK, "GetDataPacketPayloadRange max NOP");
    TEST_ASSERT(payload_range.base == NULL,
        "GetDataPacketPayloadRange max NOP base");
    TEST_ASSERT_EQ(payload_range.size, 0u,
        "GetDataPacketPayloadRange max NOP size");
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

static void test_acb_atomic_gds_0900(void) {
    SceAgcCb cb;
    uint32_t buf[16] = {0};
    uint32_t *cmd;

    agcCbInit(&cb, buf, sizeof(buf));
    cmd = sceAgcAcbAtomicGds_0900(
        &cb, 0x200u, 1u, 1u, 0x2Au, 0x1234u, 0x5678u,
        0xFFFFFFFFu, 0xAABBCCDDu, 0x1122334455667788ULL,
        0x99AABBCCDDEEFF00ULL);
    TEST_ASSERT(cmd == buf, "AcbAtomicGds_0900 returns packet start");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_ATOMIC_GDS,
        "AcbAtomicGds_0900 opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 11u,
        "AcbAtomicGds_0900 length");
    TEST_ASSERT_EQ(cmd[1], 0x00030000u,
        "AcbAtomicGds_0900 packed control");
    TEST_ASSERT_EQ(cmd[2], 0x0000012Au,
        "AcbAtomicGds_0900 operation and source");
    TEST_ASSERT_EQ(cmd[3], 0x1234u, "AcbAtomicGds_0900 offset");
    TEST_ASSERT_EQ(cmd[4], 0x5678u, "AcbAtomicGds_0900 index");
    TEST_ASSERT_EQ(cmd[5], 0x00FF00FFu, "AcbAtomicGds_0900 mask");
    TEST_ASSERT_EQ(cmd[6], 0xAABBCCDDu, "AcbAtomicGds_0900 data");
    TEST_ASSERT_EQ(cmd[7], 0x55667788u, "AcbAtomicGds_0900 compare low");
    TEST_ASSERT_EQ(cmd[8], 0x11223344u, "AcbAtomicGds_0900 compare high");
    TEST_ASSERT_EQ(cmd[9], 0xDDEEFF00u, "AcbAtomicGds_0900 extra low");
    TEST_ASSERT_EQ(cmd[10], 0x99AABBCCu, "AcbAtomicGds_0900 extra high");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 11u,
        "AcbAtomicGds_0900 advances cursor by 11 dwords");
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
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_SET_MARKER,
        "SetMarker uses set subcommand");

    cmd = sceAgcDcbPushMarkerSpan(&cb, "abcdef", 3u, 0x55667788u);
    TEST_ASSERT(cmd != 0, "PushMarkerSpan returns non-NULL");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3u,
        "PushMarkerSpan size follows explicit length");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_PUSH_MARKER,
        "PushMarkerSpan uses push subcommand");
    TEST_ASSERT_EQ(cmd[1], 0x55667788u, "PushMarkerSpan color");
    TEST_ASSERT_EQ(cmd[2], 0x00636261u,
        "PushMarkerSpan copies only explicit bytes and clears padding");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 6u,
        "marker builders advance cursor by exact packet sizes");
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
static void test_batch3_ref_patchers(void);
static void test_batch3_ref_getsize_helpers(void);
static void test_batch3_ref_driver_stubs(void);
static void test_batch3_submit_wrappers(void);

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
    TEST_RUN(test_acb_atomic_gds_0900);
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
    TEST_RUN(test_batch3_ref_patchers);
    TEST_RUN(test_batch3_ref_getsize_helpers);
    TEST_RUN(test_batch3_ref_driver_stubs);
    TEST_RUN(test_batch3_submit_wrappers);
}

/* ===================================================================== */
/* Batch 3: reference-confirmed patchers and helpers                      */
/* ===================================================================== */

static void test_batch3_ref_patchers(void) {
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

    wd[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 8);
    wd[1] = 0xFFFFFFFFu;
    TEST_ASSERT_EQ(sceAgcWriteDataPatchSetCachePolicy(wd, 2u), AGC_OK,
        "WriteData cache-policy patch returns OK");
    TEST_ASSERT_EQ(wd[1], 0xFDFFFFFFu,
        "WriteData cache-policy patch replaces bits 26:25");

    wd[1] = 0xA5A5A5A5u;
    TEST_ASSERT_EQ(sceAgcWriteDataPatchSetDst(wd, 0x1Bu), AGC_OK,
        "WriteData destination patch returns OK");
    TEST_ASSERT_EQ(wd[1],
        (0xA5A5A5A5u & 0x3FFFF0FFu) |
        (((0x1Bu << 30u) | (0x1Bu << 7u)) & 0x40000F00u),
        "WriteData destination patch uses split firmware encoding");
    wd[0] = agcPm4Header3(AGC_PM4_OP_NOP, 8);
    TEST_ASSERT_EQ(sceAgcWriteDataPatchSetCachePolicy(wd, 0u),
        AGC_ERROR_INVALID_ARGUMENT,
        "WriteData cache-policy patch rejects wrong opcode");
    TEST_ASSERT_EQ(sceAgcWriteDataPatchSetDst(wd, 0u),
        AGC_ERROR_INVALID_ARGUMENT,
        "WriteData destination patch rejects wrong opcode");

    uint32_t acquire_patch[8] = {
        agcPm4Header3(AGC_PM4_OP_ACQUIRE_MEM, 8), 0x12345678u
    };
    TEST_ASSERT_EQ(sceAgcAcquireMemSetEngine(acquire_patch, 1u), AGC_OK,
        "AcquireMem engine patch returns OK");
    TEST_ASSERT_EQ(acquire_patch[1], 0x92345678u,
        "AcquireMem engine patch sets bit 31");
    TEST_ASSERT_EQ(sceAgcAcquireMemSetEngine(acquire_patch, 0u), AGC_OK,
        "AcquireMem engine clear returns OK");
    TEST_ASSERT_EQ(acquire_patch[1], 0x12345678u,
        "AcquireMem engine patch clears bit 31");
    acquire_patch[0] = agcPm4Header3(AGC_PM4_OP_NOP, 8);
    TEST_ASSERT_EQ(sceAgcAcquireMemSetEngine(acquire_patch, 1u),
        (int32_t)0x8A6C000Cu,
        "AcquireMem engine patch returns compatibility packet error");

    wd[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 8);
    wd[1] = 0xFFFFFFFFu;
    TEST_ASSERT_EQ(sceAgcAsyncWriteDataPatchSetAddressOrOffset(wd, wd_addr),
        AGC_OK, "Async WriteData address patch returns OK");
    TEST_ASSERT_EQ(wd[2], (uint32_t)wd_addr,
        "Async WriteData address patch sets low dword");
    TEST_ASSERT_EQ(wd[3], (uint32_t)(wd_addr >> 32),
        "Async WriteData address patch sets high dword");
    TEST_ASSERT_EQ(sceAgcAsyncWriteDataPatchSetCachePolicy(wd, 1u), AGC_OK,
        "Async WriteData cache-policy patch returns OK");
    TEST_ASSERT_EQ(wd[1], 0xFBFFFFFFu,
        "Async WriteData cache-policy patch replaces bits 26:25");
    wd[1] = 0xA5A5A5A5u;
    TEST_ASSERT_EQ(sceAgcAsyncWriteDataPatchSetDst(wd, 0x1Bu), AGC_OK,
        "Async WriteData destination patch returns OK");
    TEST_ASSERT_EQ(wd[1], (0xA5A5A5A5u & 0xFFFFF0FFu) | 0xB00u,
        "Async WriteData destination patch replaces only bits 11:8");
    wd[0] = agcPm4Header3(AGC_PM4_OP_NOP, 8);
    TEST_ASSERT_EQ(sceAgcAsyncWriteDataPatchSetAddressOrOffset(wd, 0u),
        (int32_t)0x8A6C000Cu,
        "Async WriteData address patch returns compatibility packet error");
    TEST_ASSERT_EQ(sceAgcAsyncWriteDataPatchSetCachePolicy(wd, 0u),
        (int32_t)0x8A6C000Cu,
        "Async WriteData cache patch returns compatibility packet error");
    TEST_ASSERT_EQ(sceAgcAsyncWriteDataPatchSetDst(wd, 0u),
        (int32_t)0x8A6C000Cu,
        "Async WriteData destination patch returns compatibility packet error");

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

    uint32_t async_cond[10] = {0};
    uint32_t async_target[4] = {0};
    uint64_t async_address = (uint64_t)(uintptr_t)async_target;
    async_cond[0] = agcPm4Header3(AGC_PM4_OP_COND_EXEC, 5);
    async_cond[1] = 2u;
    async_cond[4] = 0xA5A5C000u;
    TEST_ASSERT_EQ(sceAgcAsyncCondExecPatchSetCommandAddress(
        async_cond, async_target), AGC_OK, "AsyncCondExec command address");
    TEST_ASSERT_EQ(async_cond[1], ((uint32_t)async_address & ~3u) | 2u,
        "AsyncCondExec command low");
    TEST_ASSERT_EQ(async_cond[2], (uint32_t)(async_address >> 32),
        "AsyncCondExec command high");
    TEST_ASSERT_EQ(sceAgcAsyncCondExecPatchSetEnd(
        async_cond, async_cond + 8), AGC_OK, "AsyncCondExec end");
    TEST_ASSERT_EQ(async_cond[4], 0xA5A5C003u, "AsyncCondExec end count");

    uint32_t branch[14] = {0};
    branch[0] = agcPm4Header3(AGC_PM4_OP_INDIRECT_BUFFER, 14);
    branch[2] = 5u;
    TEST_ASSERT_EQ(sceAgcBranchPatchSetCompareAddress(
        branch, 0x1122334455667788ull), AGC_OK, "Branch compare address");
    TEST_ASSERT_EQ(branch[2], 0x5566778Du, "Branch compare low and flags");
    TEST_ASSERT_EQ(branch[3], 0x11223344u, "Branch compare high");

    uint32_t rewind[2] = {
        agcPm4Header3(AGC_PM4_OP_REWIND, 2), 0x12345678u
    };
    TEST_ASSERT_EQ(sceAgcRewindPatchSetRewindState(rewind, 1), AGC_OK,
        "Rewind state");
    TEST_ASSERT_EQ(rewind[1], 0x92345678u, "Rewind state bit");
    TEST_ASSERT_EQ(sceAgcAsyncRewindPatchSetRewindState(rewind, 0), AGC_OK,
        "Async rewind state");
    TEST_ASSERT_EQ(rewind[1], 0x12345678u, "Async rewind state bit");

    uint32_t release[7] = {0};
    release[0] = agcPm4Header3(AGC_PM4_OP_RELEASE_MEM, 7);
    release[1] = 0x89ABCDEFu;
    release[2] = 0x01234567u;
    uint64_t release_fields = (uint64_t)release[1] |
        ((uint64_t)release[2] << 32);
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchGcrCntl(
        release, 0xA55u), AGC_OK, "Queue EOP GCR control");
    release_fields = (release_fields & ~(0xFFFull << 12)) | (0xA55ull << 12);
    TEST_ASSERT_EQ(release[1], (uint32_t)release_fields, "Queue EOP GCR low");
    TEST_ASSERT_EQ(release[2], (uint32_t)(release_fields >> 32),
        "Queue EOP GCR high");

    release_fields = (uint64_t)release[1] | ((uint64_t)release[2] << 32);
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchType(
        release, 0x2Fu), AGC_OK, "Queue EOP event type");
    release_fields = (release_fields & ~0xF3Full) | 0x62Full;
    TEST_ASSERT_EQ(release[1], (uint32_t)release_fields, "Queue EOP type low");
    TEST_ASSERT_EQ(release[2], (uint32_t)(release_fields >> 32),
        "Queue EOP type high");

    async_cond[0] = agcPm4Header3(AGC_PM4_OP_NOP, 5);
    TEST_ASSERT_EQ(sceAgcAsyncCondExecPatchSetEnd(async_cond, async_cond + 8),
        AGC_ERROR_INVALID_ARGUMENT, "AsyncCondExec rejects wrong opcode");
    rewind[0] = agcPm4Header3(AGC_PM4_OP_NOP, 2);
    TEST_ASSERT_EQ(sceAgcRewindPatchSetRewindState(rewind, 1),
        AGC_ERROR_INVALID_ARGUMENT, "Rewind rejects wrong opcode");
    release[0] = agcPm4Header3(AGC_PM4_OP_NOP, 7);
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchGcrCntl(release, 0),
        AGC_ERROR_INVALID_ARGUMENT, "Queue EOP rejects wrong opcode");
}

static void test_batch3_ref_getsize_helpers(void) {
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

#define CHECK_FIXED_GET_SIZE(name, expected) \
    TEST_ASSERT_EQ(name(), expected, #name)
    CHECK_FIXED_GET_SIZE(sceAgcAcbAtomicGdsGetSize, 44u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbAtomicMemGetSize, 36u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbCopyDataGetSize, 24u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbDispatchIndirectGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbDmaDataGetSize, 28u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbEventWriteGetSize, 8u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbJumpGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbPrimeUtcl2GetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbQueueEndOfShaderActionGetSize, 32u);
    CHECK_FIXED_GET_SIZE(sceAgcAcbRewindGetSize, 8u);
    CHECK_FIXED_GET_SIZE(sceAgcCbBranchGetSize, 56u);
    CHECK_FIXED_GET_SIZE(sceAgcCbCondWriteGetSize, 36u);
    CHECK_FIXED_GET_SIZE(sceAgcCbDispatchGetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbAtomicGdsGetSize, 44u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbAtomicMemGetSize, 36u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbCopyDataGetSize, 24u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbDispatchIndirectGetSize, 12u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbDmaDataGetSize, 28u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbDrawIndexAutoGetSize, 12u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbDrawIndexGetSize, 24u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbDrawIndexIndirectGetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbDrawIndexOffsetGetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbDrawIndirectGetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbEndOcclusionQueryGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbPrimeUtcl2GetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbQueueEndOfShaderActionGetSize, 32u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetBaseDispatchIndirectArgsGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetBaseDrawIndirectArgsGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetBoolPredicationEnableGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetCxRegisterDirectGetSize, 12u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetCxRegistersIndirectGetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetIndexBufferGetSize, 12u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetIndexCountGetSize, 8u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetIndexIndirectArgsGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetIndexSizeGetSize, 12u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetNumInstancesGetSize, 8u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetPredicationDisableGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetShRegisterDirectGetSize, 12u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetShRegistersIndirectGetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetUcRegisterDirectGetSize, 12u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetUcRegistersIndirectGetSize, 20u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbSetZPassPredicationEnableGetSize, 16u);
    CHECK_FIXED_GET_SIZE(sceAgcDcbStallCommandBufferParserGetSize, 8u);
#undef CHECK_FIXED_GET_SIZE

    TEST_ASSERT_EQ(sceAgcCbNopGetSize(0), 0u, "CbNopGetSize zero");
    TEST_ASSERT_EQ(sceAgcCbNopGetSize(7), 28u, "CbNopGetSize normal");
    TEST_ASSERT_EQ(sceAgcCbNopGetSize(0x40000001u), 4u,
        "CbNopGetSize wraps as FW 5.50 uint32 arithmetic");

#define CHECK_RANGE_GET_SIZE(name) \
    TEST_ASSERT_EQ(name(0), 8u, #name " zero"); \
    TEST_ASSERT_EQ(name(7), 36u, #name " normal"); \
    TEST_ASSERT_EQ(name(0x3FFFFFFFu), 4u, #name " wraps")
    CHECK_RANGE_GET_SIZE(sceAgcCbSetShRegisterRangeDirectGetSize);
    CHECK_RANGE_GET_SIZE(sceAgcCbSetUcRegisterRangeDirectGetSize);
#undef CHECK_RANGE_GET_SIZE

#define CHECK_LIST_GET_SIZE(name) \
    TEST_ASSERT_EQ(name(0), 0u, #name " zero"); \
    TEST_ASSERT_EQ(name(7), 84u, #name " normal"); \
    TEST_ASSERT_EQ(name(0x40000001u), 12u, #name " wraps")
    CHECK_LIST_GET_SIZE(sceAgcCbSetShRegistersDirectGetSize);
    CHECK_LIST_GET_SIZE(sceAgcCbSetUcRegistersDirectGetSize);
#undef CHECK_LIST_GET_SIZE

    TEST_ASSERT_EQ(sceAgcDriverUserDataGetPacketSize(0), 3u,
        "UserDataGetPacketSize empty");
    TEST_ASSERT_EQ(sceAgcDriverUserDataGetPacketSize(1), 4u,
        "UserDataGetPacketSize one byte");
    TEST_ASSERT_EQ(sceAgcDriverUserDataGetPacketSize(4), 4u,
        "UserDataGetPacketSize one dword");
    TEST_ASSERT_EQ(sceAgcDriverUserDataGetPacketSize(5), 9u,
        "UserDataGetPacketSize two dwords");
    TEST_ASSERT_EQ(sceAgcDriverUserDataGetPacketSize(UINT32_MAX), 0x40000007u,
        "UserDataGetPacketSize uint32 boundary");

    /* test_game_compat_init ran first and selected the no-workaround mode. */
    TEST_ASSERT_EQ(sceAgcAcbAcquireMemGetSize(), 64u,
        "AcbAcquireMemGetSize default FW 5.50 title mode");
    TEST_ASSERT_EQ(sceAgcDcbAcquireMemGetSize(), 64u,
        "DcbAcquireMemGetSize default FW 5.50 title mode");

    TEST_ASSERT_EQ(sceAgcAcbWaitOnAddressGetSize(0), 56u,
        "AcbWaitOnAddressGetSize 32-bit");
    TEST_ASSERT_EQ(sceAgcAcbWaitOnAddressGetSize(1), 64u,
        "AcbWaitOnAddressGetSize 64-bit");
    TEST_ASSERT_EQ(sceAgcAcbWaitOnAddressGetSize(2), 0u,
        "AcbWaitOnAddressGetSize invalid");
    TEST_ASSERT_EQ(sceAgcAcbWaitOnAddressGetSize(0x100), 56u,
        "AcbWaitOnAddressGetSize FW 5.50 low-byte selector");

    TEST_ASSERT_EQ(sceAgcDcbBeginOcclusionQueryGetSize(0), 16u,
        "BeginOcclusionQueryGetSize simple");
    TEST_ASSERT_EQ(sceAgcDcbBeginOcclusionQueryGetSize(1), 288u,
        "BeginOcclusionQueryGetSize extended");
    TEST_ASSERT_EQ(sceAgcDcbBeginOcclusionQueryGetSize(2), 0u,
        "BeginOcclusionQueryGetSize invalid");
    TEST_ASSERT_EQ(sceAgcDcbBeginOcclusionQueryGetSize(0x101), 288u,
        "BeginOcclusionQueryGetSize FW 5.50 low-byte selector");

    TEST_ASSERT_EQ(sceAgcDcbContextStateOpGetSize(0), 20u,
        "ContextStateOpGetSize op0");
    TEST_ASSERT_EQ(sceAgcDcbContextStateOpGetSize(1), 108u,
        "ContextStateOpGetSize op1");
    TEST_ASSERT_EQ(sceAgcDcbContextStateOpGetSize(2), 108u,
        "ContextStateOpGetSize op2");
    TEST_ASSERT_EQ(sceAgcDcbContextStateOpGetSize(3), 128u,
        "ContextStateOpGetSize op3");
    TEST_ASSERT_EQ(sceAgcDcbContextStateOpGetSize(4), 0u,
        "ContextStateOpGetSize invalid");

    TEST_ASSERT_EQ(sceAgcDcbDrawIndirectMultiGetSize(), 64u,
        "DrawIndirectMultiGetSize");
    TEST_ASSERT_EQ(sceAgcDcbDrawIndexIndirectMultiGetSize(), 64u,
        "DrawIndexIndirectMultiGetSize");
    TEST_ASSERT_EQ(sceAgcDcbDrawIndexMultiInstancedGetSize(), 60u,
        "DrawIndexMultiInstancedGetSize");

    TEST_ASSERT_EQ(sceAgcDcbEventWriteGetSize(0x37), 8u,
        "EventWriteGetSize normal");
    TEST_ASSERT_EQ(sceAgcDcbEventWriteGetSize(0x38), 16u,
        "EventWriteGetSize address event even");
    TEST_ASSERT_EQ(sceAgcDcbEventWriteGetSize(0x39), 16u,
        "EventWriteGetSize address event odd");
    TEST_ASSERT_EQ(sceAgcDcbEventWriteGetSize(0x138), 16u,
        "EventWriteGetSize FW 5.50 low-byte event");
}

static void test_batch3_ref_driver_stubs(void) {
    /* IsCaptureInProgress: returns 0 */
    TEST_ASSERT_EQ(sceAgcDriverIsCaptureInProgress(), 0, "IsCaptureInProgress returns 0");
    TEST_ASSERT_EQ(sceAgcDriverIsSubmitValidationEnabled(), 0,
        "IsSubmitValidationEnabled returns 0");
    TEST_ASSERT_EQ(sceAgcDriverIsTraceInProgress(), 0,
        "IsTraceInProgress returns 0");
    TEST_ASSERT_EQ(sceAgcDriverGetShaderDebuggingStatus(), 1,
        "GetShaderDebuggingStatus returns 1");

    uint32_t validation_mode = 0xA5A5A5A5u;
    uint32_t validation_config = 0x5A5A5A5Au;
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverSetSubmitValidationMode(2u),
        AGC_DRIVER_ERROR_DEBUG_UNAVAILABLE,
        "SetSubmitValidationMode returns compatibility stub error");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetSubmitValidationMode(&validation_mode),
        AGC_DRIVER_ERROR_DEBUG_UNAVAILABLE,
        "GetSubmitValidationMode returns compatibility stub error");
    TEST_ASSERT_EQ(validation_mode, 0xA5A5A5A5u,
        "GetSubmitValidationMode leaves output untouched");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverSetSubmitValidationConfig(&validation_config),
        AGC_DRIVER_ERROR_DEBUG_UNAVAILABLE,
        "SetSubmitValidationConfig returns compatibility stub error");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverGetSubmitValidationConfig(&validation_config),
        AGC_DRIVER_ERROR_DEBUG_UNAVAILABLE,
        "GetSubmitValidationConfig returns compatibility stub error");
    TEST_ASSERT_EQ(validation_config, 0x5A5A5A5Au,
        "GetSubmitValidationConfig leaves output untouched");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverSetValidationErrorOutputFrequency(1u),
        AGC_DRIVER_ERROR_DEBUG_UNAVAILABLE,
        "SetValidationErrorOutputFrequency returns compatibility stub error");

    /* GetDefaultOwner: returns 0 */
    TEST_ASSERT_EQ(sceAgcDriverGetDefaultOwner(), 0u, "GetDefaultOwner returns 0");

    /* GetResourceRegistrationMaxNameLength: returns 32 */
    TEST_ASSERT_EQ(sceAgcDriverGetResourceRegistrationMaxNameLength(), 32u,
        "GetResourceRegistrationMaxNameLength returns 32");

    /* DeleteEqEvent: stub returns NOT_SUPPORTED */
    TEST_ASSERT_EQ(sceAgcDriverDeleteEqEvent(NULL), AGC_ERROR_NOT_SUPPORTED,
        "DeleteEqEvent returns NOT_SUPPORTED");

    /* GetEqEventType: stub returns NOT_SUPPORTED */
    uint32_t eq_type = 0xDEAD;
    TEST_ASSERT_EQ(sceAgcDriverGetEqEventType(NULL, &eq_type), AGC_ERROR_NOT_SUPPORTED,
        "GetEqEventType returns NOT_SUPPORTED");

    /* InitResourceRegistration: stub returns NOT_SUPPORTED */
    TEST_ASSERT_EQ(sceAgcDriverInitResourceRegistration(), AGC_ERROR_NOT_SUPPORTED,
        "InitResourceRegistration returns NOT_SUPPORTED");

    /* QueryResourceRegistrationUserMemoryRequirements: stub returns NOT_SUPPORTED */
    TEST_ASSERT_EQ(sceAgcDriverQueryResourceRegistrationUserMemoryRequirements(NULL),
        AGC_ERROR_NOT_SUPPORTED, "QueryResourceRegistration returns NOT_SUPPORTED");

    /* UnregisterResource: stub returns NOT_SUPPORTED */
    TEST_ASSERT_EQ(sceAgcDriverUnregisterResource(0), AGC_ERROR_NOT_SUPPORTED,
        "UnregisterResource returns NOT_SUPPORTED");

    uint8_t workload_stream[32] = {0xA5u};
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverRegisterWorkloadStream(0, workload_stream),
        AGC_DRIVER_ERROR_INVALID_VALUE,
        "RegisterWorkloadStream rejects stream zero");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverRegisterWorkloadStream(1, NULL),
        AGC_DRIVER_ERROR_INVALID_ARGUMENT,
        "RegisterWorkloadStream rejects null record");
    TEST_ASSERT_EQ(sceAgcDriverRegisterWorkloadStream(1, workload_stream), AGC_OK,
        "RegisterWorkloadStream stores valid record");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverRegisterWorkloadStream(1, workload_stream),
        AGC_DRIVER_ERROR_INVALID_VALUE,
        "RegisterWorkloadStream rejects duplicate ID");
    TEST_ASSERT_EQ(sceAgcDriverUnregisterWorkloadStream(1), AGC_OK,
        "UnregisterWorkloadStream removes valid record");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverUnregisterWorkloadStream(1),
        AGC_DRIVER_ERROR_NOT_REGISTERED,
        "UnregisterWorkloadStream rejects missing record");
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverUnregisterWorkloadStream(32),
        AGC_DRIVER_ERROR_INVALID_VALUE,
        "UnregisterWorkloadStream rejects out-of-range ID");

    TEST_ASSERT_EQ(sceAgcDriverRegisterDefaultOwner(7u),
        AGC_ERROR_NOT_SUPPORTED,
        "RegisterDefaultOwner matches FW 5.50 stub");
    TEST_ASSERT_EQ(sceAgcDriverUnregisterAllResourcesForOwner(7u),
        AGC_ERROR_NOT_SUPPORTED,
        "UnregisterAllResourcesForOwner matches FW 5.50 stub");
    TEST_ASSERT_EQ(sceAgcDriverUnregisterOwnerAndResources(7u),
        AGC_ERROR_NOT_SUPPORTED,
        "UnregisterOwnerAndResources matches FW 5.50 stub");

    uint8_t is_trinity = 1u;
    sceAgcGetIsTrinityMode(&is_trinity);
    TEST_ASSERT_EQ(is_trinity, 0u,
        "GetIsTrinityMode reports standard FW 5.50 hardware");

    TEST_ASSERT_EQ(sceAgcGetShaderInstrumentation(), 0u,
        "GetShaderInstrumentation defaults to zero");
    TEST_ASSERT_EQ(sceAgcSetShaderInstrumentation(0xa5u), AGC_OK,
        "SetShaderInstrumentation succeeds");
    TEST_ASSERT_EQ(sceAgcGetShaderInstrumentation(), 0xa5u,
        "GetShaderInstrumentation returns stored flags");

    static uint8_t semaphore_storage[0x8000u];
    uintptr_t semaphore_address = ((uintptr_t)semaphore_storage + 0x3fffu) &
        ~(uintptr_t)0x3fffu;
    void *label0 = NULL;
    void *label1 = NULL;

    TEST_ASSERT_EQ((uint32_t)sceAgcGetSemaphoreLabel(0u, &label0),
        0x8a6c0049u, "GetSemaphoreLabel rejects uninitialized memory");
    TEST_ASSERT_EQ((uint32_t)sceAgcSetAmmSemaphoreMemory(
        (void *)(semaphore_address + 1u), 0x4000u), 0x8a6c0002u,
        "SetAmmSemaphoreMemory enforces 16 KiB alignment");
    TEST_ASSERT_EQ(sceAgcSetAmmSemaphoreMemory(
        (void *)semaphore_address, 0x4000u), AGC_OK,
        "SetAmmSemaphoreMemory accepts aligned storage");
    TEST_ASSERT_EQ(sceAgcGetSemaphoreLabel(0u, &label0), AGC_OK,
        "GetSemaphoreLabel returns label zero");
    TEST_ASSERT(label0 && *(uint8_t *)label0 == 0,
        "SetAmmSemaphoreMemory clears label storage");
    TEST_ASSERT_EQ(sceAgcGetSemaphoreLabel(1u, &label1), AGC_OK,
        "GetSemaphoreLabel returns label one");
    TEST_ASSERT_EQ((uintptr_t)label1 - (uintptr_t)label0, 32u,
        "GetSemaphoreLabel uses the FW 32-byte stride");
    TEST_ASSERT_EQ((uint32_t)sceAgcGetSemaphoreLabel(512u, &label1),
        0x8a6c000bu, "GetSemaphoreLabel rejects an out-of-range index");
    TEST_ASSERT_EQ((uint32_t)sceAgcSetAmmSemaphoreMemory(
        (void *)semaphore_address, 0x4000u), 0x8a6c0048u,
        "SetAmmSemaphoreMemory rejects duplicate initialization");
}

static void test_batch3_submit_wrappers(void) {
    TEST_ASSERT_EQ((uint32_t)sceAgcDriverAgrSubmitMultiDcbs(NULL, NULL, 0),
        AGC_DRIVER_ERROR_AGR_NOT_INITIALIZED,
        "AgrSubmitMultiDcbs reports uninitialized AGR");

    /* SubmitMultiDcbs with count=0 returns OK */
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiDcbs(NULL, NULL, 0), AGC_OK,
        "SubmitMultiDcbs count=0 returns OK");

    /* SubmitMultiDcbs with null arrays returns INVALID_ARGUMENT */
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiDcbs(NULL, NULL, 2), AGC_ERROR_INVALID_ARGUMENT,
        "SubmitMultiDcbs null arrays returns INVALID_ARGUMENT");

    /* SubmitCommandBuffer with null dcb returns OK (reference skips null) */
    TEST_ASSERT_EQ(sceAgcDriverSubmitCommandBuffer(0, NULL, 0), AGC_OK,
        "SubmitCommandBuffer null dcb returns OK");

    /* SubmitMultiCommandBuffers with count=0 returns OK */
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiCommandBuffers(0, NULL, NULL, 0), AGC_OK,
        "SubmitMultiCommandBuffers count=0 returns OK");

    /* SubmitMultiCommandBuffers with null arrays returns INVALID_ARGUMENT */
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiCommandBuffers(0, NULL, NULL, 2),
        AGC_ERROR_INVALID_ARGUMENT,
        "SubmitMultiCommandBuffers null arrays returns INVALID_ARGUMENT");

    /* SubmitMultiAcbs with count=0 returns OK */
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiAcbs(0, NULL, NULL, 0), AGC_OK,
        "SubmitMultiAcbs count=0 returns OK");

    /* SubmitMultiAcbs with null arrays returns INVALID_ARGUMENT */
    TEST_ASSERT_EQ(sceAgcDriverSubmitMultiAcbs(0, NULL, NULL, 2),
        AGC_ERROR_INVALID_ARGUMENT,
        "SubmitMultiAcbs null arrays returns INVALID_ARGUMENT");
}
