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

#ifndef _AGC_DRIVER_H_
#define _AGC_DRIVER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agc_error.h"
#include "agc_shader.h"
#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Public Sony-style AGC runtime declarations.
 *
 * This surface is intentionally small and host-buildable while the 5.50 AGC
 * firmware ABI is still being validated. Functions with unresolved command
 * payloads emit conservative PM4 placeholders or no-op success in the generic
 * backend.
 */

/* Initialization */
int32_t PS5_SYSV_ABI sce_agc_initialize(void);
int32_t PS5_SYSV_ABI sce_agc_initialize_internal_memory(void);

/* Driver submission */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiCommandBuffersDirect(
    uint32_t count,
    void *const dcb_gpu_addrs[],
    uint32_t *dcb_sizes_in_bytes,
    void *const acb_gpu_addrs[],
    uint32_t *acb_sizes_in_bytes);
int32_t PS5_SYSV_ABI sceAgcDriverSubmitDcb(const AgcCommandBufferSubmit *packet);
int32_t PS5_SYSV_ABI sceAgcDriverSubmitAcb(
    uint32_t owner_handle, const AgcCommandBufferSubmit *packet);

/* Convenience submit wrappers (reference-confirmed).
 * Multi-DCB wrappers preserve the array as one kernel frame and delegate to
 * sceAgcDriverSubmitMultiCommandBuffersDirect. Multi-ACB submission remains
 * queue-specific and delegates each entry through sceAgcDriverSubmitAcb.
 * NIDs: SubmitMultiDcbs=6UzEidRZwkg, SubmitCommandBuffer=b4fpgH5ZXxQ,
 *        SubmitMultiCommandBuffers=Fj7r9EHzF38,
 *        SubmitMultiAcbs=HF3YllT3mXU */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiDcbs(
    void *const dcb_gpu_addrs[], const uint32_t *dcb_sizes_in_dwords,
    uint32_t count);
int32_t PS5_SYSV_ABI sceAgcDriverSubmitCommandBuffer(
    uint32_t queue, void *dcb, uint32_t size_in_dwords);
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiCommandBuffers(
    uint32_t queue, void *const dcbs[], const uint32_t *sizes_in_dwords,
    uint32_t count);
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiAcbs(
    uint32_t queue, void *const acbs[], const uint32_t *sizes_in_dwords,
    uint32_t count);
int32_t PS5_SYSV_ABI sceAgcDriverSuspendPointSubmitDirect(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3);
bool    PS5_SYSV_ABI sceAgcDriverIsSuspendPointInFlightDirect(uint32_t value);
int32_t PS5_SYSV_ABI sce_agc_internal_suspend_point_submit_final(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3);

/* Async graphics setup */
int32_t PS5_SYSV_ABI sceAgcDriverSetupAsyncGraphics(uint32_t pipe_id);
int32_t PS5_SYSV_ABI sceAgcDriverSetTFRingDirect(void);
int32_t PS5_SYSV_ABI sceAgcDriverSetHsOffchipParamDirect(
    uint64_t list_addr, uint32_t num_entries);
int32_t PS5_SYSV_ABI sceAgcDriverSetTargetRingForDiag(void);

/* Default hardware state notification */
int32_t PS5_SYSV_ABI sceAgcDriverNotifyDefaultStates(uint32_t flags);

/* SDMA */
int32_t PS5_SYSV_ABI sceAgcDriverSdmaCopyLinearBlocking(
    void *dst, const void *src, size_t size);

/* EOP flip submit — builds an IT_RELEASE_MEM EOP packet and calls
 * sceVideoOutSubmitEopFlip internally. Only functional on the prospero
 * backend; the generic backend returns AGC_ERROR_NOT_SUPPORTED.
 * \param video_out_handle  VideoOut handle from sceVideoOutOpen
 * \param display_buf_index Display buffer index (0-15, validated)
 * \param flip_mode         Flip mode (0=VSYNC, 1=VBLANK, 2=immediate)
 * \param present_ptr       Present queue pointer (or 0)
 * Returns 0 on success, VideoOut error code on failure. */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitEopFlip(
    void *video_out_handle, uint32_t display_buf_index,
    uint32_t flip_mode, void *present_ptr);

/* Workload tracking — begin/end a workload on the GPU.
 * Matches SPRX ordinals 87/88 (UM9b9NunSrE / i6bfTi13ApA) in
 * libSceAgcDriver.sprx. These build and submit a SET_WORKLOAD
 * (0x1E) PM4 packet with different subcommands.
 * \param workload_id  Workload ID (validated by SPRX; must be non-zero)
 * Returns 0 on success, AGC error code on failure. */
int32_t PS5_SYSV_ABI sceAgcDriverSetWorkloadsActive(uint32_t workload_id);
int32_t PS5_SYSV_ABI sceAgcDriverSetWorkloadComplete(uint32_t workload_id);

/* User special queue management */
int32_t PS5_SYSV_ABI _sceAgcDriverCreateUserSpecialQueue(void);
int32_t PS5_SYSV_ABI _sceAgcDriverDestroyUserSpecialQueue(void);

/* Capture/debug interface */
int32_t  PS5_SYSV_ABI sceAgcDriverRegisterCaptureInterface(void);
int32_t  PS5_SYSV_ABI sceAgcDriverDeregisterCaptureInterface(void);
int32_t  PS5_SYSV_ABI sceAgcDriverAcquireRazorACQ(void);
int32_t  PS5_SYSV_ABI sceAgcDriverReleaseRazorACQ(void);
int32_t  PS5_SYSV_ABI sceAgcDriverSubmitToRazorACQ(void);
int32_t  PS5_SYSV_ABI sceAgcDriverSubmitToHDRScopesACQ(void);
/* FW 5.50 permission-stub result returned by PA-debug-only exports. */
#define AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT 0x8A6D0001u
uint32_t PS5_SYSV_ABI sceAgcDriverGetPaDebugInterfaceVersion(void);

