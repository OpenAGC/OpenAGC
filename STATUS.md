# openagc Status

## Current Milestone

Reverse-engineering foundation for PS5 Gen5 AGC packet construction.

See [PLAN.md](PLAN.md) for the broader GNM-to-AGC architecture roadmap,
including Wave32, geometry, ray tracing, cache synchronization, and VRS targets.

The host-generic implementation now has a tested model for:

- Type-3 AGC/PM4 packet headers using `length_dwords - 2` in bits `29:16`
- AGC `IT_NOP` subcommands recovered from SharpEmu
- Known Gen5 AGC NID constants for mapped exports
- `SceAgcCb` cursor offsets and cursor allocation
- `sceAgcCb*` and `sceAgcDcb*` cursor-based packet builders
- DCB/ACB submit descriptor layout
- Generic submit validation and debug capture
- AGC shader record parser (magic, pointer fields, semantics counts, shader type)
- FW 5.50 register-defaults blob builder/parser with embedded primary/internal tables

## Verified

Host generic backend:

```sh
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
make -B test
```

Expected result:

```text
531 passed, 0 failed
```

PS5 orbis backend (cross-compiled, no tests):

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
cmake -B build-orbis -DOPENAGC_PLATFORM=orbis -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-orbis
```

Expected result: `build-orbis/libopenagc.a` — PS5 x86_64 static library,
zero warnings, zero errors. All `sceAgcDriver*` and `sce_agc_*` symbols
present in the symbol table. Hardware validation pending.

### PS5 packaging (LibProsperoPkg)

Build the C++ packaging tools (one-time):

```sh
cmake -S /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar \
      -B /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build \
      -DCMAKE_BUILD_TYPE=Release -DLIBPROSPEROPKG_BUILD_TOOLS=ON
