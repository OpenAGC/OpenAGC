# openagc Status

## Current Milestone

**Graphics pipeline: first GPU draw call is hardware-validated.** The gfx1013
ES+GS/NGG path executes compiler-generated ACO vertex code plus a pixel
shader and rasterizes a magenta triangle on real PS5 hardware. Phase 7 is
complete; see PLAN.md for the remaining production-hardening work.

See [PLAN.md](PLAN.md) for the broader GNM-to-AGC architecture roadmap,
including Wave32, geometry, ray tracing, cache synchronization, and VRS targets.

The host-generic implementation now has a tested model for:

- Type-3 AGC/PM4 packet headers using `length_dwords - 2` in bits `29:16`
- AGC `IT_NOP` subcommands recovered from HLE reference
- Known Gen5 AGC NID constants for mapped exports
- `SceAgcCb` cursor offsets and cursor allocation
- `sceAgcCb*` and `sceAgcDcb*` cursor-based packet builders
- DCB/ACB submit descriptor layout
- Generic submit validation and debug capture
- AGC shader record parser (magic, pointer fields, semantics counts, shader type)
- FW 5.50 AGC shader Specials block struct (`AgcShaderSpecials`: 0x30-byte sparse register/value layout with fields at 0x00, 0x08, 0x20, and 0x28)
- AGC shader User Data Table struct (`AgcShaderUserData`: 5× 64-bit entries)
- Typed accessors for shader sub-blocks (`agcShaderRecordGetSpecialsTyped`, `agcShaderRecordGetUserDataTyped`, `agcShaderRecordGetShRegisterValues`, `agcShaderRecordGetCxRegisterValues`)
- Extended texture/surface enums: 18 tile modes, 8 image types, 28 data formats (BC1-7, depth/stencil, Fmask, subsampled), 7 number types, 5 clamp modes, 4 filter modes, 3 mip filter modes, 4 border color types
- Full 64-byte `AgcRenderTarget` struct with CMASK/FMASK/DCC compression fields, CB_COLOR register mapping, and 14 init/setter helpers
- Texture descriptor convenience helpers: `SetImageType`, `SetTileMode`, `SetMipLevels`, `SetArraySize`, `SetDepth`, `SetPitch`, `SetDstSel`, `GetBaseAddress`, `GetWidth`, `GetHeight`
- Typed sampler helpers: `SetClampMode`, `SetFilterMode`, `SetBorderColor`, `SetMaxAnisotropy` (hardware-correct SQ_IMG_SAMP_WORD0-3 bit layout)
- Texture format encode/decode helpers: `agcTextureFormatEncode`, `agcTextureFormatGetDataFormat`, `agcTextureFormatGetNumberType`
- Shader linking: `agcShaderLinkHsGs` — combines HS/LS + CS shader records into GS (matches SPRX ordinal 131)
- Fused shader support: `sceAgcGetFusedShaderSize` plus FW 5.50-accurate legacy `sceAgcFuseShaderHalves` (`nApJjpKNBl4`) and `sceAgcFuseShaderHalves_0200` (`fd5Bp5tGTgo`), including checksum copies, RSRC1/2 merging, scratch relocation, and program-address patching
- EOP flip submit: `sceAgcDriverSubmitEopFlip` (prospero) + `sceAgcDcbSetEopFlip` DCB builder (IT_RELEASE_MEM 0x49)
- NID table (FW 5.50): 354 identified exports (216 libSceAgc + 138 libSceAgcDriver) out of 366 total FW 5.50 SPRX exports (96.7% coverage). 322 NIDs algorithm-verified via SHA1(name+salt) prospero-nid computation. Sources: reference emulator LIB_FUNC, ps5-openagc agc_nid.h, FW 3.20 genstub files, aerolib.csv (154k entries), flatz ps5_symbols.txt, NID computation (67 placeholder names resolved). Remaining 12 unknown NIDs are not in any known database. 32 TSV entries are unverified placeholders (`sceAgcUnknown_*`). 9 functions have two NIDs in 5.50 SPRX (old+new version exports), disambiguated with _<NID> suffix. Version-specific NIDs (3.20-only, 11.60-only) are in `analysis/agc_nids_version_variants.tsv`. All 354 TSV NIDs confirmed present in 5.50 SPRX. All function names in source code match their NID-verified correct names (Vsh-prefixed duplicates removed, wrong-function renames fixed).
- Async-compute queue submission: generic backend queue tracking (32 slots), ACB submit validates queue in-use, full create→submit→destroy flow tested
- 13 new DCB builders from SPRX disassembly: ReleaseMem, IndirectBuffer, DrawIndirect, DrawIndex2, DrawIndexIndirect, DrawIndirectMulti, DrawIndexIndirectMulti, SetPredication, EventWrite, SetConfigReg, SetShReg, SetUconfigReg
- 4 AGC-custom flip builders: WaitFlipDone (0x4C), WaitFlip (0x51), InsertWaitFlipDone (0x54), WaitFlipEos (0x4F+0x4E)
- Workload tracking: sceAgcDriverSetWorkloadsActive / SetWorkloadComplete with SET_WORKLOAD (0x1E) submit on prospero
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
1675 passed, 0 failed
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
- `sceAgcDcbWaitRegMem` — reference-confirmed: 32-bit variant now 7 dwords
  (was 6) with proper control word (0x10 base, split op bits, cache_policy),
  address alignment masking, poll cycles field, and corrected field order
  (addr, mask, reference, control, poll)