/* Capture status query. NID: Ddwk4gLT5j0
 * reference-confirmed: returns 0 (no capture in progress). */
int32_t PS5_SYSV_ABI sceAgcDriverIsCaptureInProgress(void);

/* Event queue management. NIDs:
 * DeleteEqEvent: DL2RXaXOy88
 * GetEqEventType: 5CdQTZIQPxM
 * reference-confirmed: stubs that return AGC_ERROR_NOT_SUPPORTED. */
int32_t PS5_SYSV_ABI sceAgcDriverDeleteEqEvent(void *event);
int32_t PS5_SYSV_ABI sceAgcDriverGetEqEventType(void *event, uint32_t *type);

/* Resource registration system. NIDs:
 * GetDefaultOwner: F0ZXt5q0ZTA
 * InitResourceRegistration: F0Y42t-3e18
 * QueryResourceRegistrationUserMemoryRequirements: AOLcoIkQDgM
 * GetResourceRegistrationMaxNameLength: uJziRsODk1c
 * UnregisterResource: pWLG7WOpVcw
 * RegisterWorkloadStream: 3AyTaWcF-H8
 * reference-confirmed: stubs. GetDefaultOwner returns 0 (default owner).
 * GetResourceRegistrationMaxNameLength returns 32. */
uint32_t PS5_SYSV_ABI sceAgcDriverGetDefaultOwner(void);
int32_t  PS5_SYSV_ABI sceAgcDriverInitResourceRegistration(void);
int32_t  PS5_SYSV_ABI sceAgcDriverQueryResourceRegistrationUserMemoryRequirements(
    void *out_info);
uint32_t PS5_SYSV_ABI sceAgcDriverGetResourceRegistrationMaxNameLength(void);
int32_t  PS5_SYSV_ABI sceAgcDriverUnregisterResource(uint32_t resource_id);
int32_t  PS5_SYSV_ABI sceAgcDriverRegisterWorkloadStream(
    const char *name, uint32_t *out_id);

/* Default state queries */
int32_t PS5_SYSV_ABI sceAgcGetDefaultState(AgcContextState *out_state);
int32_t PS5_SYSV_ABI sceAgcGetRegisterDefaults(AgcContextState *out_state);
int32_t PS5_SYSV_ABI sceAgcGetDefaultCxStateFlat(void *out_state, uint32_t size);

/* Suspend point */
int32_t PS5_SYSV_ABI sceAgcSuspendPointAndCheckStatus(uint32_t value);

/* Sony-style command-buffer cursor packet builders */
uint32_t *PS5_SYSV_ABI sceAgcCbNop(SceAgcCb *cb, uint32_t dword_count);
uint32_t *PS5_SYSV_ABI sceAgcCbDispatch(
    SceAgcCb *cb, uint32_t group_count_x, uint32_t group_count_y,
    uint32_t group_count_z, uint32_t modifier);
uint32_t *PS5_SYSV_ABI sceAgcCbSetShRegistersDirect(
    SceAgcCb *cb, const AgcRegisterValue *registers, uint32_t register_count);
uint32_t *PS5_SYSV_ABI sceAgcDcbWriteData(
    SceAgcCb *cb, uint32_t destination, uint32_t cache_policy,
    uint64_t destination_address, const uint32_t *data, uint32_t dword_count,
    uint32_t increment, uint32_t write_confirm);
uint32_t *PS5_SYSV_ABI sceAgcDcbWaitRegMem(
    SceAgcCb *cb, uint32_t size, uint32_t compare_function, uint32_t operation,
    uint32_t cache_policy, uint64_t address, uint64_t reference,
    uint64_t mask, uint32_t poll_cycles);
uint32_t *PS5_SYSV_ABI sceAgcDcbDmaData(
    SceAgcCb *cb, uint32_t destination, uint32_t destination_cache_policy,
    uint32_t source, uint64_t destination_address, uint32_t source_cache_policy,
    uint32_t control4, uint64_t source_address, uint32_t byte_count,
    uint32_t control7, uint32_t control8, uint32_t control9);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetBaseIndirectArgs(
    SceAgcCb *cb, uint32_t base_index, uint64_t address);
uint32_t *PS5_SYSV_ABI sceAgcDcbDispatchIndirect(
    SceAgcCb *cb, uint32_t data_offset, uint32_t modifier);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexBuffer(
    SceAgcCb *cb, uint64_t index_buffer_address, uint32_t index_count);
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexOffset(
    SceAgcCb *cb, uint32_t index_offset, uint32_t index_count, uint64_t modifier);
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexAuto(
    SceAgcCb *cb, uint32_t index_count, uint64_t modifier);
uint32_t *PS5_SYSV_ABI sceAgcDcbWaitUntilSafeForRendering(
    SceAgcCb *cb, uint32_t video_out_handle, uint32_t display_buffer_index);
uint32_t *PS5_SYSV_ABI sceAgcDcbPushMarker(SceAgcCb *cb, const char *marker);
uint32_t *PS5_SYSV_ABI sceAgcDcbPopMarker(SceAgcCb *cb);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetFlip(
    SceAgcCb *cb, uint32_t video_out_handle, int32_t display_buffer_index,
    uint32_t flip_mode, uint64_t flip_arg);