cmake --build /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build --parallel
```

Deploy as installable `.pkg` (debug-mode PS5):

```sh
PKG_TOOLS=/Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build
$PKG_TOOLS/prosperopkg-fself payload.elf payload.self
$PKG_TOOLS/prosperopkg-gp5 app_dir out.gp5 --flat --type app
```

Or deploy as ELF payload (exploited PS5):

```sh
prospero-deploy -h $PS5_HOST -p $PS5_PORT payload.elf
```

## Implemented Packet Builders

Cursor-based builders:

- `sceAgcCbNop`
- `sceAgcCbDispatch`
- `sceAgcCbSetShRegistersDirect`
- `sceAgcCbSetCxRegistersDirect`
- `sceAgcDcbWriteData`
- `sceAgcDcbWaitRegMem`
- `sceAgcDcbDmaData`
- `sceAgcDcbSetBaseIndirectArgs`
- `sceAgcDcbDispatchIndirect`
- `sceAgcDcbSetIndexBuffer`
- `sceAgcDcbDrawIndexOffset`
- `sceAgcDcbDrawIndexAuto`
- `sceAgcDcbWaitUntilSafeForRendering`
- `sceAgcDcbPushMarker`
- `sceAgcDcbPopMarker`
- `sceAgcDcbSetFlip`
- `sceAgcCbReleaseMem`
- `sceAgcDcbSetShRegistersIndirect`
- `sceAgcDcbSetCxRegistersIndirect`
- `sceAgcDcbSetUcRegistersIndirect`

Old-style ACB stubs (from `src/acb.c`):

- `sceAgcAcbInitializeDefaultHardwareState_pre0090`
- `sceAgcAcbDispatchIndirect`
- `sceAgcAcbAcquireMem` — now emits `IT_ACQUIRE_MEM` (opcode 0x58) with 8-dword packet
- `sceAgcAcbEventWrite` — now emits `IT_EVENT_WRITE_EOP` (opcode 0x47) with 5-dword packet
- `sceAgcAcbAtomicMem` — now emits `IT_ATOMIC_MEM` (opcode 0x1B) with 5-dword packet
- `sceAgcAcbCondExec` — now emits `IT_COND_EXEC` (opcode 0x22) with 4-dword packet
- `sceAgcAcbWaitRegMem` — now emits `IT_WAIT_REG_MEM` (opcode 0x3C) with 6-dword packet
- `sceAgcAcbWriteData` — now emits `IT_WRITE_DATA` (opcode 0x37) with 5-dword packet
- `sceAgcAcbCopyData` — now emits `IT_COPY_DATA` (opcode 0x40) with 6-dword packet
- `sceAgcAcbMemSemaphore` — now emits `IT_MEM_SEMAPHORE` (opcode 0x39) with 4-dword packet
- `sceAgcAcbDmaData` — now emits `IT_DMA_DATA` (opcode 0x50) with 8-dword packet
- `sceAgcAcbResetQueue` — now emits `IT_AGC_0x79` (opcode 0x79) with 3-dword packet
- `sceAgcAcbRewind` — now emits `IT_NOP` (opcode 0x10) with 2-dword packet
- `sceAgcAcbSetFlip` — now emits `IT_RELEASE_MEM` (opcode 0x49) with 7-dword packet
- `sceAgcAcbSetWorkloadComplete` — now emits `IT_SET_WORKLOAD` (opcode 0x1E) with 8-dword packet
- `sceAgcAcbSetWorkloadStreamInactive` — now emits `IT_AGC_0x79` (opcode 0x79) with 3-dword packet
- `sceAgcAcbSetWorkloadsActive` — now emits `IT_SET_WORKLOAD` (opcode 0x1E) with 8-dword packet
- `sceAgcAcbAtomicGds` — now emits `IT_ATOMIC_GDS` (opcode 0x1D) with 10-dword packet
- `sceAgcAcbPrimeUtcl2` — now emits `IT_PRIME_UTCL2` (opcode 0x5D) with 4-dword packet
- `sceAgcAcbJump`
- `sceAgcAcbPushMarker` / `sceAgcAcbPopMarker` / `sceAgcAcbSetMarker`
- `sceAgcVshDcbClearState` — now emits `IT_CLEAR_STATE` (opcode 0x14) with 2-dword packet
- `sceAgcVshDcbAtomicGds` / `ContextStateOp` / `ResetQueue` /
  `SetWorkloadComplete` / `SetWorkloadStreamInactive` / `SetWorkloadsActive` /
  `WaitUntilSafeForRendering`
- `sceAgcVshDcbSetPreemption` — SPRX RE shows it is an intentional VSH-only
  stub that crashes; openagc returns `AGC_ERROR_INVALID_STATE`

Default state submission:

- `sceAgcDriverNotifyDefaultStates` (orbis) now builds the primary/internal
  register-defaults blobs in GPU-visible memory and submits an
  `IT_CLEAR_STATE` (0x14) DCB to load them.

Suspend points:

- `sceAgcDriverIsSuspendPointInFlightDirect` (orbis) now queries the gfx
  queue status via ioctl `nr=0x27` and returns whether the status is non-zero.
- `sceAgcSuspendPointAndCheckStatus` combines a direct suspend-point submit
  with the in-flight query.

In-place patchers:

- `sceAgcDmaDataPatchSetDstAddressOrOffset`
- `sceAgcWaitRegMemPatchAddress`
- `sceAgcQueueEndOfPipeActionPatchAddress`

LOD stats helpers:

- `sceAgcDcbGetLodStatsGetSize`
- `sceAgcDcbGetLodStats`

Ioctl / submit / queue layer (FW 5.50 kernel RE, `include/agc_ioctl.h`):

- 76 ioctl command constants (IOC-encoded, nr enum)
- `AgcGcSubmitArgs` submit ioctl arg struct (24 bytes)
- `AgcGcCommandBuffer` CB descriptor (16 bytes)
- `AgcGcFrameOpenArg`, `AgcGcMakesysmapArg8/12/48`
- `AgcGcSuspendArg` — 16-byte layout with four dwords (RE'd from kernel handler at 0x6e6ff0)
- `AgcGcSetHsOffchipArg` — 16-byte patch-list pointer/count (RE'd from kernel handler at 0x6ee6d2)
- CB header opcodes, VMID layout, num_cbs/VMID ranges
- Kernel-side error codes (module 0x4C)
- Kernel function offsets (ioctl_internal, submit_with_pid, frame_submit)

Native orbis backend (`src/driver_orbis.c`, `#ifdef OPENAGC_ORBIS`):

