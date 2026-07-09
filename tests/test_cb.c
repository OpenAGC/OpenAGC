#include "test.h"
#include "agc_cb.h"
#include "agc_pm4.h"
#include "agcdriver.h"

static void test_cb_layout_offsets(void) {
    TEST_ASSERT_EQ(offsetof(SceAgcCb, cursor_up), 0x10, "cursor_up offset");
    TEST_ASSERT_EQ(offsetof(SceAgcCb, cursor_down), 0x18, "cursor_down offset");
    TEST_ASSERT_EQ(offsetof(SceAgcCb, callback), 0x20, "callback offset");
    TEST_ASSERT_EQ(offsetof(SceAgcCb, reserved_dw), 0x30, "reserved_dw offset");
    TEST_ASSERT_EQ(sizeof(SceAgcCb), 0x38, "SceAgcCb size");
}

static void test_cb_alloc(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* first = agcCbAllocDwords(&cb, 4);
    TEST_ASSERT(first == buffer, "first allocation starts at buffer");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 4, "used dwords after alloc");
    TEST_ASSERT_EQ(agcCbCapacityDwords(&cb), 16, "capacity dwords");
    TEST_ASSERT_EQ(agcCbRemainingDwords(&cb), 12, "remaining dwords");
}

static void test_sce_agc_cb_nop(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcCbNop(&cb, 3);
    TEST_ASSERT(cmd == buffer, "Nop returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "Nop opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_ZERO, "Nop subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "Nop length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 3, "Nop advances cursor");
}

static void test_sce_agc_cb_dispatch(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcCbDispatch(&cb, 2, 3, 4, 0);
    TEST_ASSERT(cmd == buffer, "Dispatch returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DISPATCH_DIRECT, "Dispatch opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5, "Dispatch length");
    TEST_ASSERT_EQ(cmd[1], 2, "Dispatch x");
    TEST_ASSERT_EQ(cmd[2], 3, "Dispatch y");
    TEST_ASSERT_EQ(cmd[3], 4, "Dispatch z");
    TEST_ASSERT_EQ(cmd[4], 0x41, "Dispatch initiator");
}

static void test_sce_agc_cb_set_sh_registers(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    AgcRegisterValue regs[2] = {
        { .offset = 0x20C, .value = 0x1234 },
        { .offset = 0x20D, .value = 0x5678 },
    };
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcCbSetShRegistersDirect(&cb, regs, 2);
    TEST_ASSERT(cmd == buffer, "SetShRegisters returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_SH_REG, "SetSh opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "SetSh length");
    TEST_ASSERT_EQ(cmd[1], 0x20C, "SetSh start offset");
    TEST_ASSERT_EQ(cmd[2], 0x1234, "SetSh value 0");
    TEST_ASSERT_EQ(cmd[3], 0x5678, "SetSh value 1");
}

static void test_sce_agc_cb_set_cx_registers_direct(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    AgcRegisterValue regs[3] = {
        { .offset = 0x200, .value = 0xAABB },
        { .offset = 0x201, .value = 0xCCDD },
        { .offset = 0x202, .value = 0xEEFF },
    };
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcCbSetCxRegistersDirect(&cb, regs, 3);
    TEST_ASSERT(cmd == buffer, "SetCxRegistersDirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_CONTEXT_REG, "SetCx opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5, "SetCx length (3 regs + 2 header)");
    TEST_ASSERT_EQ(cmd[1], 0x200, "SetCx start offset");
    TEST_ASSERT_EQ(cmd[2], 0xAABB, "SetCx value 0");
    TEST_ASSERT_EQ(cmd[3], 0xCCDD, "SetCx value 1");
    TEST_ASSERT_EQ(cmd[4], 0xEEFF, "SetCx value 2");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 5, "SetCx advances cursor by 5");
}

static void test_sce_agc_cb_set_cx_registers_direct_rejects_invalid(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* NULL registers */
    TEST_ASSERT(sceAgcCbSetCxRegistersDirect(&cb, NULL, 4) == 0,
        "SetCx rejects NULL registers");
    /* Zero count */
    AgcRegisterValue regs[1] = { { .offset = 0x200, .value = 0x1 } };
    TEST_ASSERT(sceAgcCbSetCxRegistersDirect(&cb, regs, 0) == 0,
        "SetCx rejects zero count");
    /* Excessive count */
    TEST_ASSERT(sceAgcCbSetCxRegistersDirect(&cb, regs, 0x3FFF) == 0,
        "SetCx rejects excessive count");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0, "SetCx rejects do not advance cursor");
}

