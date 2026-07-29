#include "test.h"
#include "agc_cb.h"
#include "agc_pm4.h"
#include "agc_registers.h"
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

static void test_sce_agc_cb_memset_exclusive(void) {
    uint32_t buffer[32] = {0};
    const uint32_t pattern[4] = {
        0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u,
    };
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcCbMemsetExclusive(
        &cb, 0x0000123456789000ULL, pattern, 128u);
    TEST_ASSERT(cmd == buffer, "MemsetExclusive returns first packet");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 32u,
        "MemsetExclusive advances exact cursor size");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[0]), AGC_PM4_OP_SET_SH_REG,
        "MemsetExclusive binds program address");
    TEST_ASSERT_EQ(buffer[0] & 1u, 1u,
        "MemsetExclusive PGM packet selects compute");
    TEST_ASSERT_EQ(buffer[1], AGC_REG_COMPUTE_PGM_LO,
        "MemsetExclusive PGM register offset");
    TEST_ASSERT_EQ(buffer[4] & 1u, 1u,
        "MemsetExclusive resource packet selects compute");
    TEST_ASSERT_EQ(buffer[5], AGC_REG_COMPUTE_PGM_RSRC1,
        "MemsetExclusive resource register offset");
    TEST_ASSERT_EQ(buffer[6], 0x602C0000u,
        "MemsetExclusive embeds compiled RSRC1");
    TEST_ASSERT_EQ(buffer[7], 0x92u,
        "MemsetExclusive embeds nine-user-SGPR RSRC2");
    TEST_ASSERT_EQ(buffer[9], AGC_REG_COMPUTE_PGM_RSRC3,
        "MemsetExclusive RSRC3 register offset");
    TEST_ASSERT_EQ(buffer[12], AGC_REG_COMPUTE_NUM_THREAD_X,
        "MemsetExclusive thread register offset");
    TEST_ASSERT_EQ(buffer[13], 64u, "MemsetExclusive local size X");
    TEST_ASSERT_EQ(buffer[17], AGC_REG_COMPUTE_USER_DATA_0,
        "MemsetExclusive user-data register offset");
    TEST_ASSERT_EQ(buffer[18], 0u, "MemsetExclusive ring offset low");
    TEST_ASSERT_EQ(buffer[19], 0u, "MemsetExclusive ring offset high");
    TEST_ASSERT_EQ(buffer[20], 0x56789000u,
        "MemsetExclusive destination low");
    TEST_ASSERT_EQ(buffer[21], 0x00001234u,
        "MemsetExclusive destination high");
    TEST_ASSERT_EQ(buffer[22], 8u, "MemsetExclusive 16-byte block count");
    TEST_ASSERT_EQ(buffer[23], pattern[0], "MemsetExclusive pattern word 0");
    TEST_ASSERT_EQ(buffer[26], pattern[3], "MemsetExclusive pattern word 3");
    TEST_ASSERT_EQ(agcPm4Opcode(buffer[27]), AGC_PM4_OP_DISPATCH_DIRECT,
        "MemsetExclusive dispatch opcode");
    TEST_ASSERT_EQ(buffer[27] & 1u, 1u,
        "MemsetExclusive dispatch selects compute");
    TEST_ASSERT_EQ(buffer[28], 1u, "MemsetExclusive dispatch group count");
    TEST_ASSERT_EQ(buffer[31], 0x41u,
        "MemsetExclusive dispatch initiator");

    uint32_t short_buffer[31] = {0};
    agcCbInit(&cb, short_buffer, sizeof(short_buffer));
    TEST_ASSERT(sceAgcCbMemsetExclusive(
        &cb, 0u, pattern, 128u) == NULL,
        "MemsetExclusive rejects insufficient command space");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0u,
        "MemsetExclusive failure preserves cursor");
    TEST_ASSERT(sceAgcCbMemsetExclusive(
        &cb, 0u, NULL, 128u) == NULL,
        "MemsetExclusive rejects null pattern");
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

static void test_sce_agc_cb_set_uc_registers_direct(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    AgcRegisterValue regs[3] = {
        { .offset = 0x249, .value = 0x1111 },
        { .offset = 0x24A, .value = 0x2222 },
        { .offset = 0x24B, .value = 0x3333 },
    };
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t *cmd = sceAgcCbSetUcRegistersDirect(&cb, regs, 3);
    TEST_ASSERT(cmd == buffer, "SetUcRegistersDirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_UCONFIG_REG,
        "SetUc opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5,
        "SetUc length (3 regs + 2 header)");
    TEST_ASSERT_EQ(cmd[1], 0x249, "SetUc start offset");
    TEST_ASSERT_EQ(cmd[2], 0x1111, "SetUc value 0");
    TEST_ASSERT_EQ(cmd[3], 0x2222, "SetUc value 1");
    TEST_ASSERT_EQ(cmd[4], 0x3333, "SetUc value 2");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 5, "SetUc advances cursor by 5");
}

static void test_sce_agc_dcb_write_data(void) {
    uint32_t buffer[32];
    uint32_t data[2] = {0xAABBCCDD, 0x11223344};
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* reference-confirmed: uses IT_WRITE_DATA (0x37) directly, not NOP-wrapped.
     * dst=1 cache=2 increment=1 write_confirm=0 addr=0x100000020
     * cmd[1] = (1&1)<<30 | (1&0x1e)<<7 | 1<<16 | 0<<20 | (2&3)<<25
     *        = 0x40000000 | 0 | 0x10000 | 0 | 0x4000000 = 0x44010000 */
    uint32_t* cmd = sceAgcDcbWriteData(&cb, 1, 2, 0x100000020ULL, data, 2, 1, 0);
    TEST_ASSERT(cmd == buffer, "WriteData returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_WRITE_DATA, "WriteData opcode (IT_WRITE_DATA 0x37)");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_ZERO, "WriteData no subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "WriteData length");
    TEST_ASSERT_EQ(cmd[1], 0x44010000u, "WriteData control (dst|incr|cache bit fields)");
    TEST_ASSERT_EQ(cmd[2], 0x20, "WriteData address lo");
    TEST_ASSERT_EQ(cmd[3], 0x1, "WriteData address hi");
    TEST_ASSERT_EQ(cmd[4], data[0], "WriteData payload 0");
    TEST_ASSERT_EQ(cmd[5], data[1], "WriteData payload 1");
}

