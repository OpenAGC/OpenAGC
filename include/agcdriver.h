#ifndef _AGC_DRIVER_H_
#define _AGC_DRIVER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "agc_error.h"
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
int32_t PS5_SYSV_ABI sceAgcDriverBeginWorkload(uint32_t workload_id);
int32_t PS5_SYSV_ABI sceAgcDriverEndWorkload(uint32_t workload_id);

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
uint32_t PS5_SYSV_ABI sceAgcDriverGetPaDebugInterfaceVersion(void);

/* Default state queries */
int32_t PS5_SYSV_ABI sceAgcGetDefaultState(AgcContextState *out_state);
int32_t PS5_SYSV_ABI sceAgcGetGameDefaultState(AgcContextState *out_state);
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
    SceAgcCb *cb, uint32_t index_offset, uint32_t index_count, uint32_t flags);
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
int32_t PS5_SYSV_ABI sceAgcWaitRegMemPatchAddress(
    uint32_t *cmd, uint64_t address);
int32_t PS5_SYSV_ABI sceAgcQueueEndOfPipeActionPatchAddress(
    uint32_t *cmd, uint64_t address);

/* LOD stats helpers */
size_t PS5_SYSV_ABI sceAgcDcbGetLodStatsGetSize(uint32_t counter_count);
uint32_t *PS5_SYSV_ABI sceAgcDcbGetLodStats(
    SceAgcCb *cb, uint32_t cache_policy, uint64_t destination_address,
    uint32_t control, uint32_t counter_mask, uint32_t reset_counters,
    uint32_t enable, uint32_t counter_select);

/* DCB draw/indirect/register packet builders (SPRX capstone disassembly) */

/* IT_RELEASE_MEM (opcode 0x49) — 7-dword EOP signal write. NID: wr23dPKyWc0 */
uint32_t *PS5_SYSV_ABI sceAgcDcbReleaseMem(
    SceAgcCb *cb, uint32_t event_type, uint32_t event_index,
    uint64_t dst_addr, uint32_t data);

/* IT_INDIRECT_BUFFER (opcode 0x3F) — 4-dword IB chain. NID: w1KFAHVqpaU */
uint32_t *PS5_SYSV_ABI sceAgcDcbIndirectBuffer(
    SceAgcCb *cb, uint64_t gpu_addr, uint32_t size_dwords, uint32_t vmid);

/* IT_INDIRECT_BUFFER_CNST (opcode 0x33) — 4-dword constant IB. NID: xSAR0LTcRKM */
uint32_t *PS5_SYSV_ABI sceAgcDcbIndirectBufferConst(
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

/* VshDcb - VSH draw command buffer builders */
int32_t PS5_SYSV_ABI sceAgcVshDcbInitializeDefaultHardwareState_pre0090(
    uint32_t *dcb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcVshDcbClearState(uint32_t *dcb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcVshDcbAtomicGds(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src);
int32_t PS5_SYSV_ABI sceAgcVshDcbAtomicGds_pre0090(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t gds_offset,
    uint32_t data, uint32_t src);
int32_t PS5_SYSV_ABI sceAgcVshDcbContextStateOp(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data);
int32_t PS5_SYSV_ABI sceAgcVshDcbContextStateOp_pre0100(
    uint32_t *dcb, uint32_t size_dw, uint32_t op, uint32_t reg_type,
    uint32_t reg_offset, uint32_t reg_count, const void *reg_data);
int32_t PS5_SYSV_ABI sceAgcVshDcbMemSemaphore(uint32_t *dcb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcVshDcbResetQueue(
    uint32_t *dcb, uint32_t size_dw, uint32_t queue_id);
int32_t PS5_SYSV_ABI sceAgcVshDcbResetQueueInternal(
    uint32_t *dcb, uint32_t size_dw, uint32_t queue_id);
int32_t PS5_SYSV_ABI sceAgcVshDcbSetPreemption(
    uint32_t *dcb, uint32_t size_dw, uint32_t mode);
int32_t PS5_SYSV_ABI sceAgcVshDcbWaitUntilSafeForRendering(uint32_t *dcb, uint32_t size_dw);
int32_t PS5_SYSV_ABI sceAgcVshDcbSetFlip(
    uint32_t *dcb, uint32_t size_dw, uint32_t vo_handle, uint32_t buf_idx);
int32_t PS5_SYSV_ABI sceAgcVshDcbSetWorkloadComplete(
    uint32_t *dcb, uint32_t size_dw, AgcWorkloadId workload);
int32_t PS5_SYSV_ABI sceAgcVshDcbSetWorkloadStreamInactive(
    uint32_t *dcb, uint32_t size_dw, AgcWorkloadId workload);
int32_t PS5_SYSV_ABI sceAgcVshDcbSetWorkloadsActive(
    uint32_t *dcb, uint32_t size_dw, uint32_t flags);

/* VshCb - shared VSH command buffer helpers */
int32_t PS5_SYSV_ABI sceAgcVshCbMemSemaphore(uint32_t *cb, uint32_t size_dw);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_DRIVER_H_ */
