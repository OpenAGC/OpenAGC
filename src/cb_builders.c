/*
 * openagc - cb_builders.c
 *
 * Sony-style command-buffer packet builders recovered from Gen5 AGC HLE.
 */

#include "agc_cb.h"
#include "agc_pm4.h"
#include "agc_types.h"
#include "agcdriver.h"

#include <string.h>

uint32_t *PS5_SYSV_ABI sceAgcCbNop(SceAgcCb *cb, uint32_t dword_count)
{
    if (dword_count < 2 || dword_count > 0x4001)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, dword_count);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_ZERO, dword_count);
    for (uint32_t i = 1; i < dword_count; ++i)
        cmd[i] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcCbDispatch(
    SceAgcCb *cb, uint32_t group_count_x, uint32_t group_count_y,
    uint32_t group_count_z, uint32_t modifier)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DISPATCH_DIRECT, 5);
    cmd[1] = group_count_x;
    cmd[2] = group_count_y;
    cmd[3] = group_count_z;
    cmd[4] = (modifier & 0xA038u) | 0x41u;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcCbSetShRegistersDirect(
    SceAgcCb *cb, const AgcRegisterValue *registers, uint32_t register_count)
{
    if (!registers || register_count == 0 || register_count > 0x3FFE)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, register_count + 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG, register_count + 2);
    cmd[1] = registers[0].offset & 0xFFFFu;
    for (uint32_t i = 0; i < register_count; ++i)
        cmd[2 + i] = registers[i].value;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcCbSetCxRegistersDirect(
    SceAgcCb *cb, const AgcRegisterValue *registers, uint32_t register_count)
{
    if (!registers || register_count == 0 || register_count > 0x3FFE)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, register_count + 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, register_count + 2);
    cmd[1] = registers[0].offset & 0xFFFFu;
    for (uint32_t i = 0; i < register_count; ++i)
        cmd[2 + i] = registers[i].value;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbWriteData(
    SceAgcCb *cb, uint32_t destination, uint32_t cache_policy,
    uint64_t destination_address, const uint32_t *data, uint32_t dword_count,
    uint32_t increment, uint32_t write_confirm)
{
    if (!destination_address || !data || dword_count > 0x3FFD)
        return 0;

    uint32_t packet_dwords = dword_count + 4;
    uint32_t *cmd = agcCbAllocDwords(cb, packet_dwords);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_WRITE_DATA, packet_dwords);
    cmd[1] = (destination & 0xFFu) |
        ((cache_policy & 0xFFu) << 8) |
        ((increment & 0xFFu) << 16) |
        ((write_confirm & 0xFFu) << 24);
    cmd[2] = (uint32_t)destination_address;
    cmd[3] = (uint32_t)(destination_address >> 32);
    memcpy(&cmd[4], data, dword_count * sizeof(uint32_t));
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbWaitRegMem(
    SceAgcCb *cb, uint32_t size, uint32_t compare_function, uint32_t operation,
    uint32_t cache_policy, uint64_t address, uint64_t reference,
    uint64_t mask, uint32_t poll_cycles)
{
    (void)cache_policy;
    if (size > 1 || compare_function > 7 || operation > 4)
        return 0;

    uint32_t standard_wait = operation == 2 || operation == 3;
    uint32_t packet_dwords = standard_wait ? 7u : (size == 0 ? 6u : 9u);
    uint32_t *cmd = agcCbAllocDwords(cb, packet_dwords);
    if (!cmd)
        return 0;

    if (standard_wait) {
        cmd[0] = agcPm4Header3(AGC_PM4_OP_WAIT_REG_MEM, packet_dwords);
        cmd[1] = compare_function | ((operation & 1u) << 8);
        cmd[2] = (uint32_t)address;
        cmd[3] = (uint32_t)(address >> 32);
        cmd[4] = (uint32_t)reference;
        cmd[5] = (uint32_t)mask;
        cmd[6] = poll_cycles / 40u;
        return cmd;
    }

    cmd[0] = agcPm4Header3Sub(
        AGC_PM4_OP_NOP,
        size == 0 ? AGC_PM4_SUB_WAIT_MEM32 : AGC_PM4_SUB_WAIT_MEM64,
        packet_dwords);
    cmd[1] = (uint32_t)address;
    cmd[2] = (uint32_t)(address >> 32);
    cmd[3] = (uint32_t)mask;
    if (size == 0) {
        cmd[4] = compare_function | (operation << 8);
        cmd[5] = (uint32_t)reference;
    } else {
        cmd[4] = (uint32_t)(mask >> 32);
        cmd[5] = (uint32_t)reference;
        cmd[6] = (uint32_t)(reference >> 32);
        cmd[7] = compare_function | (operation << 8);
        cmd[8] = poll_cycles / 40u;
    }
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbPushMarker(SceAgcCb *cb, const char *marker)
{
    if (!marker)
        return 0;

    uint32_t len = (uint32_t)strlen(marker);
    uint32_t payload_dwords = (len + 4u) / 4u;
    if (payload_dwords == 0)
        payload_dwords = 1;

    uint32_t packet_dwords = payload_dwords + 1;
    uint32_t *cmd = agcCbAllocDwords(cb, packet_dwords);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_PUSH_MARKER, packet_dwords);
    for (uint32_t i = 1; i < packet_dwords; ++i)
        cmd[i] = 0;
    memcpy(&cmd[1], marker, len);
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbDmaData(
    SceAgcCb *cb, uint32_t destination, uint32_t destination_cache_policy,
    uint32_t source, uint64_t destination_address, uint32_t source_cache_policy,
    uint32_t control4, uint64_t source_address, uint32_t byte_count,
    uint32_t control7, uint32_t control8, uint32_t control9)
{
    if (byte_count == 0 || (byte_count & 3u) != 0)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_DMA_DATA, 8);
    cmd[1] = (destination & 0xFFu) |
        ((destination_cache_policy & 0xFFu) << 8) |
        ((source & 0xFFu) << 16) |
        ((source_cache_policy & 0xFFu) << 24);
    cmd[2] = (control4 & 0xFFu) |
        ((control7 & 0xFFu) << 8) |
        ((control8 & 0xFFu) << 16) |
        ((control9 & 0xFFu) << 24);
    cmd[3] = byte_count;
    cmd[4] = (uint32_t)destination_address;
    cmd[5] = (uint32_t)(destination_address >> 32);
    cmd[6] = (uint32_t)source_address;
    cmd[7] = (uint32_t)(source_address >> 32);
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetBaseIndirectArgs(
    SceAgcCb *cb, uint32_t base_index, uint64_t address)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_BASE, 4) | (base_index << 1);
    cmd[1] = 1;
    cmd[2] = (uint32_t)address & ~7u;
    cmd[3] = (uint32_t)(address >> 32);
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbDispatchIndirect(
    SceAgcCb *cb, uint32_t data_offset, uint32_t modifier)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DISPATCH_INDIRECT, 3);
    cmd[1] = data_offset;
    cmd[2] = (modifier & 0xA038u) | 0x41u;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexBuffer(
    SceAgcCb *cb, uint64_t index_buffer_address, uint32_t index_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDEX_BASE, 3);
    cmd[1] = (uint32_t)index_buffer_address;
    cmd[2] = (uint32_t)(index_buffer_address >> 32);
    cmd[3] = agcPm4Header3(AGC_PM4_OP_INDEX_BUFFER_SIZE, 2);
    cmd[4] = index_count;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexOffset(
    SceAgcCb *cb, uint32_t index_offset, uint32_t index_count, uint32_t flags)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_OFFSET_2, 5);
    cmd[1] = index_count;
    cmd[2] = index_offset;
    cmd[3] = index_count;
    cmd[4] = flags & 0xE0000001u;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexAuto(
    SceAgcCb *cb, uint32_t index_count, uint64_t modifier)
{
    if (modifier != 0x40000000ull)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 7);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_DRAW_INDEX_AUTO, 7);
    cmd[1] = index_count;
    cmd[2] = 0;
    cmd[3] = 0;
    cmd[4] = 0;
    cmd[5] = 0;
    cmd[6] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbWaitUntilSafeForRendering(
    SceAgcCb *cb, uint32_t video_out_handle, uint32_t display_buffer_index)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 7);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_WAIT_FLIP_DONE, 7);
    cmd[1] = video_out_handle;
    cmd[2] = display_buffer_index;
    cmd[3] = 0;
    cmd[4] = 0;
    cmd[5] = 0;
    cmd[6] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbPopMarker(SceAgcCb *cb)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_POP_MARKER, 2);
    cmd[1] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetFlip(
    SceAgcCb *cb, uint32_t video_out_handle, int32_t display_buffer_index,
    uint32_t flip_mode, uint64_t flip_arg)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 6);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_FLIP, 6);
    cmd[1] = video_out_handle;
    cmd[2] = (uint32_t)display_buffer_index;
    cmd[3] = flip_mode;
    cmd[4] = (uint32_t)flip_arg;
    cmd[5] = (uint32_t)(flip_arg >> 32);
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcCbReleaseMem(
    SceAgcCb *cb, uint32_t action, uint32_t gcr_control, uint32_t destination,
    uint32_t cache_policy, uint64_t destination_address, uint32_t data_selection,
    uint64_t data, uint32_t gds_offset, uint32_t gds_size, uint32_t interrupt,
    uint32_t interrupt_context_id)
{
    /*
     * RE source: SharpEmu sceAgcCbReleaseMem (NID wr23dPKyWc0).
     * 8-dword IT_NOP packet, subcommand 0x18 (AGC_PM4_SUB_RELEASE_MEM).
     * Layout:
     *   [0] header
     *   [1] action[7:0] | cache_policy[15:8]
     *   [2] gcr_control[15:0] | data_selection[23:16] | interrupt[31:24]
     *   [3..4] destination_address (lo/hi)
     *   [5..6] data (lo/hi)
     *   [7] interrupt_context_id
     * gds_offset/gds_size are accepted but gds_offset must be 0 and
     * gds_size <= 2 (SharpEmu rejects otherwise); they are not currently
     * encoded into the packet because the GDS path is unused on Gen5 AGC.
     */
    if (destination > 1 || data_selection > 3 || gds_offset != 0 ||
        gds_size > 2 || interrupt > 3)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_RELEASE_MEM, 8);
    cmd[1] = (action & 0xFFu) | ((cache_policy & 0xFFu) << 8);
    cmd[2] = (gcr_control & 0xFFFFu) |
        ((data_selection & 0xFFu) << 16) |
        ((interrupt & 0xFFu) << 24);
    cmd[3] = (uint32_t)destination_address;
    cmd[4] = (uint32_t)(destination_address >> 32);
    cmd[5] = (uint32_t)data;
    cmd[6] = (uint32_t)(data >> 32);
    cmd[7] = interrupt_context_id;
    return cmd;
}

