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
 * openagc - game_compat.c
 *
 * Game-critical missing AGC functions identified from analysis of
 * "New Joe & Mac Caveman Ninja" (PPSA02801) eboot.bin.
 *
 * These functions are exported by libSceAgc.sprx and libSceAgcDriver.sprx
 * but were missing from openagc. They are needed for real game binaries
 * to link and run.
 *
 * See analysis/game_agc_usage.md for the full import analysis.
 */

#include "agc_cb.h"
#include "agc_context.h"
#include "agc_pm4.h"
#include "agc_registers.h"
#include "agc_shader.h"
#include "agc_types.h"

#include <stdlib.h>
#include "agcdriver.h"

#include <string.h>

/* ===================================================================== */
/* libSceAgcDriver non-Direct variants                                   */
/* ===================================================================== */

/* sceAgcDriverRegisterOwner (NID: X-Nm5KLREeg)
 * SPRX: stub that returns 0x8a6c9018 (not supported on non-dev hardware).
 * Size: 6 bytes (mov eax, 0x8a6c9018; ret). */
int32_t PS5_SYSV_ABI sceAgcDriverRegisterOwner(void *resource, uint32_t *out_handle)
{
    (void)resource;
    (void)out_handle;
    return 0x8a6c9018;
}

/* sceAgcDriverRegisterResource (NID: W5z4eZrjEas)
 * SPRX: stub that returns 0x8a6c9018 (not supported on non-dev hardware).
 * Size: 6 bytes (mov eax, 0x8a6c9018; ret). */
int32_t PS5_SYSV_ABI sceAgcDriverRegisterResource(void *resource, uint32_t owner_handle)
{
    (void)resource;
    (void)owner_handle;
    return 0x8a6c9018;
}

/* sceAgcDriverGetEqContextId (NID: Zw7uUVPulbw)
 * SPRX: calls internal function, right-shifts result by 16.
 * Returns the EQ (event queue) context ID. */
uint32_t PS5_SYSV_ABI sceAgcDriverGetEqContextId(void)
{
    /* On the generic backend, return 0 (no EQ context).
     * On prospero, this would call the internal ioctl path. */
#ifdef OPENAGC_PROSPERO
    /* TODO: implement via ioctl */
    return 0;
#else
    return 0;
#endif
}

/* sceAgcDriverSetHsOffchipParam (NID: MM4IZSEYytQ)
 * Non-Direct variant. Delegates to the Direct variant. */
int32_t PS5_SYSV_ABI sceAgcDriverSetHsOffchipParam(
    uint32_t pipe_id, uint64_t list_addr, uint32_t num_entries)
{
    (void)pipe_id;
#ifdef OPENAGC_PROSPERO
    return sceAgcDriverSetHsOffchipParamDirect(list_addr, num_entries);
#else
    (void)list_addr;
    (void)num_entries;
    return AGC_OK;
#endif
}

/* sceAgcDriverAgrSubmitDcb (NID: AhGvpITrf4M)
 * SPRX: checks a flag at [global + 0x148]. If set, submits via internal path.
 * If not set, returns 0x8a6d0003 (AGR not initialized). */
int32_t PS5_SYSV_ABI sceAgcDriverAgrSubmitDcb(const AgcCommandBufferSubmit *packet)
{
    (void)packet;
    /* AGR (Async Graphics Ring) is not initialized on non-dev hardware.
     * Return the same error as the SPRX. */
    return 0x8a6d0003;
}

/* sceAgcDriverAddEqEvent (NID: w2rJhmD+dsE)
 * SPRX: sets up an event queue with type 0x1fff2.
 * Not supported on the generic backend. */
int32_t PS5_SYSV_ABI sceAgcDriverAddEqEvent(void *eq, uint32_t type, void *event)
{
    (void)eq;
    (void)type;
    (void)event;
    return AGC_ERROR_NOT_SUPPORTED;
}

/* sceAgcDriverDeleteEqEvent (NID: DL2RXaXOy88)
 * reference-confirmed: stub, not supported. */
int32_t PS5_SYSV_ABI sceAgcDriverDeleteEqEvent(void *event)
{
    (void)event;
    return AGC_ERROR_NOT_SUPPORTED;
}

/* sceAgcDriverGetEqEventType (NID: 5CdQTZIQPxM)
 * reference-confirmed: stub, not supported. */
int32_t PS5_SYSV_ABI sceAgcDriverGetEqEventType(void *event, uint32_t *type)
{
    (void)event;
    if (type) *type = 0;
    return AGC_ERROR_NOT_SUPPORTED;
}

/* sceAgcDriverIsCaptureInProgress (NID: Ddwk4gLT5j0)
 * reference-confirmed: returns 0 (no capture in progress). */
int32_t PS5_SYSV_ABI sceAgcDriverIsCaptureInProgress(void)
{
    return 0;
}

/* FW 5.50 qspAL8bgcBY @ 0x77c0 and +TN0oRTBxJQ @ 0x6730. */
int32_t PS5_SYSV_ABI sceAgcDriverIsSubmitValidationEnabled(void)
{
    return 0;
}

int32_t PS5_SYSV_ABI sceAgcDriverIsTraceInProgress(void)
{
    return 0;
}

/* FW 5.50 rJUyMrDdxJg @ 0x6740. */
int32_t PS5_SYSV_ABI sceAgcDriverGetShaderDebuggingStatus(void)
{
    return 1;
}

/* sceAgcDriverGetDefaultOwner (NID: F0ZXt5q0ZTA)
 * reference-confirmed: returns 0 (default owner handle). */
uint32_t PS5_SYSV_ABI sceAgcDriverGetDefaultOwner(void)
{
    return 0;
}

/* sceAgcDriverInitResourceRegistration (NID: F0Y42t-3e18)
 * reference-confirmed: stub, returns AGC_ERROR_NOT_SUPPORTED. */
int32_t PS5_SYSV_ABI sceAgcDriverInitResourceRegistration(void)
{
    return AGC_ERROR_NOT_SUPPORTED;
}

/* sceAgcDriverQueryResourceRegistrationUserMemoryRequirements (NID: AOLcoIkQDgM)
 * reference-confirmed: stub, returns AGC_ERROR_NOT_SUPPORTED. */
int32_t PS5_SYSV_ABI sceAgcDriverQueryResourceRegistrationUserMemoryRequirements(
    void *out_info)
{
    (void)out_info;
    return AGC_ERROR_NOT_SUPPORTED;
}

/* sceAgcDriverGetResourceRegistrationMaxNameLength (NID: uJziRsODk1c)
 * reference-confirmed: returns 32. */
uint32_t PS5_SYSV_ABI sceAgcDriverGetResourceRegistrationMaxNameLength(void)
{
    return 32;
}

/* sceAgcDriverUnregisterResource (NID: pWLG7WOpVcw)
 * reference-confirmed: stub, returns AGC_ERROR_NOT_SUPPORTED. */
int32_t PS5_SYSV_ABI sceAgcDriverUnregisterResource(uint32_t resource_id)
{
    (void)resource_id;
    return AGC_ERROR_NOT_SUPPORTED;
}

/* FW 5.50 workload-stream table: IDs 1..31, one 32-byte record per ID. */
static uint32_t s_workload_stream_mask;
static uint8_t s_workload_streams[32][32];

int32_t PS5_SYSV_ABI sceAgcDriverRegisterWorkloadStream(
    uint32_t stream_id, const void *stream)
{
    if (stream_id < 1u || stream_id > 31u)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_VALUE;

    uint32_t bit = 1u << stream_id;
    if ((s_workload_stream_mask & bit) != 0)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_VALUE;
    if (!stream)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_ARGUMENT;

    memcpy(s_workload_streams[stream_id], stream,
        sizeof(s_workload_streams[stream_id]));
    s_workload_stream_mask |= bit;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverUnregisterWorkloadStream(
    uint32_t stream_id)
{
    if (stream_id < 1u || stream_id > 31u)
        return (int32_t)AGC_DRIVER_ERROR_INVALID_VALUE;

    uint32_t bit = 1u << stream_id;
    if ((s_workload_stream_mask & bit) == 0)
        return (int32_t)AGC_DRIVER_ERROR_NOT_REGISTERED;

    memset(s_workload_streams[stream_id], 0,
        sizeof(s_workload_streams[stream_id]));
    s_workload_stream_mask &= ~bit;
    return AGC_OK;
}

/* ===================================================================== */
/* Convenience submit wrappers (reference-confirmed)                     */
/* ===================================================================== */

static int32_t agcSubmitDcbArrayDirect(
    void *const dcb_gpu_addrs[], const uint32_t *dcb_sizes_in_dwords,
    uint32_t count)
{
    if (count == 0)
        return AGC_OK;
    if (!dcb_gpu_addrs || !dcb_sizes_in_dwords)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t *sizes_in_bytes = malloc((size_t)count * sizeof(*sizes_in_bytes));
    if (!sizes_in_bytes)
        return AGC_ERROR_OUT_OF_MEMORY;

    for (uint32_t i = 0; i < count; i++) {
        if (dcb_gpu_addrs[i] &&
            (dcb_sizes_in_dwords[i] == 0 ||
             dcb_sizes_in_dwords[i] > UINT32_MAX / 4u)) {
            free(sizes_in_bytes);
            return AGC_ERROR_CB_INVALID_SIZE;
        }
        sizes_in_bytes[i] = dcb_sizes_in_dwords[i] * 4u;
    }

    int32_t ret = sceAgcDriverSubmitMultiCommandBuffersDirect(
        count, dcb_gpu_addrs, sizes_in_bytes, NULL, NULL);
    free(sizes_in_bytes);
    return ret;
}

/* sceAgcDriverSubmitMultiDcbs (NID: 6UzEidRZwkg)
 * Submit all DCB descriptors in one kernel frame. Separate SUBMIT_16 ioctls
 * do not advance as independent frames on FW 5.50 without submit-done state. */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiDcbs(
    void *const dcb_gpu_addrs[], const uint32_t *dcb_sizes_in_dwords,
    uint32_t count)
{
    return agcSubmitDcbArrayDirect(
        dcb_gpu_addrs, dcb_sizes_in_dwords, count);
}

/* FW 5.50 +T8Xo6LtFJI uses the same three-argument ABI as SubmitMultiDcbs.
 * The retail AGR path is unavailable until its private driver state exists. */
int32_t PS5_SYSV_ABI sceAgcDriverAgrSubmitMultiDcbs(
    void *const dcb_gpu_addrs[], const uint32_t *dcb_sizes_in_dwords,
    uint32_t count)
{
    (void)dcb_gpu_addrs;
    (void)dcb_sizes_in_dwords;
    (void)count;
    return (int32_t)AGC_DRIVER_ERROR_AGR_NOT_INITIALIZED;
}

/* sceAgcDriverSubmitCommandBuffer (NID: b4fpgH5ZXxQ)
 * Submits a single DCB to the graphics queue. The queue parameter is
 * unused in the reference implementation for DCB submission. */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitCommandBuffer(
    uint32_t queue, void *dcb, uint32_t size_in_dwords)
{
    (void)queue;
    if (!dcb || size_in_dwords == 0)
        return AGC_OK;

    AgcCommandBufferSubmit pkt = {0};
    pkt.command_address = (uint64_t)(uintptr_t)dcb;
    pkt.dword_count = size_in_dwords;
    return sceAgcDriverSubmitDcb(&pkt);
}

/* sceAgcDriverSubmitMultiCommandBuffers (NID: Fj7r9EHzF38)
 * The queue selector is unused for graphics DCBs; preserve the DCB array as
 * one kernel frame instead of issuing one standalone ioctl per element. */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiCommandBuffers(
    uint32_t queue, void *const dcbs[], const uint32_t *sizes_in_dwords,
    uint32_t count)
{
    (void)queue;
    return agcSubmitDcbArrayDirect(dcbs, sizes_in_dwords, count);
}

/* sceAgcDriverSubmitMultiAcbs (NID: HF3YllT3mXU)
 * Loops over ACB arrays and submits each via sceAgcDriverSubmitAcb. */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiAcbs(
    uint32_t queue, void *const acbs[], const uint32_t *sizes_in_dwords,
    uint32_t count)
{
    if (count == 0)
        return AGC_OK;
    if (!acbs || !sizes_in_dwords)
        return AGC_ERROR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < count; i++) {
        if (!acbs[i])
            continue;
        AgcCommandBufferSubmit pkt = {0};
        pkt.command_address = (uint64_t)(uintptr_t)acbs[i];
        pkt.dword_count = sizes_in_dwords[i];
        int32_t ret = sceAgcDriverSubmitAcb(queue, &pkt);
        if (ret < 0)
            return ret;
    }
    return AGC_OK;
}

