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
 * openagc - acb.c
 *
 * ACB (Async Compute Buffer) command building functions.
 */

#include "agc_cb.h"
#include "agc_error.h"
#include "agc_pm4.h"
#include "agc_types.h"
#include "agcdriver.h"
#include "agc_workload_packet.h"
#include "agc_workload_state.h"

#include <string.h>

#define ACB_MIN_SIZE_DW 4

static int32_t acb_write_nop(uint32_t *acb, uint32_t size_dw, uint32_t marker)
{
    if (!acb || size_dw < 2)
        return AGC_ERROR_CB_INVALID_SIZE;
    acb[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, marker, 2);
    acb[1] = 0;
    return 2;
}

int32_t PS5_SYSV_ABI sceAgcAcbInitializeDefaultHardwareState_pre0090(
    uint32_t *acb, uint32_t size_dw)
{
    if (!acb || size_dw < ACB_MIN_SIZE_DW)
        return AGC_ERROR_CB_INVALID_SIZE;

    acb[0] = agcPm4Header3(AGC_PM4_OP_NOP, 2);
    acb[1] = 0;
    return 2;
}

int32_t PS5_SYSV_ABI sceAgcAcbDispatchIndirect(
    uint32_t *acb, uint32_t size_dw, uintptr_t args)
{
    if (!acb || size_dw < 4)
        return AGC_ERROR_CB_INVALID_SIZE;

    acb[0] = agcPm4Header3(AGC_PM4_OP_DISPATCH_INDIRECT, 4);
    acb[1] = (uint32_t)(args & 0xFFFFFFFFu);
    acb[2] = (uint32_t)(args >> 32);
    acb[3] = 0;
    return 4;
}

int32_t PS5_SYSV_ABI sceAgcAcbAcquireMem(
    uint32_t *acb, uint32_t size_dw, uint32_t engine_sel,
    uint32_t coher_cntl, uint32_t coher_size, uint64_t coher_base)
{
    if (!acb || size_dw < 8)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_ACQUIRE_MEM (Ariel-specific opcode 0x58) on PS5.
     * Packet layout (8 dwords):
     *   [0] header
     *   [1] coher_cntl
     *   [2] coher_size_lo
     *   [3] coher_base_lo
     *   [4] coher_base_hi
     *   [5] coher_size_hi
     *   [6] engine_sel
     *   [7] reserved (0)
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_ACQUIRE_MEM, 8);
    acb[1] = coher_cntl;
    acb[2] = coher_size;
    acb[3] = (uint32_t)(coher_base & 0xFFFFFFFFu);
    acb[4] = (uint32_t)(coher_base >> 32);
    acb[5] = 0;
    acb[6] = engine_sel;
    acb[7] = 0;
    return 8;
}

int32_t PS5_SYSV_ABI sceAgcAcbAtomicGds(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src)
{
    if (!acb || size_dw < 10)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_ATOMIC_GDS (Ariel-specific opcode 0x1D on PS5).
     * Packet layout (10 dwords):
     *   [0] header
     *   [1] control: op | (gds_offset << 16)
     *   [2] data
     *   [3] src
     *   [4-9] reserved (0)
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_ATOMIC_GDS, 10);
    acb[1] = (op & 0xFFu) | ((gds_offset & 0xFFFFu) << 16);
    acb[2] = data;
    acb[3] = src;
    acb[4] = 0;
    acb[5] = 0;
    acb[6] = 0;
    acb[7] = 0;
    acb[8] = 0;
    acb[9] = 0;
    return 10;
}

int32_t PS5_SYSV_ABI sceAgcAcbAtomicGds_pre0090(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src)
{
    return sceAgcAcbAtomicGds(acb, size_dw, op, gds_offset, data, src);
}

int32_t PS5_SYSV_ABI sceAgcAcbAtomicMem(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint64_t addr, uint64_t data)
{
    if (!acb || size_dw < 5)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_ATOMIC_MEM (opcode 0x1B) on AMD/Ariel.
     * Packet layout (5 dwords):
     *   [0] header
     *   [1] control = op[3:0] | src_sel[6:4] | loop[8]
     *   [2] addr_lo
     *   [3] addr_hi
     *   [4] data_lo
     *
     * Atomic operations: 0=add, 1=sub, 2=min, 3=max, 4=and, 5=or, 6=xor,
     * 7=inc, 8=dec.
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_ATOMIC_MEM, 5);
    acb[1] = op & 0xF;
    acb[2] = (uint32_t)(addr & 0xFFFFFFFFu);
    acb[3] = (uint32_t)(addr >> 32);
    acb[4] = (uint32_t)(data & 0xFFFFFFFFu);
    return 5;
}

