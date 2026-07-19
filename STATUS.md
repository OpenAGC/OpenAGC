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
- NID table expanded to 148 identified exports (78 original + 36 from deep SPRX disassembly + 34 from batch 2 disassembly)
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
1483 passed, 0 failed
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
present in the symbol table. Hardware validation in progress — see
"Hardware Validation Results" section below.

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
- `sceAgcDcbDrawIndexAuto` — now emits `IT_DRAW_INDEX_AUTO` (opcode 0x2D)
  with 3-dword packet and proper draw initiator decoding (KytyPS5-confirmed;
  was incorrectly NOP-wrapped 7-dword stub)
- `sceAgcDcbWaitUntilSafeForRendering`
- `sceAgcDcbPushMarker`
- `sceAgcDcbPopMarker`
- `sceAgcDcbSetFlip`
- `sceAgcDcbSetEopFlip` — emits `IT_RELEASE_MEM` (opcode 0x49) with 8-dword
  EOP flip packet (header 0xc0064900, matching SPRX RE)
- `sceAgcCbReleaseMem`
- `sceAgcDcbSetShRegistersIndirect` — opcode 0x63, 5 dwords (SPRX-confirmed;
  was incorrectly NOP-wrapped 4 dwords)
- `sceAgcDcbSetCxRegistersIndirect` — opcode 0x9F, 5 dwords (SPRX-confirmed)
- `sceAgcDcbSetUcRegistersIndirect` — opcode 0x64, 5 dwords (SPRX-confirmed)

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
- `sceAgcSetShRegIndirectPatchSetAddress` / `AddRegisters` — SPRX-confirmed:
  validate opcode 0x63, SetAddress patches cmd[1..2] preserving low 2 bits,
  AddRegisters patches cmd[4] bits 13:0
- `sceAgcSetCxRegIndirectPatchSetAddress` / `AddRegisters` — SPRX-confirmed:
  validate opcode 0x9F, same patch logic
- `sceAgcSetUcRegIndirectPatchSetAddress` / `AddRegisters` — SPRX-confirmed:
  validate opcode 0x64, same patch logic

Game-compat packet builders (from Joe & Mac game analysis):

- `sceAgcDcbAcquireMem` — `IT_ACQUIRE_MEM` (0x58), 8 dwords
- `sceAgcDcbCopyData` — `IT_COPY_DATA` (0x40), 6 dwords
- `sceAgcDcbJump` — `IT_INDIRECT_BUFFER` (0x3F), 4 dwords (SPRX-confirmed;
  was incorrectly 0x33/IB_CNST)
- `sceAgcDcbResetQueue` — queue reset, 3 dwords
- `sceAgcDcbSetIndexCount` — `IT_INDEX_BUFFER_SIZE` (0x13), 2 dwords
  (SPRX-confirmed; was incorrectly 3 dwords; clamps count to max(count,1))
- `sceAgcDcbSetIndexSize` — opcode 0x7A, 3 dwords (SPRX-confirmed;
  was incorrectly 0x2A/INDEX_TYPE; cmd[1]=0x20000243 constant,
  cmd[2]=(type&3)|(swap<<6)|0x400)
- `sceAgcDcbSetNumInstances` — `IT_NUM_INSTANCES` (0x2F), 2 dwords
- `sceAgcDcbStallCommandBufferParser` — opcode 0x42, 2 dwords
  (SPRX-confirmed; was incorrectly NOP+subcommand)
- `sceAgcDcbDrawIndex` — `IT_DRAW_INDEX_2` (0x27), 6 dwords
  (SPRX-confirmed field order: cmd[1]=max(count,1), cmd[4]=index_count,
  cmd[5]=draw_initiator)
- `sceAgcCbSetShRegisterRangeDirect` — `IT_SET_SH_REG` (0x76), variable
- `sceAgcCbSetUcRegistersDirect` — `IT_SET_UCONFIG_REG` (0x79), variable
- `sceAgcSetNop` — patches byte at cmd+1 to 0x10 (NOP), returns NULL
  (SPRX-confirmed; 1 param, not 2)
- `sceAgcGetDataPacketPayload` — 3-param payload getter
  (out_addr, cmd, skip_header); returns NULL (SPRX-confirmed)