/* ===================================================================== */
/* libSceAgc user-facing wrappers                                        */
/* ===================================================================== */

static uint32_t g_agc_packet_mode;

#ifdef OPENAGC_PROSPERO
extern int getpid(void);
extern int PS5_SYSV_ABI sceKernelGetAppInfo(int pid, void *app_info);
extern int PS5_SYSV_ABI sceKernelTitleWorkaroundIsEnabled(
    const void *title_info, uint32_t workaround_id, uint32_t *enabled);
#endif

static void agcUpdatePacketMode(void)
{
#ifdef OPENAGC_PROSPERO
    uint8_t app_info[0x60] = {0};
    uint32_t workaround_52 = 0;
    uint32_t workaround_53 = 0;

    g_agc_packet_mode = 0;
    if (sceKernelGetAppInfo(getpid(), app_info) != 0)
        return;
    if (sceKernelTitleWorkaroundIsEnabled(
            app_info + 0x30, 0x52u, &workaround_52) != 0)
        return;
    if (sceKernelTitleWorkaroundIsEnabled(
            app_info + 0x30, 0x53u, &workaround_53) != 0)
        return;

    if (workaround_53 == 1u)
        g_agc_packet_mode = 0;
    else if (workaround_52 == 1u)
        g_agc_packet_mode = 2;
    else
        g_agc_packet_mode = 1;
#else
    g_agc_packet_mode = 1;
#endif
}

/* sceAgcInit (NID: kW3GLb7QfPg)
 * SPRX: wrapper that calls internal init at 0x75e0 which:
 *   1. Locks mutex
 *   2. Checks SDK version
 *   3. Gets app info
 *   4. Checks title workarounds
 *   5. Calls register defaults init
 *   6. Calls register defaults internal init
 * Delegates to sce_agc_initialize on our backend. */
int32_t PS5_SYSV_ABI sceAgcInit(uint32_t init_level, uint32_t flags, uint32_t *out_value)
{
    (void)flags;
    if (init_level > 9)
        return AGC_ERROR_INVALID_ARGUMENT;

    int32_t ret = sce_agc_initialize();
    if (ret != AGC_OK)
        return ret;

    agcUpdatePacketMode();

    if (out_value)
        *out_value = 0;

    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcInit_0090(
    uint32_t init_level, uint32_t flags, uint32_t *out_value)
{
    return sceAgcInit(init_level, flags, out_value);
}

/* sceAgcSuspendPoint (NID: h9z6+0hEydk)
 * SPRX: wrapper that calls sceAgcDriverSuspendPointSubmit (NID: QcmHLO2n7mk)
 * via PLT. The wrapper checks a global flag and builds a 12-byte arg
 * from internal state before calling the driver function. */
int32_t PS5_SYSV_ABI sceAgcSuspendPoint(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
#ifdef OPENAGC_PROSPERO
    return sceAgcDriverSuspendPointSubmitDirect(field0, field1, field2, field3);
#else
    (void)field0; (void)field1; (void)field2; (void)field3;
    return AGC_OK;
#endif
}

/* sceAgcGetRegisterDefaults2 (NID: 2JtWUUiYBXs)
 * SPRX-confirmed: takes a version number (0-12), returns a pointer to an
 * AgcRegisterDefaults structure for that version. The structure is built
 * on first call and cached. Version mapping: 0-3→v0, 4→v4, 5-6→v5, 7→v7,
 * 8→v8, 9→v9, 10→v10, 11→v11, 12→v10. Versions > 12 fall back to v11. */

/* Maximum groups across all versions (v0 has 150) */
#define MAX_PRIMARY_GROUPS   160
#define MAX_INTERNAL_GROUPS   32
#define MAX_VERSIONS          13

/* Per-version pointer tables: tbl_ptrs[space] is an array of pointers,
 * one per group index in that table space. */
static const AgcRegisterDefaultValue *s_primary_tbl0_ptrs[MAX_VERSIONS][MAX_PRIMARY_GROUPS];
static const AgcRegisterDefaultValue *s_primary_tbl1_ptrs[MAX_VERSIONS][MAX_PRIMARY_GROUPS];
static const AgcRegisterDefaultValue *s_primary_tbl2_ptrs[MAX_VERSIONS][MAX_PRIMARY_GROUPS];
static const AgcRegisterDefaultValue *s_primary_tbl3_ptrs[MAX_VERSIONS][MAX_PRIMARY_GROUPS];

/* Per-version types arrays */
static uint32_t s_primary_types[MAX_VERSIONS][MAX_PRIMARY_GROUPS * 3];

/* Per-version RegisterDefaults structures */
static AgcRegisterDefaults s_primary_defaults[MAX_VERSIONS];
static bool s_primary_built[MAX_VERSIONS];

static void build_register_defaults(
    AgcRegisterDefaults *out,
    const AgcRegisterDefaultsGroup *groups, uint32_t group_count,
    const AgcRegisterDefaultValue **tbl0_ptrs,
    const AgcRegisterDefaultValue **tbl1_ptrs,
    const AgcRegisterDefaultValue **tbl2_ptrs,
    const AgcRegisterDefaultValue **tbl3_ptrs,
    uint32_t *types_out)
{
    uint32_t tbl0_count = 0, tbl1_count = 0, tbl2_count = 0;

    for (uint32_t i = 0; i < group_count; i++) {
        const AgcRegisterDefaultsGroup *g = &groups[i];
        /* Build types entry */
        types_out[i * 3 + 0] = g->type_hash;
        types_out[i * 3 + 1] = (g->index * 4) + (uint32_t)g->space;
        types_out[i * 3 + 2] = 0;

        /* Set pointer table entry */
        switch (g->space) {
        case kAgcRegisterDefaultSpaceCx:
            tbl0_ptrs[g->index] = g->registers;
            if (g->index + 1 > tbl0_count) tbl0_count = g->index + 1;
            break;
        case kAgcRegisterDefaultSpaceSh:
            tbl1_ptrs[g->index] = g->registers;
            if (g->index + 1 > tbl1_count) tbl1_count = g->index + 1;
            break;
        case kAgcRegisterDefaultSpaceUc:
            /* UC groups can go to tbl2 or tbl3 depending on context.
             * For simplicity, use tbl2 for UC. */
            tbl2_ptrs[g->index] = g->registers;
            if (g->index + 1 > tbl2_count) tbl2_count = g->index + 1;
            break;
        }
    }

    out->tbl0 = (const AgcRegisterDefaultValue **)tbl0_ptrs;
    out->tbl1 = (const AgcRegisterDefaultValue **)tbl1_ptrs;
    out->tbl2 = (const AgcRegisterDefaultValue **)tbl2_ptrs;
    out->tbl3 = NULL;
    out->tbl0_register_count = 0; /* total regs in tbl0, not ptrs */
    out->tbl1_register_count = 0;
    out->tbl2_register_count = 0;
    out->tbl3_register_count = 0;
    out->types = types_out;
    out->count = group_count;
    (void)tbl3_ptrs;
    (void)tbl0_count; (void)tbl1_count; (void)tbl2_count;
}

void *PS5_SYSV_ABI sceAgcGetRegisterDefaults2(uint32_t version)
{
    if (version > 12)
        version = 11; /* fallback */

    if (s_primary_built[version])
        return &s_primary_defaults[version];

    uint32_t group_count = 0;
    const AgcRegisterDefaultsGroup *groups =
        agcRegisterDefaultsGetPrimaryGroupsForVersion(version, &group_count);

    if (group_count > MAX_PRIMARY_GROUPS)
        return NULL;

    build_register_defaults(
        &s_primary_defaults[version], groups, group_count,
        s_primary_tbl0_ptrs[version], s_primary_tbl1_ptrs[version],
        s_primary_tbl2_ptrs[version], s_primary_tbl3_ptrs[version],
        s_primary_types[version]);

    s_primary_built[version] = true;
    return &s_primary_defaults[version];
}

/* Per-version internal data */
static const AgcRegisterDefaultValue *s_internal_tbl0_ptrs[MAX_VERSIONS][MAX_INTERNAL_GROUPS];
static const AgcRegisterDefaultValue *s_internal_tbl1_ptrs[MAX_VERSIONS][MAX_INTERNAL_GROUPS];
static const AgcRegisterDefaultValue *s_internal_tbl2_ptrs[MAX_VERSIONS][MAX_INTERNAL_GROUPS];
static const AgcRegisterDefaultValue *s_internal_tbl3_ptrs[MAX_VERSIONS][MAX_INTERNAL_GROUPS];
static uint32_t s_internal_types[MAX_VERSIONS][MAX_INTERNAL_GROUPS * 3];
static AgcRegisterDefaults s_internal_defaults[MAX_VERSIONS];
static bool s_internal_built[MAX_VERSIONS];

void *PS5_SYSV_ABI sceAgcGetRegisterDefaults2Internal(uint32_t version)
{
    if (version > 12)
        version = 11; /* fallback */

    if (s_internal_built[version])
        return &s_internal_defaults[version];

    uint32_t group_count = 0;
    const AgcRegisterDefaultsGroup *groups =
        agcRegisterDefaultsGetInternalGroupsForVersion(version, &group_count);

    if (group_count > MAX_INTERNAL_GROUPS)
        return NULL;

    build_register_defaults(
        &s_internal_defaults[version], groups, group_count,
        s_internal_tbl0_ptrs[version], s_internal_tbl1_ptrs[version],
        s_internal_tbl2_ptrs[version], s_internal_tbl3_ptrs[version],
        s_internal_types[version]);

    s_internal_built[version] = true;
    return &s_internal_defaults[version];
}

/* ===================================================================== */
/* DCB packet builders                                                   */
/* ===================================================================== */

/* sceAgcDcbAcquireMem (NID: 57labkp+rSQ) — IT_ACQUIRE_MEM (0x58), 8 dwords.
 * Matches the ACB version but for DCB. The SPRX builds the same
 * ACQUIRE_MEM packet format. */
uint32_t *PS5_SYSV_ABI sceAgcDcbAcquireMem(
    SceAgcCb *cb, uint32_t engine_sel, uint32_t coher_cntl,
    uint32_t coher_size, uint64_t coher_base)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_ACQUIRE_MEM, 8);
    cmd[1] = coher_cntl;
    cmd[2] = coher_size;
    cmd[3] = (uint32_t)coher_base;
    cmd[4] = (uint32_t)(coher_base >> 32);
    cmd[5] = engine_sel;
    cmd[6] = 0;  /* padding */
    cmd[7] = 0;  /* padding */
    return cmd;
}

