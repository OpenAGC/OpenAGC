/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * openagc - cb_builders.c
 *
 * Sony-style command-buffer packet builders recovered from Gen5 AGC HLE.
 */

#include "agc_cb.h"
#include "agc_pm4.h"
#include "agc_registers.h"
#include "agc_types.h"
#include "agcdriver.h"
#include "memset_exclusive_shader.h"

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

/* sceAgcCbMemsetExclusive (compatibility NID: 6nths4DHNrs) - bundled SPRX
 * @ 0x3100. Sony's implementation binds an internal compute shader rather
 * than lowering this operation to DMA_DATA. OpenAGC mirrors that architecture
 * with its own gfx1013 kernel and the hardware-proven psbc compute ABI. */
uint32_t *PS5_SYSV_ABI sceAgcCbMemsetExclusive(
    SceAgcCb *cb, uint64_t destination, const void *pattern16,
    uint64_t size_bytes)
{
    enum { kPacketDwords = 32 };
    if (!cb || !pattern16 || agcCbRemainingDwords(cb) < kPacketDwords)
        return NULL;

    uint32_t pattern[4];
    memcpy(pattern, pattern16, sizeof(pattern));

    uint64_t shader_address =
        (uint64_t)(uintptr_t)s_agc_memset_exclusive_code;
    uint32_t block_count = (uint32_t)(size_bytes >> 4u);
    uint32_t group_count = (block_count + 63u) >> 6u;
    AgcRegisterValue pgm[] = {
        {AGC_REG_COMPUTE_PGM_LO, (uint32_t)(shader_address >> 8u)},
        {AGC_REG_COMPUTE_PGM_HI, (uint32_t)(shader_address >> 40u)},
    };
    AgcRegisterValue resources[] = {
        {AGC_REG_COMPUTE_PGM_RSRC1, AGC_MEMSET_EXCLUSIVE_RSRC1},
        {AGC_REG_COMPUTE_PGM_RSRC2, AGC_MEMSET_EXCLUSIVE_RSRC2},
    };
    AgcRegisterValue resource3[] = {
        {AGC_REG_COMPUTE_PGM_RSRC3, AGC_MEMSET_EXCLUSIVE_RSRC3},
    };
    AgcRegisterValue threads[] = {
        {AGC_REG_COMPUTE_NUM_THREAD_X, 64u},
        {AGC_REG_COMPUTE_NUM_THREAD_Y, 1u},
        {AGC_REG_COMPUTE_NUM_THREAD_Z, 1u},
    };
    AgcRegisterValue user_data[] = {
        {AGC_REG_COMPUTE_USER_DATA_0 + 0u, 0u},
        {AGC_REG_COMPUTE_USER_DATA_0 + 1u, 0u},
        {AGC_REG_COMPUTE_USER_DATA_0 + 2u, (uint32_t)destination},
        {AGC_REG_COMPUTE_USER_DATA_0 + 3u, (uint32_t)(destination >> 32u)},
        {AGC_REG_COMPUTE_USER_DATA_0 + 4u, block_count},
        {AGC_REG_COMPUTE_USER_DATA_0 + 5u, pattern[0]},
        {AGC_REG_COMPUTE_USER_DATA_0 + 6u, pattern[1]},
        {AGC_REG_COMPUTE_USER_DATA_0 + 7u, pattern[2]},
        {AGC_REG_COMPUTE_USER_DATA_0 + 8u, pattern[3]},
    };

    uint32_t *first = sceAgcCbSetShRegistersDirect(cb, pgm, 2u);
    first[0] |= 1u;
    uint32_t *cmd = sceAgcCbSetShRegistersDirect(cb, resources, 2u);
    cmd[0] |= 1u;
    cmd = sceAgcCbSetShRegistersDirect(cb, resource3, 1u);
    cmd[0] |= 1u;
    cmd = sceAgcCbSetShRegistersDirect(cb, threads, 3u);
    cmd[0] |= 1u;
    cmd = sceAgcCbSetShRegistersDirect(cb, user_data, 9u);
    cmd[0] |= 1u;
    cmd = sceAgcCbDispatch(cb, group_count, 1u, 1u, 0u);
    cmd[0] |= 1u;
    return first;
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

    /* SPRX-confirmed: uses IT_WRITE_DATA (0x37) directly, not NOP-wrapped.
     * cmd[1] = (dst & 1) << 30 | (dst & 0x1e) << 7 | (increment & 1) << 16 |
     *          (write_confirm & 1) << 20 | (cache_policy & 3) << 25
     * write_confirm is always encoded (no conditional on dst). */
    uint32_t dst_val = destination & 0x1Fu;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, packet_dwords);
    cmd[1] = ((dst_val & 0x1u) << 30u) |
        ((dst_val & 0x1Eu) << 7u) |
        ((increment & 0x1u) << 16u) |
        ((write_confirm & 0x1u) << 20u) |
        ((cache_policy & 0x3u) << 25u);
    cmd[2] = (uint32_t)destination_address & ~0x3u;
    cmd[3] = (uint32_t)(destination_address >> 32);
    memcpy(&cmd[4], data, dword_count * sizeof(uint32_t));
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbWaitRegMem(
    SceAgcCb *cb, uint32_t size, uint32_t compare_function, uint32_t operation,
    uint32_t cache_policy, uint64_t address, uint64_t reference,
    uint64_t mask, uint32_t poll_cycles)
{
    /* reference-confirmed encoding for NOP-wrapped WaitRegMem.
     *
     * 32-bit (size=0): 7 dwords, IT_NOP + R_WAIT_MEM32
     *   [0] header
     *   [1] address_lo & ~0x3
     *   [2] (address >> 32) & 0x3FFFF
     *   [3] mask_lo
     *   [4] reference_lo
     *   [5] control = 0x10 | (cmp&7) | ((op&3)<<8) | ((op&0xC)<<4) | ((cache&3)<<25)
     *   [6] poll = min(poll_cycles >> 4, 0xFFFF)
     *
     * 64-bit (size=1): 9 dwords, IT_NOP + R_WAIT_MEM64
     *   [0] header
     *   [1] address_lo & ~0x7
     *   [2] (address >> 32) & 0x3FFFF
     *   [3] mask_lo
     *   [4] mask_hi
     *   [5] reference_lo
     *   [6] reference_hi
     *   [7] control = 0x10 | (cmp&7) | ((op&1)<<8) | ((op&6)<<5) | ((cache&3)<<25)
     *   [8] poll = min(poll_cycles >> 4, 0xFFFF)
     *
     * SharpEmu and KytyPS5 both keep this NOP-wrapped layout for every
     * supported operation value. */
    if (size > 1 || compare_function > 7 || operation > 4 || cache_policy > 3)
        return 0;

    uint32_t packet_dwords = size == 0 ? 7u : 9u;
    uint32_t *cmd = agcCbAllocDwords(cb, packet_dwords);
    if (!cmd)
        return 0;

    uint32_t poll = poll_cycles >> 4u;
    if (poll > 0xFFFFu)
        poll = 0xFFFFu;

    if (size == 0) {
        cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_WAIT_MEM32, 7);
        cmd[1] = (uint32_t)address & ~0x3u;
        cmd[2] = (uint32_t)(address >> 32) & 0x3FFFFu;
        cmd[3] = (uint32_t)mask;
        cmd[4] = (uint32_t)reference;
        cmd[5] = 0x10u | (compare_function & 0x7u) |
                 ((operation & 0x3u) << 8u) | ((operation & 0xCu) << 4u) |
                 ((cache_policy & 0x3u) << 25u);
        cmd[6] = poll;
    } else {
        cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_WAIT_MEM64, 9);
        cmd[1] = (uint32_t)address & ~0x7u;
        cmd[2] = (uint32_t)(address >> 32) & 0x3FFFFu;
        cmd[3] = (uint32_t)mask;
        cmd[4] = (uint32_t)(mask >> 32);
        cmd[5] = (uint32_t)reference;
        cmd[6] = (uint32_t)(reference >> 32);
        cmd[7] = 0x10u | (compare_function & 0x7u) |
                 ((operation & 0x1u) << 8u) | ((operation & 0x6u) << 5u) |
                 ((cache_policy & 0x3u) << 25u);
        cmd[8] = poll;
    }
    return cmd;
}

