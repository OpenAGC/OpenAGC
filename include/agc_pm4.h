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

#ifndef _AGC_PM4_H_
#define _AGC_PM4_H_

#include <stdint.h>

/*
 * PS5 Gen5 AGC/PM4 packet helpers.
 *
 * HLE reference and RPCSX both decode type-3 packets with length stored in bits
 * 29:16 as (length_dwords - 2). HLE reference also shows several AGC commands
 * encoded as IT_NOP packets with an AGC subcommand in bits 7:2.
 */

#define AGC_PM4_TYPE3 3u

typedef enum AgcPm4Opcode {
    AGC_PM4_OP_NOP                       = 0x10,
    AGC_PM4_OP_SET_BASE                  = 0x11,
    /* AGC-custom clear-state opcode (not standard AMD 0x14).
     * RE: SPRX sceAgcDcbClearState emits header 0xc0001200. */
    AGC_PM4_OP_CLEAR_STATE_AGC           = 0x12,
    AGC_PM4_OP_CLEAR_STATE               = 0x14,
    AGC_PM4_OP_INDEX_BUFFER_SIZE         = 0x13,
    AGC_PM4_OP_DISPATCH_DIRECT           = 0x15,
    AGC_PM4_OP_DISPATCH_INDIRECT         = 0x16,
    AGC_PM4_OP_ATOMIC_MEM                = 0x1B,
    AGC_PM4_OP_ATOMIC_GDS                = 0x1D,
    /* Opcode 0x1E is used for both ATOMIC_MEM (sub=0, 9 dwords) and
     * SET_WORKLOAD (sub=0x20/0x21, 3 dwords) on AGC. The SPRX function
     * sceAgcDcbAtomicMem uses 0x1E with sub=0. */
    AGC_PM4_OP_SET_WORKLOAD              = 0x1E,
    AGC_PM4_OP_ATOMIC_MEM_AGC            = 0x1E,  /* alias */
    AGC_PM4_OP_SET_PREDICATION           = 0x20,
    AGC_PM4_OP_COND_EXEC                 = 0x22,
    AGC_PM4_OP_DRAW_INDIRECT             = 0x24,
    AGC_PM4_OP_DRAW_INDEX_INDIRECT       = 0x25,
    AGC_PM4_OP_INDEX_BASE                = 0x26,
    AGC_PM4_OP_DRAW_INDEX_2              = 0x27,
    AGC_PM4_OP_DRAW_INDIRECT_MULTI       = 0x2C,
    AGC_PM4_OP_INDEX_TYPE                = 0x2A,
    AGC_PM4_OP_DRAW_INDEX_AUTO           = 0x2D,
    AGC_PM4_OP_DRAW_INDEX_INDIRECT_MULTI = 0x38,
    AGC_PM4_OP_NUM_INSTANCES             = 0x2F,
    /* AGC-custom multi-instanced indexed draw / dispatch draw preamble.
     * RE: SPRX sceAgcDcbDrawIndexMultiInstanced emits 0xc0073a00.
     * reference-confirmed: IT_DISPATCH_DRAW_PREAMBLE = 0x3A (same opcode). */
    AGC_PM4_OP_DRAW_INDEX_MULTI_INSTANCED = 0x3A,
    AGC_PM4_OP_DISPATCH_DRAW_PREAMBLE     = 0x3A,  /* alias, the reference name */
    AGC_PM4_OP_INDIRECT_BUFFER_CNST      = 0x33,
    AGC_PM4_OP_DRAW_INDEX_OFFSET_2       = 0x35,
    AGC_PM4_OP_WRITE_DATA                = 0x37,
    AGC_PM4_OP_MEM_SEMAPHORE             = 0x39,
    AGC_PM4_OP_WAIT_REG_MEM              = 0x3C,
    AGC_PM4_OP_INDIRECT_BUFFER           = 0x3F,
    AGC_PM4_OP_COPY_DATA                 = 0x40,
    /* AGC-custom conditional write (CB only).
     * RE: SPRX sceAgcCbCondWrite emits 0xc0074500. */
    AGC_PM4_OP_COND_WRITE                = 0x45,
    AGC_PM4_OP_EVENT_WRITE               = 0x46,
    AGC_PM4_OP_EVENT_WRITE_EOP           = 0x47,
    AGC_PM4_OP_EVENT_WRITE_EOS           = 0x48,
    AGC_PM4_OP_RELEASE_MEM               = 0x49,
    AGC_PM4_OP_DMA_DATA                  = 0x50,
    AGC_PM4_OP_ACQUIRE_MEM               = 0x58,
    AGC_PM4_OP_REWIND                    = 0x59,
    AGC_PM4_OP_SET_CONFIG_REG            = 0x68,
    AGC_PM4_OP_SET_CONTEXT_REG           = 0x69,
    /* reference-confirmed: IT_SET_CONTEXT_REG_INDIRECT = 0x9F.
     * Alias for AGC_PM4_OP_SET_CX_REG_INDIRECT below. */
    AGC_PM4_OP_SET_CONTEXT_REG_INDIRECT  = 0x9F,
    AGC_PM4_OP_SET_SH_REG                = 0x76,
    AGC_PM4_OP_SET_SH_REG_OFFSET         = 0x77,
    AGC_PM4_OP_SET_QUEUE_REG             = 0x78,
    AGC_PM4_OP_SET_UCONFIG_REG           = 0x79,
    AGC_PM4_OP_DISPATCH_DRAW             = 0x8D,
    AGC_PM4_OP_GET_LOD_STATS             = 0x8E,
    /* AGC-custom set index indirect args.
     * RE: SPRX sceAgcDcbSetIndexIndirectArgs emits 0xc0029100. */
    AGC_PM4_OP_SET_INDEX_INDIRECT_ARGS   = 0x91,
    AGC_PM4_OP_WAIT_REG_MEM64            = 0x93,
    AGC_PM4_OP_SUSPEND_POINT_MARKER      = 0x93,  /* kernel-side preemption marker */
    AGC_PM4_OP_PRIME_UTCL2               = 0x5D,
    AGC_PM4_OP_MAP_PROCESS               = 0xA1,
    AGC_PM4_OP_MAP_QUEUES                = 0xA2,
    AGC_PM4_OP_UNMAP_QUEUES              = 0xA3,
    AGC_PM4_OP_QUERY_STATUS              = 0xA4,
    AGC_PM4_OP_RUN_LIST                  = 0xA5,
    /* AGC-custom display/flip opcodes (libSceAgc.sprx only).
     * These are not standard PM4 opcodes — they are AGC-specific
     * extensions used by the flip/display wait builders. */
    AGC_PM4_OP_WAIT_FLIP_DONE         = 0x4C,
    AGC_PM4_OP_WAIT_FLIP_EOS_2        = 0x4E,
    AGC_PM4_OP_WAIT_FLIP_EOS          = 0x4F,
    AGC_PM4_OP_WAIT_FLIP              = 0x51,
    AGC_PM4_OP_INSERT_WAIT_FLIP_DONE  = 0x54,
    /* AGC-custom indirect register write opcodes.
     * RE source: SPRX disassembly of libSceAgc.sprx (FW 5.50).
     * These are not NOP-wrapped subcommands — they are direct type-3
     * opcodes used by sceAgcDcbSet{Sh,Cx,Uc}RegistersIndirect and
     * validated by the corresponding patchers. */
    AGC_PM4_OP_SET_SH_REG_INDIRECT    = 0x63,
    AGC_PM4_OP_SET_UC_REG_INDIRECT    = 0x64,
    AGC_PM4_OP_STALL_PARSER           = 0x42,
    AGC_PM4_OP_SET_INDEX_SIZE         = 0x7A,
    AGC_PM4_OP_SET_CX_REG_INDIRECT    = 0x9F,
} AgcPm4Opcode;