- `sce_agc_initialize` — opens `/dev/gc`, calls `FRAME_OPEN` ioctl
- `sce_agc_initialize_internal_memory` — allocates 8 named regions via `sceKernelAllocateDirectMemory` + `sceKernelMapDirectMemory` + `MAKESYSMAP_8`
- `sceAgcDriverSubmitMultiCommandBuffersDirect` — builds CB descriptors, calls `SUBMIT_PID`
- `sceAgcDriverSubmitDcb` — single DCB submit via `SUBMIT_PID`
- `sceAgcDriverSubmitAcb` — single ACB submit (const IB type) via `SUBMIT_PID`
- `sceAgcDriverSetupAsyncGraphics` — `SETUP_ASYNC` ioctl
- `sceAgcDriverSetTFRingDirect` — `SET_TF_RING` ioctl (user arg ignored on FW 5.50)
- `sceAgcDriverSetHsOffchipParamDirect` — `SET_HS_OFFCHIP` ioctl with RE'd 16-byte patch-list argument
- `sceAgcDriverGetPaDebugInterfaceVersion` — `PADEBUG_4` ioctl
- `_sceAgcDriverCreateUserSpecialQueue` — `QUEUE_CREATE` ioctl
- `_sceAgcDriverDestroyUserSpecialQueue` — `QUEUE_DESTROY` ioctl
- `sceAgcDriverNotifyDefaultStates` — takes `uint32_t flags`; builds FW 5.50 primary/internal register-defaults blobs in GPU-visible memory (kernel consumption path still pending RE)
- `sceAgcDriverSuspendPointSubmitDirect` — `SUSPEND_16` ioctl with RE'd 4-dword argument
- `sceAgcDriverIsSuspendPointInFlightDirect` — stub query (returns false)
- `sceAgcSuspendPointAndCheckStatus` — stub query (returns OK)
- `sce_agc_internal_suspend_point_submit_final` — `SUSPEND_39` ioctl with same 4-dword argument
- `agcOrbisMakeSysmap` (internal) — `MAKESYSMAP_8` ioctl for GPU VA mapping
- CB descriptor builder using `AgcGcCommandBuffer` with VMID masking
- Queue tracking (32 slots, gfx/compute/dma types)

Submit model:

- `AgcCommandBufferSubmit`
- `sceAgcDriverSubmitDcb`
- `sceAgcDriverSubmitAcb`

## Next RE Tasks

1. **Hardware validation** — build and deploy `samples/hw_test/`:
   - **Build (done locally):** `videoout_linear.elf`, `agc_init.elf`, and
     fake-SELF `*.bin` packages built successfully with ps5-payload-sdk +
     LibProsperoPkg.
   - **Sample updated:** `agc_init.c` now exercises `sce_agc_initialize`,
     `sce_agc_initialize_internal_memory`, `sceAgcDriverNotifyDefaultStates`,
     `sceAgcDriverGetPaDebugInterfaceVersion`, `sceAgcDriverSubmitDcb(NOP)`,
     `sceAgcDriverSuspendPointSubmitDirect`,
     `sceAgcDriverIsSuspendPointInFlightDirect`, and user special queue
     create/destroy.
   - **Step 1:** `videoout_linear.elf` — VideoOut display pipeline smoke
     test (no GPU). Validates `sceVideoOutOpen`, direct memory allocation,
     buffer registration, and flip. Adapted from `freegnm-examples/videoout-linear/`.
   - **Step 2:** `agc_init.elf` — AGC init + NOP submit + queue test.
     Validates `sce_agc_initialize`, `SubmitDcb(NOP)`, `NotifyDefaultStates`,
     `SuspendPointSubmitDirect`, queue create/destroy.
     Adapted from `freegnm-examples/triangle/` submit pattern.
   - Deploy: `make deploy_videoout` / `make deploy_agc` (exploited PS5)
   - Install: `make install_videoout` / `make install_agc` (debug PS5)
2. **Validate default state blobs** — confirm the primary/internal
   register-defaults blobs built by `sceAgcDriverNotifyDefaultStates` are
   accepted by the kernel and that the GPU state matches expectations.
3. **Validate suspend ioctls** — confirm the RE'd 4-dword argument layout for
   `SUSPEND_16` (nr=0x1c) and `SUSPEND_39` (nr=0x39) by testing on hardware.
4. **Queue creation** — implement real `QUEUE_CREATE` / `QUEUE_DESTROY` in
   `sceAgcDriverSetupAsyncGraphics` and async-compute submission.

## Non-Goals For Current Milestone

- No firmware blobs or proprietary microcode are embedded.
- No native PS5 queue submission is treated as working yet.
- No claim of official SDK drop-in completeness.
