#include "agc_workload_state.h"

#include "agc_error.h"
#include "agcdriver.h"

#include <string.h>

static uint64_t g_stream_table_gpu_address;
static uint32_t g_stream_mask;
static uint8_t g_stream_descriptors[32][32];

void agcSonyWorkloadConfigureStreamTable(uint64_t gpu_address)
{
    g_stream_table_gpu_address = gpu_address;
}

void agcSonyWorkloadResetStreamState(void)
{
    g_stream_mask = 1u;
    memset(g_stream_descriptors, 0, sizeof(g_stream_descriptors));
    memcpy(g_stream_descriptors[0], "System", sizeof("System"));
}

int32_t agcSonyWorkloadRegisterStream(uint32_t stream_id,
    const void *descriptor)
{
    uint32_t bit;

    if (stream_id < 1u || stream_id > 31u)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_VALUE;
    bit = UINT32_C(1) << stream_id;
    if ((g_stream_mask & bit) != 0)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_VALUE;
    if (!descriptor)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_ARGUMENT;

    memcpy(g_stream_descriptors[stream_id], descriptor,
        sizeof(g_stream_descriptors[stream_id]));
    g_stream_mask |= bit;
    return AGC_OK;
}

int32_t agcSonyWorkloadUnregisterStream(uint32_t stream_id)
{
    uint32_t bit;

    if (stream_id < 1u || stream_id > 31u)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_VALUE;
    bit = UINT32_C(1) << stream_id;
    if ((g_stream_mask & bit) == 0)
        return (int32_t)AGC_DRIVER_ERROR_NOT_REGISTERED;

    memset(g_stream_descriptors[stream_id], 0,
        sizeof(g_stream_descriptors[stream_id]));
    g_stream_mask &= ~bit;
    return AGC_OK;
}

bool agcSonyWorkloadGetStreamSlotAddress(uint32_t stream_id,
    uint64_t *address_out)
{
    if (!address_out || stream_id < 1u || stream_id > 31u ||
        (g_stream_mask & (UINT32_C(1) << stream_id)) == 0 ||
        g_stream_table_gpu_address == 0)
        return false;
    *address_out = g_stream_table_gpu_address +
        stream_id * sizeof(uint64_t);
    return true;
}