static void test_sce_agc_dcb_wait_reg_mem(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* FW 5.50 SPRX-confirmed: native WAIT_REG_MEM, 7 dwords.
     * size=0, cmp=3, op=0, cache=0, addr=0x100000020, ref=0x55, mask=0xFF, poll=400
     * control = 0x10 | (3&7) | ((0&3)<<8) | ((0&0xC)<<4) | ((0&3)<<25) = 0x13
     * poll = (400 >> 4) & 0xFFFF = 25
     * addr_lo = 0x20 & ~0x3 = 0x20
     * addr_hi = 0x1 & 0x3FFFF = 0x1 */
    uint32_t* cmd = sceAgcDcbWaitRegMem(&cb, 0, 3, 0, 0, 0x100000020ULL, 0x55, 0xFF, 400);
    TEST_ASSERT(cmd == buffer, "WaitRegMem returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_WAIT_REG_MEM, "WaitRegMem32 opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_ZERO, "WaitRegMem32 header controls");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 7, "WaitRegMem32 length (reference: 7 dwords)");
    TEST_ASSERT_EQ(cmd[1], 0x13u, "WaitRegMem32 control (0x10|cmp=3)");
    TEST_ASSERT_EQ(cmd[2], 0x20u, "WaitRegMem32 addr_lo aligned");
    TEST_ASSERT_EQ(cmd[3], 0x1u, "WaitRegMem32 addr_hi masked");
    TEST_ASSERT_EQ(cmd[4], 0x55u, "WaitRegMem32 reference");
    TEST_ASSERT_EQ(cmd[5], 0xFFu, "WaitRegMem32 mask");
    TEST_ASSERT_EQ(cmd[6], 25u, "WaitRegMem32 poll (400>>4)");

    /* 64-bit variant: 9 dwords.
     * size=1, cmp=5, op=1, cache=2, addr=0x200000040, ref=0xAABB, mask=0xFFFF, poll=800
     * control = 0x10 | (5&7) | ((1&1)<<8) | ((1&6)<<5) | ((2&3)<<25)
     *   = 0x10 | 5 | 0x100 | 0 | 0x4000000 = 0x4000115
     *   (note: (1&0x6)=0, so the <<5 term is 0)
     * poll = (800 >> 4) & 0xFFFF = 50
     * addr_lo = 0x40 & ~0x7 = 0x40
     * addr_hi = 0x2 & 0x3FFFF = 0x2 */
    uint32_t buffer2[32];
    agcCbInit(&cb, buffer2, sizeof(buffer2));
    uint32_t* cmd64 = sceAgcDcbWaitRegMem(&cb, 1, 5, 1, 2, 0x200000040ULL, 0xAABB, 0xFFFF, 800);
    TEST_ASSERT(cmd64 != NULL, "WaitRegMem64 returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd64[0]), AGC_PM4_OP_WAIT_REG_MEM64, "WaitRegMem64 opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd64[0]), AGC_PM4_SUB_ZERO, "WaitRegMem64 header controls");
    TEST_ASSERT_EQ(agcPm4Length(cmd64[0]), 9, "WaitRegMem64 length");
    TEST_ASSERT_EQ(cmd64[1], 0x4000115u, "WaitRegMem64 control (0x10|5|op1|cache2)");
    TEST_ASSERT_EQ(cmd64[2], 0x40u, "WaitRegMem64 addr_lo aligned");
    TEST_ASSERT_EQ(cmd64[3], 0x2u, "WaitRegMem64 addr_hi masked");
    TEST_ASSERT_EQ(cmd64[4], 0xAABBu, "WaitRegMem64 reference_lo");
    TEST_ASSERT_EQ(cmd64[5], 0u, "WaitRegMem64 reference_hi");
    TEST_ASSERT_EQ(cmd64[6], 0xFFFFu, "WaitRegMem64 mask_lo");
    TEST_ASSERT_EQ(cmd64[7], 0u, "WaitRegMem64 mask_hi");
    TEST_ASSERT_EQ(cmd64[8], 50u, "WaitRegMem64 poll (800>>4)");

    /* DCB operations 2/3 retain the native nine-dword packet shape. */
    uint32_t buffer3[32];
    agcCbInit(&cb, buffer3, sizeof(buffer3));
    uint32_t *cmd_op2 = sceAgcDcbWaitRegMem(
        &cb, 1, 6, 2, 3, 0x300000088ULL, 0x1122334455667788ULL,
        0xFFEEDDCCBBAA0099ULL, UINT32_MAX);
    TEST_ASSERT(cmd_op2 == buffer3, "WaitRegMem op2 returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd_op2[0]), AGC_PM4_OP_WAIT_REG_MEM64, "WaitRegMem op2 opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd_op2[0]), 9, "WaitRegMem op2 preserves 9-dword shape");
    TEST_ASSERT_EQ(cmd_op2[1], 0x06000056u, "WaitRegMem op2 control");
    TEST_ASSERT_EQ(cmd_op2[4], 0x55667788u, "WaitRegMem op2 reference low preserved");
    TEST_ASSERT_EQ(cmd_op2[5], 0x11223344u, "WaitRegMem op2 reference high preserved");
    TEST_ASSERT_EQ(cmd_op2[6], 0xBBAA0099u, "WaitRegMem op2 mask low preserved");
    TEST_ASSERT_EQ(cmd_op2[7], 0xFFEEDDCCu, "WaitRegMem op2 mask high preserved");
    TEST_ASSERT_EQ(cmd_op2[8], 0xFFFFu, "WaitRegMem poll saturates instead of wrapping");
    TEST_ASSERT_EQ(cb.cursor_up, (uintptr_t)(buffer3 + 9), "WaitRegMem op2 cursor advance");
}