typedef enum AgcPm4Subcommand {
    AGC_PM4_SUB_ZERO             = 0x00,
    /* DEPRECATED: DrawIndexAuto now uses IT_DRAW_INDEX_AUTO (0x2D) directly
     * (reference-confirmed). This NOP subcommand is retained for reference
     * only — do not use in new packet builders. */
    AGC_PM4_SUB_DRAW_INDEX_AUTO  = 0x04,
    AGC_PM4_SUB_DRAW_RESET       = 0x05,
    AGC_PM4_SUB_WAIT_FLIP_DONE   = 0x06,
    AGC_PM4_SUB_ACB_RESET        = 0x09,
    AGC_PM4_SUB_WAIT_MEM32       = 0x0A,
    AGC_PM4_SUB_PUSH_MARKER      = 0x0B,
    AGC_PM4_SUB_POP_MARKER       = 0x0C,
    AGC_PM4_SUB_SH_REGS_INDIRECT = 0x11,
    AGC_PM4_SUB_CX_REGS_INDIRECT = 0x12,
    AGC_PM4_SUB_UC_REGS_INDIRECT = 0x13,
    AGC_PM4_SUB_ACQUIRE_MEM      = 0x14,
    AGC_PM4_SUB_WRITE_DATA       = 0x15,
    AGC_PM4_SUB_WAIT_MEM64       = 0x16,
    AGC_PM4_SUB_FLIP             = 0x17,
    AGC_PM4_SUB_RELEASE_MEM      = 0x18,
    AGC_PM4_SUB_DMA_DATA         = 0x19,
    /* SET_WORKLOAD subcommands — used by sceAgcDriverBeginWorkload /
     * sceAgcDriverEndWorkload (libSceAgcDriver.sprx ordinals 87/88).
     * The 0xcc / 0xcd prefix bits in the SPRX correspond to these
     * subcommand selectors within the SET_WORKLOAD opcode (0x1E). */
    AGC_PM4_SUB_WORKLOAD_BEGIN    = 0x20,
    AGC_PM4_SUB_WORKLOAD_END      = 0x21,
} AgcPm4Subcommand;

static inline uint32_t agcPm4Header3Sub(
    uint32_t opcode, uint32_t subcommand, uint32_t length_dwords)
{
    return 0xC0000000u |
        (((length_dwords - 2u) & 0x3FFFu) << 16) |
        ((opcode & 0xFFu) << 8) |
        ((subcommand & 0x3Fu) << 2);
}

static inline uint32_t agcPm4Header3(uint32_t opcode, uint32_t length_dwords)
{
    return agcPm4Header3Sub(opcode, 0, length_dwords);
}

static inline uint32_t agcPm4Type(uint32_t header)
{
    return header >> 30;
}

static inline uint32_t agcPm4Length(uint32_t header)
{
    return ((header >> 16) & 0x3FFFu) + 2u;
}

static inline uint32_t agcPm4Opcode(uint32_t header)
{
    return (header >> 8) & 0xFFu;
}

static inline uint32_t agcPm4Subcommand(uint32_t header)
{
    return (header >> 2) & 0x3Fu;
}

#endif /* _AGC_PM4_H_ */