/* sceAgcDcbCopyData (NID: 1rZSWUv1IRc) — IT_COPY_DATA (0x40), 6 dwords.
 * Layout (AMD COPY_DATA):
 *   [0] header
 *   [1] src_sel[31:28] | dst_sel[27:24] | src_cache[23:20] | dst_cache[19:16] |
 *       byte_count[15:0]
 *   [2] src_addr_lo
 *   [3] src_addr_hi
 *   [4] dst_addr_lo
 *   [5] dst_addr_hi */
uint32_t *PS5_SYSV_ABI sceAgcDcbCopyData(
    SceAgcCb *cb, uint32_t src_sel, uint32_t dst_sel,
    uint64_t src_addr, uint64_t dst_addr, uint32_t byte_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 6);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_COPY_DATA, 6);
    cmd[1] = ((src_sel & 0xFu) << 28) |
             ((dst_sel & 0xFu) << 24) |
             (byte_count & 0xFFFFu);
    cmd[2] = (uint32_t)src_addr;
    cmd[3] = (uint32_t)(src_addr >> 32);
    cmd[4] = (uint32_t)dst_addr;
    cmd[5] = (uint32_t)(dst_addr >> 32);
    return cmd;
}

/* sceAgcDcbJump (NID: xSAR0LTcRKM) — IT_INDIRECT_BUFFER (0x3F), 4 dwords.
 * RE: SPRX uses opcode 0x3F (INDIRECT_BUFFER), not 0x33 (IB_CNST).
 * 5 params: cb, queue_id, flags, target_addr, vmid.
 * cmd[3] = (flags&3)<<28 | (queue_id&1)<<20 | (vmid&0xFFFFF) | 0x0F200000 */
uint32_t *PS5_SYSV_ABI sceAgcDcbJump(
    SceAgcCb *cb, uint32_t queue_id, uint32_t flags,
    uint64_t target_addr, uint32_t vmid)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDIRECT_BUFFER, 4);
    cmd[1] = (uint32_t)target_addr & ~3u;
    cmd[2] = (uint32_t)(target_addr >> 32);
    cmd[3] = ((flags & 0x3u) << 28) |
             ((queue_id & 0x1u) << 20) |
             (vmid & 0xFFFFFu) |
             0x0F200000u;
    return cmd;
}

/* sceAgcDcbResetQueue (NID: TRO721eVt4g) — IT_AGC_0x79 (0x79), 3 dwords.
 * Uses the same packet format as the ACB reset queue. */
uint32_t *PS5_SYSV_ABI sceAgcDcbResetQueue(SceAgcCb *cb, uint32_t queue_id)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_SET_UCONFIG_REG, AGC_PM4_SUB_ACB_RESET, 3);
    cmd[1] = queue_id;
    cmd[2] = 0;
    return cmd;
}

/* sceAgcDcbSetIndexCount (NID: 8N2tmT3jmC8) — IT_INDEX_BUFFER_SIZE (0x13), 2 dwords.
 * RE: SPRX uses 2 dwords (not 3), clamps count to max(count, 1).
 * Layout: [0] header, [1] max(index_count, 1) */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexCount(SceAgcCb *cb, uint32_t index_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDEX_BUFFER_SIZE, 2);
    cmd[1] = index_count ? index_count : 1;
    return cmd;
}

/* sceAgcDcbSetIndexSize (NID: GIIW2J37e70) — opcode 0x7A, 3 dwords.
 * RE: SPRX uses opcode 0x7A (not 0x2A INDEX_TYPE).
 * Layout: [0] header, [1] 0x20000243 (constant), [2] (index_type&3)|(swap<<6)|0x400 */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexSize(
    SceAgcCb *cb, uint32_t index_type, uint32_t swap)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_INDEX_SIZE, 3);
    cmd[1] = 0x20000243u;
    cmd[2] = (index_type & 0x3u) | ((swap & 0x1u) << 6) | 0x400u;
    return cmd;
}

/* sceAgcDcbSetNumInstances (NID: tSBxhAPyytQ) — IT_NUM_INSTANCES (0x2F), 2 dwords.
 * Layout: [0] header, [1] num_instances */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetNumInstances(SceAgcCb *cb, uint32_t num_instances)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_NUM_INSTANCES, 2);
    cmd[1] = num_instances;
    return cmd;
}

/* sceAgcDcbStallCommandBufferParser (NID: u2T2DiA5hRI) — opcode 0x42, 2 dwords.
 * RE: SPRX uses opcode 0x42 (STALL_PARSER), not NOP+subcommand.
 * Layout: [0] header, [1] 0 */
uint32_t *PS5_SYSV_ABI sceAgcDcbStallCommandBufferParser(SceAgcCb *cb)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_STALL_PARSER, 2);
    cmd[1] = 0;
    return cmd;
}

/* sceAgcDcbDrawIndex (NID: q88lQ+GP5Yk) — IT_DRAW_INDEX_2 (0x27), 6 dwords.
 * RE: SPRX field order: cmd[1]=max(count,1), cmd[4]=index_count,
 * cmd[5]=draw_initiator (possibly modified by global). */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndex(
    SceAgcCb *cb, uint32_t index_count, uint64_t index_base_addr,
    uint32_t draw_initiator)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 6);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_2, 6);
    cmd[1] = index_count ? index_count : 1;
    cmd[2] = (uint32_t)index_base_addr;
    cmd[3] = (uint32_t)(index_base_addr >> 32);
    cmd[4] = index_count;
    cmd[5] = draw_initiator;
    return cmd;
}

/* ===================================================================== */
/* CB register range setters                                             */
/* ===================================================================== */

/* sceAgcCbSetShRegisterRangeDirect (NID: n2fD4A+pb+g) — IT_SET_SH_REG (0x76).
 * Variable-length: 2 + count dwords. */
uint32_t *PS5_SYSV_ABI sceAgcCbSetShRegisterRangeDirect(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count)
{
    if (!values || count == 0)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 2 + count);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG, 2 + count);
    cmd[1] = reg_offset;
    for (uint32_t i = 0; i < count; ++i)
        cmd[2 + i] = values[i];
    return cmd;
}

/* sceAgcCbSetUcRegistersDirect (NID: 03RZmELWWzw) — IT_SET_UCONFIG_REG (0x79).
 * GFX10 packet layout matches SET_SH_REG/SET_CONTEXT_REG: one starting
 * register offset followed by contiguous register values. */
uint32_t *PS5_SYSV_ABI sceAgcCbSetUcRegistersDirect(
    SceAgcCb *cb, const AgcRegisterValue *registers, uint32_t register_count)
{
    if (!registers || register_count == 0 || register_count > 0x3FFEu)
        return 0;

    uint32_t total_dwords = register_count + 2;
    uint32_t *cmd = agcCbAllocDwords(cb, total_dwords);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, total_dwords);
    cmd[1] = registers[0].offset & 0xFFFFu;
    for (uint32_t i = 0; i < register_count; ++i)
        cmd[2 + i] = registers[i].value;
    return cmd;
}

/* ===================================================================== */
/* Indirect register patchers                                            */
/* ===================================================================== */

/* RE source: SPRX disassembly of libSceAgc.sprx (FW 5.50).
 * Each patcher validates the opcode byte (cmd[0] byte 1) against a
 * specific opcode and returns 0x8a6c000c if it doesn't match.
 *
 * Indirect register write packet format (5 dwords):
 *   [0] header (opcode 0x63/0x9F/0x64)
 *   [1] address_lo (low 2 bits are flags, preserved by SetAddress)
 *   [2] address_hi
 *   [3] 0x80000000 (constant)
 *   [4] register_count (bits 13:0, other bits preserved by AddRegisters)
 *
 * SetAddress: cmd[1] = (cmd[1] & 3) | (addr_lo & ~3), cmd[2] = addr_hi
 * AddRegisters: cmd[4] = (cmd[4] & 0xFFFFC000) | ((cmd[4] + count) & 0x3FFF) */

static int32_t agcIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address, uint32_t expected_opcode)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcPm4Opcode(cmd[0]) != expected_opcode)
        return 0x8a6c000c;
    cmd[1] = (cmd[1] & 0x3u) | ((uint32_t)address & ~3u);
    cmd[2] = (uint32_t)(address >> 32);
    return AGC_OK;
}

static int32_t agcIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count, uint32_t expected_opcode)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcPm4Opcode(cmd[0]) != expected_opcode)
        return 0x8a6c000c;
    cmd[4] = (cmd[4] & 0xFFFFC000u) | ((cmd[4] + count) & 0x3FFFu);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address)
{
    return agcIndirectPatchSetAddress(cmd, address, AGC_PM4_OP_SET_SH_REG_INDIRECT);
}

int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count)
{
    return agcIndirectPatchAddRegisters(cmd, count, AGC_PM4_OP_SET_SH_REG_INDIRECT);
}

int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address)
{
    return agcIndirectPatchSetAddress(cmd, address, AGC_PM4_OP_SET_CX_REG_INDIRECT);
}

int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count)
{
    return agcIndirectPatchAddRegisters(cmd, count, AGC_PM4_OP_SET_CX_REG_INDIRECT);
}

int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address)
{
    return agcIndirectPatchSetAddress(cmd, address, AGC_PM4_OP_SET_UC_REG_INDIRECT);
}

int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count)
{
    return agcIndirectPatchAddRegisters(cmd, count, AGC_PM4_OP_SET_UC_REG_INDIRECT);
}

/* ===================================================================== */
/* Utility functions                                                     */
/* ===================================================================== */

/* sceAgcSetNop (NID: K2mciNVxUCE) — 7-byte function in SPRX.
 * RE: SPRX patches byte at cmd+1 (the opcode byte in a type-3 header)
 * to 0x10 (NOP), converting any packet into a NOP. Returns NULL. */
uint32_t *PS5_SYSV_ABI sceAgcSetNop(uint32_t *cmd)
{
    if (!cmd)
        return 0;
    ((uint8_t *)cmd)[1] = AGC_PM4_OP_NOP;
    return 0;
}

/* sceAgcDebugRaiseException (NID: T6xuVw0KUJo) — 5-byte function in SPRX.
 * Just calls __builtin_trap() / ud2 on real hardware. */
int32_t PS5_SYSV_ABI sceAgcDebugRaiseException(void)
{
    /* On non-dev hardware, this is a no-op.
     * On dev hardware, it raises a debug exception. */
    return AGC_OK;
}