/* Build an EOP release-memory packet in a DCB for flip signaling.
 * This is the PM4 portion of the EOP flip submit path — an IT_RELEASE_MEM
 * (opcode 0x49) type-3 packet that signals end-of-pipe for the flip.
 * \param dcb          Command buffer cursor
 * \param event_type   EOP event type (e.g. 0 = bottom-of-pipe)
 * \param event_index  EOP event index
 * \param dst_addr     GPU destination address for the EOP signal write
 * \param data         Data value written to dst_addr on EOP completion
 * Returns dword count on success, negative AGC error code on failure. */
int32_t PS5_SYSV_ABI sceAgcDcbSetEopFlip(SceAgcCb *dcb,
    uint32_t event_type, uint32_t event_index,
    uint64_t dst_addr, uint32_t data);
uint32_t *PS5_SYSV_ABI sceAgcCbReleaseMem(
    SceAgcCb *cb, uint32_t action, uint32_t gcr_control, uint32_t destination,
    uint32_t cache_policy, uint64_t destination_address, uint32_t data_selection,
    uint64_t data, uint32_t gds_offset, uint32_t gds_size, uint32_t interrupt,
    uint32_t interrupt_context_id);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetShRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetCxRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetUcRegistersIndirect(
    SceAgcCb *cb, uint64_t registers_address, uint32_t register_count);
uint32_t *PS5_SYSV_ABI sceAgcCbSetCxRegistersDirect(
    SceAgcCb *cb, const AgcRegisterValue *registers, uint32_t register_count);

/* In-place packet patchers — overwrite an address field in an already-emitted packet */
int32_t PS5_SYSV_ABI sceAgcDmaDataPatchSetDstAddressOrOffset(
    uint32_t *cmd, uint64_t destination_address);
int32_t PS5_SYSV_ABI sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate(
    uint32_t *cmd, uint64_t source_address);
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchAddress(
    uint32_t *cmd, uint64_t address);
int32_t PS5_SYSV_ABI sceAgcQueueEndOfPipeActionPatchAddress(
    uint32_t *cmd, uint64_t address);
int32_t PS5_SYSV_ABI sceAgcQueueEndOfPipeActionPatchData(
    uint32_t *cmd, uint32_t context_id, uint32_t data_sel, uint64_t data);
uint32_t PS5_SYSV_ABI sceAgcCbQueueEndOfPipeActionGetSize(void);

/* LOD stats helpers */
size_t PS5_SYSV_ABI sceAgcDcbGetLodStatsGetSize(uint32_t counter_count);
uint32_t *PS5_SYSV_ABI sceAgcDcbGetLodStats(
    SceAgcCb *cb, uint32_t cache_policy, uint64_t destination_address,
    uint32_t control, uint32_t counter_mask, uint32_t reset_counters,
    uint32_t enable, uint32_t counter_select);

/* DCB draw/indirect/register packet builders (SPRX capstone disassembly) */

/* IT_INDIRECT_BUFFER (opcode 0x3F) — 4-dword IB chain. NID: w1KFAHVqpaU */
uint32_t *PS5_SYSV_ABI sceAgcDcbIndirectBuffer(
    SceAgcCb *cb, uint64_t gpu_addr, uint32_t size_dwords, uint32_t vmid);

/* IT_DRAW_INDIRECT (opcode 0x24) — 5-dword indirect draw. NID: 1rZSWUv1IRc */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndirect(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t draw_initiator);

/* IT_DRAW_INDEX_2 (opcode 0x27) — 6-dword indexed draw. NID: q88lQ+GP5Yk */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndex2(
    SceAgcCb *cb, uint32_t max_size, uint64_t index_base_addr,
    uint32_t index_count, uint32_t draw_initiator);

/* IT_DRAW_INDEX_INDIRECT (opcode 0x25) — 5-dword indirect indexed draw. NID: t1vNu082-jM */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexIndirect(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t draw_initiator);

/* IT_DRAW_INDIRECT_MULTI (opcode 0x2C) — 7-dword multi indirect draw. NID: kUlvghKs-mA */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndirectMulti(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t count, uint32_t stride,
    uint32_t draw_initiator);

/* IT_DRAW_INDEX_INDIRECT_MULTI (opcode 0x38) — 7-dword multi indirect indexed draw. NID: ypVBz4uPKcQ */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexIndirectMulti(
    SceAgcCb *cb, uint32_t data_offset, uint32_t base_vtx_loc,
    uint32_t start_inst_loc, uint32_t count, uint32_t stride,
    uint32_t draw_initiator);

/* IT_SET_PREDICATION (opcode 0x20) — 3-dword predication setup. NID: bbFueFP+J4k */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetPredication(
    SceAgcCb *cb, uint64_t addr, uint32_t op, uint32_t keep_count,
    bool predicate);

/* IT_EVENT_WRITE (opcode 0x46) — 2-dword event signal. NID: aJf+j5yntiU */
uint32_t *PS5_SYSV_ABI sceAgcDcbEventWrite(
    SceAgcCb *cb, uint32_t event_type, uint32_t event_index);

/* IT_SET_CONFIG_REG (opcode 0x68) — variable-length config register write. NID: BVFg3CWU6Eo */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetConfigReg(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count);

/* IT_SET_SH_REG (opcode 0x76) — variable-length SH register write. NID: n2fD4A+pb+g */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetShReg(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count);

/* IT_SET_UCONFIG_REG (opcode 0x79) — variable-length uconfig register write. NID: MDLD5Ly94Xk */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetUconfigReg(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count);

