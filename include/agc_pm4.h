#ifndef _AGC_PM4_H_
#define _AGC_PM4_H_

#include <stdint.h>

/*
 * PS5 Gen5 AGC/PM4 packet helpers.
 *
 * SharpEmu and RPCSX both decode type-3 packets with length stored in bits
 * 29:16 as (length_dwords - 2). SharpEmu also shows several AGC commands
 * encoded as IT_NOP packets with an AGC subcommand in bits 7:2.
 */

#define AGC_PM4_TYPE3 3u

typedef enum AgcPm4Opcode {
    AGC_PM4_OP_NOP                       = 0x10,
    AGC_PM4_OP_SET_BASE                  = 0x11,
    AGC_PM4_OP_CLEAR_STATE               = 0x14,
    AGC_PM4_OP_INDEX_BUFFER_SIZE         = 0x13,
    AGC_PM4_OP_DISPATCH_DIRECT           = 0x15,
    AGC_PM4_OP_DISPATCH_INDIRECT         = 0x16,
    AGC_PM4_OP_ATOMIC_MEM                = 0x1B,
    AGC_PM4_OP_ATOMIC_GDS                = 0x1D,
    AGC_PM4_OP_SET_WORKLOAD              = 0x1E,
    AGC_PM4_OP_SET_PREDICATION           = 0x20,
    AGC_PM4_OP_COND_EXEC                 = 0x22,
    AGC_PM4_OP_DRAW_INDIRECT             = 0x24,
    AGC_PM4_OP_DRAW_INDEX_INDIRECT       = 0x25,
    AGC_PM4_OP_INDEX_BASE                = 0x26,
    AGC_PM4_OP_DRAW_INDEX_2              = 0x27,
    AGC_PM4_OP_INDEX_TYPE                = 0x2A,
    AGC_PM4_OP_DRAW_INDEX_AUTO           = 0x2D,
    AGC_PM4_OP_NUM_INSTANCES             = 0x2F,
    AGC_PM4_OP_INDIRECT_BUFFER_CNST      = 0x33,
    AGC_PM4_OP_DRAW_INDEX_OFFSET_2       = 0x35,
    AGC_PM4_OP_WRITE_DATA                = 0x37,
    AGC_PM4_OP_MEM_SEMAPHORE             = 0x39,
    AGC_PM4_OP_WAIT_REG_MEM              = 0x3C,
    AGC_PM4_OP_INDIRECT_BUFFER           = 0x3F,
    AGC_PM4_OP_COPY_DATA                 = 0x40,
    AGC_PM4_OP_EVENT_WRITE               = 0x46,
    AGC_PM4_OP_EVENT_WRITE_EOP           = 0x47,
    AGC_PM4_OP_EVENT_WRITE_EOS           = 0x48,
    AGC_PM4_OP_RELEASE_MEM               = 0x49,
    AGC_PM4_OP_DMA_DATA                  = 0x50,
    AGC_PM4_OP_ACQUIRE_MEM               = 0x58,
    AGC_PM4_OP_REWIND                    = 0x59,
    AGC_PM4_OP_SET_CONFIG_REG            = 0x68,
    AGC_PM4_OP_SET_CONTEXT_REG           = 0x69,
    AGC_PM4_OP_SET_CONTEXT_REG_INDIRECT  = 0x73,
    AGC_PM4_OP_SET_SH_REG                = 0x76,
    AGC_PM4_OP_SET_SH_REG_OFFSET         = 0x77,
    AGC_PM4_OP_SET_QUEUE_REG             = 0x78,
    AGC_PM4_OP_SET_UCONFIG_REG           = 0x79,
    AGC_PM4_OP_DISPATCH_DRAW_PREAMBLE    = 0x8C,
    AGC_PM4_OP_DISPATCH_DRAW             = 0x8D,
    AGC_PM4_OP_GET_LOD_STATS             = 0x8E,
    AGC_PM4_OP_WAIT_REG_MEM64            = 0x93,
    AGC_PM4_OP_SUSPEND_POINT_MARKER      = 0x93,  /* kernel-side preemption marker */
    AGC_PM4_OP_PRIME_UTCL2               = 0x5D,
    AGC_PM4_OP_MAP_PROCESS               = 0xA1,
    AGC_PM4_OP_MAP_QUEUES                = 0xA2,
    AGC_PM4_OP_UNMAP_QUEUES              = 0xA3,
    AGC_PM4_OP_QUERY_STATUS              = 0xA4,
    AGC_PM4_OP_RUN_LIST                  = 0xA5,
} AgcPm4Opcode;

typedef enum AgcPm4Subcommand {
    AGC_PM4_SUB_ZERO             = 0x00,
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