static uint32_t *agcCbMarkerSpan(
    SceAgcCb *cb, uint32_t subcommand, const char *marker,
    uint32_t length, uint32_t color)
{
    if (!cb || (!marker && length != 0u))
        return 0;

    uint64_t payload_dwords = ((uint64_t)length + 3u) / 4u;
    uint64_t packet_dwords64 = 2u + payload_dwords;
    if (packet_dwords64 > UINT32_MAX)
        return 0;
    uint32_t packet_dwords = (uint32_t)packet_dwords64;
    uint32_t *cmd = agcCbAllocDwords(cb, packet_dwords);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, subcommand, packet_dwords);
    cmd[1] = color;
    if (payload_dwords != 0u) {
        memset(&cmd[2], 0, (size_t)payload_dwords * sizeof(uint32_t));
        memcpy(&cmd[2], marker, length);
    }
    return cmd;
}

static uint32_t agcMarkerLength(const char *marker)
{
    return marker ? (uint32_t)strlen(marker) : 0u;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetMarker(
    SceAgcCb *cb, const char *marker, uint32_t color)
{
    return agcCbMarkerSpan(cb, AGC_PM4_SUB_SET_MARKER, marker,
        agcMarkerLength(marker), color);
}

uint32_t *PS5_SYSV_ABI sceAgcDcbPushMarker(
    SceAgcCb *cb, const char *marker, uint32_t color)
{
    return agcCbMarkerSpan(cb, AGC_PM4_SUB_PUSH_MARKER, marker,
        agcMarkerLength(marker), color);
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetMarkerSpan(
    SceAgcCb *cb, const char *marker, uint32_t length, uint32_t color)
{
    return agcCbMarkerSpan(
        cb, AGC_PM4_SUB_SET_MARKER, marker, length, color);
}

uint32_t *PS5_SYSV_ABI sceAgcDcbPushMarkerSpan(
    SceAgcCb *cb, const char *marker, uint32_t length, uint32_t color)
{
    return agcCbMarkerSpan(
        cb, AGC_PM4_SUB_PUSH_MARKER, marker, length, color);
}

uint32_t *PS5_SYSV_ABI sceAgcAcbSetMarker(
    SceAgcCb *cb, const char *marker, uint32_t color)
{
    return agcCbMarkerSpan(cb, AGC_PM4_SUB_SET_MARKER, marker,
        agcMarkerLength(marker), color);
}

uint32_t *PS5_SYSV_ABI sceAgcAcbPushMarker(
    SceAgcCb *cb, const char *marker, uint32_t color)
{
    return agcCbMarkerSpan(cb, AGC_PM4_SUB_PUSH_MARKER, marker,
        agcMarkerLength(marker), color);
}

uint32_t *PS5_SYSV_ABI sceAgcAcbSetMarkerSpan(
    SceAgcCb *cb, const char *marker, uint32_t length, uint32_t color)
{
    return agcCbMarkerSpan(
        cb, AGC_PM4_SUB_SET_MARKER, marker, length, color);
}

uint32_t *PS5_SYSV_ABI sceAgcAcbPushMarkerSpan(
    SceAgcCb *cb, const char *marker, uint32_t length, uint32_t color)
{
    return agcCbMarkerSpan(
        cb, AGC_PM4_SUB_PUSH_MARKER, marker, length, color);
}

uint32_t *PS5_SYSV_ABI sceAgcAcbPopMarker(SceAgcCb *cb)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2u);
    if (!cmd)
        return 0;
    cmd[0] = agcPm4Header3Sub(
        AGC_PM4_OP_NOP, AGC_PM4_SUB_POP_MARKER, 2u);
    cmd[1] = 0u;
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
    SceAgcCb *cb, uint32_t index_offset, uint32_t index_count, uint64_t modifier)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    /* reference-confirmed: cmd[1] is 1 if index_count == 0.
     * cmd[4] = decode_draw_index_initiator(modifier):
     *   if modifier bit 32 set → 0, else (modifier >> 3) & 0x20 */
    uint32_t initiator;
    if (modifier & (1ull << 32u))
        initiator = 0;
    else
        initiator = (uint32_t)(modifier >> 3) & 0x20u;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_OFFSET_2, 5);
    cmd[1] = (index_count == 0) ? 1u : index_count;
    cmd[2] = index_offset;
    cmd[3] = index_count;
    cmd[4] = initiator;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexAuto(
    SceAgcCb *cb, uint32_t index_count, uint64_t modifier)
{
    /* reference-confirmed: uses IT_DRAW_INDEX_AUTO (0x2D) directly, 3 dwords.
     * The previous NOP-wrapped 7-dword encoding was a stub that would fail
     * on real PS5 hardware. The draw initiator is decoded from the modifier:
     * if bit 32 is set, initiator base is 0; otherwise bits 8:3 of the
     * modifier map to bit 5 of the initiator. The | 0x2 sets the
     * "source select" field indicating auto-indexed draw. */
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    uint32_t initiator = 0;
    if ((modifier & (1ull << 32)) == 0)
        initiator = ((uint32_t)modifier >> 3) & 0x20u;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_AUTO, 3);
    cmd[1] = index_count;
    cmd[2] = initiator | 0x2u;
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
     * SPRX-confirmed: 8-dword IT_RELEASE_MEM (0x49) packet, direct opcode.
     * Layout:
     *   [0] header (opcode 0x49, not NOP-wrapped)
     *   [1] action[5:0] | event_index[11:8] | gcr_cntl[23:12] | cache_policy[26:25]
     *   [2] dst[17:16] | interrupt[26:24] | data_sel[31:29]
     *   [3..4] address (lo/hi), masked to 0xfffffffc
     *   [5..6] data (lo/hi)
     *   [7] interrupt_ctx_id & 0x07ffffff
     * event_index = 5 + (action >= 0x2f ? 1 : 0) = 5 or 6.
     * If interrupt == 4, address and data are zeroed.
     * If data_sel == 5, data = gds_offset | (gds_size << 16).
     */
    if (destination > 1)
        return 0;
    if (data_selection != 0 && data_selection != 1 &&
        data_selection != 2 && data_selection != 3 && data_selection != 5)
        return 0;
    if (interrupt > 4)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    uint64_t addr_val = destination_address;
    uint64_t data_val = data;
    if ((interrupt & 0x7u) == 4u) {
        addr_val = 0;
        data_val = 0;
    } else if ((data_selection & 0x7u) == 5u) {
        data_val = (uint64_t)gds_offset | ((uint64_t)gds_size << 16);
    }

    uint32_t packet_action = action & 0x3Fu;
    uint32_t event_index = (action >= 0x2Fu) ? 6u : 5u;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_RELEASE_MEM, 8);
    cmd[1] = packet_action |
        (event_index << 8u) |
        ((gcr_control & 0xFFFu) << 12u) |
        ((cache_policy & 0x3u) << 25u);
    cmd[2] = ((destination & 0x3u) << 16u) |
        ((interrupt & 0x7u) << 24u) |
        ((data_selection & 0x7u) << 29u);
    cmd[3] = (uint32_t)addr_val & 0xFFFFFFFCu;
    cmd[4] = (uint32_t)(addr_val >> 32);
    cmd[5] = (uint32_t)data_val;
    cmd[6] = (uint32_t)(data_val >> 32);
    cmd[7] = interrupt_context_id & 0x07FFFFFFu;
    return cmd;
}