/* AGC-custom display/flip wait builders (libSceAgc.sprx only).
 * These use AGC-specific PM4 opcodes (0x4C/0x4E/0x4F/0x51/0x54) rather
 * than the standard NOP-wrapped subcommand encoding. The SPRX validates
 * the flip slot index < 32; builders return NULL (0) on invalid input.
 * \param cb         Command buffer cursor
 * \param flip_slot  Flip slot index (0-31, validated < 32)
 * Returns pointer to the emitted packet, or NULL on validation failure. */
uint32_t *PS5_SYSV_ABI sceAgcDcbWaitFlipDone(SceAgcCb *cb, uint32_t flip_slot);
uint32_t *PS5_SYSV_ABI sceAgcDcbWaitFlip(SceAgcCb *cb, uint32_t flip_slot);
uint32_t *PS5_SYSV_ABI sceAgcDcbInsertWaitFlipDone(SceAgcCb *cb, uint32_t flip_slot);
uint32_t *PS5_SYSV_ABI sceAgcDcbWaitFlipEos(SceAgcCb *cb, uint32_t flip_slot);

/* ACB - async compute command buffer builders */
int32_t PS5_SYSV_ABI sceAgcAcbInitializeDefaultHardwareState_pre0090(
    uint32_t *acb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcAcbDispatchIndirect(
    uint32_t *acb, uint32_t size_dw, uintptr_t args);
int32_t PS5_SYSV_ABI sceAgcAcbAcquireMem(
    uint32_t *acb, uint32_t size_dw, uint32_t engine_sel,
    uint32_t coher_cntl, uint32_t coher_size, uint64_t coher_base);
int32_t PS5_SYSV_ABI sceAgcAcbAtomicGds(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src);
int32_t PS5_SYSV_ABI sceAgcAcbAtomicGds_pre0090(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src);
int32_t PS5_SYSV_ABI sceAgcAcbAtomicMem(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint64_t addr, uint64_t data);
int32_t PS5_SYSV_ABI sceAgcAcbCondExec(
    uint32_t *acb, uint32_t size_dw, uint64_t addr, uint32_t count);
int32_t PS5_SYSV_ABI sceAgcAcbCopyData(
    uint32_t *acb, uint32_t size_dw, uint32_t src_sel, uint32_t dst_sel,
    uint64_t src_addr, uint64_t dst_addr, uint32_t byte_count);
int32_t PS5_SYSV_ABI sceAgcAcbDmaData(
    uint32_t *acb, uint32_t size_dw, uint64_t src_addr, uint64_t dst_addr,
    uint32_t byte_count, uint32_t src_swap, uint32_t dst_swap);
int32_t PS5_SYSV_ABI sceAgcAcbEventWrite(
    uint32_t *acb, uint32_t size_dw, uint32_t event_type,
    uint64_t gpu_addr, uint32_t data, uint32_t int_ctx);
int32_t PS5_SYSV_ABI sceAgcAcbJump(uint32_t *acb, uint32_t size_dw, uintptr_t target);
int32_t PS5_SYSV_ABI sceAgcAcbMemSemaphore(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint64_t addr, uint64_t data);
int32_t PS5_SYSV_ABI sceAgcAcbPrimeUtcl2(
    uint32_t *acb, uint32_t size_dw, uint64_t addr, uint32_t size);
int32_t PS5_SYSV_ABI sceAgcAcbResetQueue(
    uint32_t *acb, uint32_t size_dw, uint32_t queue_id);
int32_t PS5_SYSV_ABI sceAgcAcbResetQueueInternal(
    uint32_t *acb, uint32_t size_dw, uint32_t queue_id);
int32_t PS5_SYSV_ABI sceAgcAcbRewind(uint32_t *acb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcAcbWaitRegMem(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint32_t ref,
    uint32_t mask, uint64_t addr, uint32_t func);
int32_t PS5_SYSV_ABI sceAgcAcbWaitUntilSafeForRendering(uint32_t *acb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcAcbWriteData(
    uint32_t *acb, uint32_t size_dw, uint32_t op, uint64_t addr, uint32_t data);

/* ACB workload/marker helpers */
int32_t PS5_SYSV_ABI sceAgcAcbSetFlip(
    uint32_t *acb, uint32_t size_dw, uint32_t vo_handle, uint32_t buf_idx,
    uint32_t vsync);
int32_t PS5_SYSV_ABI sceAgcAcbSetWorkloadComplete(
    uint32_t *acb, uint32_t size_dw, AgcWorkloadId workload);
int32_t PS5_SYSV_ABI sceAgcAcbSetWorkloadStreamInactive(
    uint32_t *acb, uint32_t size_dw, AgcWorkloadId workload);
int32_t PS5_SYSV_ABI sceAgcAcbSetWorkloadsActive(
    uint32_t *acb, uint32_t size_dw, uint32_t flags);
int32_t PS5_SYSV_ABI sceAgcAcbPushMarker(
    uint32_t *acb, uint32_t size_dw, const char *marker);
int32_t PS5_SYSV_ABI sceAgcAcbPopMarker(uint32_t *acb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcAcbSetMarker(
    uint32_t *acb, uint32_t size_dw, const char *marker);

/* DCB raw-buffer variant functions (version variants and specials) */
int32_t PS5_SYSV_ABI sceAgcDcbInitializeDefaultHardwareState(
    uint32_t *dcb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcDcbAtomicGds_0900(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src);
int32_t PS5_SYSV_ABI sceAgcDcbContextStateOp_pre0100(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data);
int32_t PS5_SYSV_ABI sceAgcDcbResetQueueInternal(
    uint32_t *dcb, uint32_t size_dw, uint32_t queue_id);
int32_t PS5_SYSV_ABI sceAgcDcbSetPreemption(
    uint32_t *dcb, uint32_t size_dw, uint32_t mode);

/* ===================================================================== */
/* Game-critical missing functions (from Joe & Mac game binary analysis)  */
/* ===================================================================== */

/* libSceAgcDriver non-Direct variants (games import these directly) */

/* Stub: returns AGC_ERROR_NOT_SUPPORTED (0x8a6c9018) per SPRX. */
int32_t PS5_SYSV_ABI sceAgcDriverRegisterOwner(void *resource, uint32_t *out_handle);
/* Stub: returns AGC_ERROR_NOT_SUPPORTED (0x8a6c9018) per SPRX. */
int32_t PS5_SYSV_ABI sceAgcDriverRegisterResource(void *resource, uint32_t owner_handle);
/* Returns the EQ (event queue) context ID. */
uint32_t PS5_SYSV_ABI sceAgcDriverGetEqContextId(void);
/* Non-Direct TF ring set (256-byte aligned address, size clamps to 0x4000). */
int32_t PS5_SYSV_ABI sceAgcDriverSetTFRing(uintptr_t ring_addr, uint32_t size);
/* Non-Direct HS offchip param set. */
int32_t PS5_SYSV_ABI sceAgcDriverSetHsOffchipParam(uint32_t pipe_id, uint64_t list_addr, uint32_t num_entries);
/* AGR (async graphics ring) DCB submit. */
int32_t PS5_SYSV_ABI sceAgcDriverAgrSubmitDcb(const AgcCommandBufferSubmit *packet);
/* Add an EQ event. */
int32_t PS5_SYSV_ABI sceAgcDriverAddEqEvent(void *eq, uint32_t type, void *event);

/* libSceAgc user-facing init (wrapper around sce_agc_initialize) */
int32_t PS5_SYSV_ABI sceAgcInit(uint32_t init_level, uint32_t flags, uint32_t *out_value);
int32_t PS5_SYSV_ABI sceAgcInit_0090(
    uint32_t init_level, uint32_t flags, uint32_t *out_value);

/* libSceAgc SuspendPoint wrapper (calls sceAgcDriverSuspendPointSubmit) */
int32_t PS5_SYSV_ABI sceAgcSuspendPoint(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3);

/* Register defaults version queries (NID: 2JtWUUiYBXs / wRbq6ZjNop4)
 * SPRX-confirmed: takes a version number, returns a pointer to the
 * register-defaults blob for that version. */
void *PS5_SYSV_ABI sceAgcGetRegisterDefaults2(uint32_t version);
void *PS5_SYSV_ABI sceAgcGetRegisterDefaults2Internal(uint32_t version);

/* DCB packet builders missing from our API */

/* IT_ACQUIRE_MEM for DCB. NID: 57labkp+rSQ */
uint32_t *PS5_SYSV_ABI sceAgcDcbAcquireMem(
    SceAgcCb *cb, uint32_t engine_sel, uint32_t coher_cntl,
    uint32_t coher_size, uint64_t coher_base);

/* IT_COPY_DATA for DCB. NID: 1rZSWUv1IRc */
uint32_t *PS5_SYSV_ABI sceAgcDcbCopyData(
    SceAgcCb *cb, uint32_t src_sel, uint32_t dst_sel,
    uint64_t src_addr, uint64_t dst_addr, uint32_t byte_count);

/* IT_INDIRECT_BUFFER for DCB jump. NID: xSAR0LTcRKM
 * RE: SPRX uses opcode 0x3F (INDIRECT_BUFFER), not 0x33 (IB_CNST).
 * 5 params: cb, queue_id, flags, target_addr, vmid. */
uint32_t *PS5_SYSV_ABI sceAgcDcbJump(
    SceAgcCb *cb, uint32_t queue_id, uint32_t flags,
    uint64_t target_addr, uint32_t vmid);

/* Queue reset for DCB. NID: TRO721eVt4g */
uint32_t *PS5_SYSV_ABI sceAgcDcbResetQueue(SceAgcCb *cb, uint32_t queue_id);

/* Set index count. NID: 8N2tmT3jmC8
 * RE: SPRX uses 2 dwords (not 3), clamps count to max(count, 1). */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexCount(SceAgcCb *cb, uint32_t index_count);

/* Set index size. NID: GIIW2J37e70
 * RE: SPRX uses opcode 0x7A (not 0x2A), 3 dwords with constant cmd[1]. */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexSize(
    SceAgcCb *cb, uint32_t index_type, uint32_t swap);

/* Set number of instances. NID: tSBxhAPyytQ */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetNumInstances(SceAgcCb *cb, uint32_t num_instances);

/* Stall command buffer parser. NID: u2T2DiA5hRI
 * RE: SPRX uses opcode 0x42 (not NOP+sub), 2 dwords. */
uint32_t *PS5_SYSV_ABI sceAgcDcbStallCommandBufferParser(SceAgcCb *cb);

/* Indexed draw. NID: q88lQ+GP5Yk
 * RE: SPRX field order: cmd[1]=max(count,1), cmd[4]=count, cmd[5]=draw_initiator. */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndex(
    SceAgcCb *cb, uint32_t index_count, uint64_t index_base_addr,
    uint32_t draw_initiator);

/* CB register range setters */
uint32_t *PS5_SYSV_ABI sceAgcCbSetShRegisterRangeDirect(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count);
uint32_t *PS5_SYSV_ABI sceAgcCbSetUcRegistersDirect(
    SceAgcCb *cb, const AgcRegisterValue *registers, uint32_t register_count);

/* Patcher functions for indirect register writes */
int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address);
int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count);
int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address);
int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count);
int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address);
int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count);