- `sceAgcDcbDmaData`
- `sceAgcDcbSetBaseIndirectArgs`
- `sceAgcDcbDispatchIndirect`
- `sceAgcDcbSetIndexBuffer`
- `sceAgcDcbDrawIndexOffset`
- `sceAgcDcbDrawIndexAuto` — now emits `IT_DRAW_INDEX_AUTO` (opcode 0x2D)
  with 3-dword packet and proper draw initiator decoding (reference-confirmed;
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

Old-style ACB stubs (from `src/acb.c`) and DCB raw-buffer variants (from `src/dcb.c`):

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
- `sceAgcDcbClearState` — now emits `IT_CLEAR_STATE` (opcode 0x14) with 2-dword packet
- `sceAgcDcbAtomicGds` / `ContextStateOp` / `ResetQueue` /
  `SetWorkloadComplete` / `SetWorkloadStreamInactive` / `SetWorkloadsActive` /
  `WaitUntilSafeForRendering`
- `sceAgcDcbSetPreemption` — SPRX RE shows it is an intentional VSH-only
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
- `sceAgcCreatePrimState` — FW 5.50-accurate 5-param primitive state builder;
  emits two CX and three UCONFIG pairs with shader Specials, hull merging,
  GS-enable handling, and the recovered 18-entry primitive lookup
- `sceAgcCreateInterpolantMapping` — FW 5.50-accurate 3-param interpolant
  builder; emits all 32 raw CX descriptors with semantic matching, F16,
  flat/custom, missing-semantic, and default-value transformations

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

reference-confirmed patchers and helpers:
- `sceAgcGetPacketSize` — returns packet size in dwords from PM4 header
- `sceAgcSetPacketPredication` — sets/clears bit 0 (predication) of packet header
- `sceAgcSetRangePredication` — walks packet range setting predication bit
- `sceAgcCondExecPatchSetEnd` — patches cmd[4] bits 13:0 with dword count
- `sceAgcCondExecPatchSetCommandAddress` — patches cmd[1..2] with command address
- `sceAgcWriteDataPatchSetAddressOrOffset` — patches cmd[2..3] for IT_WRITE_DATA
- `sceAgcJumpPatchSetTarget` — patches cmd[1..3] for IT_INDIRECT_BUFFER
- `sceAgcSetCxRegIndirectPatchSetNumRegisters` — patches cmd[4] bits 13:0
- `sceAgcSetShRegIndirectPatchSetNumRegisters` — patches cmd[4] bits 13:0
- `sceAgcSetUcRegIndirectPatchSetNumRegisters` — patches cmd[4] bits 13:0

reference-confirmed GetSize helpers:
- `sceAgcDcbWriteDataGetSize` — returns 4*num_dwords + 16 bytes
- `sceAgcDcbJumpGetSize` — returns 16 bytes
- `sceAgcDcbRewindGetSize` — returns 8 bytes
- `sceAgcDcbCondExecGetSize` — returns 20 bytes
- `sceAgcAcbCondExecGetSize` — returns 20 bytes
- `sceAgcDcbWaitOnAddressGetSize` — returns 56 (32-bit) or 64 (64-bit) bytes

Game-compat driver functions:

- `sceAgcDriverRegisterOwner` — stub (returns 0x8a6c9018, matches SPRX)
- `sceAgcDriverRegisterResource` — stub (returns 0x8a6c9018, matches SPRX)
- `sceAgcDriverGetEqContextId` — EQ context ID query
- `sceAgcDriverSetTFRing` — non-Direct TF ring set (clamps to 0x4000)
- `sceAgcDriverSetHsOffchipParam` — non-Direct HS offchip param
- `sceAgcDriverAgrSubmitDcb` — AGR submit (returns 0x8a6d0003 if not initialized)
- `sceAgcDriverAddEqEvent` — EQ event registration
- `sceAgcDriverDeleteEqEvent` — EQ event deletion (stub, NOT_SUPPORTED)
- `sceAgcDriverGetEqEventType` — EQ event type query (stub, NOT_SUPPORTED)
- `sceAgcDriverIsCaptureInProgress` — capture status (returns 0)
- `sceAgcDriverGetDefaultOwner` — default owner handle (returns 0)
- `sceAgcDriverInitResourceRegistration` — resource reg init (stub, NOT_SUPPORTED)
- `sceAgcDriverQueryResourceRegistrationUserMemoryRequirements` — (stub, NOT_SUPPORTED)
- `sceAgcDriverGetResourceRegistrationMaxNameLength` — returns 32
- `sceAgcDriverUnregisterResource` — resource unregister (stub, NOT_SUPPORTED)
- `sceAgcDriverRegisterWorkloadStream` — workload stream reg (stub, NOT_SUPPORTED)

Game-compat wrapper functions:

- `sceAgcInit` — user-facing init (delegates to `sce_agc_initialize`)
- `sceAgcSuspendPoint` — wrapper for `sceAgcDriverSuspendPointSubmitDirect`
- `sceAgcGetRegisterDefaults2` — register defaults query
- `sceAgcGetRegisterDefaults2Internal` — internal register defaults query

Register defaults (reference v8, FW 5.50):
- `agcRegisterDefaultsV8GetPrimaryGroups` — 127 groups, 703 registers (489 CX, 159 SH, 55 UC)
- `agcRegisterDefaultsV8GetInternalGroups` — 22 groups, 25 registers (4 CX, 15 SH, 6 UC)
- Extracted from the reference `agcRegisterDefaults.inc` `g_agc_public_reg_defaults_v8`
  and `g_agc_internal_reg_defaults_v8`. Replaces incomplete HLE-reference-derived
  data (which had only 38/703 public and 22/25 internal registers with many
  wrong zero-placeholder values).

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
- **[10] sceAgcDriverSetWorkloadsActive/EndWorkload** — PASS
  - Sub-region carving from SceGnmDdid for workload tracking works

### Hardware-discovered bugs fixed
- PS5 memory type constants differ from PS4:
  - PS4: WB_ONION=0, WC_GARLIC=1, WB_GARLIC=3
  - PS5: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2 (type=1 fails on exploited PS5)
- PS5 VideoOut requires userId=0xFF, not 0
- PS5 VideoOut requires tiled mode (linear needs debug setting)
  - **Workaround:** Runtime patch of `libSceVideoOut.sprx` at offset 0x7e61
    (NOP the `je` instruction that rejects linear tiling without debug setting)
- PS5 direct memory: garlic searchEnd=0x300000000, alignment=0x200000
- `__ORBIS__` → `__PROSPERO__` (prospero toolchain defines __PROSPERO__)
- **psbc compute SH register offsets were wrong** — RSRC1/2/3 for compute
  shaders are at 0x212/0x213/0x228, NOT `pgm_lo + 2/3/4` (which are
  COMPUTE_DISPATCH_PKT_ADDR_LO/HI). Fixed with per-stage offset functions.
- **AgcShaderType enum encoding was wrong** — CS was 6, should be 0.
  Firmware expects: CS=0, PS=1, ES=2, VS=3, GS=4, HS=5, ES-alt=6, LS=7.
  Confirmed by sharpemu's `PatchShaderProgramRegisters`.

### agc_videoout.elf — PASS
- GPU credential bypass (cr_sceAuthId = 0x4801000000000000) — OK
- `sce_agc_initialize()` + `sce_agc_initialize_internal_memory()` — OK
- `sceAgcDriverNotifyDefaultStates(0)` — FAIL (0x80890001, non-blocking)
- `sceAgcDriverSetupAsyncGraphics(1)` — OK
- `sceVideoOutOpen(userId=0xFF)` — OK
- `sceVideoOutRegisterBuffers` (linear, with libSceVideoOut patch) — OK
- NOP DCB submission during flip loop — OK
- CPU-rendered SMPTE color bars displayed for 600 frames — OK
- Deployed via websrv (FTP upload + HTTP /hbldr launch) — OK

### agc_compute.elf — FULL PASS (100% GPU compute execution verified on hardware)
- GPU credential bypass + AGC init + VideoOut — OK
- libSceVideoOut.sprx runtime patch (NOP at 0x7e61) — OK
- Compute shader binary loaded (AgcShaderRecord magic=OK, type=CS(0)) — OK
- Shader code uploaded to GPU flexible memory pool — OK
- SH registers set: PGM_LO/HI (0x20C), RSRC1/2/3 (0x212/0x213/0x228), START_X/Y/Z (0x204), NUM_THREAD_X/Y/Z (0x207) — OK
- User data set: `s2` (buf_lo), `s3` (buf_hi), `s4` (total_pixels), `s5` (fill_color) matching RDNA2 disassembler — OK
- Compute Unit enabling: `COMPUTE_STATIC_THREAD_MGMT_SE0..SE3` (0x216, 0x217, 0x219, 0x21A) set to 0xFFFFFFFF — OK
- FW 5.50 primary + internal SH defaults applied via `apply_sh_defaults` — OK
- `sceAgcDriverSubmitDcb` with SET_SH_REG + DISPATCH_DIRECT — OK (0x00000000)
- **Output verification**: 2073600 / 2073600 pixels match `0xFF00FF00` (100% GPU rendered on real hardware) — OK
- Display flip — OK (GPU-rendered solid green frame visible on TV/display)

## Next RE Tasks

### Priority 1: Graphics draw call (hardware validated)

Compute dispatch and the first graphics draw are fully hardware-validated on
real PS5 hardware. `agc_graphics.elf` executes the gfx1013 NGG front program,
rasterizes the expected magenta triangle, and changes 1,036,800 of 8,294,400
pixels, exactly matching the triangle's geometric area.

The immediate next hardware milestone is an interpolated RGB triangle. The
fragment shader will consume `v_color` instead of returning constant magenta,
validating the complete ES/NGG-to-PS semantic and interpolant path. Compiler
front/back placement regression coverage, sample cleanup, vertex/index
buffers, textures, and advanced graphics stages follow in that order.

Subtasks:
1. ~~Submit a compute dispatch and verify the GPU executes it.~~ ✅ Done
   (agc_compute.elf — GPU accepts DISPATCH_DIRECT DCB).
2. ~~Fix `sceAgcDriverNotifyDefaultStates`~~ ✅ Done
   (Fixed by correcting DDID allocation sizes: `AGC_DDID_PRIMARY_SIZE=0x41000`, `AGC_DDID_INTERNAL_SIZE=0xc000`. Returns `AGC_OK`).
3. ~~Verify compute shader pixel output~~ ✅ Done
   (100% VERIFIED ON HARDWARE: 2073600 / 2073600 pixels match `0xFF00FF00`!).
4. ~~Compile VS+PS via psbc~~ ✅ Done
   (Minimal GLSL VS+PS compiled via glslc → SPIR-V → psbc → AgcShaderRecord).
5. ~~Set up render target + graphics state~~ ✅ Done
   (CB_COLOR0, viewport, scissor, blend, primitive type, SPI_SHADER_POS/COL_FORMAT).
6. ~~Submit IT_DRAW_INDEX_AUTO~~ ✅ Done
   (DCB accepted by `sceAgcDriverSubmitDcb`, returns AGC_OK).
7. ~~Verify visual output~~ ✅ Done — the front-entry probe wrote
   `0x4E474721`, the post-draw WRITE_DATA marker wrote `0xDEADCAFE`, and the
   real ACO VS+PS path changed 1,036,800 render-target pixels to the
   fragment shader's magenta output.

#### Key findings from graphics draw call debugging (Phase 7)

These issues were discovered and fixed during hardware validation of the
graphics draw call. The final gfx1013 path now renders successfully.

1. **Non-contiguous register default groups corrupt GPU state.** Five
   register-default groups in `register_defaults_v8.c` have non-contiguous
   offsets but were being written as batch `SET_SH_REG`/`SET_CX_REG`
   packets (which assume contiguous offsets). The worst offender is group
   72 (128 CB_COLOR0 registers) with offsets like 0x318, 0x31b, 0x31c,
   0x31d, 0x31e, 0x31f, 0x321, 0x323... — writing these contiguously
   overwrites unrelated registers and causes a GPU hang. **Fix:** write
   each register individually with `register_count=1` (see
   `apply_sh_defaults_graphics` / `apply_cx_defaults` in `agc_graphics.c`).
   The 5 non-contiguous groups are: `_64` (16 regs), `_72` (128 regs),
   `_76` (160 regs), `_90` (3 regs), `internal_regs_21` (3 regs).

2. **Tile mode 0 is Depth_2DThin_64, NOT linear.** The `AgcTileMode` enum
   starts with depth tile modes (0-7). Tile mode 0 = `kAgcTileDepth_2DThin_64`.
   For a linear color render target, use `kAgcTileDisplay_LinearGeneral` (31).
   Setting `CB_COLOR0_ATTRIB.tile_mode_index = 0` for a color RT causes the
   CB hardware to interpret the surface as a depth buffer and not write
   color data. **Fix:** `CB_COLOR0_ATTRIB = 0x0000001F` (tile_mode_index=31).

3. **SPI_SHADER_COL_FORMAT (0x1C5) and SPI_SHADER_POS_FORMAT (0x1C3) are
   NOT in shader records or register defaults.** The AgcShaderRecord
   produced by psbc does not include these registers, and the FW 5.50
   register defaults do not set them. They default to 0 (no export),
   which means the PS does not export color and the PA cannot process
   vertex positions. **Fix:** set them manually in the DCB:
   - `SPI_SHADER_POS_FORMAT (0x1C3) = 1` (4_32_32_32_32 — vec4 position)
   - `SPI_SHADER_Z_FORMAT (0x1C4) = 0` (no Z export)
   - `SPI_SHADER_COL_FORMAT (0x1C5) = 1` (8_8_8_8 — RGBA8 color)
   - `CB_SHADER_MASK (0x08F) = 0x0F` (all RGBA channels to RT0)

4. **VGT_SHADER_STAGES_EN should be 0 (default) for VS+PS.** Setting
   `ES_EN` routes the vertex shader through the ES (export shader) stage,
   which is wrong for a type-3 (VS) shader. The default (0) means VS runs
   as VS. Do NOT set this register for a simple VS+PS pipeline.

5. **CONTEXT_CONTROL packet is required.** Same as the compute sample:
   opcode 0x28, 3 dwords, `LOAD_ENABLE_CONTEXT=0x80000000`. Without this,
   the CP may not load the context state from the default-state blobs.

6. **DB_Z_INFO must be explicitly disabled.** The default `DB_Z_INFO`
   (0x010) is `0x80000000` (FORMAT=x8_24 with some bits set), which
   enables the depth buffer. If no depth buffer memory is bound, the DB
   may discard all pixels. **Fix:** set `DB_Z_INFO = 0` and
   `DB_STENCIL_INFO = 0` to disable depth/stencil.

7. **CB_COLOR0_PITCH uses 8-element tiles for linear mode.** The
   `TILE_MAX` field (11 bits) is `(pitch_elements / 8) - 1`, not
   `(pitch_elements - 1)`. For 1920px: `(1920/8)-1 = 239 = 0x000EF`.
   The `SLICE` field (22 bits) is `(tiles_per_row * height) - 1`.

#### Resolved launch and raster issues

The CP marker originally executed after every draw while no shader marker
or color output appeared. The decisive gfx1013 finding was that NGG code is
launched from `SPI_SHADER_PGM_LO_ES` while its resources remain in the GS
register block. The compiler had placed real ACO code in the GS-back record
and a dummy `s_endpgm` in the GS-front record, so fusion installed the dummy
address in the executable ES program register. Swapping those code payloads
made the front-entry probe execute on hardware. The real fixture then still
faulted on an unbound debug push-constant pointer; removing that diagnostic
global store allowed normal NGG exports and rasterization.

Additional fixes applied since the last update (still black output):

8. **PS CX block re-enables depth after explicit disable.** The PS
   shader's CX register block writes `DB_DEPTH_INFO` (0x00F) = 0x0F and
   `DB_SHADER_CONTROL` (0x203) = 0x10 *after* the code disabled depth.
   With no depth buffer bound, depth testing discards all fragments.
   **Fix:** override `DB_DEPTH_INFO`, `DB_Z_INFO`, `DB_STENCIL_INFO`,
   `DB_SHADER_CONTROL`, and `DB_DEPTH_CONTROL` to 0 *after* the PS CX
   block is written.

9. **`SPI_PS_INPUT_CNTL_0` register offset was wrong.** The code wrote
   to `0x1B8` (`AGC_REG_SPI_BARYC_CNTL`) instead of the correct
   `0x191` (`AGC_REG_SPI_PS_INPUT_CNTL_0`). This prevented the PS from
   fetching its input. **Fix:** use `AGC_REG_SPI_PS_INPUT_CNTL_0` with
   `OFFSET=0` so PS input 0 reads from VS param export 0 (v_color).

10. **`VGT_SHADER_STAGES_EN` bit layout corrected.** The previous code
    set bit 8 (0x100) thinking it was `ES_EN`, but bit 8 is actually
    `dynamicHs` on RDNA2. The real `esEn` field is at bits [4:3]
    (EsReal=2 → 0x10), and `vsEn` is at bits [7:6] (VsReal=0). For a
    VS+PS pipeline with the VS shader at ES PGM (0x0C8, as psbc outputs),
    set `VGT_SHADER_STAGES_EN = 0x10` (EsReal). This does not kernel panic.

11. **`SPI_SHADER_COL_FORMAT` must match `CB_COLOR0_INFO` format.**
    Setting COL_FORMAT=4 (16_16_16_16) while CB_COLOR0_INFO is
    COLOR_8_8_8_8 (format 1) is a mismatch that can prevent CB writes.
    **Fix:** set `SPI_SHADER_COL_FORMAT = 1` (8_8_8_8) to match.

12. **`CB_COLOR0_ATTRIB2` (0x3B0) was missing.** This register packs
    `MIP0_HEIGHT` (bits [13:0]) and `MIP0_WIDTH` (bits [27:14]). Without
    it, the CB clips all writes to 0x0. **Fix:** set
    `CB_COLOR0_ATTRIB2 = ((height-1) & 0x3FFF) | (((width-1) & 0x3FFF) << 14)`
    and `CB_COLOR0_ATTRIB3 = 0`.

13. **Invalid partial NGG state removed.** `VGT_GS_OUT_PRIM_TYPE` uses a
    GS-output enum where triangles are `2`, not the input `TRILIST` value
    `4`. A plain VS record also has no GS/NGG `specials` block, so speculative
    `GE_CNTL` and GS-output programming was removed.

14. **`PA_CL_VS_OUT_CNTL` and `PA_CL_CLIP_CNTL` set to 0.** The previous
    `VS_OUT_CNTL=0x00010000` (USE_VTX_POINT_SIZE) and
    `CLIP_CNTL=0x00010000` (CLIP_DISABLE) may have been interfering with
    NGG rasterization. Set both to 0 (defaults).

#### Corrected hardware-validation candidate

Cross-checking the latest commits against KytyPS5 and sharpemu found four
additional state bugs: the `8_8_8_8` color format is `10` rather than `2`,
`CB_COLOR_CONTROL` had MODE=Disable instead of Normal, shader color-export
format `4` was incorrectly replaced with render-target format `1`, and the
UCONFIG-only `VGT_PRIMITIVE_TYPE` offset was also emitted as a context write.

The plain-VS workaround has been replaced by a compiler-generated gfx1013 NGG
path. `openagc-psbc --ngg-front` now performs Mesa RADV no-GS NGG lowering,
runs ACO, and emits fusion-compatible GS-front/GS-back records containing
compiler-derived resource registers, subgroup state, complete `specials`,
and semantic maps. The graphics sample relocates and fuses those records,
derives primitive/interpolant state through OpenAGC, binds executable ACO code
through `SPI_SHADER_PGM_LO_ES`, and keeps the GS-back record as the fused
state/resource container. Real-PS5 validation passes: the entry probe runs,
the CP survives the draw, and the magenta triangle covers 1,036,800 pixels.

#### Experimental approaches that caused a kernel panic (DO NOT RETRY)

1. **Mixing compute dispatch into a graphics DCB** — inserting
   `DISPATCH_DIRECT` (compute) into the same DCB as
   `IT_DRAW_INDEX_AUTO` caused a kernel panic. Keep compute and graphics
   in separate DCB submissions.
2. **Enabling RDNA2 NGG mode** — setting `GE_NGG_SUBGRP_CNTL=1` and
   `VGT_SHADER_STAGES_EN=0x8110` without a bound GS/NGG passthrough
   shader crashed the GPU. Do not enable NGG without proper NGG shader
   setup.
3. **Dual-binding ES and VS SH registers** — copying VS registers to
   both VS (0x048-0x04B) and ES (0x0C8-0x0CB) stages simultaneously
   caused instability. Use a single active vertex-processing stage.


### Priority 3: PA debug ioctl (kernel RE)

`sceAgcDriverGetPaDebugInterfaceVersion` still returns EPERM (errno=1).
This is a separate kernel permission check (not the cr_sceAuthId check
at 0xd8e70400). Needs further kernel RE to identify the required
capability. Low priority — non-blocking for rendering.

### Priority 4: FRAME_OPEN ioctl (kernel RE)

`sce_agc_initialize` calls FRAME_OPEN (0xC0088100) which returns EINVAL.
This may need additional context setup or credentials. Currently
non-blocking — init succeeds without it. Low priority.

### Priority 5: Game compatibility expansion

Continue analyzing game binaries to identify and implement remaining
missing AGC functions. See `analysis/game_agc_usage.md` for the Joe & Mac
analysis. Current coverage: 3 games, 72 unique AGC functions, 100%
implemented. Analyzing more games will surface new function requirements.

### Blocked: Remaining 12+32 unknown NIDs

12 SPRX NIDs are not in any known database (aerolib.csv 154k entries,
flatz ps5_symbols.txt, reference emulator, ps5-openagc, FW 3.20 genstubs
all exhausted). 32 TSV entries are unverified placeholders. These are
blocked on new external data sources — no actionable work without new
NID databases or firmware dumps.

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
- No claim of official SDK drop-in completeness.
- Graphics draw calls are hardware-validated for the current gfx1013
  no-GS NGG VS+PS sample. This does not yet claim complete tessellation,
  geometry-shader, mesh-shader, or game-wide graphics compatibility.
