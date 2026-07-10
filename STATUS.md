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
- AGC shader Specials block struct (`AgcShaderSpecials`: GE_CNTL, VGT_SHADER_STAGES_EN, VGT_GS_OUT_PRIM_TYPE, GE_USER_VGPR_EN)
- AGC shader User Data Table struct (`AgcShaderUserData`: 5× 64-bit entries)
- Typed accessors for shader sub-blocks (`agcShaderRecordGetSpecialsTyped`, `agcShaderRecordGetUserDataTyped`, `agcShaderRecordGetShRegisterValues`, `agcShaderRecordGetCxRegisterValues`)
- Extended texture/surface enums: 18 tile modes, 8 image types, 28 data formats (BC1-7, depth/stencil, Fmask, subsampled), 7 number types, 5 clamp modes, 4 filter modes, 3 mip filter modes, 4 border color types
- Full 64-byte `AgcRenderTarget` struct with CMASK/FMASK/DCC compression fields, CB_COLOR register mapping, and 14 init/setter helpers
- Texture descriptor convenience helpers: `SetImageType`, `SetTileMode`, `SetMipLevels`, `SetArraySize`, `SetDepth`, `SetPitch`, `SetDstSel`, `GetBaseAddress`, `GetWidth`, `GetHeight`
- Typed sampler helpers: `SetClampMode`, `SetFilterMode`, `SetBorderColor`, `SetMaxAnisotropy` (hardware-correct SQ_IMG_SAMP_WORD0-3 bit layout)
- Texture format encode/decode helpers: `agcTextureFormatEncode`, `agcTextureFormatGetDataFormat`, `agcTextureFormatGetNumberType`
- Shader linking: `agcShaderLinkHsGs` — combines HS/LS + CS shader records into GS (matches SPRX ordinal 131)
- EOP flip submit: `sceAgcDriverSubmitEopFlip` (prospero) + `sceAgcDcbSetEopFlip` DCB builder (IT_RELEASE_MEM 0x49)
- NID table expanded to 114 identified exports (78 original + 36 new from deep SPRX capstone disassembly)
- Async-compute queue submission: generic backend queue tracking (32 slots), ACB submit validates queue in-use, full create→submit→destroy flow tested
- 13 new DCB builders from SPRX disassembly: ReleaseMem, IndirectBuffer, IndirectBufferConst, DrawIndirect, DrawIndex2, DrawIndexIndirect, DrawIndirectMulti, DrawIndexIndirectMulti, SetPredication, EventWrite, SetConfigReg, SetShReg, SetUconfigReg
- 4 AGC-custom flip builders: WaitFlipDone (0x4C), WaitFlip (0x51), InsertWaitFlipDone (0x54), WaitFlipEos (0x4F+0x4E)
- Workload tracking: sceAgcDriverBeginWorkload / EndWorkload with SET_WORKLOAD (0x1E) submit on prospero
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
1270 passed, 0 failed
```

PS5 prospero backend (cross-compiled, no tests):

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
cmake -B build-prospero -DOPENAGC_PLATFORM=prospero -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-prospero
```

Expected result: `build-prospero/libopenagc.a` — PS5 x86_64 static library,
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
- `sceAgcDcbSetEopFlip` — emits `IT_RELEASE_MEM` (opcode 0x49) with 8-dword
  EOP flip packet (header 0xc0064900, matching SPRX RE)
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

- `sceAgcDriverNotifyDefaultStates` (prospero) now builds the primary/internal
  register-defaults blobs in GPU-visible memory and submits an
  `IT_CLEAR_STATE` (0x14) DCB to load them.

Suspend points:

- `sceAgcDriverIsSuspendPointInFlightDirect` (prospero) now queries the gfx
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
- `AgcGcFrameOpenArg` (nr=0x00, NOT HANDLED in FW 5.50), `AgcGcContextQueryResult` (nr=0x2e), `AgcGcMakesysmapArg8/12/48`
- `AgcGcSuspendArg` — 16-byte layout with four dwords (RE'd from kernel handler at 0x6e6ff0)
- `AgcGcSetHsOffchipArg` — 16-byte patch-list pointer/count (RE'd from kernel handler at 0x6ee6d2)
- CB header opcodes, VMID layout, num_cbs/VMID ranges
- Kernel-side error codes (module 0x4C)
- Kernel function offsets (ioctl_internal, submit_with_pid, frame_submit)

Native prospero backend (`src/driver_prospero.c`, `#ifdef OPENAGC_PROSPERO`):

- `sce_agc_initialize` — opens `/dev/gc`, calls `CONTEXT_QUERY` ioctl (0xc004812e, nr=0x2e), mmaps GPU register space at 0xfe0200000 if context not yet initialized. **NOTE:** The old `FRAME_OPEN` (nr=0x00) does NOT exist in FW 5.50 — the kernel returns EINVAL. See `analysis/sprx_sce_agc_initialize_disasm.md`.
- `sce_agc_initialize_internal_memory` — allocates 8 named regions via `sceKernelAllocateDirectMemory` + `sceKernelMapDirectMemory` + `MAKESYSMAP_8`
- `sceAgcDriverSubmitMultiCommandBuffersDirect` — builds CB descriptors, calls `SUBMIT_PID`
- `sceAgcDriverSubmitDcb` — single DCB submit via `SUBMIT_PID`
- `sceAgcDriverSubmitAcb` — single ACB submit (const IB type) via `SUBMIT_PID`
- `sceAgcDriverSubmitEopFlip` — EOP flip submit; validates display buffer
  index (< 16), delegates to `sceVideoOutSubmitEopFlip` (SPRX ordinals 49/50)
- `sceAgcDriverSetupAsyncGraphics` — `QUEUE_STATUS` ioctl (nr=0x26, arg=1; SPRX-confirmed)
- `sceAgcDriverSetTFRingDirect` — `SET_TF_RING` ioctl (user arg ignored on FW 5.50)
- `sceAgcDriverSetHsOffchipParamDirect` — `SET_HS_OFFCHIP` ioctl with RE'd 16-byte patch-list argument
- `sceAgcDriverGetPaDebugInterfaceVersion` — `PADEBUG_4` ioctl
- `_sceAgcDriverCreateUserSpecialQueue` — `QUEUE_CREATE` ioctl (nr=0x21, 64-byte RW arg with magic auth tokens; SPRX-confirmed)
- `_sceAgcDriverDestroyUserSpecialQueue` — `QUEUE_DESTROY` ioctl (nr=0x0e, 12-byte RW arg with magic auth tokens; SPRX-confirmed)
- `sceAgcDriverNotifyDefaultStates` — takes `uint32_t flags`; builds FW 5.50 primary/internal register-defaults blobs in GPU-visible memory (kernel consumption path still pending RE)
- `sceAgcDriverSuspendPointSubmitDirect` — `SUSPEND_16` ioctl with RE'd 4-dword argument
- `sceAgcDriverIsSuspendPointInFlightDirect` — stub query (returns false)
- `sceAgcSuspendPointAndCheckStatus` — stub query (returns OK)
- `sce_agc_internal_suspend_point_submit_final` — `SUSPEND_39` ioctl with same 4-dword argument
- `agcProsperoMakeSysmap` (internal) — `MAKESYSMAP_8` ioctl for GPU VA mapping
- CB descriptor builder using `AgcGcCommandBuffer` with VMID masking
- Queue tracking (32 slots, gfx/compute/dma types)

Submit model:

- `AgcCommandBufferSubmit`
- `sceAgcDriverSubmitDcb`
- `sceAgcDriverSubmitAcb`

## Hardware Validation Results (FW 5.50, exploited PS5 @ 10.0.1.41)

### videoout_linear.elf — PASS
- `sceVideoOutOpen(userId=0xFF, BUS_MAIN, 0)` — OK (PS5 requires userId=0xFF, not 0)
- `sceVideoOutGetResolutionStatus` — 3840x2160 (4K)
- `sceKernelAllocateDirectMemory` (garlic, 12GB range, 2MB align) — OK
- `sceVideoOutRegisterBuffers` (A8R8G8B8_SRGB, tiled) — OK
- Flip loop running at 60fps

### agc_init.elf — PARTIAL PASS
- **[1] sce_agc_initialize()** — PASS
  - /dev/gc opened (fd=7), CONTEXT_QUERY OK, mmap at 0xfe0200000
  - FRAME_OPEN correctly returns EINVAL (confirms ps5-openagc audit)
- **[2] sce_agc_initialize_internal_memory()** — PASS
  - All 8 regions allocated with WC_GARLIC (type=3)
  - WB_ONION (type=1) returns EINVAL on this PS5 — all regions use garlic
  - GPU VAs: 0x200200000–0x201000000
- **[3] sceAgcDriverNotifyDefaultStates()** — PASS
- **[4] sceAgcDriverGetPaDebugInterfaceVersion()** — returns 0 (unsupported?)
- **[5] sceAgcDriverSubmitDcb(NOP)** — PASS (NOP submitted to GPU!)
- **[6] SuspendPointSubmitDirect** — FAIL (0x80890201 SUBMIT_FAILED)
  - 4-dword argument layout needs RE validation
- **[7] CreateUserSpecialQueue** — FAIL (0x80890201 INTERNAL)
  - Ring buffer allocation or magic tokens may be wrong
- **[9] BeginWorkload/EndWorkload** — PASS

### Hardware-discovered bugs fixed
- PS5 memory type constants differ from PS4:
  - PS4: WB_ONION=0, WC_GARLIC=1, WB_GARLIC=3
  - PS5: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2 (type=1 fails on exploited PS5)
- PS5 VideoOut requires userId=0xFF, not 0
- PS5 VideoOut requires tiled mode (linear needs debug setting)
- PS5 direct memory: garlic searchEnd=0x300000000, alignment=0x200000
- `__ORBIS__` → `__PROSPERO__` (prospero toolchain defines __PROSPERO__)

## Next RE Tasks

1. **Fix SuspendPointSubmitDirect** — the 4-dword argument layout for
   `SUSPEND_16` (nr=0x1c) needs RE validation from SPRX disassembly.
   Currently returns SUBMIT_FAILED on hardware.
2. **Fix CreateUserSpecialQueue** — the ring buffer allocation or magic auth
   tokens may be wrong. Needs SPRX disassembly of the queue create path to
   verify the ring buffer address computation and ioctl argument layout.
3. **Verify internal memory region sizes** — the 8 region sizes in
   `sce_agc_initialize_internal_memory` (DDID 0x1000, CWSR 0x10000, etc.)
   are INHERITED UNVERIFIED from ps5-openagc. All 8 allocate successfully
   on hardware with garlic type=3, but the actual sizes may differ from
   what the SPRX uses. Needs SPRX disassembly to confirm.
4. **Investigate WB_ONION (type=1) failure** — onion memory allocation
   returns EINVAL on this exploited PS5. May be a sandbox restriction or
   the memory type constant may be different. The working PS5 examples
   (PS5_DEV_HOMEBREW) use type=1 for command buffers successfully, so this
   needs investigation.

## ps5-openagc Audit

The sibling `ps5-openagc` project is **NOT proven working on hardware** and
contains known errors. It was used for initial NID mapping cross-reference
only. All ioctl layouts and struct sizes in openagc have been independently
verified from SPRX/kernel disassembly.

**Confirmed wrong in ps5-openagc:**
- `FRAME_OPEN` (nr=0x00) — claimed valid, but kernel returns `EINVAL`
  (hardware-confirmed)
- Queue create — used nr=0x2a (4-byte), real SPRX uses nr=0x21 (64-byte RW
  with magic tokens)
- Queue destroy — used nr=0x2b (4-byte), real SPRX uses nr=0x0e (12-byte RW)
- VMID mask — `0x000FFFFF00000000` (transcription error), correct is
  `0x000FFFFFFFFFFFFF`
- Memory type constants — ps5-openagc had WB_ONION=0, WC_GARLIC=1, WB_GARLIC=2
  (wrong for PS5). Correct PS5 values: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2
  (hardware-confirmed)

**Still unverified (inherited from ps5-openagc):**
- Internal memory region sizes in `sce_agc_initialize_internal_memory`
  (all 8 allocate successfully on hardware, but actual SPRX sizes unconfirmed)

## Non-Goals For Current Milestone

- No firmware blobs or proprietary microcode are embedded.
- No native PS5 queue submission is treated as working yet.
- No claim of official SDK drop-in completeness.