static void test_sce_agc_dcb_markers_and_flip(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* push = sceAgcDcbPushMarker(&cb, "abc", 0x10203040u);
    TEST_ASSERT(push == buffer, "PushMarker returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(push[0]), AGC_PM4_SUB_PUSH_MARKER, "PushMarker subcommand");
    TEST_ASSERT_EQ(push[1], 0x10203040u, "PushMarker color");

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

    /* reference-confirmed: cmd[4] = decode_draw_index_initiator(modifier):
     *   if modifier bit 32 set → 0, else (modifier >> 3) & 0x20
     * modifier 0xFFFFFFFF: bit 32 not set, (0xFFFFFFFF >> 3) & 0x20 = 0x20 */
    uint32_t* offset = sceAgcDcbDrawIndexOffset(&cb, 4, 12, 0xFFFFFFFFull);
    TEST_ASSERT(offset == buffer, "DrawIndexOffset returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(offset[0]), AGC_PM4_OP_DRAW_INDEX_OFFSET_2, "DrawIndexOffset opcode");
    TEST_ASSERT_EQ(agcPm4Length(offset[0]), 5, "DrawIndexOffset length");
    TEST_ASSERT_EQ(offset[1], 12, "DrawIndexOffset count 0");
    TEST_ASSERT_EQ(offset[2], 4, "DrawIndexOffset offset");
    TEST_ASSERT_EQ(offset[4], 0x20u, "DrawIndexOffset initiator (modifier 0xFFFFFFFF, bit 5 set)");

    /* modifier with bit 5 set (0x20 << 3 = 0x100) → initiator = (0x100 >> 3) & 0x20 = 0x20 */
    uint32_t buffer2[64];
    agcCbInit(&cb, buffer2, sizeof(buffer2));
    uint32_t* offset2 = sceAgcDcbDrawIndexOffset(&cb, 8, 16, 0x100ull);
    TEST_ASSERT(offset2 != NULL, "DrawIndexOffset (modifier 0x100) returns allocated packet");
    TEST_ASSERT_EQ(offset2[4], 0x20u, "DrawIndexOffset initiator (modifier 0x100, bit 5 set)");

    /* modifier with bit 32 set → initiator = 0 */
    uint32_t buffer3[64];
    agcCbInit(&cb, buffer3, sizeof(buffer3));
    uint32_t* offset3 = sceAgcDcbDrawIndexOffset(&cb, 12, 20, (1ull << 32));
    TEST_ASSERT(offset3 != NULL, "DrawIndexOffset (bit 32 set) returns allocated packet");
    TEST_ASSERT_EQ(offset3[4], 0u, "DrawIndexOffset initiator (bit 32 set → 0)");

    /* reference-confirmed: IT_DRAW_INDEX_AUTO (0x2D), 3 dwords.
     * modifier 0x40000000 → initiator = ((0x40000000 >> 3) & 0x20) | 0x2 = 0x2 */
    uint32_t* auto_draw = sceAgcDcbDrawIndexAuto(&cb, 6, 0x40000000u);
    TEST_ASSERT(auto_draw != NULL, "DrawIndexAuto returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(auto_draw[0]), AGC_PM4_OP_DRAW_INDEX_AUTO, "DrawIndexAuto opcode");
    TEST_ASSERT_EQ(agcPm4Length(auto_draw[0]), 3, "DrawIndexAuto length");
    TEST_ASSERT_EQ(auto_draw[1], 6, "DrawIndexAuto count");
    TEST_ASSERT_EQ(auto_draw[2], 0x2u, "DrawIndexAuto initiator (modifier 0x40000000)");

    /* modifier with bit 8 set (0x100) → initiator = ((0x100 >> 3) & 0x20) | 0x2 = 0x22 */
    uint32_t* auto_draw2 = sceAgcDcbDrawIndexAuto(&cb, 10, 0x100ull);
    TEST_ASSERT(auto_draw2 != NULL, "DrawIndexAuto (modifier 0x100) returns allocated packet");
    TEST_ASSERT_EQ(auto_draw2[2], 0x22u, "DrawIndexAuto initiator (modifier 0x100)");

    /* modifier with bit 32 set → initiator = 0 | 0x2 = 0x2 */
    uint32_t* auto_draw3 = sceAgcDcbDrawIndexAuto(&cb, 20, (1ull << 32));
    TEST_ASSERT(auto_draw3 != NULL, "DrawIndexAuto (bit 32 set) returns allocated packet");
    TEST_ASSERT_EQ(auto_draw3[2], 0x2u, "DrawIndexAuto initiator (bit 32 set)");
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
     * event_index=5 (action 0x12 < 0x2f), gcr&0x300=0x300 (not 0x100, no bit 9)
     * cmd[1] = 0x12 | (5<<8) | (0x456<<12) | (3<<25) = 0x6456512
     * cmd[2] = (1<<16) | (2<<24) | (3<<29) = 0x62010000
     *   [3]=0x20 [4]=0x1 [5]=0x11223344 [6]=0xAABBCCDD [7]=0xCAFE */
    uint32_t* cmd = sceAgcCbReleaseMem(
        &cb, 0x12, 0x3456, 1, 0x07, 0x100000020ULL, 3,
        0xAABBCCDD11223344ULL, 0, 1, 2, 0xCAFE);
    TEST_ASSERT(cmd == buffer, "ReleaseMem returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_RELEASE_MEM, "ReleaseMem opcode (SPRX-confirmed IT_RELEASE_MEM 0x49)");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_ZERO, "ReleaseMem no subcommand");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 8, "ReleaseMem length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 8, "ReleaseMem advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x6456512u, "ReleaseMem action|event_index|gcr_cntl|cache_policy");
    TEST_ASSERT_EQ(cmd[2], 0x62010000u, "ReleaseMem dst|interrupt|data_sel");
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
        "ReleaseMem rejects data_selection 4 (not in {0,1,2,3,5})");
    TEST_ASSERT(sceAgcCbReleaseMem(&cb, 0, 0, 0, 0, 0x100, 0, 0, 0, 0, 5, 0) == 0,
        "ReleaseMem rejects interrupt > 4");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0, "ReleaseMem rejects do not advance cursor");

    /* reference-confirmed: gds_offset != 0 and gds_size > 2 are now accepted
     * when data_sel == 5 (data = gds_offset | (gds_size << 16)). */
    uint32_t buffer2[16];
    agcCbInit(&cb, buffer2, sizeof(buffer2));
    uint32_t* cmd = sceAgcCbReleaseMem(
        &cb, 0x2f, 0, 0, 0, 0x100, 5, 0, 0x10, 4, 0, 0);
    TEST_ASSERT(cmd != NULL, "ReleaseMem accepts gds_offset != 0 when data_sel == 5");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 8, "ReleaseMem gds_offset accepted advances cursor");

    uint32_t buffer3[16];
    agcCbInit(&cb, buffer3, sizeof(buffer3));
    uint32_t* cmd2 = sceAgcCbReleaseMem(
        &cb, 0x2f, 0, 0, 0, 0x100, 5, 0, 0, 8, 0, 0);
    TEST_ASSERT(cmd2 != NULL, "ReleaseMem accepts gds_size > 2 when data_sel == 5");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 8, "ReleaseMem gds_size accepted advances cursor");
}

/* All three *RegistersIndirect builders share a 4-dword IT_NOP packet,
 * RE: SPRX uses direct opcodes 0x63 (SH), 0x9F (CX), 0x64 (UC), 5 dwords.
 * addr=0x1_0000_0040 count=8 → [1]=0x40 [2]=0x1 [3]=0x80000000 [4]=8 */