/*
 * RE source: HLE reference DcbSetRegistersIndirect (sceAgcDcbSet{Sh,Cx,Uc}RegistersIndirect).
 * Three exports share an identical 4-dword IT_NOP packet, differing only by
 * subcommand (0x11 SH / 0x12 CX / 0x13 UC). Layout:
 *   [0] header
 *   [1] register_count
 *   [2..3] registers_address (lo/hi) — GPU address of the register-value array
 */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetShRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count)
{
    /* RE: SPRX uses opcode 0x63 (SET_SH_REG_INDIRECT), 5 dwords.
     * Format: [0] header, [1] addr_lo&~3, [2] addr_hi,
     *         [3] 0x80000000, [4] count&0x3FFF */
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG_INDIRECT, 5);
    cmd[1] = (uint32_t)registers_address & ~3u;
    cmd[2] = (uint32_t)(registers_address >> 32);
    cmd[3] = 0x80000000u;
    cmd[4] = register_count & 0x3FFFu;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetCxRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count)
{
    /* RE: SPRX uses opcode 0x9F (SET_CX_REG_INDIRECT), 5 dwords.
     * Same format as Sh variant. */
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CX_REG_INDIRECT, 5);
    cmd[1] = (uint32_t)registers_address & ~3u;
    cmd[2] = (uint32_t)(registers_address >> 32);
    cmd[3] = 0x80000000u;
    cmd[4] = register_count & 0x3FFFu;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetUcRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count)
{
    /* RE: SPRX uses opcode 0x64 (SET_UC_REG_INDIRECT), 5 dwords.
     * Same format as Sh variant. */
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UC_REG_INDIRECT, 5);
    cmd[1] = (uint32_t)registers_address & ~3u;
    cmd[2] = (uint32_t)(registers_address >> 32);
    cmd[3] = 0x80000000u;
    cmd[4] = register_count & 0x3FFFu;
    return cmd;
}