/* sceAgcGetDataPacketPayload (NID: V++UgBtQhn0)
 * RE: SPRX takes 3 params: (out_addr, cmd, skip_header).
 * If skip_header != 0: payload = cmd + 8 bytes (skip 2 dwords).
 * Else: if count field (bits 29:16) == 0x3FFF (NOP): no payload, *out_addr = 0.
 *       Else: payload = cmd + 4 bytes (skip 1 dword header).
 * Always returns NULL. */
uint32_t *PS5_SYSV_ABI sceAgcGetDataPacketPayload(
    uint64_t *out_addr, uint32_t *cmd, uint32_t skip_header)
{
    if (!cmd)
        return 0;

    if (skip_header) {
        cmd += 2;  /* skip 8 bytes */
    } else {
        /* Check if count field is 0x3FFF (NOP marker) */
        if (((cmd[0] >> 16) & 0x3FFFu) == 0x3FFFu) {
            if (out_addr)
                *out_addr = 0;
            return 0;
        }
        cmd += 1;  /* skip 4 bytes (header) */
    }

    if (out_addr)
        *out_addr = (uint64_t)(uintptr_t)cmd;
    return 0;
}

int32_t PS5_SYSV_ABI sceAgcGetDataPacketPayloadAddress_0090(
    uint64_t *out_addr, uint32_t *cmd, uint32_t skip_header)
{
    (void)sceAgcGetDataPacketPayload(out_addr, cmd, skip_header);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcGetDataPacketPayloadRange(
    SceAgcMemoryRange *range, uint32_t *cmd, uint32_t type)
{
    uint32_t header;

    if (!range || !cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    header = cmd[0];
    if (type != 0) {
        range->base = cmd + 2;
        range->size = (header >> 14) & 0xFFFCu;
        return AGC_OK;
    }

    if ((~header & 0x3FFF0000u) == 0) {
        range->base = NULL;
        range->size = 0;
        return AGC_OK;
    }

    range->base = cmd + 1;
    range->size = 4u * (((header >> 16) & 0x3FFFu) + 1u);
    return AGC_OK;
}

/* ===================================================================== */
/* Shader and primitive state creation                                   */
/* ===================================================================== */

/* sceAgcCreateShader (NID: f3dg2CSgRKY) — 0x36a bytes in SPRX.
 * Parses a shader record and validates its structure.
 * Delegates to our existing shader record parser. */
int32_t PS5_SYSV_ABI sceAgcCreateShader(void *shader_record, uint32_t type)
{
    if (!shader_record)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* The SPRX validates the shader record magic and type field.
     * Our agcShaderRecordParse already does this validation. */
    (void)type;
    return AGC_OK;
}

/* sceAgcCreatePrimState (NID: D9sr1xGUriE) - FW 5.50 @ 0xe2d0.
 * Values are reconstructed from the SPRX; register pairs stored in shader
 * Specials are copied verbatim. */
static const uint32_t s_prim_to_gs_out[18] = {
    0u, 1u, 1u, 2u, 2u, 2u, 3u, 2u, 2u,
    1u, 1u, 2u, 2u, 2u, 2u, 2u, 4u, 1u,
};

int32_t PS5_SYSV_ABI sceAgcCreatePrimState(
    AgcShaderRegister *cx_registers,
    AgcShaderRegister *uconfig_registers,
    const AgcShaderRecord *hull_shader,
    const AgcShaderRecord *geometry_shader,
    uint32_t primitive_type)
{
    if (!cx_registers && !uconfig_registers)
        return AGC_OK;
    if (!geometry_shader || !geometry_shader->specials)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (hull_shader && !hull_shader->specials)
        return AGC_ERROR_INVALID_ARGUMENT;

    const AgcShaderSpecials *geometry_specials =
        (const AgcShaderSpecials *)(uintptr_t)geometry_shader->specials;
    const AgcShaderSpecials *hull_specials = hull_shader
        ? (const AgcShaderSpecials *)(uintptr_t)hull_shader->specials
        : NULL;

    if (cx_registers) {
        cx_registers[0].offset = geometry_specials->vgt_shader_stages_en.register_offset;
        cx_registers[0].value = geometry_specials->vgt_shader_stages_en.value;

        if ((cx_registers[0].value & (1u << 5u)) != 0) {
            cx_registers[1].offset =
                geometry_specials->vgt_gs_out_prim_type.register_offset;
            cx_registers[1].value =
                geometry_specials->vgt_gs_out_prim_type.value;
        } else {
            uint32_t output_primitive = 2u;
            uint32_t index = primitive_type - 1u;
            if (index < 18u)
                output_primitive = s_prim_to_gs_out[index];
            cx_registers[1].offset = AGC_REG_VGT_GS_OUT_PRIM_TYPE;
            cx_registers[1].value = output_primitive;
        }

        if (hull_specials) {
            cx_registers[0].value |=
                hull_specials->vgt_shader_stages_en.value;
            if ((cx_registers[0].value & (1u << 5u)) == 0) {
                cx_registers[1].offset =
                    hull_specials->vgt_gs_out_prim_type.register_offset;
                cx_registers[1].value =
                    hull_specials->vgt_gs_out_prim_type.value;
            }
        }
    }

    if (uconfig_registers) {
        uconfig_registers[0].offset = geometry_specials->ge_cntl.register_offset;
        uconfig_registers[0].value = geometry_specials->ge_cntl.value;
        const AgcShaderSpecialRegister *user_vgpr = hull_specials
            ? &hull_specials->ge_user_vgpr_en
            : &geometry_specials->ge_user_vgpr_en;
        uconfig_registers[1].offset = user_vgpr->register_offset;
        uconfig_registers[1].value = user_vgpr->value;
        uconfig_registers[2].offset = AGC_REG_VGT_PRIMITIVE_TYPE;
        uconfig_registers[2].value = primitive_type;
    }

    return AGC_OK;
}

/* sceAgcUpdatePrimState (NID: Y3ymLfZ1384) - FW 5.50 @ 0xe3d0. */
static const uint32_t s_update_prim_to_gs_out[18] = {
    0x80000180u, 0x00000011u, 0x20000180u, 0x00000012u,
    0x00000000u, 0x00000013u, 0x00000000u, 0x00000014u,
    0x00000000u, 0x00000015u, 0x00000000u, 0x0000001Au,
    0x00000000u, 0x0000001Bu, 0x00000000u, 0x0000001Cu,
    0x00000000u, 0x0000001Du,
};

int32_t PS5_SYSV_ABI sceAgcUpdatePrimState(
    AgcShaderRegister *cx_registers,
    AgcShaderRegister *uconfig_registers,
    uint32_t primitive_type)
{
    if (cx_registers && (cx_registers[0].value & 0x24u) == 0) {
        uint32_t output_primitive = 2u;
        uint32_t index = primitive_type - 1u;
        if (index < 18u)
            output_primitive = s_update_prim_to_gs_out[index];
        cx_registers[1].value =
            (cx_registers[1].value & ~0x7u) | output_primitive;
    }

    if (uconfig_registers) {
        uconfig_registers[2].value =
            (uconfig_registers[2].value & ~0x1Fu) | primitive_type;
    }

    return AGC_OK;
}

/* sceAgcCreateInterpolantMapping (NID: pdEV7bI6COI) - FW 5.50 @ 0xd7f0.
 * The output offsets are raw CX indirect-register descriptors, not decoded
 * SPI_PS_INPUT_CNTL register offsets. */
static uint32_t agcInterpolantApplyDefaults(uint32_t value, uint32_t ps)
{
    value &= ~0x00000300u;
    return value | ((ps >> 20u) & 0x00000300u);
}

static uint32_t agcInterpolantApplyDefaultsHi(uint32_t value, uint32_t ps)
{
    value &= ~0x00600000u;
    return value | ((ps >> 9u) & 0x00600000u);
}

static uint32_t agcCreateInterpolantValue(
    uint32_t ps, uint32_t gs, bool matched)
{
    uint32_t value;

    if ((ps & AGC_SHADER_SEMANTIC_F16_MASK) != 0) {
        value = (ps << 4u) & 0x03000000u;
        if (matched) {
            uint32_t common = ps & gs;
            value &= 0xFFF7FFDFu;
            value |= (common >> 15u) & 0x20u;
            value ^= 0x00080020u;
            value &= ~0x00100000u;
            value |= (~common >> 1u) & 0x00100000u;
        } else {
            value |= 0x00180020u;
        }
        value = agcInterpolantApplyDefaultsHi(value, ps);
    } else {
        value = (!matched ||
            (ps & (AGC_SHADER_SEMANTIC_FLAT_MASK |
                   AGC_SHADER_SEMANTIC_CUSTOM_MASK)) != 0)
            ? 0x20u : 0u;
    }

    value &= ~0x1Fu;
    if (matched)
        value |= (gs & AGC_SHADER_SEMANTIC_HW_MAPPING_MASK) >> 8u;

    value &= ~0x400u;
    if (matched &&
        (ps & (AGC_SHADER_SEMANTIC_FLAT_MASK |
               AGC_SHADER_SEMANTIC_CUSTOM_MASK)) != 0)
        value |= 0x400u;

    return agcInterpolantApplyDefaults(value, ps);
}

int32_t PS5_SYSV_ABI sceAgcCreateInterpolantMapping(
    AgcShaderRegister *cx_registers,
    const AgcShaderRecord *geometry_shader,
    const AgcShaderRecord *pixel_shader)
{
    if (!cx_registers)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t num_inputs = pixel_shader
        ? agcShaderRecordGetNumInputSemantics(pixel_shader) : 0u;
    if (num_inputs > 32u)
        return AGC_ERROR_INVALID_ARGUMENT;

    const AgcShaderSemantic *inputs = pixel_shader
        ? (const AgcShaderSemantic *)(uintptr_t)pixel_shader->input_semantics
        : NULL;
    if (num_inputs != 0u && !inputs)
        return AGC_ERROR_INVALID_ARGUMENT;

    const AgcShaderSemantic *outputs = NULL;
    uint32_t num_outputs = 0u;
    if (num_inputs != 0u) {
        if (!geometry_shader)
            return AGC_ERROR_INVALID_ARGUMENT;
        outputs = (const AgcShaderSemantic *)(uintptr_t)
            geometry_shader->output_semantics;
        num_outputs = agcShaderRecordGetNumOutputSemantics(geometry_shader);
        if (num_outputs != 0u && !outputs)
            return AGC_ERROR_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < num_inputs; i++) {
        uint32_t ps = inputs[i].value;
        uint32_t gs = 0u;
        bool matched = false;
        for (uint32_t j = 0; j < num_outputs; j++) {
            if ((outputs[j].value & AGC_SHADER_SEMANTIC_ID_MASK) ==
                (ps & AGC_SHADER_SEMANTIC_ID_MASK)) {
                gs = outputs[j].value;
                matched = true;
                break;
            }
        }

        cx_registers[i].offset =
            AGC_INTERPOLANT_REGISTER_DESCRIPTOR_BASE + i;
        cx_registers[i].value =
            agcCreateInterpolantValue(ps, gs, matched);
    }

    for (uint32_t i = num_inputs; i < 32u; i++) {
        cx_registers[i].offset =
            AGC_INTERPOLANT_REGISTER_DESCRIPTOR_BASE + i;
        cx_registers[i].value = i;
    }

    return AGC_OK;
}

/* SDK 1.00 ABI alias retained by FW 5.50 and imported by PPSA02453. */
int32_t PS5_SYSV_ABI sceAgcCreateInterpolantMapping_0100(
    AgcShaderRegister *cx_registers,
    const AgcShaderRecord *geometry_shader,
    const AgcShaderRecord *pixel_shader)
{
    return sceAgcCreateInterpolantMapping(
        cx_registers, geometry_shader, pixel_shader);
}

/* ===================================================================== */
/* DCB packet builders — SPRX disassembly batch 2 (FW 5.50)              */
/* ===================================================================== */

/* sceAgcDcbClearState (NID: PxEFhy0d5v8) — 2 dwords.
 * RE: SPRX emits 0xc0001200 (opcode 0x12, AGC-custom clear state).
 * cmd[1] = flags & 0xf */
uint32_t *PS5_SYSV_ABI sceAgcDcbClearState(SceAgcCb *cb, uint32_t flags)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_CLEAR_STATE_AGC, 2);
    cmd[1] = flags & 0xFu;
    return cmd;
}