/* Utility functions.
 * RE: sceAgcSetNop takes 1 param (cmd), patches byte at offset 1 to 0x10,
 * returns NULL. sceAgcGetDataPacketPayload takes 3 params. */
uint32_t *PS5_SYSV_ABI sceAgcSetNop(uint32_t *cmd);
int32_t PS5_SYSV_ABI sceAgcDebugRaiseException(void);
uint32_t *PS5_SYSV_ABI sceAgcGetDataPacketPayload(
    uint64_t *out_addr, uint32_t *cmd, uint32_t skip_header);
int32_t PS5_SYSV_ABI sceAgcGetDataPacketPayloadAddress_0090(
    uint64_t *out_addr, uint32_t *cmd, uint32_t skip_header);

/* Shader and primitive state creation. */
int32_t PS5_SYSV_ABI sceAgcCreateShader(void *shader_record, uint32_t type);
/* FW 5.50 D9sr1xGUriE: emits two CX and three UCONFIG register/value pairs.
 * Either output may be NULL. The hull shader is optional; the geometry
 * shader and every supplied shader must have a valid Specials block when an
 * output is requested. */
int32_t PS5_SYSV_ABI sceAgcCreatePrimState(
    AgcShaderRegister *cx_registers,
    AgcShaderRegister *uconfig_registers,
    const AgcShaderRecord *hull_shader,
    const AgcShaderRecord *geometry_shader,
    uint32_t primitive_type);