static void test_sce_agc_dcb_set_registers_indirect(void) {
    uint32_t buffer[32];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* sh = sceAgcDcbSetShRegistersIndirect(&cb, 0x100000040ULL, 8);
    TEST_ASSERT(sh == buffer, "SetShRegistersIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(sh[0]), AGC_PM4_OP_SET_SH_REG_INDIRECT, "SetSh opcode 0x63");
    TEST_ASSERT_EQ(agcPm4Length(sh[0]), 5, "SetSh length 5 dwords");
    TEST_ASSERT_EQ(sh[1], 0x40u, "SetSh addr lo (& ~3)");
    TEST_ASSERT_EQ(sh[2], 0x1u, "SetSh addr hi");
    TEST_ASSERT_EQ(sh[3], 0x80000000u, "SetSh constant dword");
    TEST_ASSERT_EQ(sh[4], 8u, "SetSh register count");

    uint32_t* cx = sceAgcDcbSetCxRegistersIndirect(&cb, 0x100000040ULL, 8);
    TEST_ASSERT(cx != NULL, "SetCxRegistersIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cx[0]), AGC_PM4_OP_SET_CX_REG_INDIRECT, "SetCx opcode 0x9F");
    TEST_ASSERT_EQ(agcPm4Length(cx[0]), 5, "SetCx length 5 dwords");
    TEST_ASSERT_EQ(cx[1], 0x40u, "SetCx addr lo");
    TEST_ASSERT_EQ(cx[2], 0x1u, "SetCx addr hi");
    TEST_ASSERT_EQ(cx[3], 0x80000000u, "SetCx constant dword");
    TEST_ASSERT_EQ(cx[4], 8u, "SetCx register count");

    uint32_t* uc = sceAgcDcbSetUcRegistersIndirect(&cb, 0x100000040ULL, 8);
    TEST_ASSERT(uc != NULL, "SetUcRegistersIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(uc[0]), AGC_PM4_OP_SET_UC_REG_INDIRECT, "SetUc opcode 0x64");
    TEST_ASSERT_EQ(agcPm4Length(uc[0]), 5, "SetUc length 5 dwords");
    TEST_ASSERT_EQ(uc[1], 0x40u, "SetUc addr lo");
    TEST_ASSERT_EQ(uc[2], 0x1u, "SetUc addr hi");
    TEST_ASSERT_EQ(uc[3], 0x80000000u, "SetUc constant dword");
    TEST_ASSERT_EQ(uc[4], 8u, "SetUc register count");

    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 15, "Three indirect builders advance cursor by 5 each");
}

/* Patchers overwrite a 64-bit address field in an already-emitted packet.
 * Each is tested by: emit the right packet type, patch, verify the field
 * changed and non-target fields are intact; then verify rejection of the
 * wrong packet type. */
static void test_sce_agc_dcb_patch_address(void) {
    uint32_t buffer[128];
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

    /* DmaData Src patch: src address at cmd[6..7] in NOP-wrapped format */
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate(dma, 0x400000080ULL), AGC_OK,
        "DmaDataSrcPatch returns OK on DmaData packet");
    TEST_ASSERT_EQ(dma[6], 0x80u, "DmaDataSrcPatch src lo");
    TEST_ASSERT_EQ(dma[7], 0x4u, "DmaDataSrcPatch src hi");
    TEST_ASSERT_EQ(dma[4], 0x60u, "DmaDataSrcPatch dst lo unchanged");

    /* DmaDataPatch on a non-DmaData packet must fail with 0x8a6c000c (SPRX) */
    uint32_t* flip = sceAgcDcbSetFlip(&cb, 1, 0, 0, 0);
    TEST_ASSERT_EQ((uint32_t)sceAgcDmaDataPatchSetDstAddressOrOffset(flip, 0x300000060ULL),
        0x8a6c000cu, "DmaDataPatch rejects non-DmaData packet");
    TEST_ASSERT_EQ((uint32_t)sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate(flip, 0x400000080ULL),
        0x8a6c000cu, "DmaDataSrcPatch rejects non-DmaData packet");

    /* Raw DMA_DATA (opcode 0x50) patch test */
    uint32_t raw_dma[8];
    raw_dma[0] = agcPm4Header3(AGC_PM4_OP_DMA_DATA, 8);
    raw_dma[1] = 0;
    raw_dma[2] = 0;  /* src lo */
    raw_dma[3] = 0;  /* src hi */
    raw_dma[4] = 0;  /* dst lo */
    raw_dma[5] = 0;  /* dst hi */
    raw_dma[6] = 0;
    raw_dma[7] = 0;
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate(raw_dma, 0x12345678ABULL), AGC_OK,
        "DmaDataSrcPatch on raw DMA_DATA");
    TEST_ASSERT_EQ(raw_dma[2], 0x345678ABu, "Raw DMA_DATA src lo");
    TEST_ASSERT_EQ(raw_dma[3], 0x12u, "Raw DMA_DATA src hi");
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetDstAddressOrOffset(raw_dma, 0xDEAD0000BEEFULL), AGC_OK,
        "DmaDataDstPatch on raw DMA_DATA");
    TEST_ASSERT_EQ(raw_dma[4], 0x0000BEEFu, "Raw DMA_DATA dst lo");
    TEST_ASSERT_EQ(raw_dma[5], 0xDEADu, "Raw DMA_DATA dst hi");

    /* WaitRegMemPatch accepts a native WAIT_REG_MEM packet. */
    uint32_t wrm_std[7] = {
        agcPm4Header3(AGC_PM4_OP_WAIT_REG_MEM, 7), 3u,
        0x20u, 0x1u, 0x55u, 0xFFu, 10u
    };
    TEST_ASSERT_EQ(sceAgcWaitRegMemPatchAddress(wrm_std, 0x400000080ULL), AGC_OK,
        "WaitRegMemPatch returns OK on standard WAIT_REG_MEM");
    TEST_ASSERT_EQ(wrm_std[2], 0x80u, "WaitRegMemPatch std addr lo");
    TEST_ASSERT_EQ(wrm_std[3], 0x4u, "WaitRegMemPatch std addr hi");
    TEST_ASSERT_EQ(wrm_std[4], 0x55u, "WaitRegMemPatch std ref unchanged");

    /* Builder-produced native WAIT_REG_MEM → address at cmd[2..3]. */
    uint32_t* wrm32 = sceAgcDcbWaitRegMem(&cb, 0, 3, 0, 0, 0x100000020ULL, 0x55, 0xFF, 400);
    TEST_ASSERT_EQ(sceAgcWaitRegMemPatchAddress(wrm32, 0x400000080ULL), AGC_OK,
        "WaitRegMemPatch returns OK on native WAIT_REG_MEM");
    TEST_ASSERT_EQ(wrm32[2], 0x80u, "WaitRegMemPatch mem32 addr lo");
    TEST_ASSERT_EQ(wrm32[3], 0x4u, "WaitRegMemPatch mem32 addr hi");

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

    /* QueueEndOfPipeActionPatchData: patches cmd[5..6] with data */
    uint32_t* rel2 = sceAgcCbReleaseMem(
        &cb, 0x12, 0x3456, 1, 0x07, 0x100000020ULL, 3,
        0xAABBCCDD11223344ULL, 0, 1, 2, 0xCAFE);
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchData(rel2, 0, 3, 0xDEADBEEFCAFEULL), AGC_OK,
        "QueueEndOfPipeActionPatchData returns OK on ReleaseMem packet");
    TEST_ASSERT_EQ(rel2[5], 0xBEEFCAFEu, "QueueEndOfPipeActionPatchData data lo");
    TEST_ASSERT_EQ(rel2[6], 0xDEADu, "QueueEndOfPipeActionPatchData data hi");
    TEST_ASSERT_EQ(rel2[3], 0x20u, "QueueEndOfPipeActionPatchData addr unchanged");

    /* QueueEndOfPipeActionPatchData rejects interrupt==4 */
    uint32_t* rel3 = sceAgcCbReleaseMem(
        &cb, 0x12, 0, 1, 0, 0x100, 0, 0, 0, 0, 4, 0);
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchData(rel3, 0, 0, 0x1234), AGC_ERROR_INVALID_ARGUMENT,
        "QueueEndOfPipeActionPatchData rejects interrupt==4");

    /* QueueEndOfPipeActionPatchData rejects data_sel==5 */
    uint32_t* rel4 = sceAgcCbReleaseMem(
        &cb, 0x12, 0, 1, 0, 0x100, 5, 0, 0x10, 4, 0, 0);
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchData(rel4, 0, 5, 0x1234), AGC_ERROR_INVALID_ARGUMENT,
        "QueueEndOfPipeActionPatchData rejects data_sel==5");

    /* QueueEndOfPipeActionPatchAddress rejects interrupt==4 */
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchAddress(rel3, 0x5000000A0ULL),
        AGC_ERROR_INVALID_ARGUMENT, "QueueEndOfPipeActionPatch rejects interrupt==4");

    /* QueueEndOfPipeActionGetSize returns 0x20 (32 bytes = 8 dwords) */
    TEST_ASSERT_EQ(sceAgcCbQueueEndOfPipeActionGetSize(), 0x20u,
        "QueueEndOfPipeActionGetSize returns 32");

    /* NULL cmd must fail for all patchers */
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetDstAddressOrOffset(0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "DmaDataPatch rejects NULL");
    TEST_ASSERT_EQ(sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate(0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "DmaDataSrcPatch rejects NULL");
    TEST_ASSERT_EQ(sceAgcWaitRegMemPatchAddress(0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "WaitRegMemPatch rejects NULL");
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchAddress(0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "QueueEndOfPipeActionPatch rejects NULL");
    TEST_ASSERT_EQ(sceAgcQueueEndOfPipeActionPatchData(0, 0, 0, 0), AGC_ERROR_INVALID_ARGUMENT,
        "QueueEndOfPipeActionPatchData rejects NULL");
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

/* AGC-custom flip/display wait builders (libSceAgc.sprx only).
 * These use AGC-specific opcodes (0x4C/0x4E/0x4F/0x51/0x54) and
 * validate flip_slot < 32. */
static void test_sce_agc_dcb_wait_flip_done(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* Valid flip slots: 0, 15, 31 */
    uint32_t* cmd0 = sceAgcDcbWaitFlipDone(&cb, 0);
    TEST_ASSERT(cmd0 != NULL, "WaitFlipDone slot 0 returns packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd0[0]), AGC_PM4_OP_WAIT_FLIP_DONE, "WaitFlipDone opcode 0x4C");
    TEST_ASSERT_EQ(agcPm4Length(cmd0[0]), 3, "WaitFlipDone length");
    TEST_ASSERT_EQ(cmd0[1], 0u, "WaitFlipDone slot 0");
    TEST_ASSERT_EQ(cmd0[2], 0u, "WaitFlipDone reserved");

    uint32_t* cmd15 = sceAgcDcbWaitFlipDone(&cb, 15);
    TEST_ASSERT(cmd15 != NULL, "WaitFlipDone slot 15 returns packet");
    TEST_ASSERT_EQ(cmd15[1], 15u, "WaitFlipDone slot 15");

    uint32_t* cmd31 = sceAgcDcbWaitFlipDone(&cb, 31);
    TEST_ASSERT(cmd31 != NULL, "WaitFlipDone slot 31 returns packet");
    TEST_ASSERT_EQ(cmd31[1], 31u, "WaitFlipDone slot 31");

    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 9, "WaitFlipDone advances cursor by 3 each");
}

static void test_sce_agc_dcb_wait_flip_done_rejects_invalid(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    /* Invalid flip slots: 32, 100 */
    TEST_ASSERT(sceAgcDcbWaitFlipDone(&cb, 32) == 0, "WaitFlipDone rejects slot 32");
    TEST_ASSERT(sceAgcDcbWaitFlipDone(&cb, 100) == 0, "WaitFlipDone rejects slot 100");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 0, "WaitFlipDone rejects do not advance cursor");

    /* NULL cb */
    TEST_ASSERT(sceAgcDcbWaitFlipDone(NULL, 0) == 0, "WaitFlipDone rejects NULL cb");

    /* Overflow: buffer too small for 3 dwords */
    uint32_t small[2];
    SceAgcCb cb_small;
    agcCbInit(&cb_small, small, sizeof(small));
    TEST_ASSERT(sceAgcDcbWaitFlipDone(&cb_small, 0) == 0, "WaitFlipDone rejects overflow");
}

static void test_sce_agc_dcb_wait_flip(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbWaitFlip(&cb, 15);
    TEST_ASSERT(cmd != NULL, "WaitFlip slot 15 returns packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_WAIT_FLIP, "WaitFlip opcode 0x51");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "WaitFlip length");
    TEST_ASSERT_EQ(cmd[1], 15u, "WaitFlip slot");
    TEST_ASSERT_EQ(cmd[2], 0u, "WaitFlip reserved");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 3, "WaitFlip advances cursor");

    /* Invalid slot */
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT(sceAgcDcbWaitFlip(&cb, 32) == 0, "WaitFlip rejects slot 32");
    TEST_ASSERT(sceAgcDcbWaitFlip(NULL, 0) == 0, "WaitFlip rejects NULL cb");
}

static void test_sce_agc_dcb_insert_wait_flip_done(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbInsertWaitFlipDone(&cb, 7);
    TEST_ASSERT(cmd != NULL, "InsertWaitFlipDone slot 7 returns packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INSERT_WAIT_FLIP_DONE, "InsertWaitFlipDone opcode 0x54");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_WAIT_FLIP_DONE, "InsertWaitFlipDone sub 0x06");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "InsertWaitFlipDone length");
    TEST_ASSERT_EQ(cmd[1], 7u, "InsertWaitFlipDone slot");
    TEST_ASSERT_EQ(cmd[2], 0u, "InsertWaitFlipDone reserved");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 3, "InsertWaitFlipDone advances cursor");

    /* Invalid slot */
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT(sceAgcDcbInsertWaitFlipDone(&cb, 32) == 0, "InsertWaitFlipDone rejects slot 32");
    TEST_ASSERT(sceAgcDcbInsertWaitFlipDone(NULL, 0) == 0, "InsertWaitFlipDone rejects NULL cb");
}

static void test_sce_agc_dcb_wait_flip_eos(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbWaitFlipEos(&cb, 3);
    TEST_ASSERT(cmd != NULL, "WaitFlipEos slot 3 returns packet");
    /* First packet: opcode 0x4F */
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_WAIT_FLIP_EOS, "WaitFlipEos first opcode 0x4F");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "WaitFlipEos first length");
    TEST_ASSERT_EQ(cmd[1], 3u, "WaitFlipEos first slot");
    TEST_ASSERT_EQ(cmd[2], 0u, "WaitFlipEos first reserved");
    /* Second packet: opcode 0x4E */
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[3]), AGC_PM4_OP_WAIT_FLIP_EOS_2, "WaitFlipEos second opcode 0x4E");
    TEST_ASSERT_EQ(agcPm4Length(cmd[3]), 3, "WaitFlipEos second length");
    TEST_ASSERT_EQ(cmd[4], 3u, "WaitFlipEos second slot");
    TEST_ASSERT_EQ(cmd[5], 0u, "WaitFlipEos second reserved");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 6, "WaitFlipEos advances cursor by 6");

    /* Invalid slot */
    agcCbInit(&cb, buffer, sizeof(buffer));
    TEST_ASSERT(sceAgcDcbWaitFlipEos(&cb, 32) == 0, "WaitFlipEos rejects slot 32");
    TEST_ASSERT(sceAgcDcbWaitFlipEos(NULL, 0) == 0, "WaitFlipEos rejects NULL cb");

    /* Overflow: buffer too small for 6 dwords */
    uint32_t small[4];
    SceAgcCb cb_small;
    agcCbInit(&cb_small, small, sizeof(small));
    TEST_ASSERT(sceAgcDcbWaitFlipEos(&cb_small, 0) == 0, "WaitFlipEos rejects overflow");
}