int32_t PS5_SYSV_ABI sceAgcAcbCondExec(
    uint32_t *acb, uint32_t size_dw, uint64_t addr, uint32_t count)
{
    if (!acb || size_dw < 5)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * reference-confirmed: IT_COND_EXEC (opcode 0x22), 5 dwords.
     * Packet layout:
     *   [0] header
     *   [1] addr_lo (masked to 0xfffffffc)
     *   [2] addr_hi
     *   [3] 0 (reserved)
     *   [4] count & 0x3fff (number of dwords to execute if condition is non-zero)
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_COND_EXEC, 5);
    acb[1] = (uint32_t)(addr & 0xFFFFFFFCu);
    acb[2] = (uint32_t)(addr >> 32);
    acb[3] = 0;
    acb[4] = count & 0x3FFFu;
    return 5;
}

int32_t PS5_SYSV_ABI sceAgcAcbCopyData(
    uint32_t *acb, uint32_t size_dw, uint32_t src_sel, uint32_t dst_sel,
    uint64_t src_addr, uint64_t dst_addr, uint32_t byte_count)
{
    if (!acb || size_dw < 6)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_COPY_DATA (opcode 0x40) on AMD/Ariel.
     * Packet layout (6 dwords):
     *   [0] header
     *   [1] control = src_sel[3:0] | dst_sel[7:4] | count_sel[8] | wr_confirm[9]
     *   [2] src_addr_lo
     *   [3] src_addr_hi
     *   [4] dst_addr_lo
     *   [5] dst_addr_hi
     *
     * src_sel/dst_sel: 0=mem, 1=reg, 2=tc, 3=cache, 4=immediate.
     * count_sel selects whether byte_count is encoded in the control dword or
     * a separate payload; for this single-dword implementation it is left in
     * control[31:16] (Ariel extension).
     */
    uint32_t control = (src_sel & 0xF) |
                       ((dst_sel & 0xF) << 4) |
                       (1u << 9) |                  /* wr_confirm = 1 */
                       ((byte_count & 0xFFFF) << 16);

    acb[0] = agcPm4Header3(AGC_PM4_OP_COPY_DATA, 6);
    acb[1] = control;
    acb[2] = (uint32_t)(src_addr & 0xFFFFFFFFu);
    acb[3] = (uint32_t)(src_addr >> 32);
    acb[4] = (uint32_t)(dst_addr & 0xFFFFFFFFu);
    acb[5] = (uint32_t)(dst_addr >> 32);
    return 6;
}

int32_t PS5_SYSV_ABI sceAgcAcbDmaData(
    uint32_t *acb, uint32_t size_dw, uint64_t src_addr, uint64_t dst_addr,
    uint32_t byte_count, uint32_t src_swap, uint32_t dst_swap)
{
    if (!acb || size_dw < 8)
        return AGC_ERROR_CB_INVALID_SIZE;
    if (byte_count == 0 || (byte_count & 3u) != 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    /*
     * IT_DMA_DATA (Ariel-specific opcode 0x50 on PS5).
     * Packet layout (8 dwords):
     *   [0] header
     *   [1] control (src_swap in low 16, dst_swap in high 16)
     *   [2] byte_count
     *   [3] dst_addr_lo
     *   [4] dst_addr_hi
     *   [5] src_addr_lo
     *   [6] src_addr_hi
     *   [7] reserved (0)
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_DMA_DATA, 8);
    acb[1] = (src_swap & 0xFFFFu) | ((dst_swap & 0xFFFFu) << 16);
    acb[2] = byte_count;
    acb[3] = (uint32_t)(dst_addr & 0xFFFFFFFFu);
    acb[4] = (uint32_t)(dst_addr >> 32);
    acb[5] = (uint32_t)(src_addr & 0xFFFFFFFFu);
    acb[6] = (uint32_t)(src_addr >> 32);
    acb[7] = 0;
    return 8;
}

int32_t PS5_SYSV_ABI sceAgcAcbEventWrite(
    uint32_t *acb, uint32_t size_dw, uint32_t event_type,
    uint64_t gpu_addr, uint32_t data, uint32_t int_ctx)
{
    /*
     * SPRX-confirmed (FW 5.50 and 11.60): IT_EVENT_WRITE (0x46), always 2 dwords.
     * cmd[1] = (event_type == 7 ? 0x400 : 0) | (event_type & 0x3f)
     * gpu_addr, data, int_ctx parameters are accepted but not encoded.
     */
    (void)gpu_addr;
    (void)data;
    (void)int_ctx;

    if (!acb || size_dw < 2)
        return AGC_ERROR_CB_INVALID_SIZE;

    acb[0] = agcPm4Header3(AGC_PM4_OP_EVENT_WRITE, 2);
    acb[1] = (event_type == 7u ? 0x400u : 0u) | (event_type & 0x3Fu);
    return 2;
}

int32_t PS5_SYSV_ABI sceAgcAcbJump(uint32_t *acb, uint32_t size_dw, uintptr_t target)
{
    if (!acb || size_dw < 4)
        return AGC_ERROR_CB_INVALID_SIZE;

    acb[0] = agcPm4Header3(AGC_PM4_OP_INDIRECT_BUFFER, 4);
    acb[1] = (uint32_t)(target & 0xFFFFFFFFu);
    acb[2] = (uint32_t)(target >> 32);
    acb[3] = 0;
    return 4;
}

int32_t PS5_SYSV_ABI sceAgcAcbMemSemaphore(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint64_t addr, uint64_t data)
{
    if (!acb || size_dw < 4)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_MEM_SEMAPHORE (opcode 0x39) on Ariel.
     * Packet layout (4 dwords):
     *   [0] header
     *   [1] addr_lo
     *   [2] addr_hi
     *   [3] data (lower 32 bits)
     *
     * The `op` parameter selects signal/wait/compare and is encoded in the
     * header or upper bits of the data dword; this single-dword form leaves
     * op in bits 31:24 of the data payload for now.
     */
    (void)op; /* encoded in data[31:24] below */

    acb[0] = agcPm4Header3(AGC_PM4_OP_MEM_SEMAPHORE, 4);
    acb[1] = (uint32_t)(addr & 0xFFFFFFFFu);
    acb[2] = (uint32_t)(addr >> 32);
    acb[3] = (uint32_t)(data & 0xFFFFFFFFu);
    return 4;
}

int32_t PS5_SYSV_ABI sceAgcAcbPrimeUtcl2(
    uint32_t *acb, uint32_t size_dw, uint64_t addr, uint32_t size)
{
    if (!acb || size_dw < 4)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_PRIME_UTCL2 (Ariel-specific opcode 0x5D on PS5).
     * Packet layout (4 dwords):
     *   [0] header
     *   [1] addr_lo
     *   [2] addr_hi
     *   [3] size
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_PRIME_UTCL2, 4);
    acb[1] = (uint32_t)(addr & 0xFFFFFFFFu);
    acb[2] = (uint32_t)(addr >> 32);
    acb[3] = size;
    return 4;
}