/*
 * RE source: SPRX disassembly of libSceAgc.sprx (FW 5.50).
 * Both DmaData patchers check for raw DMA_DATA (opcode 0x50) by reading
 * byte [cmd + 1] (the opcode byte in the header) and comparing shifted
 * left by 8 against 0x5000. They return 0x8a6c000c if the packet is not
 * DMA_DATA.
 *
 * DmaDataPatchSetDstAddressOrOffset: patches qword at [cmd + 0x10]
 *   (cmd[4..5] = destination address in raw DMA_DATA format).
 * DmaDataPatchSetSrcAddressOrOffsetOrImmediate: patches qword at [cmd + 0x08]
 *   (cmd[2..3] = source address in raw DMA_DATA format).
 *
 * For backward compatibility with our NOP-wrapped DmaData builder
 * (opcode 0x10, sub 0x19), we also accept that format and patch the
 * corresponding fields (dst at cmd[4..5], src at cmd[6..7]).
 *
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
    if (opcode == AGC_PM4_OP_DMA_DATA) {
        /* Raw DMA_DATA: dst at cmd[4..5] (offset 0x10) */
        cmd[4] = (uint32_t)destination_address;
        cmd[5] = (uint32_t)(destination_address >> 32);
        return AGC_OK;
    }

    /* Backward compat: NOP-wrapped DMA_DATA (sub 0x19) */
    uint32_t sub = agcPm4Subcommand(cmd[0]);
    if (opcode == AGC_PM4_OP_NOP && sub == AGC_PM4_SUB_DMA_DATA) {
        cmd[4] = (uint32_t)destination_address;
        cmd[5] = (uint32_t)(destination_address >> 32);
        return AGC_OK;
    }

    return 0x8a6c000c;
}