/* === SPRX DCB builder tests (capstone disassembly) === */

/* sceAgcDcbIndirectBuffer — IT_INDIRECT_BUFFER (0x3F), 4 dwords.
 * gpu_addr=0x2_0000_0040 size=256 vmid=0xA
 * → [1]=0x40 [2]=0x2 [3]=(256&0xFFFFF)|((0xA)<<24)=0x0A000100 */
static void test_sce_agc_dcb_indirect_buffer(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbIndirectBuffer(&cb, 0x200000040ULL, 256, 0xA);
    TEST_ASSERT(cmd == buffer, "DcbIndirectBuffer returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_INDIRECT_BUFFER, "DcbIndirectBuffer opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "DcbIndirectBuffer length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 4, "DcbIndirectBuffer advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x40u, "DcbIndirectBuffer ibase lo");
    TEST_ASSERT_EQ(cmd[2], 0x2u, "DcbIndirectBuffer ibase hi");
    TEST_ASSERT_EQ(cmd[3], 0x0A000100u, "DcbIndirectBuffer size|vmid");

    TEST_ASSERT(sceAgcDcbIndirectBuffer(NULL, 0x100, 4, 0) == 0,
        "DcbIndirectBuffer rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[3];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbIndirectBuffer(&small, 0x100, 4, 0) == 0,
        "DcbIndirectBuffer rejects overflow");
}

