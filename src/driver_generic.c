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
 * openagc - driver_generic.c
 *
 * Generic host backend for tests and tools.
 */

#include "agc_error.h"
#include "agc_types.h"
#include "agcdriver.h"
#include "driver_ops.h"

#include <string.h>

static bool g_agc_initialized = false;
static AgcCommandBufferSubmit g_last_dcb_submit;
static AgcCommandBufferSubmit g_last_acb_submit;
static uint32_t g_last_acb_owner;

/* Async-compute queue tracking (mirrors the prospero backend's 32-slot array).
 * Each slot records whether a user special queue is active and the pipe_id
 * passed to agcGenericSetupAsyncGraphics. */
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

int32_t PS5_SYSV_ABI agcGenericInitialize(void)
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

int32_t PS5_SYSV_ABI agcGenericInitializeInternalMemory(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSubmitMultiCommandBuffersDirect(
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

int32_t PS5_SYSV_ABI agcGenericSubmitDcb(const AgcCommandBufferSubmit *packet)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    g_last_dcb_submit = *packet;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSubmitAcb(
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

int32_t PS5_SYSV_ABI agcGenericSuspendPointSubmitDirect(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    (void)field0;
    (void)field1;
    (void)field2;
    (void)field3;
    return AGC_OK;
}

bool PS5_SYSV_ABI agcGenericIsSuspendPointInFlightDirect(uint32_t value)
{
    (void)value;
    return false;
}

int32_t PS5_SYSV_ABI agcGenericInternalSuspendPointSubmitFinal(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    (void)field0;
    (void)field1;
    (void)field2;
    (void)field3;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSetupAsyncGraphics(uint32_t pipe_id)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    g_async_setup_done = true;
    g_async_pipe_id    = pipe_id;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSetTFRingDirect(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSetTFRing(uintptr_t ring_addr, uint32_t size)
{
    (void)ring_addr;
    (void)size;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSetHsOffchipParamDirect(
    uint64_t list_addr, uint32_t num_entries)
{
    (void)list_addr;
    (void)num_entries;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSetTargetRingForDiag(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericNotifyDefaultStates(uint32_t flags)
{
    (void)flags;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSdmaCopyLinearBlocking(
    void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return AGC_ERROR_INVALID_ARGUMENT;
    memcpy(dst, src, size);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSubmitEopFlip(
    void *video_out_handle, uint32_t display_buf_index,
    uint32_t flip_mode, void *present_ptr)
{
    (void)video_out_handle;
    (void)display_buf_index;
    (void)flip_mode;
    (void)present_ptr;

    /* EOP flip submit requires the prospero /dev/gc backend and
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
int32_t PS5_SYSV_ABI agcGenericSetWorkloadsActive(uint32_t workload_id)
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

int32_t PS5_SYSV_ABI agcGenericSetWorkloadComplete(uint32_t workload_id)
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

int32_t PS5_SYSV_ABI agcGenericCreateUserSpecialQueue(void)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Find a free queue slot and mark it in use. The queue index is
     * returned as the handle so callers can pass it to agcGenericSubmitAcb
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

int32_t PS5_SYSV_ABI agcGenericDestroyUserSpecialQueue(void)
{
    if (!g_agc_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Find the first in-use queue and destroy it (matching prospero behavior:
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

int32_t PS5_SYSV_ABI agcGenericRegisterCaptureInterface(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericDeregisterCaptureInterface(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericAcquireRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericReleaseRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSubmitToRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGenericSubmitToHDRScopesACQ(void)
{
    return AGC_OK;
}

uint32_t PS5_SYSV_ABI agcGenericGetPaDebugInterfaceVersion(void)
{
    return AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT;
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

const AgcDriverOps agcGenericDriverOps = {
    .name = "generic",
    .initialize = agcGenericInitialize,
    .initialize_internal_memory = agcGenericInitializeInternalMemory,
    .submit_multi_command_buffers_direct = agcGenericSubmitMultiCommandBuffersDirect,
    .submit_dcb = agcGenericSubmitDcb,
    .submit_acb = agcGenericSubmitAcb,
    .suspend_point_submit_direct = agcGenericSuspendPointSubmitDirect,
    .is_suspend_point_in_flight_direct = agcGenericIsSuspendPointInFlightDirect,
    .internal_suspend_point_submit_final = agcGenericInternalSuspendPointSubmitFinal,
    .setup_async_graphics = agcGenericSetupAsyncGraphics,
    .set_tf_ring = agcGenericSetTFRing,
    .set_tf_ring_direct = agcGenericSetTFRingDirect,
    .set_hs_offchip_param_direct = agcGenericSetHsOffchipParamDirect,
    .set_target_ring_for_diag = agcGenericSetTargetRingForDiag,
    .notify_default_states = agcGenericNotifyDefaultStates,
    .sdma_copy_linear_blocking = agcGenericSdmaCopyLinearBlocking,
    .submit_eop_flip = agcGenericSubmitEopFlip,
    .set_workloads_active = agcGenericSetWorkloadsActive,
    .set_workload_complete = agcGenericSetWorkloadComplete,
    .create_user_special_queue = agcGenericCreateUserSpecialQueue,
    .destroy_user_special_queue = agcGenericDestroyUserSpecialQueue,
    .register_capture_interface = agcGenericRegisterCaptureInterface,
    .deregister_capture_interface = agcGenericDeregisterCaptureInterface,
    .acquire_razor_acq = agcGenericAcquireRazorACQ,
    .release_razor_acq = agcGenericReleaseRazorACQ,
    .submit_to_razor_acq = agcGenericSubmitToRazorACQ,
    .submit_to_hdr_scopes_acq = agcGenericSubmitToHDRScopesACQ,
    .get_pa_debug_interface_version = agcGenericGetPaDebugInterfaceVersion,
};