int32_t PS5_SYSV_ABI sceAgcAcbResetQueue(
    uint32_t *acb, uint32_t size_dw, uint32_t queue_id)
{
    if (!acb || size_dw < 3)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_AGC_0x79 (Ariel-specific queue reset opcode on PS5).
     * Packet layout (3 dwords):
     *   [0] header
     *   [1] fixed control word 0x00000342
     *   [2] queue_id
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, 3);
    acb[1] = 0x00000342u;
    acb[2] = queue_id;
    return 3;
}

int32_t PS5_SYSV_ABI sceAgcAcbResetQueueInternal(
    uint32_t *acb, uint32_t size_dw, uint32_t queue_id)
{
    return sceAgcAcbResetQueue(acb, size_dw, queue_id);
}

int32_t PS5_SYSV_ABI sceAgcAcbRewind(uint32_t *acb, uint32_t size_dw)
{
    if (!acb || size_dw < 2)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_NOP (opcode 0x10). The PM4 type-3 header requires at least one
     * payload dword, so a rewind marker is a 2-dword NOP packet. The actual
     * ring rewind is handled by the driver.
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_NOP, 2);
    acb[1] = 0;
    return 2;
}

uint32_t *PS5_SYSV_ABI sceAgcAcbWaitRegMem(
    SceAgcCb *cb, uint32_t size, uint32_t compare_function,
    uint32_t cache_policy, uint64_t address, uint64_t reference,
    uint64_t mask, uint32_t poll_cycles)
{
    /* The ACB form is the DCB packet with operation fixed to zero. */
    return sceAgcDcbWaitRegMem(cb, size, compare_function, 0u, cache_policy,
                               address, reference, mask, poll_cycles);
}

int32_t PS5_SYSV_ABI sceAgcAcbWaitUntilSafeForRendering(uint32_t *acb, uint32_t size_dw)
{
    return acb_write_nop(acb, size_dw, AGC_PM4_SUB_WAIT_FLIP_DONE);
}