- `sceAgcDebugRaiseException` — debug stub (no-op on non-dev)
- `sceAgcCreateShader` — shader record validation
- `sceAgcCreatePrimState` — 5-param primitive state builder
  (SPRX-confirmed; was incorrectly 2-param with range 0-10)

SPRX disassembly batch 2 (FW 5.50 deep disassembly):

DCB packet builders:
- `sceAgcDcbClearState` — AGC-custom clear state (opcode 0x12), 2 dwords
- `sceAgcDcbRewind` — `IT_REWIND` (0x59), 2 dwords
- `sceAgcDcbCondExec` — `IT_COND_EXEC` (0x22), 5 dwords
- `sceAgcDcbSetIndexIndirectArgs` — AGC-custom (opcode 0x91), 4 dwords
- `sceAgcDcbAtomicMem` — opcode 0x1E (AGC ATOMIC_MEM), 9 dwords
  (NID "1-gUn1PI4Sw" was mislabeled as SET_WORKLOAD; it's actually AtomicMem)
- `sceAgcDcbAtomicGds` — `IT_ATOMIC_GDS` (0x1D), 11 dwords
- `sceAgcDcbMemSemaphore` — `IT_MEM_SEMAPHORE` (0x39), 4 dwords
- `sceAgcDcbPrimeUtcl2` — `IT_PRIME_UTCL2` (0x5D), 5 dwords
- `sceAgcDcbDrawIndexMultiInstanced` — AGC-custom (opcode 0x3A), 9+count dwords
- `sceAgcDcbSetMarker` — NOP-wrapped marker string
- `sceAgcDcbContextStateOp` — 4-op context state switch (CLEAR_STATE /
  SET_CONTEXT_REG / SET_CX_REG_INDIRECT / combined)
- `sceAgcDcbSetWorkloadsActive` / `SetWorkloadComplete` / `SetWorkloadStreamInactive`
  — DCB cursor workload helpers (SET_WORKLOAD 0x1E, 8 dwords)

DCB register direct setters (3 dwords each):
- `sceAgcDcbSetCfRegisterDirect` — `IT_SET_CONFIG_REG` (0x68)
- `sceAgcDcbSetCxRegisterDirect` — `IT_SET_CONTEXT_REG` (0x69)
- `sceAgcDcbSetShRegisterDirect` — `IT_SET_SH_REG` (0x76)
- `sceAgcDcbSetUcRegisterDirect` — `IT_SET_UCONFIG_REG` (0x79)
- `sceAgcDcbSetCfRegisterRangeDirect` — variable-length config reg range
- `sceAgcCbSetUcRegisterRangeDirect` — variable-length uconfig reg range

CB builders:
- `sceAgcCbBranch` — `IT_INDIRECT_BUFFER` (0x3F), 14 dwords, 12-arg signature
  (SPRX-confirmed: cmd[1]=((ctrl&7)<<8)|(flags&3), cmd[2]=addr_lo&~7,
  cmd[10]=((engine&3)<<28)|(size&0xfffff), etc.)
- `sceAgcCbCondWrite` — AGC-custom `IT_COND_WRITE` (0x45), 9 dwords
  (SPRX-confirmed: cmd[2..3]=ref, cmd[4]=mask, cmd[6..7]=address, cmd[8]=write_data)
- `sceAgcCbMemSemaphore` — `IT_MEM_SEMAPHORE` (0x39), 4 dwords

WaitRegMem patchers (SPRX-confirmed: require 0x79 wrapper, use adjusted pointer):
- `sceAgcWaitRegMemPatchCompareFunction` — patches adjusted[1] bits 2:0
- `sceAgcWaitRegMemPatchReference` — patches adjusted[4]
- `sceAgcWaitRegMemPatchMask` — patches adjusted[5] (32-bit 0x3C) or adjusted[6] (64-bit 0x93)

Game-compat driver functions:

- `sceAgcDriverRegisterOwner` — stub (returns 0x8a6c9018, matches SPRX)
- `sceAgcDriverRegisterResource` — stub (returns 0x8a6c9018, matches SPRX)
- `sceAgcDriverGetEqContextId` — EQ context ID query
- `sceAgcDriverSetTFRing` — non-Direct TF ring set (clamps to 0x4000)
- `sceAgcDriverSetHsOffchipParam` — non-Direct HS offchip param
- `sceAgcDriverAgrSubmitDcb` — AGR submit (returns 0x8a6d0003 if not initialized)
- `sceAgcDriverAddEqEvent` — EQ event registration

Game-compat wrapper functions:

- `sceAgcInit` — user-facing init (delegates to `sce_agc_initialize`)
- `sceAgcSuspendPoint` — wrapper for `sceAgcDriverSuspendPointSubmitDirect`
- `sceAgcGetRegisterDefaults2` — register defaults query
- `sceAgcGetRegisterDefaults2Internal` — internal register defaults query

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
- `sce_agc_initialize_internal_memory` — allocates 9 named regions via `sceKernelMapNamedSystemFlexibleMemory` (matches SPRX). Region sizes confirmed from SPRX disassembly: SceGnmGpuInfo (1MB), SceGnmTrapCode (16KB), SceGnmTrapData (16KB), SceGnmDdid (1008KB), SceGnmEopFifo (240KB), SceGnmShadowReg (16KB), SceGnmCwsr (16MB), SceGnmMisc (16KB), SceGnmACQRB (1920KB). See `analysis/sprx_agc_driver_internal_mem_disasm.md`.
- `sceAgcDriverSubmitMultiCommandBuffersDirect` — builds CB descriptors, calls `SUBMIT_PID`
- `sceAgcDriverSubmitDcb` — single DCB submit via `SUBMIT_PID`
- `sceAgcDriverSubmitAcb` — single ACB submit (const IB type) via `SUBMIT_PID`
- `sceAgcDriverSubmitEopFlip` — EOP flip submit; validates display buffer
  index (< 16), delegates to `sceVideoOutSubmitEopFlip` (SPRX ordinals 49/50)
- `sceAgcDriverSetupAsyncGraphics` — `QUEUE_STATUS` ioctl (nr=0x26, arg=1; SPRX-confirmed)
- `sceAgcDriverSetTFRingDirect` — `SET_TF_RING` ioctl (user arg ignored on FW 5.50)
- `sceAgcDriverSetHsOffchipParamDirect` — `SET_HS_OFFCHIP` ioctl with RE'd 16-byte patch-list argument
- `sceAgcDriverGetPaDebugInterfaceVersion` — `PADEBUG_4` ioctl
- `_sceAgcDriverCreateUserSpecialQueue` — `QUEUE_CREATE` ioctl (nr=0x21, 64-byte RW arg). Ring buffer address computed from EOP FIFO base + 0x39000 (SPRX-confirmed). Arg layout: magic tokens, pipe_id, mmio_base, queue_id, ring_addr, ring_size.
- `_sceAgcDriverDestroyUserSpecialQueue` — `QUEUE_DESTROY` ioctl (nr=0x0e, 12-byte RW arg with magic auth tokens; SPRX-confirmed)
- `sceAgcDriverNotifyDefaultStates` — takes `uint32_t flags`; builds FW 5.50 primary/internal register-defaults blobs in GPU-visible memory (kernel consumption path still pending RE)
- `sceAgcDriverSuspendPointSubmitDirect` — `SUSPEND_16` ioctl with RE'd 4-dword argument
- `sceAgcDriverIsSuspendPointInFlightDirect` — stub query (returns false)
- `sceAgcSuspendPointAndCheckStatus` — stub query (returns OK)
- `sce_agc_internal_suspend_point_submit_final` — `SUSPEND_39` ioctl with same 4-dword argument
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

### agc_init.elf — PARTIAL PASS (post-SPRX-disassembly + kernel RE)
- **[1] sce_agc_initialize()** — PASS
  - /dev/gc opened (fd=7), CONTEXT_QUERY OK, mmap at 0xfe0200000
  - FRAME_OPEN correctly returns EINVAL (confirms ps5-openagc audit)
- **[2] sce_agc_initialize_internal_memory()** — PASS
  - All 9 regions allocated with sceKernelMapNamedSystemFlexibleMemory (type=0x33)
  - GPU VAs: 0x200024000–0x20145C000
  - Region sizes match SPRX disassembly exactly
- **[3] sceAgcDriverNotifyDefaultStates()** — PASS
  - Sub-region carving from SceGnmDdid (1008KB) works correctly
- **[4] sceAgcDriverGetPaDebugInterfaceVersion()** — FAIL (errno=1, EPERM)
  - Kernel returns EPERM — requires root/debug capabilities
  - Not a bug in our implementation; kernel-level permission check
- **[5] sceAgcDriverSubmitDcb(NOP)** — PASS (NOP submitted to GPU!)
- **[6] sceAgcDriverSetupAsyncGraphics(1)** — PASS
  - Ioctl 0x80048126 with arg=1 succeeds
- **[7] _sceAgcDriverCreateUserSpecialQueue()** — PASS (with credential bypass)
  - Ioctl arg layout confirmed correct via SPRX disassembly
  - Ring buffer carved from EOP FIFO base + 0x39000 (correct)
  - Read ptr from ACQRB base + 0x1C8000, metadata from ACQRB base + 0x1CC000
  - **Credential bypass**: The kernel handler at 0xffffffffd8f66bb0
    calls 0xffffffffd8e70400 which checks the process's GPU credentials
    at `[ucred + 0x58]` (cr_sceAuthId). The check masks with 0xff0f000000000000
    and adds 0xb7ff000000000000; if the result is zero (after >>49), the
    check passes. Setting cr_sceAuthId = 0x4801000000000000 satisfies this:
    (0x4801000000000000 & 0xff0f000000000000) + 0xb7ff000000000000 = 0 (64-bit overflow).
    When the credential check passes, the handler falls through to the
    magic-value checks. The magic triple (0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c)
    selects config table at 0xd9d5b360, mapping to slot (field0=2, field1=3,
    field2=5) at ctx offset 0x158.
- **[8] sceAgcDriverSuspendPointSubmitDirect()** — PASS (with credential bypass)
  - Ioctl 0xC010811C with 4-dword arg layout confirmed correct
  - **Key finding**: The suspend point handler at 0xffffffffd8f66ff0 uses
    the SAME credential check (0xd8e70400) and the SAME magic triple
    (0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c) as the queue create handler.
    When credentials pass, the magic triple selects the SAME config table
    (0xd9d5b360), mapping to the SAME slot (2,3,5) at ctx offset 0x158.
    Non-magic values like (1,0,0) would compute a different slot (0x64)
    and fail with 0x804C0001 (no queue at that slot).
  - **field3 constraint**: The tail-called function at 0xd8e57700 checks
    `(field3 >> queue->shift_amount) == 0` where shift_amount is read from
    queue+0x48. If field3 is non-zero and shift_amount is 0, returns EINVAL.
    Passing field3=0 works.
- **[9] _sceAgcDriverDestroyUserSpecialQueue()** — PASS
  - Queue destroyed successfully after suspend point
- **[10] sceAgcDriverBeginWorkload/EndWorkload** — PASS
  - Sub-region carving from SceGnmDdid for workload tracking works

### Hardware-discovered bugs fixed
- PS5 memory type constants differ from PS4:
  - PS4: WB_ONION=0, WC_GARLIC=1, WB_GARLIC=3
  - PS5: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2 (type=1 fails on exploited PS5)
- PS5 VideoOut requires userId=0xFF, not 0
- PS5 VideoOut requires tiled mode (linear needs debug setting)
- PS5 direct memory: garlic searchEnd=0x300000000, alignment=0x200000
- `__ORBIS__` → `__PROSPERO__` (prospero toolchain defines __PROSPERO__)

## Next RE Tasks

1. **PA debug ioctl** — `sceAgcDriverGetPaDebugInterfaceVersion` still
   returns EPERM (errno=1). This is a separate kernel permission check
   (not the cr_sceAuthId check at 0xd8e70400). Needs further kernel RE
   to identify the required capability.
2. **FRAME_OPEN ioctl** — `sce_agc_initialize` calls FRAME_OPEN
   (0xC0088100) which returns EINVAL. This may need additional context
   setup or credentials. Currently non-blocking — init succeeds without it.
3. **Validate default state blobs** — confirm the primary/internal
   register-defaults blobs are accepted by the kernel and produce the
   expected GPU state.
4. **Full GPU command submission** — now that queue create, suspend point,
   and DCB submit all work, the next step is to submit actual rendering
   commands (draw calls, state setup) via the compute queue.
5. **Game compatibility** — continue analyzing game binaries to identify
   and implement remaining missing AGC functions. See
   `analysis/game_agc_usage.md` for the Joe & Mac analysis.

## Game Compatibility

### Coverage across 3 game binaries

| Game | Title ID | AGC imports | Implemented | Missing |
|------|----------|-------------|-------------|---------|
| Joe & Mac Caveman Ninja | PPSA02801 | 70 | 70 | 0 |
| PPSA09076 (backport) | PPSA09076 | 69 | 69 | 0 |
| PPSA03157 | PPSA03157 | 58 | 58 | 0 |

**Total unique AGC functions across all 3 games: 72**
**All 72 implemented.** 100% coverage.

### Joe & Mac Caveman Ninja (PPSA02801, v01.003)
- **Engine:** Unity IL2CPP
- **SDK:** PS5 5.00
- **AGC imports:** 70 total (61 from libSceAgc, 9 from libSceAgcDriver)
- **Analysis:** `analysis/game_agc_usage.md`

### PPSA09076 (01.000.000 backport)
- **AGC imports:** 69 total (60 from libSceAgc, 9 from libSceAgcDriver)
- Same import set as Joe & Mac minus `sceAgcInit`, `sceAgcGetDataPacketPayload`

### PPSA03157
- **AGC imports:** 58 total (52 from libSceAgc, 6 from libSceAgcDriver)
- Smallest import set; no `sceAgcAcbJump`, `sceAgcAcbCopyData`,
  `sceAgcAcbPopMarker`, `sceAgcAcbPushMarker`, `sceAgcCbSetUcRegistersDirect`,
  `sceAgcDebugRaiseException`, `sceAgcSetNop`, `sceAgcDriverGetEqContextId`,
  `sceAgcDriverRegisterOwner`, `sceAgcDriverRegisterResource`
- Uses `sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate` (unique to this game)

### Implemented missing functions

All missing functions across the 3 games are now implemented:

1. **libSceAgcDriver stubs** — `RegisterOwner`/`RegisterResource` return
   `0x8a6c9018` (not supported on non-dev hardware per SPRX).
2. **Non-Direct driver variants** — `SetTFRing`/`SetHsOffchipParam` are
   wrapper versions that delegate to the Direct variants.
3. **DCB packet builders** — `AcquireMem`, `CopyData`, `Jump`,
   `ResetQueue`, `SetIndexCount`, `SetIndexSize`, `SetNumInstances`,
   `StallCommandBufferParser`, `DrawIndex`.
4. **CB register setters** — `SetShRegisterRangeDirect`,
   `SetUcRegistersDirect`.
5. **Indirect register patchers** — 6 functions for Sh/Cx/Uc register
   indirect write patching.
6. **Utility functions** — `SetNop`, `DebugRaiseException`,
   `GetDataPacketPayload`, `CreateShader`, `CreatePrimState`.
7. **Wrapper functions** — `sceAgcInit`, `sceAgcSuspendPoint`,
   `sceAgcGetRegisterDefaults2` / `2Internal`.
8. **DmaData Src patcher** — `sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate`
   (SPRX-confirmed: checks raw DMA_DATA opcode 0x50, patches cmd[2..3]).
   Also fixed `sceAgcDmaDataPatchSetDstAddressOrOffset` to match SPRX
   (now accepts both raw DMA_DATA and NOP-wrapped formats).

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
- Internal memory region sizes — 6 of 9 sizes were wrong. SPRX uses
  `sceKernelMapNamedSystemFlexibleMemory` (not `sceKernelAllocateDirectMemory`)
  with type=0x33. Correct sizes from SPRX disassembly:
  SceGnmGpuInfo=1MB, SceGnmDdid=1008KB, SceGnmEopFifo=240KB,
  SceGnmCwsr=16MB, SceGnmACQRB=1920KB (all were 4KB-64KB in ps5-openagc).
  See `analysis/sprx_agc_driver_internal_mem_disasm.md`.
- Queue create ioctl arg layout — field order at offsets 0x10-0x28 was
  completely wrong. Correct layout from SPRX: pipe_id at 0x10, caller_arg
  at 0x18, mmio_base at 0x20, queue_id at 0x28. Ring buffer is carved
  from EOP FIFO base + 0x39000, not separately allocated.

## Non-Goals For Current Milestone

- No firmware blobs or proprietary microcode are embedded.
- No native PS5 queue submission is treated as working yet.
- No claim of official SDK drop-in completeness.