int32_t PS5_SYSV_ABI sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate(
    uint32_t *cmd, uint64_t source_address)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t opcode = agcPm4Opcode(cmd[0]);
    if (opcode == AGC_PM4_OP_DMA_DATA) {
        /* Raw DMA_DATA: src at cmd[2..3] (offset 0x08) */
        cmd[2] = (uint32_t)source_address;
        cmd[3] = (uint32_t)(source_address >> 32);
        return AGC_OK;
    }

    /* Backward compat: NOP-wrapped DMA_DATA (sub 0x19), src at cmd[6..7] */
    uint32_t sub = agcPm4Subcommand(cmd[0]);
    if (opcode == AGC_PM4_OP_NOP && sub == AGC_PM4_SUB_DMA_DATA) {
        cmd[6] = (uint32_t)source_address;
        cmd[7] = (uint32_t)(source_address >> 32);
        return AGC_OK;
    }

    return 0x8a6c000c;
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

    /* SPRX-confirmed: accepts direct IT_RELEASE_MEM (0x49) and NOP-wrapped
     * (op 0x10, sub 0x18) for reference emulator compatibility. Rejects interrupt==4
     * (address is zeroed in that case). */
    uint32_t opcode = agcPm4Opcode(cmd[0]);
    uint32_t sub = agcPm4Subcommand(cmd[0]);
    bool is_release_mem = (opcode == AGC_PM4_OP_RELEASE_MEM) ||
        (opcode == AGC_PM4_OP_NOP && sub == AGC_PM4_SUB_RELEASE_MEM);
    if (!is_release_mem)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* cmd[2] bits 26:24 = interrupt field. Reject if interrupt == 4. */
    if (((cmd[2] >> 24) & 0x7u) == 4u)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[3] = (uint32_t)address & 0xFFFFFFFCu;
    cmd[4] = (uint32_t)(address >> 32);
    return AGC_OK;
}

/* sceAgcQueueEndOfPipeActionPatchData (NID: MlEw1feXcjg)
 * SPRX-confirmed: patches cmd[5..6] (data lo/hi) in a ReleaseMem packet.
 * Rejects interrupt==4 (data is zeroed) and data_sel==5 (data is GDS-encoded). */
int32_t PS5_SYSV_ABI sceAgcQueueEndOfPipeActionPatchData(
    uint32_t *cmd, uint32_t context_id, uint32_t data_sel, uint64_t data)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t opcode = agcPm4Opcode(cmd[0]);
    uint32_t sub = agcPm4Subcommand(cmd[0]);
    bool is_release_mem = (opcode == AGC_PM4_OP_RELEASE_MEM) ||
        (opcode == AGC_PM4_OP_NOP && sub == AGC_PM4_SUB_RELEASE_MEM);
    if (!is_release_mem)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* cmd[2] bits 26:24 = interrupt, bits 31:29 = data_sel.
     * Reject interrupt==4 or data_sel==5. */
    uint32_t interrupt = (cmd[2] >> 24) & 0x7u;
    uint32_t packet_data_sel = (cmd[2] >> 29) & 0x7u;
    if (interrupt == 4u || packet_data_sel == 5u)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* reference emulator: for NOP-wrapped ReleaseMem with context_id > 1 and data_sel==1,
     * pack the segment generation into bits 24..31 of the data. */
    uint64_t packet_data = data;
    if (opcode == AGC_PM4_OP_NOP && sub == AGC_PM4_SUB_RELEASE_MEM &&
        context_id > 1 && data_sel == 1) {
        packet_data = ((uint64_t)(context_id - 2u) << 24u) | (data & 0x00FFFFFFull);
    }

    cmd[5] = (uint32_t)packet_data;
    cmd[6] = (uint32_t)(packet_data >> 32);
    return AGC_OK;
}

