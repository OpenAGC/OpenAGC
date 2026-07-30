/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Stable public driver ABI dispatch into the selected private backend.
 */

#include "driver_ops.h"

#include "agc_context.h"
#include "driver_registry.h"
#include "game_compat_internal.h"

#ifdef OPENAGC_GENERIC
#define AGC_DEFAULT_DRIVER_OPS agcGenericDriverOps
#endif

#ifdef OPENAGC_PROSPERO
static const AgcDriverOps *g_driver_ops;
#else
static const AgcDriverOps *g_driver_ops = &AGC_DEFAULT_DRIVER_OPS;
#endif

const AgcDriverOps *agcDriverGetOps(void)
{
    return g_driver_ops;
}

const char *agcDriverDebugBackendName(void)
{
    return g_driver_ops ? g_driver_ops->name : "unselected";
}

int32_t agcDriverSelectRegisterDefaultsVersion(uint32_t version)
{
    if (version > AGC_REGISTER_DEFAULTS_VERSION_12)
        return AGC_ERROR_INVALID_ARGUMENT;
#ifdef OPENAGC_PROSPERO
    return agcProsperoSelectRegisterDefaultsVersion(version);
#else
    return AGC_OK;
#endif
}

#ifdef OPENAGC_GENERIC
int32_t agcDriverInstallOpsForTesting(const AgcDriverOps *ops)
{
    if (!ops)
        return AGC_ERROR_INVALID_ARGUMENT;
    g_driver_ops = ops;
    return AGC_OK;
}

void agcDriverClearOpsForTesting(void)
{
    g_driver_ops = NULL;
}

void agcDriverResetOpsForTesting(void)
{
    g_driver_ops = &AGC_DEFAULT_DRIVER_OPS;
}
#endif

#define AGC_DISPATCH_OR_UNSUPPORTED(member, call) \
    do { \
        const AgcDriverOps *ops = agcDriverGetOps(); \
        if (!ops || !ops->member) \
            return AGC_ERROR_NOT_SUPPORTED; \
        return ops->member call; \
    } while (0)

int32_t PS5_SYSV_ABI sce_agc_initialize(void)
{
#ifdef OPENAGC_PROSPERO
    if (!g_driver_ops) {
        int32_t result = agcDriverSelectRuntime(NULL, &g_driver_ops);

        if (result != AGC_OK)
            return result;
    }
#endif
    AGC_DISPATCH_OR_UNSUPPORTED(initialize, ());
}

int32_t PS5_SYSV_ABI sce_agc_initialize_internal_memory(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(initialize_internal_memory, ());
}