/* FW 5.50 pdEV7bI6COI: emits all 32 SPI_PS_INPUT_CNTL descriptors by
 * matching pixel inputs to geometry outputs and transforming interpolation
 * and default-value flags. */
int32_t PS5_SYSV_ABI sceAgcCreateInterpolantMapping(
    AgcShaderRegister *cx_registers,
    const AgcShaderRecord *geometry_shader,
    const AgcShaderRecord *pixel_shader);
int32_t PS5_SYSV_ABI sceAgcCreateInterpolantMapping_0100(
    AgcShaderRegister *cx_registers,
    const AgcShaderRecord *geometry_shader,
    const AgcShaderRecord *pixel_shader);

/* ===================================================================== */
/* DCB packet builders — SPRX disassembly batch 2 (FW 5.50)              */
/* ===================================================================== */

/* AGC-custom clear state. NID: PxEFhy0d5v8
 * 2 dwords: [0] header 0xc0001200, [1] flags&0xf */
uint32_t *PS5_SYSV_ABI sceAgcDcbClearState(SceAgcCb *cb, uint32_t flags);

/* Rewind command buffer. NID: zfcxg-ewMK8
 * 2 dwords: [0] header 0xc0005900, [1] flags<<31 */
uint32_t *PS5_SYSV_ABI sceAgcDcbRewind(SceAgcCb *cb, uint32_t flags);

/* Conditional execute. NID: BIPexNBSGog
 * 5 dwords: [0] header 0xc0032200, [1] addr_lo&~3, [2] addr_hi,
 * [3] 0, [4] count&0x3fff */
uint32_t *PS5_SYSV_ABI sceAgcDcbCondExec(
    SceAgcCb *cb, uint64_t address, uint32_t count);

/* Atomic memory operation. NID: 1-gUn1PI4Sw
 * 9 dwords: [0] header 0xc0071e00, [1] packed control, [2] addr_lo,
 * [3] addr_hi, [4] data_lo, [5] data_hi, [6] cmp_lo, [7] cmp_hi,
 * [8] loop_count */
uint32_t *PS5_SYSV_ABI sceAgcDcbAtomicMem(
    SceAgcCb *cb, uint32_t op, uint32_t loop_count, uint32_t atomic_op,
    uint64_t address, uint64_t data, uint64_t compare);

/* Atomic GDS operation. NID: pH3-dfRpfA0
 * 11 dwords: [0] header 0xc0091d00, [1..2] packed control+addr,
 * [3..4] data, [5] mask, [6] addr2, [7..8] cmp_data, [9..10] extra */
uint32_t *PS5_SYSV_ABI sceAgcDcbAtomicGds(
    SceAgcCb *cb, uint32_t op, uint32_t gds_op, uint32_t src,
    uint32_t data, uint16_t offset, uint16_t index, uint32_t loop_count,
    uint64_t cmp_data, uint32_t mask);

/* Memory semaphore. NID: G0jrLdvEqDw
 * 4 dwords: [0] header 0xc0023900, [1] addr_lo&~7, [2] addr_hi,
 * [3] packed (op<<29)|(wait<<16)|(signal<<20) */
uint32_t *PS5_SYSV_ABI sceAgcDcbMemSemaphore(
    SceAgcCb *cb, uint64_t address, uint32_t wait, uint32_t signal,
    uint32_t op);

/* Prime UTCL2. NID: jt3pl7EN17o
 * 5 dwords: [0] header 0xc0035d00, [1] packed control, [2] addr_lo,
 * [3] addr_hi, [4] reserved */
uint32_t *PS5_SYSV_ABI sceAgcDcbPrimeUtcl2(
    SceAgcCb *cb, uint32_t cache_policy, uint32_t flags,
    uint64_t address, uint32_t reserved);

/* Set index indirect args. NID: 0o3VDdtA6nM
 * 4 dwords: [0] header 0xc0029100, [1] addr_lo&~0xf, [2] addr_hi,
 * [3] offset&0xffff */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexIndirectArgs(
    SceAgcCb *cb, uint64_t address, uint32_t offset);

/* Draw index multi-instanced. NID: Rlx+bykm0r0
 * Variable length: 9 + count dwords. */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndexMultiInstanced(
    SceAgcCb *cb, uint32_t index_count, uint64_t index_base_addr,
    uint32_t instance_count, uint32_t draw_initiator,
    const uint32_t *instance_data, uint32_t data_count);