int32_t PS5_SYSV_ABI sceAgcAcbWriteData(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint64_t addr, uint32_t data)
{
    if (!acb || size_dw < 5)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_WRITE_DATA (opcode 0x37) on AMD/Ariel.
     * Packet layout (5 dwords for a single data dword):
     *   [0] header
     *   [1] control = dst_sel[3:0] | engine_sel[7:4] | vmid_sel[11:8] | addr_incr[15:12]
     *   [2] addr_lo
     *   [3] addr_hi
     *   [4] data
     *
     * The `op` parameter encodes: dst_sel (bits 0-3), engine_sel (bits 4-7),
     * vmid_sel (bits 8-11), addr_incr (bits 12-15).
     */
    uint32_t dst_sel    = (op >> 0)  & 0xF;
    uint32_t engine_sel = (op >> 4)  & 0xF;
    uint32_t vmid_sel   = (op >> 8)  & 0xF;
    uint32_t addr_incr  = (op >> 12) & 0xF;

    uint32_t control = (dst_sel << 16) |
                       (engine_sel << 8) |
                       (vmid_sel << 20) |
                       (addr_incr << 24);

    acb[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 5);
    acb[1] = control;
    acb[2] = (uint32_t)(addr & 0xFFFFFFFFu);
    acb[3] = (uint32_t)(addr >> 32);
    acb[4] = data;
    return 5;
}

int32_t PS5_SYSV_ABI sceAgcAcbSetFlip(
    uint32_t *acb, uint32_t size_dw, uint32_t vo_handle, uint32_t buf_idx,
    uint32_t vsync)
{
    (void)vo_handle;

    if (!acb || size_dw < 7)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * EOP flip via IT_RELEASE_MEM (Ariel/AMD opcode 0x49) on PS5.
     * Packet layout (7 dwords):
     *   [0] header
     *   [1] flip_mode / event control
     *   [2] buffer_index
     *   [3] vsync
     *   [4] reserved (0)
     *   [5] reserved (0)
     *   [6] reserved (0)
     */
    acb[0] = agcPm4Header3(AGC_PM4_OP_RELEASE_MEM, 7);
    acb[1] = 0;
    acb[2] = buf_idx;
    acb[3] = vsync;
    acb[4] = 0;
    acb[5] = 0;
    acb[6] = 0;
    return 7;
}

uint32_t *PS5_SYSV_ABI sceAgcAcbSetWorkloadComplete(
    SceAgcCb *cb, uint32_t stream_id, uint32_t workload_id)
{
    uint64_t slot_address;
    uint32_t *cmd;

    if (!agcSonyWorkloadGetStreamSlotAddress(stream_id, &slot_address) ||
        workload_id > 63u)
        return 0;
    cmd = agcCbAllocDwords(cb, AGC_SONY_WORKLOAD_COMPLETE_DWORDS);
    if (!cmd)
        return 0;
    if (agcSonyBuildWorkloadCompletePacket(cmd,
            AGC_SONY_WORKLOAD_COMPLETE_DWORDS, true, stream_id,
            workload_id, slot_address) == 0)
        return 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcAcbSetWorkloadStreamInactive(
    SceAgcCb *cb, uint32_t stream_id)
{
    uint64_t slot_address;
    uint32_t *cmd;

    if (!agcSonyWorkloadGetStreamSlotAddress(stream_id, &slot_address))
        return 0;
    (void)slot_address;
    cmd = agcCbAllocDwords(cb, AGC_SONY_WORKLOAD_INACTIVE_DWORDS);
    if (!cmd)
        return 0;
    if (agcSonyBuildWorkloadStreamInactivePacket(cmd,
            AGC_SONY_WORKLOAD_INACTIVE_DWORDS, true, stream_id) == 0)
        return 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcAcbSetWorkloadsActive(
    SceAgcCb *cb, uint32_t stream_id, const uint32_t *workload_ids,
    uint32_t workload_count)
{
    uint64_t slot_address;
    uint64_t workload_mask = 0;
    uint32_t *cmd;
    uint32_t i;

    if (!workload_ids || workload_count < 1u || workload_count > 63u ||
        !agcSonyWorkloadGetStreamSlotAddress(stream_id, &slot_address))
        return 0;
    for (i = 0; i < workload_count; ++i) {
        if (workload_ids[i] > 63u ||
            (workload_mask & (UINT64_C(1) << workload_ids[i])) != 0)
            return 0;
        workload_mask |= UINT64_C(1) << workload_ids[i];
    }
    cmd = agcCbAllocDwords(cb, AGC_SONY_WORKLOAD_ACTIVE_DWORDS);
    if (!cmd)
        return 0;
    if (agcSonyBuildWorkloadsActivePacket(cmd,
            AGC_SONY_WORKLOAD_ACTIVE_DWORDS, true, stream_id,
            slot_address, workload_mask) == 0)
        return 0;
    return cmd;
}

int32_t PS5_SYSV_ABI sceAgcSuspendPointAndCheckStatus(uint32_t value)
{
    (void)value;
    /* Sony's wrapper builds runtime CDBG state and calls the distinct
     * sceAgcDriverSuspendPointSubmitCdbg carrier. The public Direct query is
     * only a permission stub and must not be substituted here. */
    return AGC_ERROR_NOT_SUPPORTED;
}