/* sceAgcDcbDrawIndirect — IT_DRAW_INDIRECT (0x24), 5 dwords.
 * data_offset=0x100 base_vtx=0x200 start_inst=0x300 initiator=0x42
 * → [1]=0x100 [2]=0x200 [3]=0x300 [4]=0x42 */
static void test_sce_agc_dcb_draw_indirect(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbDrawIndirect(&cb, 0x100, 0x200, 0x300, 0x42);
    TEST_ASSERT(cmd == buffer, "DcbDrawIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDIRECT, "DcbDrawIndirect opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5, "DcbDrawIndirect length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 5, "DcbDrawIndirect advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x100u, "DcbDrawIndirect data_offset");
    TEST_ASSERT_EQ(cmd[2], 0x200u, "DcbDrawIndirect base_vtx_loc");
    TEST_ASSERT_EQ(cmd[3], 0x300u, "DcbDrawIndirect start_inst_loc");
    TEST_ASSERT_EQ(cmd[4], 0x42u, "DcbDrawIndirect draw_initiator");

    TEST_ASSERT(sceAgcDcbDrawIndirect(NULL, 0, 0, 0, 0) == 0,
        "DcbDrawIndirect rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[4];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbDrawIndirect(&small, 0, 0, 0, 0) == 0,
        "DcbDrawIndirect rejects overflow");
}

/* sceAgcDcbDrawIndex2 — IT_DRAW_INDEX_2 (0x27), 6 dwords.
 * max_size=0x1000 index_base=0x1_0000_0080 count=123 initiator=0x42
 * → [1]=0x1000 [2]=0x80 [3]=0x1 [4]=123 [5]=0x42 */
static void test_sce_agc_dcb_draw_index2(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbDrawIndex2(&cb, 0x1000, 0x100000080ULL, 123, 0x42);
    TEST_ASSERT(cmd == buffer, "DcbDrawIndex2 returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDEX_2, "DcbDrawIndex2 opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 6, "DcbDrawIndex2 length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 6, "DcbDrawIndex2 advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x1000u, "DcbDrawIndex2 max_size");
    TEST_ASSERT_EQ(cmd[2], 0x80u, "DcbDrawIndex2 index_base lo");
    TEST_ASSERT_EQ(cmd[3], 0x1u, "DcbDrawIndex2 index_base hi");
    TEST_ASSERT_EQ(cmd[4], 123u, "DcbDrawIndex2 index_count");
    TEST_ASSERT_EQ(cmd[5], 0x42u, "DcbDrawIndex2 draw_initiator");

    TEST_ASSERT(sceAgcDcbDrawIndex2(NULL, 0, 0x100, 0, 0) == 0,
        "DcbDrawIndex2 rejects NULL cb");

    /* Odd address gets word-aligned (bit 0 cleared) */
    agcCbInit(&cb, buffer, sizeof(buffer));
    uint32_t* cmd_odd = sceAgcDcbDrawIndex2(&cb, 0, 0x100000081ULL, 0, 0);
    TEST_ASSERT(cmd_odd != NULL, "DcbDrawIndex2 odd addr returns packet");
    TEST_ASSERT_EQ(cmd_odd[2], 0x80u, "DcbDrawIndex2 aligns odd addr lo (clears bit 0)");

    SceAgcCb small;
    uint32_t small_buf[5];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbDrawIndex2(&small, 0, 0x100, 0, 0) == 0,
        "DcbDrawIndex2 rejects overflow");
}

/* sceAgcDcbDrawIndexIndirect — IT_DRAW_INDEX_INDIRECT (0x25), 5 dwords.
 * Same layout as DrawIndirect but with INDEX variant opcode. */
static void test_sce_agc_dcb_draw_index_indirect(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbDrawIndexIndirect(&cb, 0x100, 0x200, 0x300, 0x42);
    TEST_ASSERT(cmd == buffer, "DcbDrawIndexIndirect returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDEX_INDIRECT, "DcbDrawIndexIndirect opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5, "DcbDrawIndexIndirect length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 5, "DcbDrawIndexIndirect advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x100u, "DcbDrawIndexIndirect data_offset");
    TEST_ASSERT_EQ(cmd[2], 0x200u, "DcbDrawIndexIndirect base_vtx_loc");
    TEST_ASSERT_EQ(cmd[3], 0x300u, "DcbDrawIndexIndirect start_inst_loc");
    TEST_ASSERT_EQ(cmd[4], 0x42u, "DcbDrawIndexIndirect draw_initiator");

    TEST_ASSERT(sceAgcDcbDrawIndexIndirect(NULL, 0, 0, 0, 0) == 0,
        "DcbDrawIndexIndirect rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[4];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbDrawIndexIndirect(&small, 0, 0, 0, 0) == 0,
        "DcbDrawIndexIndirect rejects overflow");
}

/* sceAgcDcbDrawIndirectMulti — IT_DRAW_INDIRECT_MULTI (0x2C), 7 dwords.
 * data_offset=0x100 base_vtx=0x200 start_inst=0x300 count=4 stride=0x40 init=0x42
 * → [1]=0x100 [2]=0x200 [3]=0x300 [4]=4 [5]=0x40 [6]=0x42 */
