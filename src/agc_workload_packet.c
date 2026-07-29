#include "agc_workload_packet.h"

#include "agc_pm4.h"

#define AGC_WORKLOAD_PREFIX_ACTIVE   UINT32_C(0xcc000000)
#define AGC_WORKLOAD_PREFIX_COMPLETE UINT32_C(0xcd000000)
#define AGC_WORKLOAD_PACKET_ACTIVE   UINT32_C(0x00000267)
#define AGC_WORKLOAD_PACKET_COMPLETE UINT32_C(0x00000275)
#define AGC_WORKLOAD_STANDALONE_BIT  UINT32_C(0x40000000)

static bool agcSonyWorkloadStreamValid(uint32_t stream_id,
    uint64_t stream_slot_address)
{
    return stream_id >= 1u && stream_id <= 31u &&
        (stream_slot_address & UINT64_C(7)) == 0;
}

size_t agcSonyBuildWorkloadsActivePacket(uint32_t *dst,
    size_t capacity_dwords, bool control, uint32_t stream_id,
    uint64_t stream_slot_address, uint64_t workload_mask)
{
    uint32_t prefix_sub = control ? 0u : 1u;
    uint32_t packet_control = AGC_WORKLOAD_PACKET_ACTIVE;

    if (!dst || capacity_dwords < AGC_SONY_WORKLOAD_ACTIVE_DWORDS ||
        !agcSonyWorkloadStreamValid(stream_id, stream_slot_address) ||
        workload_mask == 0)
        return 0;
    if (!control)
        packet_control |= AGC_WORKLOAD_STANDALONE_BIT;

    dst[0] = agcPm4Header3Sub(AGC_PM4_OP_SET_UCONFIG_REG, prefix_sub, 4);
    dst[1] = UINT32_C(0x00000342);
    dst[2] = AGC_WORKLOAD_PREFIX_ACTIVE | stream_id;
    dst[3] = (uint32_t)workload_mask;
    dst[4] = agcPm4Header3Sub(AGC_PM4_OP_WRITE_DATA, prefix_sub, 5);
    dst[5] = UINT32_C(0x06010000);
    dst[6] = UINT32_C(0x0000c343);
    dst[7] = (uint32_t)(workload_mask >> 32);
    dst[8] = 0;
    dst[9] = agcPm4Header3(AGC_PM4_OP_SET_WORKLOAD, 9);
    dst[10] = packet_control;
    dst[11] = (uint32_t)stream_slot_address;
    dst[12] = (uint32_t)(stream_slot_address >> 32);
    dst[13] = (uint32_t)workload_mask;
    dst[14] = (uint32_t)(workload_mask >> 32);
    dst[15] = 0;
    dst[16] = 0;
    dst[17] = 0;
    return AGC_SONY_WORKLOAD_ACTIVE_DWORDS;
}

size_t agcSonyBuildWorkloadCompletePacket(uint32_t *dst,
    size_t capacity_dwords, bool control, uint32_t stream_id,
    uint32_t workload_id, uint64_t stream_slot_address)
{
    uint64_t workload_mask;
    uint32_t prefix_sub = control ? 0u : 1u;
    uint32_t packet_control = AGC_WORKLOAD_PACKET_COMPLETE;

    if (!dst || capacity_dwords < AGC_SONY_WORKLOAD_COMPLETE_DWORDS ||
        !agcSonyWorkloadStreamValid(stream_id, stream_slot_address) ||
        workload_id > 63u)
        return 0;
    if (!control)
        packet_control |= AGC_WORKLOAD_STANDALONE_BIT;
    workload_mask = ~(UINT64_C(1) << workload_id);

    dst[0] = agcPm4Header3Sub(AGC_PM4_OP_SET_UCONFIG_REG, prefix_sub, 3);
    dst[1] = UINT32_C(0x00000342);
    dst[2] = AGC_WORKLOAD_PREFIX_COMPLETE |
        (workload_id << 5) | stream_id;
    dst[3] = agcPm4Header3(AGC_PM4_OP_SET_WORKLOAD, 9);
    dst[4] = packet_control;
    dst[5] = (uint32_t)stream_slot_address;
    dst[6] = (uint32_t)(stream_slot_address >> 32);
    dst[7] = (uint32_t)workload_mask;
    dst[8] = (uint32_t)(workload_mask >> 32);
    dst[9] = 0;
    dst[10] = 0;
    dst[11] = 0;
    return AGC_SONY_WORKLOAD_COMPLETE_DWORDS;
}