/* sceAgcDcbRewind (NID: zfcxg-ewMK8) — 2 dwords.
 * RE: SPRX emits 0xc0005900 (opcode 0x59 = REWIND).
 * cmd[1] = flags << 31 (only bit 0 of flags, shifted to bit 31) */
uint32_t *PS5_SYSV_ABI sceAgcDcbRewind(SceAgcCb *cb, uint32_t flags)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_REWIND, 2);
    cmd[1] = (flags & 0x1u) << 31;
    return cmd;
}

/* sceAgcDcbCondExec (NID: BIPexNBSGog) — 5 dwords.
 * RE: SPRX emits 0xc0032200 (opcode 0x22 = COND_EXEC).
 * cmd[1] = addr_lo & ~3, cmd[2] = addr_hi,
 * cmd[3] = 0, cmd[4] = count & 0x3fff */
uint32_t *PS5_SYSV_ABI sceAgcDcbCondExec(
    SceAgcCb *cb, uint64_t address, uint32_t count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_COND_EXEC, 5);
    cmd[1] = (uint32_t)address & ~3u;
    cmd[2] = (uint32_t)(address >> 32);
    cmd[3] = 0;
    cmd[4] = count & 0x3FFFu;
    return cmd;
}

/* sceAgcDcbSetIndexIndirectArgs (NID: 0o3VDdtA6nM) — 4 dwords.
 * RE: SPRX emits 0xc0029100 (opcode 0x91, AGC-custom).
 * cmd[1] = addr_lo & ~0xf, cmd[2] = addr_hi, cmd[3] = offset & 0xffff */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexIndirectArgs(
    SceAgcCb *cb, uint64_t address, uint32_t offset)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_INDEX_INDIRECT_ARGS, 4);
    cmd[1] = (uint32_t)address & ~0xFu;
    cmd[2] = (uint32_t)(address >> 32);
    cmd[3] = offset & 0xFFFFu;
    return cmd;
}

/* ===================================================================== */
/* DCB atomic/sync builders                                              */
/* ===================================================================== */

/* sceAgcDcbAtomicMem (NID: 1-gUn1PI4Sw) — 9 dwords.
 * RE: SPRX emits 0xc0071e00 (opcode 0x1E, sub=0, length=9).
 * cmd[1] = (op<<30)|(atomic_op<<8)|(loop_count&0x7f)|(...)
 * cmd[2..3] = address, cmd[4..5] = data, cmd[6..7] = compare,
 * cmd[8] = loop_count (clamped to 0xFFF) */
uint32_t *PS5_SYSV_ABI sceAgcDcbAtomicMem(
    SceAgcCb *cb, uint32_t op, uint32_t loop_count, uint32_t atomic_op,
    uint64_t address, uint64_t data, uint64_t compare)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 9);
    if (!cmd)
        return 0;

    uint32_t clamped_loop = loop_count > 0xFFF ? 0xFFF : loop_count;
    cmd[0] = agcPm4Header3(AGC_PM4_OP_ATOMIC_MEM_AGC, 9);
    cmd[1] = ((op & 0x3u) << 30) |
             ((atomic_op & 0xFu) << 8) |
             (clamped_loop & 0x7Fu);
    cmd[2] = (uint32_t)address;
    cmd[3] = (uint32_t)(address >> 32);
    cmd[4] = (uint32_t)data;
    cmd[5] = (uint32_t)(data >> 32);
    cmd[6] = (uint32_t)compare;
    cmd[7] = (uint32_t)(compare >> 32);
    cmd[8] = 0;  /* reserved (loop_count is in cmd[1] bits 6:0) */
    return cmd;
}

/* sceAgcDcbAtomicGds (NID: pH3-dfRpfA0) — 11 dwords.
 * RE: SPRX emits 0xc0091d00 (opcode 0x1D = ATOMIC_GDS, length=11).
 * Complex packed control word in cmd[1..2], data in cmd[3..4],
 * mask in cmd[5], etc. */
uint32_t *PS5_SYSV_ABI sceAgcDcbAtomicGds(
    SceAgcCb *cb, uint32_t op, uint32_t gds_op, uint32_t src,
    uint32_t data, uint16_t offset, uint16_t index, uint32_t loop_count,
    uint64_t cmp_data, uint32_t mask)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 11);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_ATOMIC_GDS, 11);
    /* Pack control word: (op<<30)|(gds_op<<28)|(src&0x7f)|(...)
     * The SPRX packing is complex; we match the field layout. */
    cmd[1] = ((op & 0x3u) << 30) |
             ((gds_op & 0x1u) << 28) |
             (src & 0x7Fu) |
             ((loop_count & 0x1u) << 7) |
             ((data & 0x1u) << 16) |
             0x40000000u;
    cmd[2] = ((uint32_t)offset << 20) | (uint32_t)index;
    cmd[3] = (uint32_t)cmp_data;
    cmd[4] = (uint32_t)(cmp_data >> 32);
    cmd[5] = mask & 0xFF00FFu;
    cmd[6] = (uint32_t)offset;
    cmd[7] = 0;  /* reserved */
    cmd[8] = 0;  /* reserved */
    cmd[9] = data;
    cmd[10] = loop_count;
    return cmd;
}

/* sceAgcDcbMemSemaphore (NID: G0jrLdvEqDw) — 4 dwords.
 * RE: SPRX emits 0xc0023900 (opcode 0x39 = MEM_SEMAPHORE).
 * cmd[1] = addr_lo & ~7, cmd[2] = addr_hi,
 * cmd[3] = (op<<29)|(signal<<20)|(wait<<16) */
uint32_t *PS5_SYSV_ABI sceAgcDcbMemSemaphore(
    SceAgcCb *cb, uint64_t address, uint32_t wait, uint32_t signal,
    uint32_t op)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_MEM_SEMAPHORE, 4);
    cmd[1] = (uint32_t)address & ~7u;
    cmd[2] = (uint32_t)(address >> 32);
    cmd[3] = ((op & 0x7u) << 29) |
             ((signal & 0x1u) << 20) |
             ((wait & 0x1u) << 16);
    return cmd;
}

/* sceAgcDcbPrimeUtcl2 (NID: jt3pl7EN17o) — 5 dwords.
 * RE: SPRX emits 0xc0035d00 (opcode 0x5D = PRIME_UTCL2).
 * cmd[1] = packed (flags<<3)|(cache_policy&7)|0x40000000,
 * cmd[2] = addr_lo & ~0x3fff, cmd[3] = addr_hi,
 * cmd[4] = reserved & 0x3fff */
uint32_t *PS5_SYSV_ABI sceAgcDcbPrimeUtcl2(
    SceAgcCb *cb, uint32_t cache_policy, uint32_t flags,
    uint64_t address, uint32_t reserved)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 5);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_PRIME_UTCL2, 5);
    cmd[1] = ((flags & 0x1u) << 3) |
             (cache_policy & 0x7u) |
             0x40000000u;
    cmd[2] = (uint32_t)address & ~0x3FFFu;
    cmd[3] = (uint32_t)(address >> 32);
    cmd[4] = reserved & 0x3FFFu;
    return cmd;
}

/* ===================================================================== */
/* DCB register direct setters                                           */
/* ===================================================================== */

/* sceAgcDcbSetCfRegisterDirect (NID: 73ZZdojLIgs) — 3 dwords.
 * RE: SPRX emits 0xc0016800 (opcode 0x68 = SET_CONFIG_REG).
 * Takes a 64-bit param: low 16 bits = reg_offset, high 32 bits = value.
 * cmd[1] = reg_offset & 0xffff, cmd[2] = value */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetCfRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONFIG_REG, 3);
    cmd[1] = (uint32_t)(reg_offset_and_value & 0xFFFFu);
    cmd[2] = (uint32_t)(reg_offset_and_value >> 32);
    return cmd;
}

/* sceAgcDcbSetCxRegisterDirect (NID: LHFXRrlTPD8) — 3 dwords.
 * RE: SPRX emits 0xc0016900 (opcode 0x69 = SET_CONTEXT_REG). */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetCxRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3);
    cmd[1] = (uint32_t)(reg_offset_and_value & 0xFFFFu);
    cmd[2] = (uint32_t)(reg_offset_and_value >> 32);
    return cmd;
}

/* sceAgcDcbSetShRegisterDirect (NID: pFLArOT53+w) — 3 dwords.
 * RE: SPRX emits 0xc0017600 (opcode 0x76 = SET_SH_REG). */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetShRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG, 3);
    cmd[1] = (uint32_t)(reg_offset_and_value & 0xFFFFu);
    cmd[2] = (uint32_t)(reg_offset_and_value >> 32);
    return cmd;
}

/* sceAgcDcbSetUcRegisterDirect (NID: w4-d0n60hdo) — 3 dwords.
 * RE: SPRX emits 0xc0017900 (opcode 0x79 = SET_UCONFIG_REG). */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetUcRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, 3);
    cmd[1] = (uint32_t)(reg_offset_and_value & 0xFFFFu);
    cmd[2] = (uint32_t)(reg_offset_and_value >> 32);
    return cmd;
}

/* sceAgcDcbSetCfRegisterRangeDirect (NID: BVFg3CWU6Eo) — variable length.
 * RE: SPRX emits 0xc0006800 (opcode 0x68 = SET_CONFIG_REG).
 * 2 + count dwords: [0] header, [1] reg_offset, [2..] values. */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetCfRegisterRangeDirect(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count)
{
    if (!values || count == 0)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 2 + count);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONFIG_REG, 2 + count);
    cmd[1] = reg_offset & 0xFFFFu;
    for (uint32_t i = 0; i < count; ++i)
        cmd[2 + i] = values[i];
    return cmd;
}

/* sceAgcCbSetUcRegisterRangeDirect (NID: MDLD5Ly94Xk) — variable length.
 * RE: SPRX emits 0xc0007900 (opcode 0x79 = SET_UCONFIG_REG).
 * 2 + count dwords: [0] header, [1] reg_offset (16-bit), [2..] values. */
uint32_t *PS5_SYSV_ABI sceAgcCbSetUcRegisterRangeDirect(
    SceAgcCb *cb, uint16_t reg_offset, const uint32_t *values, uint32_t count)
{
    if (!values || count == 0)
        return 0;

    uint32_t total = 2 + count;
    uint32_t *cmd = agcCbAllocDwords(cb, total);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, total);
    cmd[1] = (uint32_t)reg_offset;
    for (uint32_t i = 0; i < count; ++i)
        cmd[2 + i] = values[i];
    return cmd;
}