/* sceAgcCbQueueEndOfPipeActionGetSize (NID: hL7C0IRpWZI)
 * SPRX-confirmed: returns 0x20 (32 bytes = 8 dwords). */
uint32_t PS5_SYSV_ABI sceAgcCbQueueEndOfPipeActionGetSize(void)
{
    return 0x20u;
}

/*
 * RE source: HLE reference sceAgcDcbGetLodStats / sceAgcDcbGetLodStatsGetSize.
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

/*
 * RE source: SPRX capstone disassembly of libSceAgc.sprx DCB builders.
 * These are cursor-based PM4 type-3 packet builders using standard AMD/Gen5
 * opcodes (not NOP-wrapped subcommands). Each follows the same pattern:
 * agcCbAllocDwords → NULL check → write header + payload → return pointer.
 */

/* sceAgcDcbIndirectBuffer (NID w1KFAHVqpaU) — IT_INDIRECT_BUFFER (0x3F), 4 dwords.
 * SPRX evidence: 14-dword IB, vmid/addr packing, validation 0xd/0xe.
 * Layout (shadPS4 PM4CmdIndirectBuffer):
 *   [0] header
 *   [1] ibase_lo
 *   [2] ibase_hi[15:0]
 *   [3] ib_size[19:0] | (vmid[31:24]) */
uint32_t *PS5_SYSV_ABI sceAgcDcbIndirectBuffer(
    SceAgcCb *cb, uint64_t gpu_addr, uint32_t size_dwords, uint32_t vmid)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDIRECT_BUFFER, 4);
    cmd[1] = (uint32_t)gpu_addr;
    cmd[2] = (uint32_t)(gpu_addr >> 32) & 0xFFFFu;
    cmd[3] = (size_dwords & 0xFFFFFu) | ((vmid & 0xFFu) << 24);
    return cmd;
}

/* sceAgcDcbDrawIndirect (NID 1rZSWUv1IRc) — IT_DRAW_INDIRECT (0x24), 5 dwords.
 * SPRX evidence: 0x28000000000 (VGT_INDEX_TYPE), validation 0x4/0x5.
 * Layout (shadPS4 PM4CmdDrawIndirect):
 *   [0] header
 *   [1] data_offset
 *   [2] base_vtx_loc[15:0]
 *   [3] start_inst_loc[15:0]
 *   [4] draw_initiator */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndirect(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t draw_initiator)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDIRECT, 5);
    cmd[1] = data_offset;
    cmd[2] = base_vtx_loc & 0xFFFFu;
    cmd[3] = start_inst_loc & 0xFFFFu;
    cmd[4] = draw_initiator;
    return cmd;
}

/* sceAgcDcbDrawIndex2 (NID q88lQ+GP5Yk) — IT_DRAW_INDEX_2 (0x27), 6 dwords.
 * SPRX evidence: validation 0x5/0x6, leaq data ref.
 * Layout (shadPS4 PM4CmdDrawIndex2):
 *   [0] header
 *   [1] max_size
 *   [2] index_base_lo
 *   [3] index_base_hi
 *   [4] index_count
 *   [5] draw_initiator */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndex2(
    SceAgcCb *cb, uint32_t max_size, uint64_t index_base_addr,
    uint32_t index_count, uint32_t draw_initiator)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 6);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_2, 6);
    cmd[1] = max_size;
    cmd[2] = (uint32_t)index_base_addr & ~0x1u;  /* [31:1] word-aligned */
    cmd[3] = (uint32_t)(index_base_addr >> 32);
    cmd[4] = index_count;
    cmd[5] = draw_initiator;
    return cmd;
}

/* sceAgcDcbDrawIndexIndirect (NID t1vNu082-jM) — IT_DRAW_INDEX_INDIRECT (0x25), 5 dwords.
 * SPRX evidence: 0x28000000000, bextrl 0x509/0x50e/0x513, validation 0x4/0x5.
 * Layout (shadPS4 PM4CmdDrawIndexIndirect):
 *   [0] header
 *   [1] data_offset
 *   [2] base_vtx_loc[15:0]
 *   [3] start_inst_loc[15:0]
 *   [4] draw_initiator */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexIndirect(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t draw_initiator)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_INDIRECT, 5);
    cmd[1] = data_offset;
    cmd[2] = base_vtx_loc & 0xFFFFu;
    cmd[3] = start_inst_loc & 0xFFFFu;
    cmd[4] = draw_initiator;
    return cmd;
}