int32_t PS5_SYSV_ABI agcDriverShutdown(void)
{
    const AgcDriverOps *ops = agcDriverGetOps();
    int32_t result;

    if (!ops || !ops->shutdown)
        return AGC_ERROR_NOT_SUPPORTED;
    result = ops->shutdown();
#ifdef OPENAGC_PROSPERO
    if (result == AGC_OK)
        g_driver_ops = NULL;
#endif
    if (result == AGC_OK)
        agcGameCompatResetRegisterDefaultsVersion();
    return result;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiCommandBuffersDirect(
    uint32_t count, void *const dcb_gpu_addrs[], uint32_t *dcb_sizes_in_bytes,
    void *const acb_gpu_addrs[], uint32_t *acb_sizes_in_bytes)
{
    AGC_DISPATCH_OR_UNSUPPORTED(submit_multi_command_buffers_direct,
        (count, dcb_gpu_addrs, dcb_sizes_in_bytes,
         acb_gpu_addrs, acb_sizes_in_bytes));
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitDcb(const AgcCommandBufferSubmit *packet)
{
    AGC_DISPATCH_OR_UNSUPPORTED(submit_dcb, (packet));
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitAcb(
    uint32_t owner_handle, const AgcCommandBufferSubmit *packet)
{
    AGC_DISPATCH_OR_UNSUPPORTED(submit_acb, (owner_handle, packet));
}

int32_t PS5_SYSV_ABI sceAgcDriverSuspendPointSubmitDirect(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    (void)field0;
    (void)field1;
    (void)field2;
    (void)field3;
    return (int32_t)AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT;
}

int32_t PS5_SYSV_ABI sce_agc_internal_suspend_point_submit_primary(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    AGC_DISPATCH_OR_UNSUPPORTED(internal_suspend_point_submit_primary,
        (field0, field1, field2, field3));
}

int32_t PS5_SYSV_ABI sceAgcDriverIsSuspendPointInFlightDirect(uint32_t value)
{
    (void)value;
    return (int32_t)AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT;
}

int32_t PS5_SYSV_ABI sce_agc_internal_suspend_point_submit_final(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    AGC_DISPATCH_OR_UNSUPPORTED(internal_suspend_point_submit_final,
        (field0, field1, field2, field3));
}

int32_t PS5_SYSV_ABI sceAgcDriverSetupAsyncGraphics(uint32_t pipe_id)
{
    AGC_DISPATCH_OR_UNSUPPORTED(setup_async_graphics, (pipe_id));
}

int32_t PS5_SYSV_ABI sceAgcDriverSetTFRing(
    uintptr_t ring_addr, uint32_t size)
{
    /* FW 5.50 vaddr 0x9180 rejects addresses that are not 256-byte
     * aligned and sizes that are not dword-aligned. */
    if ((ring_addr & 0xffu) != 0u || (size & 3u) != 0u)
        return AGC_ERROR_INTERNAL;
    if (size > 0x4000u)
        size = 0x4000u;
    AGC_DISPATCH_OR_UNSUPPORTED(set_tf_ring, (ring_addr, size));
}

int32_t PS5_SYSV_ABI sceAgcDriverSetTFRingDirect(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(set_tf_ring_direct, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverSetHsOffchipParamDirect(
    uint64_t list_addr, uint32_t num_entries)
{
    AGC_DISPATCH_OR_UNSUPPORTED(set_hs_offchip_param_direct,
        (list_addr, num_entries));
}

int32_t PS5_SYSV_ABI sceAgcDriverSetTargetRingForDiag(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(set_target_ring_for_diag, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverNotifyDefaultStates(uint32_t flags)
{
    AGC_DISPATCH_OR_UNSUPPORTED(notify_default_states, (flags));
}

int32_t PS5_SYSV_ABI sceAgcDriverSdmaCopyLinearBlocking(
    void *dst, const void *src, size_t size)
{
    AGC_DISPATCH_OR_UNSUPPORTED(sdma_copy_linear_blocking, (dst, src, size));
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitEopFlip(
    void *video_out_handle, uint32_t display_buf_index,
    uint32_t flip_mode, void *present_ptr)
{
    AGC_DISPATCH_OR_UNSUPPORTED(submit_eop_flip,
        (video_out_handle, display_buf_index, flip_mode, present_ptr));
}

int32_t PS5_SYSV_ABI sceAgcDriverSetWorkloadsActive(uint32_t workload_id)
{
    AGC_DISPATCH_OR_UNSUPPORTED(set_workloads_active, (workload_id));
}

int32_t PS5_SYSV_ABI sceAgcDriverSetWorkloadComplete(uint32_t workload_id)
{
    AGC_DISPATCH_OR_UNSUPPORTED(set_workload_complete, (workload_id));
}

int32_t PS5_SYSV_ABI _sceAgcDriverCreateUserSpecialQueue(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(create_user_special_queue, ());
}

int32_t PS5_SYSV_ABI _sceAgcDriverDestroyUserSpecialQueue(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(destroy_user_special_queue, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverRegisterCaptureInterface(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(register_capture_interface, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverDeregisterCaptureInterface(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(deregister_capture_interface, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverAcquireRazorACQ(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(acquire_razor_acq, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverReleaseRazorACQ(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(release_razor_acq, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitToRazorACQ(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(submit_to_razor_acq, ());
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitToHDRScopesACQ(void)
{
    AGC_DISPATCH_OR_UNSUPPORTED(submit_to_hdr_scopes_acq, ());
}

uint32_t PS5_SYSV_ABI sceAgcDriverGetPaDebugInterfaceVersion(void)
{
    const AgcDriverOps *ops = agcDriverGetOps();
    return ops && ops->get_pa_debug_interface_version
        ? ops->get_pa_debug_interface_version()
        : (uint32_t)AGC_ERROR_NOT_SUPPORTED;
}