static void test_sce_agc_dcb_draw_indirect_multi(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbDrawIndirectMulti(&cb, 0x100, 0x200, 0x300, 4, 0x40, 0x42);
    TEST_ASSERT(cmd == buffer, "DcbDrawIndirectMulti returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDIRECT_MULTI, "DcbDrawIndirectMulti opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 7, "DcbDrawIndirectMulti length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 7, "DcbDrawIndirectMulti advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x100u, "DcbDrawIndirectMulti data_offset");
    TEST_ASSERT_EQ(cmd[2], 0x200u, "DcbDrawIndirectMulti base_vtx_loc");
    TEST_ASSERT_EQ(cmd[3], 0x300u, "DcbDrawIndirectMulti start_inst_loc");
    TEST_ASSERT_EQ(cmd[4], 4u, "DcbDrawIndirectMulti count");
    TEST_ASSERT_EQ(cmd[5], 0x40u, "DcbDrawIndirectMulti stride");
    TEST_ASSERT_EQ(cmd[6], 0x42u, "DcbDrawIndirectMulti draw_initiator");

    TEST_ASSERT(sceAgcDcbDrawIndirectMulti(NULL, 0, 0, 0, 0, 0, 0) == 0,
        "DcbDrawIndirectMulti rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[6];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbDrawIndirectMulti(&small, 0, 0, 0, 0, 0, 0) == 0,
        "DcbDrawIndirectMulti rejects overflow");
}

/* sceAgcDcbDrawIndexIndirectMulti — IT_DRAW_INDEX_INDIRECT_MULTI (0x38), 7 dwords.
 * Same layout as DrawIndirectMulti but with INDEX variant opcode. */
static void test_sce_agc_dcb_draw_index_indirect_multi(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbDrawIndexIndirectMulti(&cb, 0x100, 0x200, 0x300, 4, 0x40, 0x42);
    TEST_ASSERT(cmd == buffer, "DcbDrawIndexIndirectMulti returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_DRAW_INDEX_INDIRECT_MULTI, "DcbDrawIndexIndirectMulti opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 7, "DcbDrawIndexIndirectMulti length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 7, "DcbDrawIndexIndirectMulti advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x100u, "DcbDrawIndexIndirectMulti data_offset");
    TEST_ASSERT_EQ(cmd[2], 0x200u, "DcbDrawIndexIndirectMulti base_vtx_loc");
    TEST_ASSERT_EQ(cmd[3], 0x300u, "DcbDrawIndexIndirectMulti start_inst_loc");
    TEST_ASSERT_EQ(cmd[4], 4u, "DcbDrawIndexIndirectMulti count");
    TEST_ASSERT_EQ(cmd[5], 0x40u, "DcbDrawIndexIndirectMulti stride");
    TEST_ASSERT_EQ(cmd[6], 0x42u, "DcbDrawIndexIndirectMulti draw_initiator");

    TEST_ASSERT(sceAgcDcbDrawIndexIndirectMulti(NULL, 0, 0, 0, 0, 0, 0) == 0,
        "DcbDrawIndexIndirectMulti rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[6];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbDrawIndexIndirectMulti(&small, 0, 0, 0, 0, 0, 0) == 0,
        "DcbDrawIndexIndirectMulti rejects overflow");
}

/* sceAgcDcbSetPredication — IT_SET_PREDICATION (0x20), 3 dwords.
 * addr=0x1_0000_0027 op=2 keep_count=0x1234 predicate=true
 * addr lo = 0x27 & ~0xF = 0x20
 * addr hi = 0x1, op<<16 = 0x20000, keep<<18 = 0x48D0000, pred<<31 = 0x80000000
 * [2] = 0x80000000 | 0x48D0000 | 0x20000 | 0x1 = 0xC8D20001
 * (0x48D0000 = 0x048D0000, | 0x00020000 = 0x048F0000, | 0x1 = 0x048F0001, | 0x80000000 = 0x8048F0001... wait)
 * Recalc: 0x1234 = 4660. 4660 << 18 = 4660 * 262144 = 1,222,174,720 = 0x48D00000
 * 2 << 16 = 0x00020000
 * 1 << 31 = 0x80000000
 * 0x48D00000 | 0x00020000 = 0x48D20000
 * 0x48D20000 | 0x00000001 = 0x48D20001
 * 0x48D20001 | 0x80000000 = 0xC8D20001 */
static void test_sce_agc_dcb_set_predication(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbSetPredication(&cb, 0x100000027ULL, 2, 0x1234, true);
    TEST_ASSERT(cmd == buffer, "DcbSetPredication returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_PREDICATION, "DcbSetPredication opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 3, "DcbSetPredication length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 3, "DcbSetPredication advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x20u, "DcbSetPredication addr lo aligned");
    TEST_ASSERT_EQ(cmd[2], 0xC8D20001u, "DcbSetPredication addr hi|op|keep|pred");

    /* predicate=false clears bit 31 */
    uint32_t* cmd2 = sceAgcDcbSetPredication(&cb, 0x100000020ULL, 0, 0, false);
    TEST_ASSERT(cmd2 != NULL, "DcbSetPredication false returns allocated packet");
    TEST_ASSERT_EQ(cmd2[2], 0x1u, "DcbSetPredication false clears predicate bit");

    TEST_ASSERT(sceAgcDcbSetPredication(NULL, 0x100, 0, 0, false) == 0,
        "DcbSetPredication rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[2];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbSetPredication(&small, 0x100, 0, 0, false) == 0,
        "DcbSetPredication rejects overflow");
}

/* sceAgcDcbEventWrite — IT_EVENT_WRITE (0x46), 2 dwords.
 * event_type=0x05 event_index=0x03 → [1]=0x0305 */
static void test_sce_agc_dcb_event_write(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t* cmd = sceAgcDcbEventWrite(&cb, 0x05, 0x03);
    TEST_ASSERT(cmd == buffer, "DcbEventWrite returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_EVENT_WRITE, "DcbEventWrite opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 2, "DcbEventWrite length");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 2, "DcbEventWrite advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x0305u, "DcbEventWrite event_control");

    TEST_ASSERT(sceAgcDcbEventWrite(NULL, 0, 0) == 0,
        "DcbEventWrite rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[1];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbEventWrite(&small, 0, 0) == 0,
        "DcbEventWrite rejects overflow");
}

/* sceAgcDcbSetConfigReg — IT_SET_CONFIG_REG (0x68), variable (count+2).
 * reg_offset=0x200 values={0xAA,0xBB,0xCC} count=3
 * → [0] header len=5 [1]=0x200 [2]=0xAA [3]=0xBB [4]=0xCC */