/*
 * RE source: SharpEmu DcbSetRegistersIndirect (sceAgcDcbSet{Sh,Cx,Uc}RegistersIndirect).
 * Three exports share an identical 4-dword IT_NOP packet, differing only by
 * subcommand (0x11 SH / 0x12 CX / 0x13 UC). Layout:
 *   [0] header
 *   [1] register_count
 *   [2..3] registers_address (lo/hi) — GPU address of the register-value array
 */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetShRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_SH_REGS_INDIRECT, 4);
    cmd[1] = register_count;
    cmd[2] = (uint32_t)registers_address;
    cmd[3] = (uint32_t)(registers_address >> 32);
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetCxRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_CX_REGS_INDIRECT, 4);
    cmd[1] = register_count;
    cmd[2] = (uint32_t)registers_address;
    cmd[3] = (uint32_t)(registers_address >> 32);
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetUcRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_UC_REGS_INDIRECT, 4);
    cmd[1] = register_count;
    cmd[2] = (uint32_t)registers_address;
    cmd[3] = (uint32_t)(registers_address >> 32);
    return cmd;
}

/*
 * RE source: SharpEmu sceAgc{DmaData,WaitRegMem,QueueEndOfPipeAction}PatchAddress.
 * These are in-place patchers — they take a pointer to an already-emitted packet
 * (the return value from a builder) and overwrite a specific 64-bit address field.
 * They return int32_t AGC error codes, not uint32_t* like the builders.
 *
 * DmaDataPatchSetDstAddressOrOffset: patches cmd[4..5] (dst address at +16 bytes)
 *   in a DmaData packet (NOP + sub 0x19).
 * WaitRegMemPatchAddress: patches cmd[2..3] (addr at +8 bytes) for a standard
 *   WAIT_REG_MEM packet (op 0x3C), or cmd[1..2] (addr at +4 bytes) for a
 *   NOP-wrapped wait (sub 0x0A WAIT_MEM32 or 0x16 WAIT_MEM64).
 * QueueEndOfPipeActionPatchAddress: patches cmd[3..4] (dst address at +12 bytes)
 *   in a ReleaseMem packet (NOP + sub 0x18).
 */
