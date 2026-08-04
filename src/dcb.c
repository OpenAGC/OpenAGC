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
 * openagc - dcb.c
 *
 * DCB (Draw Command Buffer) raw-buffer variant functions.
 * These use the (uint32_t *dcb, uint32_t size_dw) API for version-variant
 * and special functions that don't use the SceAgcCb cursor model.
 * The main cursor-based DCB builders are in cb_builders.c and game_compat.c.
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

int32_t PS5_SYSV_ABI sceAgcDcbInitializeDefaultHardwareState(
    uint32_t *dcb, uint32_t size_dw)
{
    return dcb_write_nop(dcb, size_dw, AGC_PM4_SUB_ZERO);
}

int32_t PS5_SYSV_ABI sceAgcDcbAtomicGds_0900(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src)
{
    if (!dcb || size_dw < 10)
        return AGC_ERROR_CB_INVALID_SIZE;

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

int32_t PS5_SYSV_ABI sceAgcDcbContextStateOp_pre0100(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data)
{
    (void)reg_type;

    if (!dcb || !reg_data || reg_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

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

int32_t PS5_SYSV_ABI sceAgcDcbResetQueueInternal(
    uint32_t *dcb, uint32_t size_dw, uint32_t queue_id)
{
    if (!dcb || size_dw < 3)
        return AGC_ERROR_CB_INVALID_SIZE;

    dcb[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, 3);
    dcb[1] = 0x00000342u;
    dcb[2] = queue_id;
    return 3;
}

int32_t PS5_SYSV_ABI sceAgcDcbSetPreemption(
    uint32_t *dcb, uint32_t size_dw, uint32_t mode)
{
    (void)dcb;
    (void)size_dw;
    (void)mode;

    /*
     * SPRX RE (libSceAgcVsh.sprx vaddr 0x4140): sceAgcDcbSetPreemption is
     * a stub that prints "not allowed to be called from agc vsh" and then
     * executes int 0x41 (crash). It is not exported via the NID table and has
     * no ordinal. Real GPU preemption is handled kernel-side by
     * gc_pm4_suspend_point_marker (opcode 0x93), not by userspace CB builders.
     *
     * openagc returns an error instead of crashing.
     */
    return AGC_ERROR_INVALID_STATE;
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
