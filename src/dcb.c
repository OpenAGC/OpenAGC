/*
 * openagc - dcb.c
 *
 * VshDcb (VSH Draw Command Buffer) command building functions.
 */

#include "agc_error.h"
#include "agc_pm4.h"
#include "agc_types.h"
#include "agcdriver.h"

#include "agc_cb.h"

static int32_t dcb_write_nop(uint32_t *dcb, uint32_t size_dw, uint32_t marker)
{
    if (!dcb || size_dw < 2)
        return AGC_ERROR_CB_INVALID_SIZE;
    dcb[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, marker, 2);
    dcb[1] = 0;
    return 2;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbInitializeDefaultHardwareState_pre0090(
    uint32_t *dcb, uint32_t size_dw)
{
    return dcb_write_nop(dcb, size_dw, AGC_PM4_SUB_ZERO);
}

int32_t PS5_SYSV_ABI sceAgcVshDcbClearState(uint32_t *dcb, uint32_t size_dw)
{
    if (!dcb || size_dw < 2)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_CLEAR_STATE (opcode 0x14). Resets context state to defaults.
     * Packet layout (2 dwords):
     *   [0] header
     *   [1] flags (0)
     */
    dcb[0] = agcPm4Header3(AGC_PM4_OP_CLEAR_STATE, 2);
    dcb[1] = 0;
    return 2;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbAtomicGds(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src)
{
    if (!dcb || size_dw < 10)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_ATOMIC_GDS (Ariel-specific opcode 0x1D). Same packet layout as the
     * ACB variant; the DCB version targets the graphics queue.
     */
    dcb[0] = agcPm4Header3(AGC_PM4_OP_ATOMIC_GDS, 10);
    dcb[1] = (op & 0xFFu) | ((gds_offset & 0xFFFFu) << 16);
    dcb[2] = data;
    dcb[3] = src;
    dcb[4] = 0;
    dcb[5] = 0;
    dcb[6] = 0;
    dcb[7] = 0;
    dcb[8] = 0;
    dcb[9] = 0;
    return 10;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbAtomicGds_pre0090(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src)
{
    return sceAgcVshDcbAtomicGds(dcb, size_dw, op, gds_offset, data, src);
}

int32_t PS5_SYSV_ABI sceAgcVshDcbContextStateOp(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data)
{
    (void)reg_type;

    if (!dcb || !reg_data || reg_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    /*
     * IT_SET_CONTEXT_REG / IT_SET_SH_REG / etc. (variable-length).
     * Total packet size = 1 header + 1 offset dword + reg_count data dwords.
     */
    uint32_t total_dwords = reg_count + 2u;
    if (size_dw < total_dwords)
        return AGC_ERROR_CB_INVALID_SIZE;

    uint32_t opcode;
    switch (op) {
        case 0:  opcode = AGC_PM4_OP_SET_CONTEXT_REG;           break;
        case 1:  opcode = AGC_PM4_OP_SET_SH_REG;                break;
        case 2:  opcode = AGC_PM4_OP_SET_CONFIG_REG;           break;
        case 3:  opcode = AGC_PM4_OP_SET_UCONFIG_REG;           break;
        case 4:  opcode = AGC_PM4_OP_SET_CONTEXT_REG_INDIRECT;  break;
        case 5:  opcode = AGC_PM4_OP_SET_SH_REG_OFFSET;         break;
        default: opcode = AGC_PM4_OP_SET_CONTEXT_REG;           break;
    }

    dcb[0] = agcPm4Header3(opcode, total_dwords);
    dcb[1] = reg_offset;
    const uint32_t *data = (const uint32_t *)reg_data;
    for (uint32_t i = 0; i < reg_count; i++)
        dcb[2 + i] = data[i];
    return (int32_t)total_dwords;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbContextStateOp_pre0100(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data)
{
    return sceAgcVshDcbContextStateOp(dcb, size_dw, op, reg_type,
                                      reg_offset, reg_count, reg_data);
}

int32_t PS5_SYSV_ABI sceAgcVshDcbMemSemaphore(uint32_t *dcb, uint32_t size_dw)
{
    if (!dcb || size_dw < 4)
        return AGC_ERROR_CB_INVALID_SIZE;

    dcb[0] = agcPm4Header3(AGC_PM4_OP_MEM_SEMAPHORE, 4);
    dcb[1] = 0;
    dcb[2] = 0;
    dcb[3] = 0;
    return 4;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbResetQueue(
    uint32_t *dcb, uint32_t size_dw, uint32_t queue_id)
{
    if (!dcb || size_dw < 3)
        return AGC_ERROR_CB_INVALID_SIZE;

    /*
     * IT_AGC_0x79 (Ariel-specific queue reset opcode). Same packet layout as
     * the ACB variant; the DCB version targets the graphics queue.
     */
    dcb[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, 3);
    dcb[1] = 0x00000342u;
    dcb[2] = queue_id;
    return 3;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbResetQueueInternal(
    uint32_t *dcb, uint32_t size_dw, uint32_t queue_id)
{
    return sceAgcVshDcbResetQueue(dcb, size_dw, queue_id);
}

int32_t PS5_SYSV_ABI sceAgcVshDcbSetPreemption(
    uint32_t *dcb, uint32_t size_dw, uint32_t mode)
{
    (void)dcb;
    (void)size_dw;
    (void)mode;

    /*
     * SPRX RE (libSceAgcVsh.sprx vaddr 0x4140): sceAgcVshDcbSetPreemption is
     * a stub that prints "not allowed to be called from agc vsh" and then
     * executes int 0x41 (crash). It is not exported via the NID table and has
     * no ordinal. Real GPU preemption is handled kernel-side by
     * gc_pm4_suspend_point_marker (opcode 0x93), not by userspace CB builders.
     *
     * openagc returns an error instead of crashing.
     */
    return AGC_ERROR_INVALID_STATE;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbWaitUntilSafeForRendering(uint32_t *dcb, uint32_t size_dw)
{
    if (!dcb || size_dw < 7)
        return AGC_ERROR_CB_INVALID_SIZE;

    dcb[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_WAIT_FLIP_DONE, 7);
    for (uint32_t i = 1; i < 7; ++i)
        dcb[i] = 0;
    return 7;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbSetFlip(
    uint32_t *dcb, uint32_t size_dw, uint32_t vo_handle, uint32_t buf_idx)
{
    if (!dcb || size_dw < 6)
        return AGC_ERROR_CB_INVALID_SIZE;

    dcb[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_FLIP, 6);
    dcb[1] = vo_handle;
    dcb[2] = buf_idx;
    dcb[3] = 0;
    dcb[4] = 0;
    dcb[5] = 0;
    return 6;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbSetWorkloadComplete(
    uint32_t *dcb, uint32_t size_dw, AgcWorkloadId workload)
{
    if (!dcb || size_dw < 8)
        return AGC_ERROR_CB_INVALID_SIZE;

    dcb[0] = agcPm4Header3(AGC_PM4_OP_SET_WORKLOAD, 8);
    dcb[1] = (uint32_t)(workload & 0xFFFFFFFFu);
    dcb[2] = 0;
    dcb[3] = 0;
    dcb[4] = 0;
    dcb[5] = 0;
    dcb[6] = 0;
    dcb[7] = 0;
    return 8;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbSetWorkloadStreamInactive(
    uint32_t *dcb, uint32_t size_dw, AgcWorkloadId workload)
{
    if (!dcb || size_dw < 3)
        return AGC_ERROR_CB_INVALID_SIZE;

    dcb[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, 3);
    dcb[1] = 0x00000342u;
    dcb[2] = (uint32_t)(workload & 0xFFFFFFFFu);
    return 3;
}

int32_t PS5_SYSV_ABI sceAgcVshDcbSetWorkloadsActive(
    uint32_t *dcb, uint32_t size_dw, uint32_t flags)
{
    if (!dcb || size_dw < 8)
        return AGC_ERROR_CB_INVALID_SIZE;

    dcb[0] = agcPm4Header3(AGC_PM4_OP_SET_WORKLOAD, 8);
    dcb[1] = flags;
    dcb[2] = 0;
    dcb[3] = 0;
    dcb[4] = 0;
    dcb[5] = 0;
    dcb[6] = 0;
    dcb[7] = 0;
    return 8;
}

int32_t PS5_SYSV_ABI sceAgcDcbSetEopFlip(SceAgcCb *dcb,
    uint32_t event_type, uint32_t event_index,
    uint64_t dst_addr, uint32_t data)
{
    if (!dcb)
        return AGC_ERROR_INVALID_ARGUMENT;

    /*
     * EOP flip via IT_RELEASE_MEM (Ariel/AMD opcode 0x49) type-3 packet.
     *
     * RE'd from libSceAgcDriver.sprx ordinals 49/50 (cwbxjPSJ7WQ /
     * u8BkdHb1+Po). The SPRX writes 0xc0064900 as the PM4 header — a
     * type-3 packet with opcode 0x49 (IT_RELEASE_MEM) and count field 6
     * (7 payload dwords + 1 header = 8 total). The SPRX also uses
     * 0xfffd1000 as a mask/flag value in the EOP control field.
     *
     * Packet layout (8 dwords):
     *   [0] header (0xc0064900 = type-3, opcode 0x49, length 8)
     *   [1] event_control = event_type[5:0] | event_index[13:8]
     *   [2] dst_addr_lo
     *   [3] dst_addr_hi
     *   [4] data
     *   [5] reserved (0)
     *   [6] reserved (0)
     *   [7] reserved (0)
     */
    uint32_t *cmd = agcCbAllocDwords(dcb, 8);
    if (!cmd)
        return AGC_ERROR_CB_OVERFLOW;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_RELEASE_MEM, 8);
    cmd[1] = (event_type & 0x3Fu) | ((event_index & 0x0Fu) << 8);
    cmd[2] = (uint32_t)(dst_addr & 0xFFFFFFFFu);
    cmd[3] = (uint32_t)(dst_addr >> 32);
    cmd[4] = data;
    cmd[5] = 0;
    cmd[6] = 0;
    cmd[7] = 0;
    return 8;
}

int32_t PS5_SYSV_ABI sceAgcVshCbMemSemaphore(uint32_t *cb, uint32_t size_dw)
{
    return sceAgcVshDcbMemSemaphore(cb, size_dw);
}
