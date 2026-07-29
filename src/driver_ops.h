/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Private driver-backend dispatch contract. This header is not installed and
 * must never become part of the game-facing ABI.
 */

#ifndef OPENAGC_DRIVER_OPS_H
#define OPENAGC_DRIVER_OPS_H

#include "agcdriver.h"
#include "agc_runtime_diag.h"

typedef struct AgcDriverOps {
    const char *name;
    int32_t (PS5_SYSV_ABI *initialize)(void);
    int32_t (PS5_SYSV_ABI *initialize_internal_memory)(void);
    int32_t (PS5_SYSV_ABI *shutdown)(void);
    int32_t (PS5_SYSV_ABI *submit_multi_command_buffers_direct)(
        uint32_t, void *const[], uint32_t *, void *const[], uint32_t *);
    int32_t (PS5_SYSV_ABI *submit_dcb)(const AgcCommandBufferSubmit *);
    int32_t (PS5_SYSV_ABI *submit_acb)(uint32_t, const AgcCommandBufferSubmit *);
    int32_t (PS5_SYSV_ABI *suspend_point_submit_direct)(
        uint32_t, uint32_t, uint32_t, uint32_t);
    bool (PS5_SYSV_ABI *is_suspend_point_in_flight_direct)(uint32_t);
    int32_t (PS5_SYSV_ABI *internal_suspend_point_submit_final)(
        uint32_t, uint32_t, uint32_t, uint32_t);
    int32_t (PS5_SYSV_ABI *setup_async_graphics)(uint32_t);
    int32_t (PS5_SYSV_ABI *set_tf_ring)(uintptr_t, uint32_t);
    int32_t (PS5_SYSV_ABI *set_tf_ring_direct)(void);
    int32_t (PS5_SYSV_ABI *set_hs_offchip_param_direct)(uint64_t, uint32_t);
    int32_t (PS5_SYSV_ABI *set_target_ring_for_diag)(void);
    int32_t (PS5_SYSV_ABI *notify_default_states)(uint32_t);
    int32_t (PS5_SYSV_ABI *sdma_copy_linear_blocking)(void *, const void *, size_t);
    int32_t (PS5_SYSV_ABI *submit_eop_flip)(void *, uint32_t, uint32_t, void *);
    int32_t (PS5_SYSV_ABI *set_workloads_active)(uint32_t);
    int32_t (PS5_SYSV_ABI *set_workload_complete)(uint32_t);
    int32_t (PS5_SYSV_ABI *create_user_special_queue)(void);
    int32_t (PS5_SYSV_ABI *destroy_user_special_queue)(void);
    int32_t (PS5_SYSV_ABI *register_capture_interface)(void);
    int32_t (PS5_SYSV_ABI *deregister_capture_interface)(void);
    int32_t (PS5_SYSV_ABI *acquire_razor_acq)(void);
    int32_t (PS5_SYSV_ABI *release_razor_acq)(void);
    int32_t (PS5_SYSV_ABI *submit_to_razor_acq)(void);
    int32_t (PS5_SYSV_ABI *submit_to_hdr_scopes_acq)(void);
    uint32_t (PS5_SYSV_ABI *get_pa_debug_interface_version)(void);
} AgcDriverOps;

extern const AgcDriverOps agcGenericDriverOps;
extern const AgcDriverOps agcProsperoDriverOps;

#ifdef OPENAGC_PROSPERO
int32_t agcProsperoConfigureRuntimeProfile(uint32_t raw_version);
int32_t agcProsperoGetRuntimeProfile(AgcProsperoRuntimeProfile *profile_out);
#endif

const AgcDriverOps *agcDriverGetOps(void);
const char *agcDriverDebugBackendName(void);

#ifdef OPENAGC_GENERIC
int32_t agcDriverInstallOpsForTesting(const AgcDriverOps *ops);
void agcDriverResetOpsForTesting(void);
#endif

#endif