/* sceAgcDcbDrawIndirectMulti (NID kUlvghKs-mA) — IT_DRAW_INDIRECT_MULTI
 * (0x2C), 7 dwords. This PS5 layout is hardware-qualified on FW 5.50.
 *   [0] header
 *   [1] data_offset
 *   [2] base_vtx_loc[15:0]
 *   [3] start_inst_loc[15:0]
 *   [4] count
 *   [5] stride
 *   [6] draw_initiator */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndirectMulti(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t count, uint32_t stride,
    uint32_t draw_initiator)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 7);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDIRECT_MULTI, 7);
    cmd[1] = data_offset;
    cmd[2] = base_vtx_loc & 0xFFFFu;
    cmd[3] = start_inst_loc & 0xFFFFu;
    cmd[4] = count;
    cmd[5] = stride;
    cmd[6] = draw_initiator;
    return cmd;
}

/* sceAgcDcbDrawIndexIndirectMulti (NID ypVBz4uPKcQ) —
 * IT_DRAW_INDEX_INDIRECT_MULTI (0x38), 7 dwords. This PS5 layout is
 * hardware-qualified on FW 5.50.
 *   [0] header
 *   [1] data_offset
 *   [2] base_vtx_loc[15:0]
 *   [3] start_inst_loc[15:0]
 *   [4] count
 *   [5] stride
 *   [6] draw_initiator */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexIndirectMulti(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t count, uint32_t stride,
    uint32_t draw_initiator)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 7);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_INDIRECT_MULTI, 7);
    cmd[1] = data_offset;
    cmd[2] = base_vtx_loc & 0xFFFFu;
    cmd[3] = start_inst_loc & 0xFFFFu;
    cmd[4] = count;
    cmd[5] = stride;
    cmd[6] = draw_initiator;
    return cmd;
}

/* sceAgcDcbSetPredication (NID bbFueFP+J4k) — IT_SET_PREDICATION (0x20), 3 dwords.
 * SPRX evidence: validation 0x3/0x4, `andl 0xfffffff0` (16-byte align).
 * Layout (AMD SET_PREDICATION):
 *   [0] header
 *   [1] addr_lo (16-byte aligned)
 *   [2] addr_hi[15:0] | (op[17:16]) | (keep_count[30:18]) | (predicate[31]) */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetPredication(
    SceAgcCb *cb, uint64_t addr, uint32_t op, uint32_t keep_count,
    bool predicate)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_PREDICATION, 3);
    cmd[1] = (uint32_t)addr & ~0xFu;
    cmd[2] = ((uint32_t)(addr >> 32) & 0xFFFFu) |
        ((op & 0x3u) << 16) |
        ((keep_count & 0x1FFFu) << 18) |
        ((predicate ? 1u : 0u) << 31);
    return cmd;
}

/* sceAgcDcbEventWrite (NID aJf+j5yntiU) — IT_EVENT_WRITE (0x46), 2 dwords.
 * SPRX evidence: `orl 0xc0004600`, validation 0x38/0x39, 0x18080 bitmask.
 * Layout (shadPS4 PM4CmdEventWrite, simple event_index=0):
 *   [0] header
 *   [1] event_control = event_type[5:0] | (event_index[3:0] << 8) */
uint32_t *PS5_SYSV_ABI sceAgcDcbEventWrite(
    SceAgcCb *cb, uint32_t event_type, uint32_t event_index)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_EVENT_WRITE, 2);
    cmd[1] = (event_type & 0x3Fu) | ((event_index & 0xFu) << 8);
    return cmd;
}

/* sceAgcDcbSetConfigReg (NID BVFg3CWU6Eo) — IT_SET_CONFIG_REG (0x68), variable.
 * SPRX evidence: `orl 0xc0006800`, 3x indirect CB grow, validation 0x2/0x3.
 * Layout:
 *   [0] header (length = count + 2)
 *   [1] reg_offset
 *   [2..] register values */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetConfigReg(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count)
{
    if (!values || count == 0 || count > 0x3FFFu)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, count + 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONFIG_REG, count + 2);
    cmd[1] = reg_offset & 0xFFFFu;
    for (uint32_t i = 0; i < count; ++i)
        cmd[2 + i] = values[i];
    return cmd;
}