static void test_sce_agc_dcb_write_data(void) {
    uint32_t buffer[32];
    uint32_t data[2] = {0xAABBCCDD, 0x11223344};
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbWriteData(&cb, 1, 2, 0x100000020ULL, data, 2, 1, 0);
    TEST_ASSERT(cmd == buffer, "WriteData returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "WriteData opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_WRITE_DATA, "WriteData subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "WriteData length");
    TEST_ASSERT_EQ(cmd[2], 0x20, "WriteData address lo");
    TEST_ASSERT_EQ(cmd[3], 0x1, "WriteData address hi");
    TEST_ASSERT_EQ(cmd[4], data[0], "WriteData payload 0");
    TEST_ASSERT_EQ(cmd[5], data[1], "WriteData payload 1");
}

static void test_sce_agc_dcb_wait_reg_mem(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbWaitRegMem(&cb, 0, 3, 0, 0, 0x100000020ULL, 0x55, 0xFF, 400);
    TEST_ASSERT(cmd == buffer, "WaitRegMem returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "WaitRegMem wrapper opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_WAIT_MEM32, "WaitRegMem32 subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "WaitRegMem32 length");
    TEST_ASSERT_EQ(cmd[4], 3, "WaitRegMem compare/op control");
    TEST_ASSERT_EQ(cmd[5], 0x55, "WaitRegMem reference");
}

static void test_sce_agc_dcb_markers_and_flip(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* push = sceAgcDcbPushMarker(&cb, "abc");
    TEST_ASSERT(push == buffer, "PushMarker returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(push[0]), AGC_PM4_SUB_PUSH_MARKER, "PushMarker subcommand");

    uint32_t* pop = sceAgcDcbPopMarker(&cb);
    TEST_ASSERT(pop != NULL, "PopMarker returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(pop[0]), AGC_PM4_SUB_POP_MARKER, "PopMarker subcommand");

    uint32_t* flip = sceAgcDcbSetFlip(&cb, 7, 2, 1, 0x1122334455667788ULL);
    TEST_ASSERT(flip != NULL, "SetFlip returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(flip[0]), AGC_PM4_SUB_FLIP, "SetFlip subcommand");
    TEST_ASSERT_EQ(agcPm4Length(flip[0]), 6, "SetFlip length");
    TEST_ASSERT_EQ(flip[1], 7, "SetFlip handle");
    TEST_ASSERT_EQ(flip[2], 2, "SetFlip display buffer");
    TEST_ASSERT_EQ(flip[3], 1, "SetFlip mode");
}

static void test_sce_agc_dcb_dma_data(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbDmaData(&cb, 1, 2, 3, 0x100000020ULL, 4,
        5, 0x200000040ULL, 64, 6, 7, 8);
    TEST_ASSERT(cmd == buffer, "DmaData returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "DmaData wrapper opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_DMA_DATA, "DmaData subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8, "DmaData length");
    TEST_ASSERT_EQ(cmd[3], 64, "DmaData byte count");
    TEST_ASSERT_EQ(cmd[4], 0x20, "DmaData dst lo");
    TEST_ASSERT_EQ(cmd[5], 0x1, "DmaData dst hi");
    TEST_ASSERT_EQ(cmd[6], 0x40, "DmaData src lo");
    TEST_ASSERT_EQ(cmd[7], 0x2, "DmaData src hi");
}

static void test_sce_agc_dcb_indirect_and_index(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* base = sceAgcDcbSetBaseIndirectArgs(&cb, 2, 0x100000027ULL);
    TEST_ASSERT(base == buffer, "SetBaseIndirectArgs returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(base[0]), AGC_PM4_OP_SET_BASE, "SetBase opcode");
    TEST_ASSERT_EQ(agcPm4Length(base[0]), 4, "SetBase length");
    TEST_ASSERT_EQ(base[2], 0x20, "SetBase aligned address lo");

    uint32_t* dispatch = sceAgcDcbDispatchIndirect(&cb, 0x30, 0);
    TEST_ASSERT(dispatch != NULL, "DispatchIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(dispatch[0]), AGC_PM4_OP_DISPATCH_INDIRECT, "DispatchIndirect opcode");
    TEST_ASSERT_EQ(agcPm4Length(dispatch[0]), 3, "DispatchIndirect length");
    TEST_ASSERT_EQ(dispatch[1], 0x30, "DispatchIndirect data offset");

    uint32_t* index = sceAgcDcbSetIndexBuffer(&cb, 0x300000080ULL, 123);
    TEST_ASSERT(index != NULL, "SetIndexBuffer returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(index[0]), AGC_PM4_OP_INDEX_BASE, "Index base opcode");
    TEST_ASSERT_EQ(agcPm4Opcode(index[3]), AGC_PM4_OP_INDEX_BUFFER_SIZE, "Index size opcode");
    TEST_ASSERT_EQ(index[4], 123, "Index count");
}

static void test_sce_agc_dcb_draw_packets(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* offset = sceAgcDcbDrawIndexOffset(&cb, 4, 12, 0xFFFFFFFFu);
    TEST_ASSERT(offset == buffer, "DrawIndexOffset returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(offset[0]), AGC_PM4_OP_DRAW_INDEX_OFFSET_2, "DrawIndexOffset opcode");
    TEST_ASSERT_EQ(agcPm4Length(offset[0]), 5, "DrawIndexOffset length");
    TEST_ASSERT_EQ(offset[1], 12, "DrawIndexOffset count 0");
    TEST_ASSERT_EQ(offset[2], 4, "DrawIndexOffset offset");
    TEST_ASSERT_EQ(offset[4], 0xE0000001u, "DrawIndexOffset flag mask");

    uint32_t* auto_draw = sceAgcDcbDrawIndexAuto(&cb, 6, 0x40000000u);
    TEST_ASSERT(auto_draw != NULL, "DrawIndexAuto returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(auto_draw[0]), AGC_PM4_OP_NOP, "DrawIndexAuto wrapper opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(auto_draw[0]), AGC_PM4_SUB_DRAW_INDEX_AUTO, "DrawIndexAuto subcommand");
    TEST_ASSERT_EQ(agcPm4Length(auto_draw[0]), 7, "DrawIndexAuto length");
    TEST_ASSERT_EQ(auto_draw[1], 6, "DrawIndexAuto count");
}

static void test_sce_agc_dcb_wait_safe(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbWaitUntilSafeForRendering(&cb, 5, 1);
    TEST_ASSERT(cmd == buffer, "WaitUntilSafe returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_WAIT_FLIP_DONE, "WaitUntilSafe subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 7, "WaitUntilSafe length");
    TEST_ASSERT_EQ(cmd[1], 5, "WaitUntilSafe handle");
    TEST_ASSERT_EQ(cmd[2], 1, "WaitUntilSafe display buffer");
}

static void test_sce_agc_cb_release_mem(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* action=0x12 cache_policy=0x07 gcr=0x3456 data_sel=3 interrupt=2
     * dst=0x1_0000_0020 data=0xAABBCCDD_11223344 ctx_id=0xCAFE
     * → [1]=0x0712 [2]=0x02033456 [3]=0x20 [4]=0x1
     *   [5]=0x11223344 [6]=0xAABBCCDD [7]=0xCAFE */
    uint32_t* cmd = sceAgcCbReleaseMem(
        &cb, 0x12, 0x3456, 1, 0x07, 0x100000020ULL, 3,
        0xAABBCCDD11223344ULL, 0, 1, 2, 0xCAFE);
    TEST_ASSERT(cmd == buffer, "ReleaseMem returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_NOP, "ReleaseMem wrapper opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_RELEASE_MEM, "ReleaseMem subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8, "ReleaseMem length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 8, "ReleaseMem advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x0712u, "ReleaseMem action|cache_policy");
    TEST_ASSERT_EQ(cmd[2], 0x02033456u, "ReleaseMem gcr|data_sel|interrupt");
    TEST_ASSERT_EQ(cmd[3], 0x20u, "ReleaseMem dst lo");
    TEST_ASSERT_EQ(cmd[4], 0x1u, "ReleaseMem dst hi");
    TEST_ASSERT_EQ(cmd[5], 0x11223344u, "ReleaseMem data lo");
    TEST_ASSERT_EQ(cmd[6], 0xAABBCCDDu, "ReleaseMem data hi");
    TEST_ASSERT_EQ(cmd[7], 0xCAFEu, "ReleaseMem interrupt context id");
}

static void test_sce_agc_cb_release_mem_rejects_invalid(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    TEST_ASSERT(sceAgcCbReleaseMem(&cb, 0, 0, 2, 0, 0x100, 0, 0, 0, 0, 0, 0) == 0,
        "ReleaseMem rejects destination > 1");
    TEST_ASSERT(sceAgcCbReleaseMem(&cb, 0, 0, 0, 0, 0x100, 4, 0, 0, 0, 0, 0) == 0,
        "ReleaseMem rejects data_selection > 3");
    TEST_ASSERT(sceAgcCbReleaseMem(&cb, 0, 0, 0, 0, 0x100, 0, 0, 1, 0, 0, 0) == 0,
        "ReleaseMem rejects gds_offset != 0");
    TEST_ASSERT(sceAgcCbReleaseMem(&cb, 0, 0, 0, 0, 0x100, 0, 0, 0, 3, 0, 0) == 0,
        "ReleaseMem rejects gds_size > 2");
    TEST_ASSERT(sceAgcCbReleaseMem(&cb, 0, 0, 0, 0, 0x100, 0, 0, 0, 0, 4, 0) == 0,
        "ReleaseMem rejects interrupt > 3");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0, "ReleaseMem rejects do not advance cursor");
}

/* All three *RegistersIndirect builders share a 4-dword IT_NOP packet,
 * differing only by subcommand: SH=0x11, CX=0x12, UC=0x13.
 * addr=0x1_0000_0040 count=8 → [1]=8 [2]=0x40 [3]=0x1 */
static void test_sce_agc_dcb_set_registers_indirect(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* sh = sceAgcDcbSetShRegistersIndirect(&cb, 0x100000040ULL, 8);
    TEST_ASSERT(sh == buffer, "SetShRegistersIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(sh[0]), AGC_PM4_OP_NOP, "SetSh wrapper opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(sh[0]), AGC_PM4_SUB_SH_REGS_INDIRECT, "SetSh subcommand");
    TEST_ASSERT_EQ(agcPm4Length(sh[0]), 4, "SetSh length");
    TEST_ASSERT_EQ(sh[1], 8, "SetSh register count");
    TEST_ASSERT_EQ(sh[2], 0x40u, "SetSh addr lo");
    TEST_ASSERT_EQ(sh[3], 0x1u, "SetSh addr hi");

    uint32_t* cx = sceAgcDcbSetCxRegistersIndirect(&cb, 0x100000040ULL, 8);
    TEST_ASSERT(cx != NULL, "SetCxRegistersIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(cx[0]), AGC_PM4_SUB_CX_REGS_INDIRECT, "SetCx subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cx[0]), 4, "SetCx length");
    TEST_ASSERT_EQ(cx[1], 8, "SetCx register count");
    TEST_ASSERT_EQ(cx[2], 0x40u, "SetCx addr lo");
    TEST_ASSERT_EQ(cx[3], 0x1u, "SetCx addr hi");

    uint32_t* uc = sceAgcDcbSetUcRegistersIndirect(&cb, 0x100000040ULL, 8);
    TEST_ASSERT(uc != NULL, "SetUcRegistersIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(uc[0]), AGC_PM4_SUB_UC_REGS_INDIRECT, "SetUc subcommand");
    TEST_ASSERT_EQ(agcPm4Length(uc[0]), 4, "SetUc length");
    TEST_ASSERT_EQ(uc[1], 8, "SetUc register count");
    TEST_ASSERT_EQ(uc[2], 0x40u, "SetUc addr lo");
    TEST_ASSERT_EQ(uc[3], 0x1u, "SetUc addr hi");

    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 12, "Three indirect builders advance cursor by 4 each");
}

/* Patchers overwrite a 64-bit address field in an already-emitted packet.
 * Each is tested by: emit the right packet type, patch, verify the field
 * changed and non-target fields are intact; then verify rejection of the
 * wrong packet type. */
static void test_sce_agc_dcb_patch_address(void) {
    uint32_t buffer[64];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* DmaData: dst address at cmd[4..5] (+16 bytes) */
    uint32_t* dma = sceAgcDcbDmaData(&cb, 1, 2, 3, 0x100000020ULL, 4,
        5, 0x200000040ULL, 64, 6, 7, 8);
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetDstAddressOrOffset(dma, 0x300000060ULL), AGC_OK,
        "DmaDataPatch returns OK on DmaData packet");
    TEST_ASSERT_EQ(dma[4], 0x60u, "DmaDataPatch dst lo");
    TEST_ASSERT_EQ(dma[5], 0x3u, "DmaDataPatch dst hi");
    TEST_ASSERT_EQ(dma[6], 0x40u, "DmaDataPatch src lo unchanged");
    TEST_ASSERT_EQ(dma[7], 0x2u, "DmaDataPatch src hi unchanged");

    /* DmaDataPatch on a non-DmaData packet must fail */
    uint32_t* flip = sceAgcDcbSetFlip(&cb, 1, 0, 0, 0);
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetDstAddressOrOffset(flip, 0x300000060ULL),
        AGC_ERROR_INVALID_ARGUMENT, "DmaDataPatch rejects non-DmaData packet");

    /* WaitRegMemPatch: standard WAIT_REG_MEM → addr at cmd[2..3] (+8 bytes) */
    uint32_t* wrm_std = sceAgcDcbWaitRegMem(&cb, 0, 3, 2, 0, 0x100000020ULL, 0x55, 0xFF, 400);
    TEST_ASSERT_EQ(sceAgcWaitRegMemPatchAddress(wrm_std, 0x400000080ULL), AGC_OK,
        "WaitRegMemPatch returns OK on standard WAIT_REG_MEM");
    TEST_ASSERT_EQ(wrm_std[2], 0x80u, "WaitRegMemPatch std addr lo");
    TEST_ASSERT_EQ(wrm_std[3], 0x4u, "WaitRegMemPatch std addr hi");
    TEST_ASSERT_EQ(wrm_std[4], 0x55u, "WaitRegMemPatch std ref unchanged");

    /* WaitRegMemPatch: NOP-wrapped WAIT_MEM32 → addr at cmd[1..2] (+4 bytes) */
    uint32_t* wrm32 = sceAgcDcbWaitRegMem(&cb, 0, 3, 0, 0, 0x100000020ULL, 0x55, 0xFF, 400);
    TEST_ASSERT_EQ(sceAgcWaitRegMemPatchAddress(wrm32, 0x400000080ULL), AGC_OK,
        "WaitRegMemPatch returns OK on NOP-wrapped WAIT_MEM32");
    TEST_ASSERT_EQ(wrm32[1], 0x80u, "WaitRegMemPatch mem32 addr lo");
    TEST_ASSERT_EQ(wrm32[2], 0x4u, "WaitRegMemPatch mem32 addr hi");

    /* WaitRegMemPatch on a non-wait packet must fail */
    TEST_ASSERT_EQ(sceAgcWaitRegMemPatchAddress(flip, 0x400000080ULL),
        AGC_ERROR_INVALID_ARGUMENT, "WaitRegMemPatch rejects non-wait packet");

    /* QueueEndOfPipeActionPatch: ReleaseMem dst at cmd[3..4] (+12 bytes) */
    uint32_t* rel = sceAgcCbReleaseMem(
        &cb, 0x12, 0x3456, 1, 0x07, 0x100000020ULL, 3,
        0xAABBCCDD11223344ULL, 0, 1, 2, 0xCAFE);
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchAddress(rel, 0x5000000A0ULL), AGC_OK,
        "QueueEndOfPipeActionPatch returns OK on ReleaseMem packet");
    TEST_ASSERT_EQ(rel[3], 0xA0u, "QueueEndOfPipeActionPatch addr lo");
    TEST_ASSERT_EQ(rel[4], 0x5u, "QueueEndOfPipeActionPatch addr hi");
    TEST_ASSERT_EQ(rel[5], 0x11223344u, "QueueEndOfPipeActionPatch data lo unchanged");
    TEST_ASSERT_EQ(rel[7], 0xCAFEu, "QueueEndOfPipeActionPatch ctx id unchanged");

    /* QueueEndOfPipeActionPatch on a non-ReleaseMem packet must fail */
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchAddress(flip, 0x5000000A0ULL),
        AGC_ERROR_INVALID_ARGUMENT, "QueueEndOfPipeActionPatch rejects non-ReleaseMem packet");

    /* NULL cmd must fail for all three */
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetDstAddressOrOffset(0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "DmaDataPatch rejects NULL");
    TEST_ASSERT_EQ(sceAgcWaitRegMemPatchAddress(0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "WaitRegMemPatch rejects NULL");
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchAddress(0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "QueueEndOfPipeActionPatch rejects NULL");
}

/* GetLodStatsGetSize: pure helper, returns 0x10 + counterCount*4.
 * GetLodStats: 5-dword IT_GET_LOD_STATS packet (op 0x8E).
 *   cachePolicy=2 enable=1 reset=1 counterMask=0xAB counterSelect=0xCD
 *   → packet_control = (2<<28)|(1<<19)|(1<<18)|(0xAB<<10)|(0xCD<<2)
 *                    = 0x200EAF34
 *   dst=0x1_0000_0043 → lo masked & ~0x3F = 0x00000040 */
static void test_sce_agc_dcb_lod_stats(void) {
    TEST_ASSERT_EQ(sceAgcDcbGetLodStatsGetSize(0), 0x10u, "LodStatsGetSize 0 counters");
    TEST_ASSERT_EQ(sceAgcDcbGetLodStatsGetSize(4), 0x20u, "LodStatsGetSize 4 counters");
    TEST_ASSERT_EQ(sceAgcDcbGetLodStatsGetSize(16), 0x50u, "LodStatsGetSize 16 counters");

    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbGetLodStats(
        &cb, 2, 0x100000043ULL, 0x77, 0xAB, 1, 1, 0xCD);
    TEST_ASSERT(cmd == buffer, "GetLodStats returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_GET_LOD_STATS, "GetLodStats opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_ZERO, "GetLodStats subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5, "GetLodStats length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 5, "GetLodStats advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x77u, "GetLodStats control");
    TEST_ASSERT_EQ(cmd[2], 0x40u, "GetLodStats dst lo aligned");
    TEST_ASSERT_EQ(cmd[3], 0x1u, "GetLodStats dst hi");
    TEST_ASSERT_EQ(cmd[4], 0x200EAF34u, "GetLodStats packet_control");
}

void test_suite_cb(void) {
    TEST_SUITE("Command Buffer Builders");
    TEST_RUN(test_cb_layout_offsets);
    TEST_RUN(test_cb_alloc);
    TEST_RUN(test_sce_agc_cb_nop);
    TEST_RUN(test_sce_agc_cb_dispatch);
    TEST_RUN(test_sce_agc_cb_set_sh_registers);
    TEST_RUN(test_sce_agc_cb_set_cx_registers_direct);
    TEST_RUN(test_sce_agc_cb_set_cx_registers_direct_rejects_invalid);
    TEST_RUN(test_sce_agc_dcb_write_data);
    TEST_RUN(test_sce_agc_dcb_wait_reg_mem);
    TEST_RUN(test_sce_agc_dcb_markers_and_flip);
    TEST_RUN(test_sce_agc_dcb_dma_data);
    TEST_RUN(test_sce_agc_dcb_indirect_and_index);
    TEST_RUN(test_sce_agc_dcb_draw_packets);
    TEST_RUN(test_sce_agc_dcb_wait_safe);
    TEST_RUN(test_sce_agc_cb_release_mem);
    TEST_RUN(test_sce_agc_cb_release_mem_rejects_invalid);
    TEST_RUN(test_sce_agc_dcb_set_registers_indirect);
    TEST_RUN(test_sce_agc_dcb_patch_address);
    TEST_RUN(test_sce_agc_dcb_lod_stats);
}
