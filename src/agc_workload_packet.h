#ifndef OPENAGC_WORKLOAD_PACKET_H
#define OPENAGC_WORKLOAD_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    AGC_SONY_WORKLOAD_ACTIVE_DWORDS = 18,
    AGC_SONY_WORKLOAD_COMPLETE_DWORDS = 12,
    AGC_SONY_WORKLOAD_INACTIVE_DWORDS = 9
};

size_t agcSonyBuildWorkloadsActivePacket(uint32_t *dst,
    size_t capacity_dwords, bool control, uint32_t stream_id,
    uint64_t stream_slot_address, uint64_t workload_mask);
size_t agcSonyBuildWorkloadCompletePacket(uint32_t *dst,
    size_t capacity_dwords, bool control, uint32_t stream_id,
    uint32_t workload_id, uint64_t stream_slot_address);
size_t agcSonyBuildWorkloadStreamInactivePacket(uint32_t *dst,
    size_t capacity_dwords, bool control, uint32_t stream_id);

#endif