/* sceAgcDcbSetShReg (NID n2fD4A+pb+g) — IT_SET_SH_REG (0x76), variable.
 * SPRX evidence: `orl 0xc0007600`, validation (count+2), calls 0xe120.
 * Same layout as SetConfigReg but with SET_SH_REG opcode. */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetShReg(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count)
{
    if (!values || count == 0 || count > 0x3FFFu)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, count + 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG, count + 2);
    cmd[1] = reg_offset & 0xFFFFu;
    for (uint32_t i = 0; i < count; ++i)
        cmd[2 + i] = values[i];
    return cmd;
}

/* sceAgcDcbSetUconfigReg (NID MDLD5Ly94Xk) — IT_SET_UCONFIG_REG (0x79), variable.
 * SPRX evidence: `orl 0xc0007900`, validation (count+2), calls 0xe120.
 * Same layout as SetConfigReg but with SET_UCONFIG_REG opcode. */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetUconfigReg(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count)
{
    if (!values || count == 0 || count > 0x3FFFu)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, count + 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, count + 2);
    cmd[1] = reg_offset & 0xFFFFu;
    for (uint32_t i = 0; i < count; ++i)
        cmd[2 + i] = values[i];
    return cmd;
}

/*
 * RE source: libSceAgc.sprx flip/display wait builders.
 *
 * These use AGC-custom PM4 opcodes (0x4C/0x4E/0x4F/0x51/0x54) rather
 * than the standard NOP-wrapped subcommand encoding. The SPRX validates
 * the flip slot index < 32 (0x20) before emitting the packet; we do the
 * same and return NULL on invalid input.
 *
 * sceAgcDcbWaitFlipDone (NID pdEV7bI6COI, ordinal 216):
 *   3-dword packet, opcode 0x4C.
 *   [0] header = agcPm4Header3(WAIT_FLIP_DONE, 3)
 *   [1] flip_slot
 *   [2] reserved (0)
 *
 * sceAgcDcbWaitFlip (NID HV4j+E0MBHE, ordinal 133):
 *   Same layout as WaitFlipDone but opcode 0x51.
 *
 * sceAgcDcbInsertWaitFlipDone (NID k0E7vkgqAuE, ordinal 134):
 *   3-dword packet, opcode 0x54, sub=0x06 (WAIT_FLIP_DONE).
 *   [0] header = agcPm4Header3Sub(INSERT_WAIT_FLIP_DONE, WAIT_FLIP_DONE, 3)
 *   [1] flip_slot
 *   [2] reserved (0)
 *
 * sceAgcDcbWaitFlipEos (NID SbuY2jN+axQ, ordinal 217):
 *   Emits two 3-dword packets back-to-back (opcodes 0x4F then 0x4E).
 *   [0] header = agcPm4Header3(WAIT_FLIP_EOS, 3)
 *   [1] flip_slot
 *   [2] reserved (0)
 *   [3] header = agcPm4Header3(WAIT_FLIP_EOS_2, 3)
 *   [4] flip_slot
 *   [5] reserved (0)
 */

/* Maximum flip slot index accepted by the SPRX (validation: < 0x20). */
#define AGC_FLIP_SLOT_MAX 32u

uint32_t *PS5_SYSV_ABI sceAgcDcbWaitFlipDone(SceAgcCb *cb, uint32_t flip_slot)
{
    if (flip_slot >= AGC_FLIP_SLOT_MAX)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_WAIT_FLIP_DONE, 3);
    cmd[1] = flip_slot;
    cmd[2] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbWaitFlip(SceAgcCb *cb, uint32_t flip_slot)
{
    if (flip_slot >= AGC_FLIP_SLOT_MAX)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_WAIT_FLIP, 3);
    cmd[1] = flip_slot;
    cmd[2] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbInsertWaitFlipDone(SceAgcCb *cb, uint32_t flip_slot)
{
    if (flip_slot >= AGC_FLIP_SLOT_MAX)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_INSERT_WAIT_FLIP_DONE,
                              AGC_PM4_SUB_WAIT_FLIP_DONE, 3);
    cmd[1] = flip_slot;
    cmd[2] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbWaitFlipEos(SceAgcCb *cb, uint32_t flip_slot)
{
    if (flip_slot >= AGC_FLIP_SLOT_MAX)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 6);
    if (!cmd)
        return 0;

    /* First packet: opcode 0x4F (WAIT_FLIP_EOS) */
    cmd[0] = agcPm4Header3(AGC_PM4_OP_WAIT_FLIP_EOS, 3);
    cmd[1] = flip_slot;
    cmd[2] = 0;
    /* Second packet: opcode 0x4E (WAIT_FLIP_EOS_2) */
    cmd[3] = agcPm4Header3(AGC_PM4_OP_WAIT_FLIP_EOS_2, 3);
    cmd[4] = flip_slot;
    cmd[5] = 0;
    return cmd;
}
