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

/* Async-compute queue tracking (mirrors the orbis backend's 32-slot array).
 * Each slot records whether a user special queue is active and the pipe_id
 * passed to sceAgcDriverSetupAsyncGraphics. */
#define AGC_GENERIC_MAX_QUEUES 32
typedef struct {
    bool     in_use;
    uint32_t pipe_id;    /* pipe_id from SetupAsyncGraphics */
} AgcGenericQueue;
static AgcGenericQueue g_queues[AGC_GENERIC_MAX_QUEUES];
static bool g_async_setup_done = false;
static uint32_t g_async_pipe_id = 0;

/* Workload tracking state — mirrors the SPRX's workload begin/end logic.
 * The generic backend tracks the current active workload ID so that
 * EndWorkload can validate it matches a prior BeginWorkload. */
static uint32_t g_active_workload_id = 0;
static bool g_workload_active = false;

int32_t PS5_SYSV_ABI sce_agc_initialize(void)
{
    g_agc_initialized = true;
    /* Reset all backend state so re-initialize gives a clean slate. */
    memset(g_queues, 0, sizeof(g_queues));
    g_async_setup_done  = false;
    g_async_pipe_id     = 0;
    g_last_acb_owner    = 0;
    memset(&g_last_dcb_submit, 0, sizeof(g_last_dcb_submit));
    memset(&g_last_acb_submit, 0, sizeof(g_last_acb_submit));
    g_active_workload_id = 0;
    g_workload_active    = false;
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
    if (owner_handle >= AGC_GENERIC_MAX_QUEUES || !g_queues[owner_handle].in_use)
        return AGC_ERROR_CB_INVALID_QUEUE;

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

int32_t PS5_SYSV_ABI sceAgcDriverSetupAsyncGraphics(uint32_t pipe_id)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    g_async_setup_done = true;
    g_async_pipe_id    = pipe_id;
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

int32_t PS5_SYSV_ABI sceAgcDriverSubmitEopFlip(
    void *video_out_handle, uint32_t display_buf_index,
    uint32_t flip_mode, void *present_ptr)
{
    (void)video_out_handle;
    (void)display_buf_index;
    (void)flip_mode;
    (void)present_ptr;

    /* EOP flip submit requires the orbis /dev/gc backend and
     * sceVideoOutSubmitEopFlip — not available on the generic host. */
    return AGC_ERROR_NOT_SUPPORTED;
}

/*
 * Workload tracking — generic backend.
 *
 * The SPRX (libSceAgcDriver.sprx ordinals 87/88) builds a SET_WORKLOAD
 * (0x1E) PM4 packet and submits it. On the generic backend we only
 * validate the workload_id and track active state for EndWorkload
 * validation — there is no GPU to submit to.
 *
 * Validation matches the SPRX: workload_id == 0 returns error
 * 0x8a6c0033 (mapped to AGC_ERROR_INVALID_ARGUMENT). EndWorkload
 * without a matching BeginWorkload returns an error.
 */
int32_t PS5_SYSV_ABI sceAgcDriverBeginWorkload(uint32_t workload_id)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (workload_id == 0)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (g_workload_active)
        return AGC_ERROR_INVALID_STATE;

    g_active_workload_id = workload_id;
    g_workload_active    = true;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverEndWorkload(uint32_t workload_id)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (workload_id == 0)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!g_workload_active || g_active_workload_id != workload_id)
        return AGC_ERROR_INVALID_STATE;

    g_workload_active    = false;
    g_active_workload_id = 0;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI _sceAgcDriverCreateUserSpecialQueue(void)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Find a free queue slot and mark it in use. The queue index is
     * returned as the handle so callers can pass it to sceAgcDriverSubmitAcb
     * as the owner_handle. */
    for (int i = 0; i < AGC_GENERIC_MAX_QUEUES; i++) {
        if (!g_queues[i].in_use) {
            g_queues[i].in_use  = true;
            g_queues[i].pipe_id = g_async_pipe_id;
            return (int32_t)i;
        }
    }

    return AGC_ERROR_CB_INVALID_QUEUE;
}

int32_t PS5_SYSV_ABI _sceAgcDriverDestroyUserSpecialQueue(void)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Find the first in-use queue and destroy it (matching orbis behavior:
     * the SPRX takes no parameters — the kernel tracks the queue). */
    for (int i = 0; i < AGC_GENERIC_MAX_QUEUES; i++) {
        if (g_queues[i].in_use) {
            g_queues[i].in_use  = false;
            g_queues[i].pipe_id = 0;
            return AGC_OK;
        }
    }

    return AGC_ERROR_CB_INVALID_QUEUE;
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

/* Debug helpers for tests — not part of public API */
bool agcDriverDebugIsQueueInUse(uint32_t index)
{
    if (index >= AGC_GENERIC_MAX_QUEUES)
        return false;
    return g_queues[index].in_use;
}

uint32_t agcDriverDebugGetQueueCount(void)
{
    uint32_t count = 0;
    for (int i = 0; i < AGC_GENERIC_MAX_QUEUES; i++) {
        if (g_queues[i].in_use)
            count++;
    }
    return count;
}

bool agcDriverDebugIsAsyncSetup(void)
{
    return g_async_setup_done;
}