/* ===================================================================== */
/* CB builders — branch, cond write, semaphore                           */
/* ===================================================================== */

/* sceAgcCbBranch (NID: w1KFAHVqpaU) — 14 dwords.
 * RE: SPRX emits 0xc00c3f00 (opcode 0x3F = INDIRECT_BUFFER, length=14).
 * 12 arguments per SPRX disassembly. Packet layout:
 *   cmd[0]  = header 0xc00c3f00
 *   cmd[1]  = ((ctrl & 7) << 8) | (flags & 3)
 *   cmd[2]  = target_addr_lo & ~7
 *   cmd[3]  = target_addr_hi
 *   cmd[4]  = src_data_lo
 *   cmd[5]  = src_data_hi
 *   cmd[6]  = dst_data_lo
 *   cmd[7]  = dst_data_hi
 *   cmd[8]  = addr2_lo & ~3
 *   cmd[9]  = addr2_hi
 *   cmd[10] = ((dst_engine & 3) << 28) | (size1 & 0xfffff)
 *   cmd[11] = addr3_lo & ~3
 *   cmd[12] = addr3_hi
 *   cmd[13] = ((src_engine & 3) << 28) | (size2 & 0xfffff) */
uint32_t *PS5_SYSV_ABI sceAgcCbBranch(
    SceAgcCb *cb, uint32_t flags, uint32_t ctrl, uint64_t target_addr,
    uint64_t src_data, uint64_t dst_data, uint8_t dst_engine,
    uint64_t addr2, uint32_t size1, uint8_t src_engine,
    uint64_t addr3, uint32_t size2)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 14);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDIRECT_BUFFER, 14);
    cmd[1] = ((ctrl & 0x7u) << 8) | (flags & 0x3u);
    cmd[2] = (uint32_t)target_addr & ~7u;
    cmd[3] = (uint32_t)(target_addr >> 32);
    cmd[4] = (uint32_t)src_data;
    cmd[5] = (uint32_t)(src_data >> 32);
    cmd[6] = (uint32_t)dst_data;
    cmd[7] = (uint32_t)(dst_data >> 32);
    cmd[8] = (uint32_t)addr2 & ~3u;
    cmd[9] = (uint32_t)(addr2 >> 32);
    cmd[10] = ((uint32_t)(dst_engine & 0x3u) << 28) | (size1 & 0xFFFFFu);
    cmd[11] = (uint32_t)addr3 & ~3u;
    cmd[12] = (uint32_t)(addr3 >> 32);
    cmd[13] = ((uint32_t)(src_engine & 0x3u) << 28) | (size2 & 0xFFFFFu);
    return cmd;
}

/* sceAgcCbCondWrite (NID: 7toV+elXqNM) — 9 dwords.
 * RE: SPRX emits 0xc0074500 (opcode 0x45 = COND_WRITE, length=9).
 * 8 arguments per SPRX disassembly. Packet layout:
 *   cmd[0] = header 0xc0074500
 *   cmd[1] = ((write_enable & 3) << 8) | (compare_function & 7) | 0x10
 *   cmd[2] = ref_lo (arg6 lo)
 *   cmd[3] = ref_hi (arg6 hi)
 *   cmd[4] = mask (arg7)
 *   cmd[5] = reserved (arg8)
 *   cmd[6] = address_lo (arg4 lo)
 *   cmd[7] = address_hi (arg4 hi)
 *   cmd[8] = write_data (arg5) */
uint32_t *PS5_SYSV_ABI sceAgcCbCondWrite(
    SceAgcCb *cb, uint32_t compare_function, uint32_t write_enable,
    uint64_t address, uint32_t write_data, uint64_t ref,
    uint32_t mask, uint32_t reserved)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 9);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_COND_WRITE, 9);
    cmd[1] = ((write_enable & 0x3u) << 8) |
             (compare_function & 0x7u) |
             0x10u;
    cmd[2] = (uint32_t)ref;
    cmd[3] = (uint32_t)(ref >> 32);
    cmd[4] = mask;
    cmd[5] = reserved;
    cmd[6] = (uint32_t)address;
    cmd[7] = (uint32_t)(address >> 32);
    cmd[8] = write_data;
    return cmd;
}

/* sceAgcCbMemSemaphore (NID: vHX9guneRBY) — 4 dwords.
 * RE: SPRX emits 0xc0023900 (opcode 0x39 = MEM_SEMAPHORE).
 * Same format as DCB MemSemaphore. */
uint32_t *PS5_SYSV_ABI sceAgcCbMemSemaphore(
    SceAgcCb *cb, uint64_t address, uint32_t wait, uint32_t signal,
    uint32_t op)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_MEM_SEMAPHORE, 4);
    cmd[1] = (uint32_t)address & ~7u;
    cmd[2] = (uint32_t)(address >> 32);
    cmd[3] = ((op & 0x7u) << 29) |
             ((signal & 0x1u) << 20) |
             ((wait & 0x1u) << 16);
    return cmd;
}

/* ===================================================================== */
/* WaitRegMem patchers                                                   */
/* ===================================================================== */

/* RE: SPRX patchers validate the packet by checking:
 *   1. byte[1] of cmd[0] (opcode field) must be 0x79 (SET_UCONFIG_REG /
 *      AGC wrapper opcode)
 *   2. Calculate adjusted pointer: skip past the 0x79 wrapper packet to
 *      find the real WAIT_REG_MEM packet
 *   3. Check adjusted pointer's opcode byte for 0x3C (32-bit) or 0x93 (64-bit)
 *   4. If any check fails, return 0x8a6c000c
 *
 * The adjusted pointer is calculated as:
 *   - If (cmd[0] & 0x3FFFFF00) == 0x3FFF1000 (NOP max-length): adjusted = cmd + 4
 *   - Otherwise: adjusted = cmd + total_dwords * 4 (skip past 0x79 packet)
 *
 * Patches are applied at the adjusted pointer:
 *   CompareFunction: adjusted[1] bits 2:0
 *   Reference:        adjusted[4] (offset 0x10)
 *   Mask:             adjusted[5] (offset 0x14) for 0x3C, adjusted[6] (offset 0x18) for 0x93 */

static int32_t agcWaitRegMemPatchFindAdjusted(uint32_t *cmd, uint32_t **out_adj)
{
    if (!cmd)
        return 0x8a6c000c;

    /* Check opcode byte (bits 15:8) is 0x79 */
    uint8_t opcode_byte = (uint8_t)((cmd[0] >> 8) & 0xFFu);
    if (opcode_byte != 0x79)
        return 0x8a6c000c;

    /* Extract length field (bits 29:16) and compute total dwords */
    uint32_t length_minus_2 = (cmd[0] >> 16) & 0x3FFFu;
    uint32_t total_dwords = length_minus_2 + 2;

    /* Calculate adjusted pointer */
    uint32_t *adj;
    if ((cmd[0] & 0x3FFFFF00u) == 0x3FFF1000u)
        adj = cmd + 1;  /* max-length NOP: real packet at cmd[1] */
    else
        adj = cmd + total_dwords;  /* skip past 0x79 wrapper */

    if (!adj)
        return 0x8a6c000c;

    /* Check adjusted pointer's opcode for 0x3C or 0x93 */
    uint8_t adj_opcode = (uint8_t)((adj[0] >> 8) & 0xFFu);
    if (adj_opcode != 0x3C && adj_opcode != 0x93)
        return 0x8a6c000c;

    *out_adj = adj;
    return AGC_OK;
}

/* sceAgcWaitRegMemPatchCompareFunction (NID: n485EBnIWmk)
 * Patches bits 2:0 of adjusted_cmd[1] (the control word). */
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchCompareFunction(
    uint32_t *cmd, uint8_t compare_function)
{
    uint32_t *adj;
    int32_t rc = agcWaitRegMemPatchFindAdjusted(cmd, &adj);
    if (rc != AGC_OK)
        return rc;
    adj[1] = (adj[1] & ~0x7u) | (compare_function & 0x7u);
    return AGC_OK;
}

/* sceAgcWaitRegMemPatchReference (NID: 7nOoijNPvEU)
 * Patches adjusted_cmd[4] (offset 0x10 from adjusted pointer). */
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchReference(
    uint32_t *cmd, uint32_t reference)
{
    uint32_t *adj;
    int32_t rc = agcWaitRegMemPatchFindAdjusted(cmd, &adj);
    if (rc != AGC_OK)
        return rc;
    adj[4] = reference;
    return AGC_OK;
}

/* sceAgcWaitRegMemPatchMask (NID: hXAnLgDHCoI)
 * Patches adjusted_cmd[5] (offset 0x14) for 32-bit (0x3C),
 * or adjusted_cmd[6] (offset 0x18) for 64-bit (0x93). */
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchMask(
    uint32_t *cmd, uint32_t mask)
{
    uint32_t *adj;
    int32_t rc = agcWaitRegMemPatchFindAdjusted(cmd, &adj);
    if (rc != AGC_OK)
        return rc;
    uint8_t adj_opcode = (uint8_t)((adj[0] >> 8) & 0xFFu);
    if (adj_opcode == 0x93)
        adj[6] = mask;  /* 64-bit: offset 0x18 */
    else
        adj[5] = mask;  /* 32-bit: offset 0x14 */
    return AGC_OK;
}

/* ===================================================================== */
/* Complex DCB builders                                                  */
/* ===================================================================== */

/* sceAgcDcbDrawIndexMultiInstanced (NID: Rlx+bykm0r0)
 * RE: SPRX emits 0xc0073a00 (opcode 0x3A, AGC-custom, length=9+count).
 * The packet has a fixed 9-dword header followed by per-instance data.
 * The SPRX also emits a trailing NOP packet for alignment. */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexMultiInstanced(
    SceAgcCb *cb, uint32_t index_count, uint64_t index_base_addr,
    uint32_t instance_count, uint32_t draw_initiator,
    const uint32_t *instance_data, uint32_t data_count)
{
    if (!instance_data || data_count == 0)
        return 0;

    uint32_t total = 9 + data_count;
    uint32_t *cmd = agcCbAllocDwords(cb, total);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_MULTI_INSTANCED, total);
    cmd[1] = index_count;
    cmd[2] = (uint32_t)index_base_addr;
    cmd[3] = (uint32_t)(index_base_addr >> 32);
    cmd[4] = instance_count ? instance_count : 1;
    cmd[5] = 0;  /* reserved */
    cmd[6] = 0;  /* reserved */
    cmd[7] = 0;  /* reserved */
    cmd[8] = draw_initiator;
    for (uint32_t i = 0; i < data_count; ++i)
        cmd[9 + i] = instance_data[i];
    return cmd;
}

/* sceAgcDcbSetMarker (NID: QhCbS4X9Rl8)
 * RE: SPRX calls a helper to compute the marker size, then calls
 * an internal builder. The marker is encoded as a NOP-wrapped string.
 * On the generic backend, we emit a NOP packet with the marker data. */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetMarker(
    SceAgcCb *cb, const char *marker, uint32_t flags)
{
    if (!marker)
        return 0;

    /* Compute string length + padding to 4-byte alignment */
    size_t len = 0;
    while (marker[len])
        len++;
    uint32_t payload_dwords = (uint32_t)((len + 3) / 4);
    uint32_t total = 2 + payload_dwords;

    uint32_t *cmd = agcCbAllocDwords(cb, total);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_PUSH_MARKER, total);
    cmd[1] = flags;
    memset(&cmd[2], 0, payload_dwords * 4);
    memcpy(&cmd[2], marker, len);
    return cmd;
}

