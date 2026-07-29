#ifndef OPENAGC_WORKLOAD_STATE_H
#define OPENAGC_WORKLOAD_STATE_H

#include <stdbool.h>
#include <stdint.h>

void agcSonyWorkloadConfigureStreamTable(uint64_t gpu_address);
void agcSonyWorkloadResetStreamState(void);
int32_t agcSonyWorkloadRegisterStream(uint32_t stream_id,
    const void *descriptor);
int32_t agcSonyWorkloadUnregisterStream(uint32_t stream_id);
bool agcSonyWorkloadGetStreamSlotAddress(uint32_t stream_id,
    uint64_t *address_out);

#endif