static void test_sce_agc_dcb_set_config_reg(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t values[3] = {0xAA, 0xBB, 0xCC};
    uint32_t* cmd = sceAgcDcbSetConfigReg(&cb, 0x200, values, 3);
    TEST_ASSERT(cmd == buffer, "DcbSetConfigReg returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_CONFIG_REG, "DcbSetConfigReg opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 5, "DcbSetConfigReg length (3+2)");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 5, "DcbSetConfigReg advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x200u, "DcbSetConfigReg reg_offset");
    TEST_ASSERT_EQ(cmd[2], 0xAAu, "DcbSetConfigReg value 0");
    TEST_ASSERT_EQ(cmd[3], 0xBBu, "DcbSetConfigReg value 1");
    TEST_ASSERT_EQ(cmd[4], 0xCCu, "DcbSetConfigReg value 2");

    /* NULL values rejected */
    TEST_ASSERT(sceAgcDcbSetConfigReg(&cb, 0x200, NULL, 3) == 0,
        "DcbSetConfigReg rejects NULL values");
    /* zero count rejected */
    TEST_ASSERT(sceAgcDcbSetConfigReg(&cb, 0x200, values, 0) == 0,
        "DcbSetConfigReg rejects zero count");
    /* excessive count rejected */
    TEST_ASSERT(sceAgcDcbSetConfigReg(&cb, 0x200, values, 0x4000) == 0,
        "DcbSetConfigReg rejects excessive count");
    /* NULL cb rejected */
    TEST_ASSERT(sceAgcDcbSetConfigReg(NULL, 0x200, values, 1) == 0,
        "DcbSetConfigReg rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[3];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbSetConfigReg(&small, 0x200, values, 3) == 0,
        "DcbSetConfigReg rejects overflow");
}

/* sceAgcDcbSetShReg — IT_SET_SH_REG (0x76), variable (count+2).
 * Same layout as SetConfigReg but with SET_SH_REG opcode. */
static void test_sce_agc_dcb_set_sh_reg(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t values[2] = {0x1234, 0x5678};
    uint32_t* cmd = sceAgcDcbSetShReg(&cb, 0x2C0, values, 2);
    TEST_ASSERT(cmd == buffer, "DcbSetShReg returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_SH_REG, "DcbSetShReg opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "DcbSetShReg length (2+2)");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 4, "DcbSetShReg advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x2C0u, "DcbSetShReg reg_offset");
    TEST_ASSERT_EQ(cmd[2], 0x1234u, "DcbSetShReg value 0");
    TEST_ASSERT_EQ(cmd[3], 0x5678u, "DcbSetShReg value 1");

    TEST_ASSERT(sceAgcDcbSetShReg(&cb, 0x2C0, NULL, 2) == 0,
        "DcbSetShReg rejects NULL values");
    TEST_ASSERT(sceAgcDcbSetShReg(&cb, 0x2C0, values, 0) == 0,
        "DcbSetShReg rejects zero count");
    TEST_ASSERT(sceAgcDcbSetShReg(NULL, 0x2C0, values, 1) == 0,
        "DcbSetShReg rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[3];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbSetShReg(&small, 0x2C0, values, 2) == 0,
        "DcbSetShReg rejects overflow");
}

/* sceAgcDcbSetUconfigReg — IT_SET_UCONFIG_REG (0x79), variable (count+2).
 * Same layout as SetConfigReg but with SET_UCONFIG_REG opcode. */
static void test_sce_agc_dcb_set_uconfig_reg(void) {
    uint32_t buffer[16];
    SceAgcCb cb;
    agcCbInit(&cb, buffer, sizeof(buffer));

    uint32_t values[2] = {0xDEAD, 0xBEEF};
    uint32_t* cmd = sceAgcDcbSetUconfigReg(&cb, 0x300, values, 2);
    TEST_ASSERT(cmd == buffer, "DcbSetUconfigReg returns allocated packet");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_SET_UCONFIG_REG, "DcbSetUconfigReg opcode");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 4, "DcbSetUconfigReg length (2+2)");
    TEST_ASSERT_EQ(agcCbUsedDwords(&cb), 4, "DcbSetUconfigReg advances cursor");
    TEST_ASSERT_EQ(cmd[1], 0x300u, "DcbSetUconfigReg reg_offset");
    TEST_ASSERT_EQ(cmd[2], 0xDEADu, "DcbSetUconfigReg value 0");
    TEST_ASSERT_EQ(cmd[3], 0xBEEFu, "DcbSetUconfigReg value 1");

    TEST_ASSERT(sceAgcDcbSetUconfigReg(&cb, 0x300, NULL, 2) == 0,
        "DcbSetUconfigReg rejects NULL values");
    TEST_ASSERT(sceAgcDcbSetUconfigReg(&cb, 0x300, values, 0) == 0,
        "DcbSetUconfigReg rejects zero count");
    TEST_ASSERT(sceAgcDcbSetUconfigReg(NULL, 0x300, values, 1) == 0,
        "DcbSetUconfigReg rejects NULL cb");

    SceAgcCb small;
    uint32_t small_buf[3];
    agcCbInit(&small, small_buf, sizeof(small_buf));
    TEST_ASSERT(sceAgcDcbSetUconfigReg(&small, 0x300, values, 2) == 0,
        "DcbSetUconfigReg rejects overflow");
}

void test_suite_cb(void) {
    TEST_SUITE("Command Buffer Builders");
    TEST_RUN(test_cb_layout_offsets);
    TEST_RUN(test_cb_alloc);
    TEST_RUN(test_sce_agc_cb_nop);
    TEST_RUN(test_sce_agc_cb_dispatch);
    TEST_RUN(test_sce_agc_cb_set_sh_registers);
    TEST_RUN(test_sce_agc_cb_memset_exclusive);
    TEST_RUN(test_sce_agc_cb_set_cx_registers_direct);
    TEST_RUN(test_sce_agc_cb_set_cx_registers_direct_rejects_invalid);
    TEST_RUN(test_sce_agc_cb_set_uc_registers_direct);
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
    TEST_RUN(test_sce_agc_dcb_wait_flip_done);
    TEST_RUN(test_sce_agc_dcb_wait_flip_done_rejects_invalid);
    TEST_RUN(test_sce_agc_dcb_wait_flip);
    TEST_RUN(test_sce_agc_dcb_insert_wait_flip_done);
    TEST_RUN(test_sce_agc_dcb_wait_flip_eos);
    TEST_RUN(test_sce_agc_dcb_indirect_buffer);
    TEST_RUN(test_sce_agc_dcb_draw_indirect);
    TEST_RUN(test_sce_agc_dcb_draw_index2);
    TEST_RUN(test_sce_agc_dcb_draw_index_indirect);
    TEST_RUN(test_sce_agc_dcb_draw_indirect_multi);
    TEST_RUN(test_sce_agc_dcb_draw_index_indirect_multi);
    TEST_RUN(test_sce_agc_dcb_set_predication);
    TEST_RUN(test_sce_agc_dcb_event_write);
    TEST_RUN(test_sce_agc_dcb_set_config_reg);
    TEST_RUN(test_sce_agc_dcb_set_sh_reg);
    TEST_RUN(test_sce_agc_dcb_set_uconfig_reg);
}