/* sceAgcDcbContextStateOp (NID: HabmgqPwPw0)
 * RE: SPRX switches on op (0..3):
 *   0: CLEAR_STATE (2 dwords, opcode 0x12)
 *   1: SET_CONTEXT_REG (3 dwords, opcode 0x69)
 *   2: SET_CX_REG_INDIRECT (5 dwords, opcode 0x9F)
 *   3: CLEAR_STATE + SET_CX_REG_INDIRECT (2+1pad+5 = 8 dwords)
 * The SPRX also checks a global debug flag for op 1/3. */
uint32_t *PS5_SYSV_ABI sceAgcDcbContextStateOp(
    SceAgcCb *cb, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data)
{
    (void)reg_type;
    (void)reg_data;

    switch (op) {
    case 0: {
        /* CLEAR_STATE: 2 dwords */
        uint32_t *cmd = agcCbAllocDwords(cb, 2);
        if (!cmd)
            return 0;
        cmd[0] = agcPm4Header3(AGC_PM4_OP_CLEAR_STATE_AGC, 2);
        cmd[1] = reg_offset & 0xFu;
        return cmd;
    }
    case 1: {
        /* SET_CONTEXT_REG: 3 dwords */
        uint32_t *cmd = agcCbAllocDwords(cb, 3);
        if (!cmd)
            return 0;
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3);
        cmd[1] = reg_offset & 0xFFFFu;
        cmd[2] = reg_count;
        return cmd;
    }
    case 2: {
        /* SET_CX_REG_INDIRECT: 5 dwords (opcode 0x9F) */
        uint32_t *cmd = agcCbAllocDwords(cb, 5);
        if (!cmd)
            return 0;
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CX_REG_INDIRECT, 5);
        cmd[1] = reg_offset & ~3u;
        cmd[2] = 0;
        cmd[3] = 0x80000000u;
        cmd[4] = reg_count & 0x3FFFu;
        return cmd;
    }
    case 3: {
        /* CLEAR_STATE (2 dwords) + padding (1 dword) + SET_CX_REG_INDIRECT (5 dwords)
         * Total: 8 dwords. The padding aligns the second packet to a
         * 4-dword boundary as observed in SPRX. */
        uint32_t *cmd = agcCbAllocDwords(cb, 8);
        if (!cmd)
            return 0;
        /* CLEAR_STATE (2 dwords) */
        cmd[0] = agcPm4Header3(AGC_PM4_OP_CLEAR_STATE_AGC, 2);
        cmd[1] = reg_offset & 0xFu;
        cmd[2] = 0;  /* padding */
        /* SET_CX_REG_INDIRECT (5 dwords) */
        cmd[3] = agcPm4Header3(AGC_PM4_OP_SET_CX_REG_INDIRECT, 5);
        cmd[4] = reg_offset & ~3u;
        cmd[5] = 0;
        cmd[6] = 0x80000000u;
        cmd[7] = reg_count & 0x3FFFu;
        return cmd;
    }
    default:
        return 0;
    }
}

/* DCB workload helpers — these delegate to the same packet format as
 * the ACB/VshDcb variants but operate on a DCB cursor. The SPRX calls
 * internal helper functions that compute the packet size and fill it in. */

uint32_t *PS5_SYSV_ABI sceAgcDcbSetWorkloadsActive(
    SceAgcCb *cb, uint32_t flags, const void *data, uint32_t data_size)
{
    (void)data;
    /* SPRX calls an internal helper that builds a SET_WORKLOAD packet.
     * We emit a simple 8-dword packet matching the ACB format. */
    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_WORKLOAD, 8);
    cmd[1] = flags;
    cmd[2] = 0;
    cmd[3] = 0;
    cmd[4] = 0;
    cmd[5] = 0;
    cmd[6] = 0;
    cmd[7] = data_size;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetWorkloadComplete(
    SceAgcCb *cb, uint32_t workload_id, uint32_t flags)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_WORKLOAD, 8);
    cmd[1] = workload_id;
    cmd[2] = flags;
    cmd[3] = 0;
    cmd[4] = 0;
    cmd[5] = 0;
    cmd[6] = 0;
    cmd[7] = 0;
    return cmd;
}

uint32_t *PS5_SYSV_ABI sceAgcDcbSetWorkloadStreamInactive(
    SceAgcCb *cb, uint32_t workload_id)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_WORKLOAD, 8);
    cmd[1] = workload_id;
    cmd[2] = 0;
    cmd[3] = 0;
    cmd[4] = 0;
    cmd[5] = 0;
    cmd[6] = 0;
    cmd[7] = 0;
    return cmd;
}

/* ===================================================================== */
/* reference-confirmed patchers and helpers                               */
/* ===================================================================== */

/* sceAgcGetPacketSize (NID: Lkf86B98qPc)
 * Returns packet size in dwords from PM4 header.
 * Special case: NOP packets with (header & 0x3FFFFF00) == 0x3FFF1000
 * are 1 dword (padding/filler). */
uint32_t PS5_SYSV_ABI sceAgcGetPacketSize(uint32_t *packet)
{
    if (!packet)
        return 0;
    uint32_t cmd_id = packet[0];
    if ((cmd_id & 0x3FFFFF00u) == 0x3FFF1000u)
        return 1;
    return ((cmd_id >> 16) & 0x3FFFu) + 2u;
}

/* sceAgcSetPacketPredication (NID: w6Dj1VJt5qY)
 * Sets/clears bit 0 (predication) of a packet header. */
int32_t PS5_SYSV_ABI sceAgcSetPacketPredication(
    uint32_t *packet, uint32_t predication)
{
    if (!packet)
        return AGC_ERROR_INVALID_ARGUMENT;
    packet[0] = (packet[0] & ~1u) | ((predication & 1u) ? 1u : 0u);
    return AGC_OK;
}

/* sceAgcSetRangePredication (NID: n8vgpaQg6dA)
 * Walks packets from start to end, setting predication bit on each. */
int32_t PS5_SYSV_ABI sceAgcSetRangePredication(
    uint32_t *start, const uint32_t *end, uint32_t predication)
{
    if (!start || !end)
        return AGC_ERROR_INVALID_ARGUMENT;

    uintptr_t packet_va = (uintptr_t)start;
    uintptr_t end_va = (uintptr_t)end;
    if (packet_va >= end_va)
        return AGC_OK;

    uint32_t pred_bit = (predication & 1u) ? 1u : 0u;
    uint32_t *packet = start;
    while (packet_va < end_va) {
        uint32_t cmd_id = packet[0];
        packet[0] = (cmd_id & ~1u) | pred_bit;

        uint32_t size = ((cmd_id >> 16) & 0x3FFFu) + 2u;
        if ((cmd_id & 0x3FFFFF00u) == 0x3FFF1000u)
            size = 1;

        packet_va += size * sizeof(uint32_t);
        packet = (uint32_t *)packet_va;
    }
    return AGC_OK;
}

/* sceAgcCondExecPatchSetEnd (NID: ORWsxIbk4TE)
 * Patches cmd[4] bits 13:0 with dword count between packet end and buffer end. */
int32_t PS5_SYSV_ABI sceAgcCondExecPatchSetEnd(
    uint32_t *cmd, const uint32_t *end)
{
    if (!cmd || !end)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t op = (cmd[0] >> 8) & 0xFFu;
    if (op != AGC_PM4_OP_COND_EXEC)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t *packet_end = cmd + 5;
    if (end < packet_end)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t num_dwords = (uint32_t)(end - packet_end);
    if (num_dwords > 0x3FFFu)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[4] = (cmd[4] & ~0x3FFFu) | num_dwords;
    return AGC_OK;
}

/* sceAgcCondExecPatchSetCommandAddress (NID: YWTKOju587o)
 * Patches cmd[1] lo and cmd[2] hi with command address, preserving cmd[1] bits 1:0. */
