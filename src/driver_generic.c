/*
 * openagc - driver_generic.c
 *
 * Generic host backend for tests and tools.
 */

#include "agc_error.h"
#include "agc_types.h"
#include "agcdriver.h"

#include <string.h>

static bool g_agc_initialized = false;
static AgcCommandBufferSubmit g_last_dcb_submit;
static AgcCommandBufferSubmit g_last_acb_submit;
static uint32_t g_last_acb_owner;

int32_t PS5_SYSV_ABI sce_agc_initialize(void)
{
    g_agc_initialized = true;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sce_agc_initialize_internal_memory(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiCommandBuffersDirect(
    uint32_t count,
    void *const dcb_gpu_addrs[],
    uint32_t *dcb_sizes_in_bytes,
    void *const acb_gpu_addrs[],
    uint32_t *acb_sizes_in_bytes)
{
    (void)acb_gpu_addrs;
    (void)acb_sizes_in_bytes;

    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (count == 0 || !dcb_gpu_addrs || !dcb_sizes_in_bytes)
        return AGC_ERROR_INVALID_ARGUMENT;

    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitDcb(const AgcCommandBufferSubmit *packet)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    g_last_dcb_submit = *packet;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitAcb(
    uint32_t owner_handle, const AgcCommandBufferSubmit *packet)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    g_last_acb_owner = owner_handle;
    g_last_acb_submit = *packet;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSuspendPointSubmitDirect(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    (void)field0;
    (void)field1;
    (void)field2;
    (void)field3;
    return AGC_OK;
}

bool PS5_SYSV_ABI sceAgcDriverIsSuspendPointInFlightDirect(uint32_t value)
{
    (void)value;
    return false;
}

int32_t PS5_SYSV_ABI sce_agc_internal_suspend_point_submit_final(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    (void)field0;
    (void)field1;
    (void)field2;
    (void)field3;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSetupAsyncGraphics(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSetTFRingDirect(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSetHsOffchipParamDirect(
    uint64_t list_addr, uint32_t num_entries)
{
    (void)list_addr;
    (void)num_entries;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSetTargetRingForDiag(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverNotifyDefaultStates(uint32_t flags)
{
    (void)flags;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSdmaCopyLinearBlocking(
    void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return AGC_ERROR_INVALID_ARGUMENT;
    memcpy(dst, src, size);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI _sceAgcDriverCreateUserSpecialQueue(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI _sceAgcDriverDestroyUserSpecialQueue(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverRegisterCaptureInterface(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverDeregisterCaptureInterface(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverAcquireRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverReleaseRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitToRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitToHDRScopesACQ(void)
{
    return AGC_OK;
}

uint32_t PS5_SYSV_ABI sceAgcDriverGetPaDebugInterfaceVersion(void)
{
    return 0;
}

const AgcCommandBufferSubmit *agcDriverDebugLastDcbSubmit(void)
{
    return &g_last_dcb_submit;
}

const AgcCommandBufferSubmit *agcDriverDebugLastAcbSubmit(uint32_t *owner_handle)
{
    if (owner_handle)
        *owner_handle = g_last_acb_owner;
    return &g_last_acb_submit;
}