/* Set marker. NID: QhCbS4X9Rl8
 * Variable length: depends on marker string. */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetMarker(
    SceAgcCb *cb, const char *marker, uint32_t flags);

/* Context state operation. NID: HabmgqPwPw0
 * op 0: CLEAR_STATE (2 dwords)
 * op 1: SET_CONTEXT_REG (3 dwords)
 * op 2: SET_SH_REG_INDIRECT (5 dwords)
 * op 3: CLEAR_STATE + SET_CONTEXT_REG_INDIRECT */
uint32_t *PS5_SYSV_ABI sceAgcDcbContextStateOp(
    SceAgcCb *cb, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data);

/* DCB workload helpers (delegate to ACB-style packets) */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetWorkloadsActive(
    SceAgcCb *cb, uint32_t flags, const void *data, uint32_t data_size);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetWorkloadComplete(
    SceAgcCb *cb, uint32_t workload_id, uint32_t flags);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetWorkloadStreamInactive(
    SceAgcCb *cb, uint32_t workload_id);

/* ===================================================================== */
/* DCB register direct setters — single register writes                  */
/* ===================================================================== */

/* 3 dwords each: [0] header, [1] reg_offset&0xffff, [2] value */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetCfRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetCxRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetShRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value);
uint32_t *PS5_SYSV_ABI sceAgcDcbSetUcRegisterDirect(
    SceAgcCb *cb, uint64_t reg_offset_and_value);

/* Variable-length range setters: 2 + count dwords */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetCfRegisterRangeDirect(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count);
uint32_t *PS5_SYSV_ABI sceAgcCbSetUcRegisterRangeDirect(
    SceAgcCb *cb, uint16_t reg_offset, const uint32_t *values, uint32_t count);

/* ===================================================================== */
/* CB builders — branch, cond write, semaphore                           */
/* ===================================================================== */

/* CB branch (INDIRECT_BUFFER). NID: w1KFAHVqpaU
 * 14 dwords: [0] header 0xc00c3f00, [1] packed control, [2..13] packed
 * addr/size/engine params. 12 arguments per SPRX disassembly. */
uint32_t *PS5_SYSV_ABI sceAgcCbBranch(
    SceAgcCb *cb, uint32_t flags, uint32_t ctrl, uint64_t target_addr,
    uint64_t src_data, uint64_t dst_data, uint8_t dst_engine,
    uint64_t addr2, uint32_t size1, uint8_t src_engine,
    uint64_t addr3, uint32_t size2);

/* CB conditional write. NID: 7toV+elXqNM
 * 9 dwords: [0] header 0xc0074500, [1] packed control,
 * [2..3] ref (64-bit), [4] mask, [5] reserved, [6..7] address (64-bit),
 * [8] write_data */
uint32_t *PS5_SYSV_ABI sceAgcCbCondWrite(
    SceAgcCb *cb, uint32_t compare_function, uint32_t write_enable,
    uint64_t address, uint32_t write_data, uint64_t ref,
    uint32_t mask, uint32_t reserved);

/* CB memory semaphore. NID: vHX9guneRBY
 * 4 dwords: [0] header 0xc0023900, [1] addr_lo&~7, [2] addr_hi,
 * [3] packed (signal<<29)|(wait<<16)|(op<<20) */
uint32_t *PS5_SYSV_ABI sceAgcCbMemSemaphore(
    SceAgcCb *cb, uint64_t address, uint32_t wait, uint32_t signal,
    uint32_t op);

/* ===================================================================== */
/* WaitRegMem patchers — patch fields in an emitted WAIT_REG_MEM packet  */
/* ===================================================================== */

/* Patch compare function (bits 2:0 of cmd[4]). NID: n485EBnIWmk */
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchCompareFunction(
    uint32_t *cmd, uint8_t compare_function);

/* Patch mask value (cmd[5] or cmd[6] depending on 32/64-bit). NID: hXAnLgDHCoI */
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchMask(
    uint32_t *cmd, uint32_t mask);

/* Patch reference value (cmd[4]). NID: 7nOoijNPvEU */
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchReference(
    uint32_t *cmd, uint32_t reference);

/* ===================================================================== */
/* reference-confirmed patchers and helpers                               */
/* ===================================================================== */

/* Get packet size in dwords from a PM4 header. NID: Lkf86B98qPc
 * reference-confirmed: returns ((header >> 16) & 0x3FFF) + 2, except
 * for special NOP packets (header & 0x3FFFFF00 == 0x3FFF1000) which
 * return 1. */
uint32_t PS5_SYSV_ABI sceAgcGetPacketSize(uint32_t *packet);

/* Set predication bit (bit 0) on a packet header. NID: w6Dj1VJt5qY
 * reference-confirmed: sets/clears bit 0 of cmd[0]. */
int32_t PS5_SYSV_ABI sceAgcSetPacketPredication(
    uint32_t *packet, uint32_t predication);

/* Set predication bit across a range of packets. NID: n8vgpaQg6dA
 * reference-confirmed: walks packets from start to end, setting bit 0
 * of each packet header. */
int32_t PS5_SYSV_ABI sceAgcSetRangePredication(
    uint32_t *start, const uint32_t *end, uint32_t predication);

/* Patch CondExec end address (cmd[4] bits 13:0 = dword count). NID: ORWsxIbk4TE
 * reference-confirmed: patches cmd[4] with (end - cmd - 5) dwords. */
int32_t PS5_SYSV_ABI sceAgcCondExecPatchSetEnd(
    uint32_t *cmd, const uint32_t *end);