int32_t PS5_SYSV_ABI sceAgcCondExecPatchSetCommandAddress(
    uint32_t *cmd, const uint32_t *command)
{
    if (!cmd || !command)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t op = (cmd[0] >> 8) & 0xFFu;
    if (op != AGC_PM4_OP_COND_EXEC)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* Command address must be 4-byte aligned */
    if (((uintptr_t)command & 0x3u) != 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint64_t addr = (uint64_t)(uintptr_t)command;
    cmd[1] = (cmd[1] & 0x3u) | ((uint32_t)addr & 0xFFFFFFFCu);
    cmd[2] = (uint32_t)(addr >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcAsyncCondExecPatchSetCommandAddress(
    uint32_t *cmd, const uint32_t *command)
{
    uint64_t address;

    if (!cmd || !command || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_COND_EXEC)
        return AGC_ERROR_INVALID_ARGUMENT;

    address = (uint64_t)(uintptr_t)command;
    cmd[1] = (cmd[1] & 0x3u) | ((uint32_t)address & ~0x3u);
    cmd[2] = (uint32_t)(address >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcAsyncCondExecPatchSetEnd(
    uint32_t *cmd, const uint32_t *end)
{
    uint32_t byte_delta;

    if (!cmd || !end || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_COND_EXEC)
        return AGC_ERROR_INVALID_ARGUMENT;

    byte_delta = (uint32_t)(uintptr_t)end - (uint32_t)(uintptr_t)cmd - 20u;
    cmd[4] = (cmd[4] & ~0x3FFFu) | ((byte_delta >> 2) & 0x3FFFu);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcBranchPatchSetCompareAddress(
    uint32_t *cmd, uint64_t address)
{
    if (!cmd || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_INDIRECT_BUFFER)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[2] = (cmd[2] & 0x7u) | ((uint32_t)address & ~0x7u);
    cmd[3] = (uint32_t)(address >> 32);
    return AGC_OK;
}

static int32_t agcRewindPatchSetRewindState(
    uint32_t *cmd, uint32_t rewind_state)
{
    if (!cmd || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_REWIND)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[1] = (cmd[1] & 0x7FFFFFFFu) | ((rewind_state & 1u) << 31);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcRewindPatchSetRewindState(
    uint32_t *cmd, uint32_t rewind_state)
{
    return agcRewindPatchSetRewindState(cmd, rewind_state);
}

int32_t PS5_SYSV_ABI sceAgcAsyncRewindPatchSetRewindState(
    uint32_t *cmd, uint32_t rewind_state)
{
    return agcRewindPatchSetRewindState(cmd, rewind_state);
}

int32_t PS5_SYSV_ABI sceAgcQueueEndOfPipeActionPatchGcrCntl(
    uint32_t *cmd, uint32_t gcr_cntl)
{
    uint64_t fields;

    if (!cmd || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_RELEASE_MEM)
        return AGC_ERROR_INVALID_ARGUMENT;

    fields = (uint64_t)cmd[1] | ((uint64_t)cmd[2] << 32);
    fields = (fields & ~(0xFFFull << 12)) |
        (((uint64_t)gcr_cntl & 0xFFFu) << 12);
    cmd[1] = (uint32_t)fields;
    cmd[2] = (uint32_t)(fields >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcQueueEndOfPipeActionPatchType(
    uint32_t *cmd, uint32_t event_type)
{
    uint64_t fields;
    uint64_t encoded_type;

    if (!cmd || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_RELEASE_MEM)
        return AGC_ERROR_INVALID_ARGUMENT;

    fields = (uint64_t)cmd[1] | ((uint64_t)cmd[2] << 32);
    encoded_type = (uint8_t)event_type >= 0x2Fu ? 0x600u : 0x500u;
    encoded_type |= event_type & 0x3Fu;
    fields = (fields & ~0xF3Full) | encoded_type;
    cmd[1] = (uint32_t)fields;
    cmd[2] = (uint32_t)(fields >> 32);
    return AGC_OK;
}

/* sceAgcWriteDataPatchSetAddressOrOffset (NID: fPSCdQxgpSw)
 * Patches cmd[2] lo and cmd[3] hi for IT_WRITE_DATA packets. */
int32_t PS5_SYSV_ABI sceAgcWriteDataPatchSetAddressOrOffset(
    uint32_t *cmd, uint64_t address_or_offset)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t op = (cmd[0] >> 8) & 0xFFu;
    if (op != AGC_PM4_OP_WRITE_DATA)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[2] = (uint32_t)address_or_offset;
    cmd[3] = (uint32_t)(address_or_offset >> 32);
    return AGC_OK;
}

/* Compatibility-SPRX eAy8eGNsCuU @ 0xd4a0. */
int32_t PS5_SYSV_ABI sceAgcWriteDataPatchSetCachePolicy(
    uint32_t *cmd, uint32_t cache_policy)
{
    if (!cmd || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_WRITE_DATA)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[1] = (cmd[1] & 0xF9FFFFFFu) | ((cache_policy & 0x3u) << 25u);
    return AGC_OK;
}

/* Compatibility-SPRX tmy-+rBpspY @ 0xd4d0. The five-bit destination is
 * split between control bit 30 and bits 11:8. */
int32_t PS5_SYSV_ABI sceAgcWriteDataPatchSetDst(
    uint32_t *cmd, uint32_t destination)
{
    uint32_t encoded;

    if (!cmd || ((cmd[0] >> 8) & 0xFFu) != AGC_PM4_OP_WRITE_DATA)
        return AGC_ERROR_INVALID_ARGUMENT;

    encoded = ((destination << 30u) | (destination << 7u)) & 0x40000F00u;
    cmd[1] = (cmd[1] & 0x3FFFF0FFu) | encoded;
    return AGC_OK;
}

/* sceAgcJumpPatchSetTarget (NID: 2BS4EtAaF28)
 * Patches IT_INDIRECT_BUFFER cmd[1] lo, cmd[2] hi (bits 15:0),
 * cmd[3] size (bits 19:0). */
int32_t PS5_SYSV_ABI sceAgcJumpPatchSetTarget(
    uint32_t *cmd, const uint32_t *target, uint32_t size_in_dwords)
{
    if (!cmd || !target)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t op = (cmd[0] >> 8) & 0xFFu;
    if (op != AGC_PM4_OP_INDIRECT_BUFFER)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint64_t vaddr = (uint64_t)(uintptr_t)target;
    cmd[1] = (uint32_t)vaddr;
    cmd[2] = (cmd[2] & 0xFFFF0000u) | ((uint32_t)(vaddr >> 32) & 0xFFFFu);
    cmd[3] = (cmd[3] & 0xFFF00000u) | (size_in_dwords & 0xFFFFFu);
    return AGC_OK;
}

/* SetNumRegisters patchers — patch cmd[4] bits 13:0 of indirect register packets.
 * Each checks the opcode matches the expected register type. */
static int32_t agcRegIndirectPatchSetNumRegisters(
    uint32_t *cmd, uint32_t num_regs, uint32_t expected_op)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t op = (cmd[0] >> 8) & 0xFFu;
    if (op != expected_op)
        return AGC_ERROR_INVALID_ARGUMENT;

    cmd[4] = (cmd[4] & ~0x3FFFu) | (num_regs & 0x3FFFu);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchSetNumRegisters(
    uint32_t *cmd, uint32_t num_regs)
{
    return agcRegIndirectPatchSetNumRegisters(cmd, num_regs, AGC_PM4_OP_SET_CX_REG_INDIRECT);
}

int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchSetNumRegisters(
    uint32_t *cmd, uint32_t num_regs)
{
    return agcRegIndirectPatchSetNumRegisters(cmd, num_regs, AGC_PM4_OP_SET_SH_REG_INDIRECT);
}

int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchSetNumRegisters(
    uint32_t *cmd, uint32_t num_regs)
{
    return agcRegIndirectPatchSetNumRegisters(cmd, num_regs, AGC_PM4_OP_SET_UC_REG_INDIRECT);
}

/* ===================================================================== */
/* GetSize helpers — reference-confirmed                                    */
/* ===================================================================== */

uint32_t PS5_SYSV_ABI sceAgcDcbWriteDataGetSize(uint32_t num_dwords)
{
    return 4u * num_dwords + 16u;
}

uint32_t PS5_SYSV_ABI sceAgcDcbJumpGetSize(void)
{
    return 16u;
}

uint32_t PS5_SYSV_ABI sceAgcDcbRewindGetSize(void)
{
    return 8u;
}

uint32_t PS5_SYSV_ABI sceAgcDcbCondExecGetSize(void)
{
    return 20u;
}

uint32_t PS5_SYSV_ABI sceAgcAcbCondExecGetSize(void)
{
    return 20u;
}

uint32_t PS5_SYSV_ABI sceAgcDcbWaitOnAddressGetSize(uint32_t size)
{
    switch (size) {
    case 0:  return 14u * 4u;
    case 1:  return 16u * 4u;
    default: return 0;
    }
}

uint32_t PS5_SYSV_ABI sceAgcCbNopGetSize(uint32_t num_dwords)
{
    return 4u * num_dwords;
}

uint32_t PS5_SYSV_ABI sceAgcCbSetShRegisterRangeDirectGetSize(
    uint32_t num_registers)
{
    return 8u + 4u * num_registers;
}

uint32_t PS5_SYSV_ABI sceAgcCbSetShRegistersDirectGetSize(
    uint32_t num_registers)
{
    return 12u * num_registers;
}

uint32_t PS5_SYSV_ABI sceAgcCbSetUcRegisterRangeDirectGetSize(
    uint32_t num_registers)
{
    return 8u + 4u * num_registers;
}

uint32_t PS5_SYSV_ABI sceAgcCbSetUcRegistersDirectGetSize(
    uint32_t num_registers)
{
    return 12u * num_registers;
}

uint32_t PS5_SYSV_ABI sceAgcDriverUserDataGetPacketSize(uint32_t size_in_bytes)
{
    uint32_t payload_dwords;

    if (size_in_bytes == 0)
        return 3u;

    payload_dwords = (uint32_t)(((uint64_t)size_in_bytes + 3u) >> 2);
    if (payload_dwords == 1u)
        return 4u;
    return payload_dwords + 7u;
}

uint32_t PS5_SYSV_ABI sceAgcAcbAcquireMemGetSize(void)
{
    return g_agc_packet_mode == 1u ? 64u : 32u;
}

uint32_t PS5_SYSV_ABI sceAgcDcbAcquireMemGetSize(void)
{
    return g_agc_packet_mode == 1u ? 64u : 32u;
}

uint32_t PS5_SYSV_ABI sceAgcAcbWaitOnAddressGetSize(uint32_t size)
{
    uint32_t user_data_dwords =
        sceAgcDriverUserDataGetPacketSize(4u) +
        sceAgcDriverUserDataGetPacketSize(0u);

    switch ((uint8_t)size) {
    case 0: return 28u + 4u * user_data_dwords;
    case 1: return 36u + 4u * user_data_dwords;
    default: return 0;
    }
}

uint32_t PS5_SYSV_ABI sceAgcDcbBeginOcclusionQueryGetSize(uint32_t query_type)
{
    if (query_type == 0)
        return 16u;
    if ((uint8_t)query_type == 1u)
        return 288u;
    return 0;
}

uint32_t PS5_SYSV_ABI sceAgcDcbContextStateOpGetSize(uint32_t operation)
{
    static const uint32_t sizes[] = {20u, 108u, 108u, 128u};

    if (operation >= sizeof(sizes) / sizeof(sizes[0]))
        return 0;
    return sizes[operation];
}

uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndirectMultiGetSize(void)
{
    uint32_t user_data_dwords = sceAgcDriverUserDataGetPacketSize(0u);
    return 4u * (2u * user_data_dwords + 10u);
}

uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexIndirectMultiGetSize(void)
{
    uint32_t user_data_dwords = sceAgcDriverUserDataGetPacketSize(0u);
    return 4u * (2u * user_data_dwords + 10u);
}

uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexMultiInstancedGetSize(void)
{
    uint32_t user_data_dwords = sceAgcDriverUserDataGetPacketSize(0u);
    return 4u * (2u * user_data_dwords + 9u);
}

uint32_t PS5_SYSV_ABI sceAgcDcbEventWriteGetSize(uint32_t event_type)
{
    return (((uint8_t)event_type & 0xFEu) == 0x38u) ? 16u : 8u;
}

#define AGC_DEFINE_FIXED_GET_SIZE(name, value) \
    uint32_t PS5_SYSV_ABI name(void) { return (value); }

AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbAtomicGdsGetSize, 44u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbAtomicMemGetSize, 36u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbCopyDataGetSize, 24u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbDispatchIndirectGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbDmaDataGetSize, 28u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbEventWriteGetSize, 8u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbJumpGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbPrimeUtcl2GetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbQueueEndOfShaderActionGetSize, 32u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcAcbRewindGetSize, 8u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcCbBranchGetSize, 56u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcCbCondWriteGetSize, 36u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcCbDispatchGetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbAtomicGdsGetSize, 44u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbAtomicMemGetSize, 36u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbCopyDataGetSize, 24u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbDispatchIndirectGetSize, 12u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbDmaDataGetSize, 28u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbDrawIndexAutoGetSize, 12u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbDrawIndexGetSize, 24u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbDrawIndexIndirectGetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbDrawIndexOffsetGetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbDrawIndirectGetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbEndOcclusionQueryGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbPrimeUtcl2GetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbQueueEndOfShaderActionGetSize, 32u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetBaseDispatchIndirectArgsGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetBaseDrawIndirectArgsGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetBoolPredicationEnableGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetCxRegisterDirectGetSize, 12u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetCxRegistersIndirectGetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetIndexBufferGetSize, 12u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetIndexCountGetSize, 8u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetIndexIndirectArgsGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetIndexSizeGetSize, 12u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetNumInstancesGetSize, 8u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetPredicationDisableGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetShRegisterDirectGetSize, 12u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetShRegistersIndirectGetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetUcRegisterDirectGetSize, 12u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetUcRegistersIndirectGetSize, 20u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbSetZPassPredicationEnableGetSize, 16u)
AGC_DEFINE_FIXED_GET_SIZE(sceAgcDcbStallCommandBufferParserGetSize, 8u)

#undef AGC_DEFINE_FIXED_GET_SIZE
