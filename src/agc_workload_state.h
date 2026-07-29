#ifndef OPENAGC_WORKLOAD_STATE_H
#define OPENAGC_WORKLOAD_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool agcSonyWorkloadInitializeGpuSlots(void *table, size_t table_size,
    const uint32_t seed[4], bool seed_valid);
void agcSonyWorkloadConfigureStreamTable(uint64_t gpu_address);
void agcSonyWorkloadResetStreamState(void);
int32_t agcSonyWorkloadRegisterStream(uint32_t stream_id,
    const void *descriptor);
int32_t agcSonyWorkloadUnregisterStream(uint32_t stream_id);
bool agcSonyWorkloadGetStreamSlotAddress(uint32_t stream_id,
    uint64_t *address_out);

#endif