int32_t PS5_SYSV_ABI sceAgcDmaDataPatchSetDstAddressOrOffset(
    uint32_t *cmd, uint64_t destination_address)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t opcode = agcPm4Opcode(cmd[0]);
    uint32_t sub = agcPm4Subcommand(cmd[0]);
    if (opcode != AGC_PM4_OP_NOP || sub != AGC_PM4_SUB_DMA_DATA)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[4] = (uint32_t)destination_address;
    cmd[5] = (uint32_t)(destination_address >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchAddress(
    uint32_t *cmd, uint64_t address)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t opcode = agcPm4Opcode(cmd[0]);
    uint32_t sub = agcPm4Subcommand(cmd[0]);

    uint32_t field_index;
    if (opcode == AGC_PM4_OP_WAIT_REG_MEM) {
        field_index = 2;
    } else if (opcode == AGC_PM4_OP_NOP &&
               (sub == AGC_PM4_SUB_WAIT_MEM32 || sub == AGC_PM4_SUB_WAIT_MEM64)) {
        field_index = 1;
    } else {
        return AGC_ERROR_INVALID_ARGUMENT;
    }

    cmd[field_index] = (uint32_t)address;
    cmd[field_index + 1] = (uint32_t)(address >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcQueueEndOfPipeActionPatchAddress(
    uint32_t *cmd, uint64_t address)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t opcode = agcPm4Opcode(cmd[0]);
    uint32_t sub = agcPm4Subcommand(cmd[0]);
    if (opcode != AGC_PM4_OP_NOP || sub != AGC_PM4_SUB_RELEASE_MEM)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[3] = (uint32_t)address;
    cmd[4] = (uint32_t)(address >> 32);
    return AGC_OK;
}

/*
 * RE source: SharpEmu sceAgcDcbGetLodStats / sceAgcDcbGetLodStatsGetSize.
 * GetLodStats emits a 5-dword IT_GET_LOD_STATS packet (op 0x8E, sub 0):
 *   [0] header
 *   [1] control
 *   [2..3] destination_address (lo/hi) — lo masked & ~0x3F (64-byte aligned)
 *   [4] packet_control = (cachePolicy<<28) | (enable<<19) | (reset<<18)
 *                        | (counterMask<<10) | (counterSelect<<2)
 * GetLodStatsGetSize is a pure helper: returns 0x10 + counterCount*4 (the
 * byte size of the LOD-stats output buffer the GPU writes back).
 */
size_t PS5_SYSV_ABI sceAgcDcbGetLodStatsGetSize(uint32_t counter_count)
{
    return 0x10u + (counter_count * sizeof(uint32_t));
}

uint32_t *PS5_SYSV_ABI sceAgcDcbGetLodStats(
    SceAgcCb *cb, uint32_t cache_policy, uint64_t destination_address,
    uint32_t control, uint32_t counter_mask, uint32_t reset_counters,
    uint32_t enable, uint32_t counter_select)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    uint32_t packet_control =
        ((cache_policy & 0x3u) << 28) |
        ((enable & 0x1u) << 19) |
        ((reset_counters & 0x1u) << 18) |
        ((counter_mask & 0xFFu) << 10) |
        ((counter_select & 0xFFu) << 2);

    cmd[0] = agcPm4Header3(AGC_PM4_OP_GET_LOD_STATS, 5);
    cmd[1] = control;
    cmd[2] = (uint32_t)destination_address & ~0x3Fu;
    cmd[3] = (uint32_t)(destination_address >> 32);
    cmd[4] = packet_control;
    return cmd;
}