/* Patch CondExec command address (cmd[1..2]). NID: YWTKOju587o
 * reference-confirmed: patches cmd[1] lo and cmd[2] hi, preserving
 * cmd[1] bits 1:0. */
int32_t PS5_SYSV_ABI sceAgcCondExecPatchSetCommandAddress(
    uint32_t *cmd, const uint32_t *command);

/* Patch WriteData address (cmd[2..3]). NID: fPSCdQxgpSw
 * reference-confirmed: patches cmd[2] lo and cmd[3] hi for IT_WRITE_DATA. */
int32_t PS5_SYSV_ABI sceAgcWriteDataPatchSetAddressOrOffset(
    uint32_t *cmd, uint64_t address_or_offset);

/* Patch Jump target address and size (cmd[1..3]). NID: 2BS4EtAaF28
 * reference-confirmed: patches IT_INDIRECT_BUFFER cmd[1] lo,
 * cmd[2] hi (bits 15:0), cmd[3] size (bits 19:0). */
int32_t PS5_SYSV_ABI sceAgcJumpPatchSetTarget(
    uint32_t *cmd, const uint32_t *target, uint32_t size_in_dwords);

/* SetNumRegisters patchers for indirect register packets. NIDs:
 * Cx: whb1RL7K4Ss, Sh: nCUgItdN2ms, Uc: fRG-JOH5+sI
 * reference-confirmed: patches cmd[4] bits 13:0 with num_regs. */
int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchSetNumRegisters(
    uint32_t *cmd, uint32_t num_regs);
int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchSetNumRegisters(
    uint32_t *cmd, uint32_t num_regs);
int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchSetNumRegisters(
    uint32_t *cmd, uint32_t num_regs);

/* ===================================================================== */
/* GetSize helpers — reference-confirmed packet sizes                      */
/* ===================================================================== */

/* NID: p9tI+yTvx68 — returns 4*num_dwords + 16 (bytes) */
uint32_t PS5_SYSV_ABI sceAgcDcbWriteDataGetSize(uint32_t num_dwords);

/* NID: VEGu4dixjUg — returns 16 (bytes, = 4 dwords) */
uint32_t PS5_SYSV_ABI sceAgcDcbJumpGetSize(void);

/* NID: QIXCsbipds0 — returns 8 (bytes, = 2 dwords) */
uint32_t PS5_SYSV_ABI sceAgcDcbRewindGetSize(void);

/* NID: ou16V5hh5sg — returns 20 (bytes, = 5 dwords) */
uint32_t PS5_SYSV_ABI sceAgcDcbCondExecGetSize(void);

/* NID: ozKzBP4aki4 — returns 20 (bytes, = 5 dwords) */
uint32_t PS5_SYSV_ABI sceAgcAcbCondExecGetSize(void);

/* NID: 43WJ08sSugE — returns 14*4 (32-bit) or 16*4 (64-bit) bytes */
uint32_t PS5_SYSV_ABI sceAgcDcbWaitOnAddressGetSize(uint32_t size);

/* FW 5.50 parameter-dependent packet-size helpers. */
uint32_t PS5_SYSV_ABI sceAgcCbNopGetSize(uint32_t num_dwords);
uint32_t PS5_SYSV_ABI sceAgcCbSetShRegisterRangeDirectGetSize(
    uint32_t num_registers);
uint32_t PS5_SYSV_ABI sceAgcCbSetShRegistersDirectGetSize(
    uint32_t num_registers);
uint32_t PS5_SYSV_ABI sceAgcCbSetUcRegisterRangeDirectGetSize(
    uint32_t num_registers);
uint32_t PS5_SYSV_ABI sceAgcCbSetUcRegistersDirectGetSize(
    uint32_t num_registers);
uint32_t PS5_SYSV_ABI sceAgcAcbWaitOnAddressGetSize(uint32_t size);
uint32_t PS5_SYSV_ABI sceAgcDcbBeginOcclusionQueryGetSize(uint32_t query_type);
uint32_t PS5_SYSV_ABI sceAgcDcbContextStateOpGetSize(uint32_t operation);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexIndirectMultiGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexMultiInstancedGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndirectMultiGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbEventWriteGetSize(uint32_t event_type);

/* Returns the FW 5.50 user-data packet size in dwords. */
uint32_t PS5_SYSV_ABI sceAgcDriverUserDataGetPacketSize(uint32_t size_in_bytes);

/* FW 5.50 constant packet-size helpers. */
uint32_t PS5_SYSV_ABI sceAgcAcbAtomicGdsGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbAtomicMemGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbCopyDataGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbDispatchIndirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbDmaDataGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbEventWriteGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbJumpGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbPrimeUtcl2GetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbQueueEndOfShaderActionGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcAcbRewindGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcCbBranchGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcCbCondWriteGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcCbDispatchGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbAtomicGdsGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbAtomicMemGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbCopyDataGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDispatchIndirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDmaDataGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexAutoGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexIndirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndexOffsetGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbDrawIndirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbEndOcclusionQueryGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbPrimeUtcl2GetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbQueueEndOfShaderActionGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetBaseDispatchIndirectArgsGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetBaseDrawIndirectArgsGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetBoolPredicationEnableGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetCxRegisterDirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetCxRegistersIndirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetIndexBufferGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetIndexCountGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetIndexIndirectArgsGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetIndexSizeGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetNumInstancesGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetPredicationDisableGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetShRegisterDirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetShRegistersIndirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetUcRegisterDirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetUcRegistersIndirectGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbSetZPassPredicationEnableGetSize(void);
uint32_t PS5_SYSV_ABI sceAgcDcbStallCommandBufferParserGetSize(void);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_DRIVER_H_ */
