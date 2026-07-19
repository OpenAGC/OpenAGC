# Implemented Packet Builders & Prospero Backend

Full list in STATUS.md "Implemented Packet Builders". Summary:

## Cursor-based CB/DCB builders (src/cb_builders.c)
NOP, Dispatch, SetShRegistersDirect, SetCxRegistersDirect, WriteData, WaitRegMem, DmaData, SetBaseIndirectArgs, DispatchIndirect, SetIndexBuffer, DrawIndexOffset, DrawIndexAuto, WaitUntilSafeForRendering, PushMarker, PopMarker, SetFlip, SetEopFlip (IT_RELEASE_MEM 0x49, 8 dwords), ReleaseMem, SetShRegistersIndirect (0x63, 5 dwords), SetCxRegistersIndirect (0x9F, 5 dwords), SetUcRegistersIndirect (0x64, 5 dwords).

## ACB builders (src/acb.c) — all emit real PM4, no stubs
InitializeDefaultHardwareState_pre0090, DispatchIndirect, AcquireMem (0x58, 8dw), EventWrite (0x47, 5dw), AtomicMem (0x1B, 5dw), CondExec (0x22, 4dw), WaitRegMem (0x3C, 6dw), WriteData (0x37, 5dw), CopyData (0x40, 6dw), MemSemaphore (0x39, 4dw), DmaData (0x50, 8dw), ResetQueue (0x79, 3dw), Rewind (0x10, 2dw), SetFlip (0x49, 7dw), SetWorkloadComplete (0x1E, 8dw), SetWorkloadStreamInactive (0x79, 3dw), SetWorkloadsActive (0x1E, 8dw), AtomicGds (0x1D, 10dw), PrimeUtcl2 (0x5D, 4dw), Jump (0x3F, 4dw), PushMarker/PopMarker/SetMarker.

## VSH DCB builders (src/dcb.c)
ClearState (0x14, 2dw), AtomicGds, ContextStateOp (variable SET_*_REG), ResetQueue, SetWorkloadComplete, SetWorkloadStreamInactive, SetWorkloadsActive, WaitUntilSafeForRendering, SetPreemption (returns AGC_ERROR_INVALID_STATE — SPRX-confirmed VSH-only stub that crashes via int 0x41).

## DCB batch-2 builders (SPRX deep disassembly)
ClearState (0x12, 2dw), Rewind (0x59, 2dw), CondExec (0x22, 5dw), SetIndexIndirectArgs (0x91, 4dw), AtomicMem (0x1E, 9dw), AtomicGds (0x1D, 11dw), MemSemaphore (0x39, 4dw), PrimeUtcl2 (0x5D, 5dw), DrawIndexMultiInstanced (0x3A, 9+count dw), SetMarker, ContextStateOp, SetWorkloadsActive/SetWorkloadComplete/SetWorkloadStreamInactive.

## DCB register direct setters (3 dwords each)
SetCfRegisterDirect (0x68), SetCxRegisterDirect (0x69), SetShRegisterDirect (0x76), SetUcRegisterDirect (0x79), SetCfRegisterRangeDirect (variable), SetUcRegisterRangeDirect (variable).

## CB builders (SPRX-confirmed)
Branch (IT_INDIRECT_BUFFER 0x3F, 14 dwords, 12-arg), CondWrite (IT_COND_WRITE 0x45, 9 dwords), MemSemaphore (0x39, 4 dwords).

## In-place patchers
DmaDataPatchSetDstAddressOrOffset, DmaDataPatchSetSrcAddressOrOffsetOrImmediate (SPRX: checks raw 0x50, patches cmd[2..3]), WaitRegMemPatchAddress, QueueEndOfPipeActionPatchAddress, SetShRegIndirectPatchSetAddress/AddRegisters (0x63), SetCxRegIndirectPatchSetAddress/AddRegisters (0x9F), SetUcRegIndirectPatchSetAddress/AddRegisters (0x64). WaitRegMem patchers require 0x79 wrapper, use adjusted pointer.

## Game-compat driver functions
RegisterOwner/RegisterResource (stub 0x8a6c9018), GetEqContextId, SetTFRing (clamps 0x4000), SetHsOffchipParam, AgrSubmitDcb (0x8a6d0003 if not init), AddEqEvent.

## Game-compat wrappers
sceAgcInit (delegates to sce_agc_initialize), sceAgcSuspendPoint, sceAgcGetRegisterDefaults2/2Internal.

## LOD stats
sceAgcDcbGetLodStatsGetSize, sceAgcDcbGetLodStats.

## Prospero backend (src/driver_prospero.c, #ifdef OPENAGC_PROSPERO)
- sce_agc_initialize — open /dev/gc + CONTEXT_QUERY (0xc004812e) + mmap @0xfe0200000
- sce_agc_initialize_internal_memory — 9 regions via sceKernelMapNamedSystemFlexibleMemory (type=0x33)
- sceAgcDriverSubmitMultiCommandBuffersDirect — CB descriptors + SUBMIT_PID
- sceAgcDriverSubmitDcb / SubmitAcb — single submit via SUBMIT_PID
- sceAgcDriverSubmitEopFlip — validates display buf idx <16, delegates to sceVideoOutSubmitEopFlip
- sceAgcDriverSetupAsyncGraphics — QUEUE_STATUS (nr=0x26, arg=1)
- sceAgcDriverSetTFRingDirect — SET_TF_RING (user arg ignored on 5.50)
- sceAgcDriverSetHsOffchipParamDirect — SET_HS_OFFCHIP (16-byte patch-list arg)
- sceAgcDriverGetPaDebugInterfaceVersion — PADEBUG_4 (returns EPERM — separate kernel perm check)
- _sceAgcDriverCreateUserSpecialQueue — QUEUE_CREATE (nr=0x21, 64-byte RW, magic tokens, ring from EOP FIFO + 0x39000)
- _sceAgcDriverDestroyUserSpecialQueue — QUEUE_DESTROY (nr=0x0e, 12-byte RW, 3 magic tokens)
- sceAgcDriverNotifyDefaultStates — builds primary/internal register-defaults blobs in GPU mem + IT_CLEAR_STATE DCB
- sceAgcDriverSuspendPointSubmitDirect — SUSPEND_16 (0xc010811c, 4-dword arg)
- sceAgcDriverIsSuspendPointInFlightDirect — QUEUE_STAT_16 (nr=0x27)
- sceAgcSuspendPointAndCheckStatus — suspend + in-flight query
- sce_agc_internal_suspend_point_submit_final — SUSPEND_39 (nr=0x39)
- sceAgcDriverBeginWorkload/EndWorkload — SET_WORKLOAD (0x1E) submit
- CB descriptor builder (AgcGcCommandBuffer + VMID masking), queue tracking (32 slots, gfx/compute/dma)