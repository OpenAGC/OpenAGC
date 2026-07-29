# openagc Reference Findings

This document records findings from local PS5 emulator/reference projects used
to guide openagc development.

## HLE reference

Reference path:

`/Users/bizkut/Downloads/PS5/homebrew/hle reference`

Relevant files:

- `src/HLE reference.Libs/Agc/HLE reference`
- `src/HLE reference.Libs/Agc/Gen5ShaderTranslator.cs`
- `src/HLE reference.Libs/VideoOut/VideoOutExports.cs`
- `src/HLE reference.Libs/VideoOut/VulkanVideoPresenter.cs`
- `src/HLE reference.Core/Cpu/Native/DirectExecutionBackend.Imports.cs`

Key findings:

- HLE reference has the most directly useful PS5 AGC HLE material found so far.
- It maps concrete NIDs to AGC functions including:
  - `sceAgcCbNop`
  - `sceAgcCbDispatch`
  - `sceAgcCbSetShRegistersDirect`
  - `sceAgcAcbAcquireMem`
  - `sceAgcAcbWaitRegMem`
  - `sceAgcAcbWriteData`
  - `sceAgcAcbDispatchIndirect`
  - `sceAgcDcbWriteData`
  - `sceAgcDcbWaitRegMem`
  - `sceAgcDcbDmaData`
  - `sceAgcDcbDispatchIndirect`
  - `sceAgcDcbPushMarker`
  - `sceAgcDcbPopMarker`
  - `sceAgcDcbSetFlip`
  - `sceAgcDriverSubmitDcb`
  - `sceAgcDriverSubmitAcb`
- HLE reference models many Gen5 AGC command-buffer functions as type-3 PM4
  packets with AGC-specific subcommands stored in the low register/subfield
  bits.
- Its packet helper uses this layout:

```c
0xC0000000u |
    (((lengthDwords - 2u) & 0x3FFFu) << 16) |
    ((op & 0xFFu) << 8) |
    ((subcommand & 0x3Fu) << 2)
```

- Packet length is decoded as:

```c
((header >> 16) & 0x3FFFu) + 2u
```

- This differs from the first openagc scaffold, which used a simpler
  `count - 1` low-bit AMD packet helper. openagc should be corrected to use
  the HLE reference/RPCSX length-field layout.
- HLE reference has concrete command encodings for:
  - DCB flip packets
  - DCB/ACB write-data packets
  - DCB/ACB wait-reg-mem packets
  - ACB acquire-mem packets
  - DCB DMA packets
  - DCB draw/index packets
  - marker push/pop packets
  - submit packet parsing
- HLE reference tracks submitted GPU state enough to observe:
  - SH/CX/UC register writes
  - texture descriptors used for presentation
  - release-mem/write-data/DMA side effects
  - flip submission into VideoOut
- Shader handling includes header validation, pointer relocation, shader
  register patching, primitive-state creation, interpolant mapping, and a
  small Gen5 shader translation path for known fullscreen presentation shaders.

Licensing note:

HLE reference is GPL-2.0-or-later. Treat it as a behavioral/reference source unless
openagc licensing is changed. Do not copy implementation code directly into the
current MIT openagc tree.

## RPCSX

Reference path:

`/Users/bizkut/Downloads/PS5/homebrew/rpcsx`

Relevant files:

- `rpcsx/gpu/lib/gnm/include/gnm/pm4.hpp`
- `rpcsx/gpu/lib/gnm/src/pm4.cpp`
- `rpcsx/gpu/Pipe.cpp`
- `rpcsx/gpu/DeviceCtl.cpp`
- `rpcsx/gpu/lib/gnm/include/gnm/descriptors.hpp`
- `rpcsx/gpu/lib/gnm/include/gnm/constants.hpp`
- `rpcsx/gpu/lib/amdgpu-tiler/src/tiler.cpp`
- `rpcsx/gpu/lib/gcn-shader/src/`

Key findings:

- RPCSX is useful mainly as a GPU/PM4/GNM reference, not as a direct AGC HLE
  implementation.
- Its PM4 parser and submit paths confirm the same type-3 packet length layout
  used by HLE reference:

```c
type = header >> 30;
op = (header >> 8) & 0xFF;
lengthDwords = ((header >> 16) & 0x3FFF) + 2;
```

- `DeviceCtl.cpp` shows a host/emulator model for:
  - graphics command submission
  - indirect buffer validation
  - VMID patching into indirect buffer packets
  - flip-on-EOP bookkeeping
  - map/unmap/protect memory control packets
  - compute queue mapping and submit offsets
- `Pipe.cpp` is a useful command interpreter reference for:
  - `IT_SET_SH_REG`
  - `IT_DISPATCH_DIRECT`
  - `IT_DISPATCH_INDIRECT`
  - `IT_RELEASE_MEM`
  - `IT_WAIT_REG_MEM`
  - `IT_WRITE_DATA`
  - `IT_INDIRECT_BUFFER`
  - `IT_ACQUIRE_MEM`
  - `IT_DMA_DATA`
  - graphics draw and register packets
- `gnm/descriptors.hpp` provides compact references for PS4 GNM descriptor
  shapes:
  - 16-byte VBuffer
  - 32-byte TBuffer
  - 16-byte sampler
- `gnm/constants.hpp` has a broad enum set for data formats, numeric formats,
  primitive types, texture types, swizzles, blend factors, compare functions,
  and sampler state values.
- `amdgpu-tiler` contains a detailed AMD surface layout implementation that is
  useful for validating tiling/swizzle assumptions.
- The shader library provides GCN-to-SPIR-V infrastructure and resource
  analysis patterns that may help later emulator/backend work, but it is GCN
  oriented rather than PS5 AGC/RDNA2-native.

Licensing note:

RPCSX is GPLv2 overall except subdirectories with their own license. Treat it
as a reference source for behavior and packet validation. Do not copy GPL code
into the current MIT openagc tree unless licensing is intentionally changed.

## OpenAGC Implementation Status

Completed:

- Done: replaced `agcPm4Header3()` with an AGC/RPCSX/HLE reference-compatible
  type-3 packet helper using bits `29:16` for `lengthDwords - 2`.
- Done: added explicit helpers for:
  - `agcPm4Header3(op, length_dwords)`
  - `agcPm4Header3Sub(op, subcommand, length_dwords)`
  - `agcPm4Length(header)`
  - `agcPm4Opcode(header)`
  - `agcPm4Subcommand(header)`
- Done: updated ACB/DCB tests so they assert the corrected packet layout.
- Done: added `include/agc_nids.h`, `include/agc_re.h`,
  `analysis/agc_known_nids.tsv`, and `analysis/agc_packet_model.md`.

Done in the next pass:

- Added Sony-style command-buffer cursor APIs separately from raw pointer/size
  placeholder APIs.
- Added `SceAgcCb` with recovered offsets:
  - cursor-up: `0x10`
  - cursor-down: `0x18`
  - callback: `0x20`
  - reserved dwords: `0x30`
- Added HLE-reference-confirmed `sceAgcCb*` and `sceAgcDcb*` declarations/builders
  for NOP, dispatch, SH registers, write-data, wait-reg-mem, push/pop marker,
  and flip.
- Added additional HLE-reference-confirmed DCB builders for DMA data, base indirect
  args, indirect dispatch, index buffer setup, draw index offset, draw index
  auto, and wait-until-safe-for-rendering.
- Added recovered submit descriptor layout and generic
  `sceAgcDriverSubmitDcb` / `sceAgcDriverSubmitAcb` validation/debug capture.
- Verified host-generic build with 342 passing assertions.

Remaining corrections:

- Cover any ACB/DCB packet variants recovered from firmware that are not in
  HLE reference.
- Keep native `/dev/gc` submission separate from host packet construction until
  hardware validation is complete.

Longer-term references to mine:

- HLE reference register default groups and shader header offsets.
- HLE reference DCB flip and VideoOut integration behavior.
- RPCSX queue/ring submission model and VMID patching.
- RPCSX AMD tiler for surface size/layout validation.
- RPCSX GCN shader resource analysis for future shader tooling.

## PS5 Kernel /dev/gc Driver RE (FW 5.50)

Reference path:

`/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50/5.50-kv-dump/merged/kernel_550_merged_by_offset.bin`

Relevant kernel offsets:

- `gc_ioctl_internal` at `0x6ed39c` (ioctl dispatch)
- `gc_submit_with_pid` at `0x6e65c0` and `gc_frame_submit_internal` at `0xb7da90`
  (submit path, already documented in `include/agc_ioctl.h`)
- Suspend handler `sub_06e6ff0` at `0x6e6ff0` for ioctl `0xC010811C`
- Final suspend handler for `0xC0108139` (same arg layout, function not yet named)
- Async/direct-TF/HS setup handler `sub_06ee43c` at `0x6ee43c` for ioctls
  `0xC004811F` (SETUP_ASYNC), `0xC0108120` (privileged direct TF setup), and
  related commands
- Queue create handler `sub_06ee502` at `0x6ee502` for kernel-internal ioctl `0x8004812A`
  (not used by SPRX; SPRX uses nr=0x21 for queue create)

Key findings:

- The `gc_ioctl_internal` switch uses multiple binary-search ranges and jump
  tables. A full dispatch map is available in the sibling ps5-openagc project
  at `analysis/ioctl_dispatch.tsv` — but this table contains known errors
  (e.g. FRAME_OPEN nr=0x00 listed as valid). Do not trust it without
  independent verification. See `analysis/ps5_openagc_audit.md`.
- Suspend ioctl `0xC010811C` (nr=0x1c, size=16) reaches `sub_06e6ff0`, which
  reads the 16-byte argument as four little-endian dwords:
  - `field0` at offset 0x00 — validated as 1 or 2 in the simple path; two
    magic triples (`0x0769c766,0x72e8e3c1,0xdb72af28` and
    `0xaf1e80b7,0x8b4cdd90,0x99f68d6c`) are also accepted.
  - `field1` at offset 0x04 — must be <= 3.
  - `field2` at offset 0x08 — must be <= 7.
  - `field3` at offset 0x0C — value written to the selected internal suspend
    ring by `sub_05d7700`.
- `0xC0108139` (nr=0x39, size=16) is the final suspend variant and shares the
  same 16-byte argument layout.
- SETUP_ASYNC (`0xC004811F`) simply writes zero back to the 4-byte user buffer
  and returns success when the device is in the right state.
- The privileged direct TF command (`0xC0108120`) takes a 16-byte argument but
  the FW 5.50 handler
  at `0x6ee476` ignores the user buffer; it only writes constant/derived values
  into device offsets `0x178` and `0x180`.
- The public `sceAgcDriverSetTFRing` export (`XlNp7jzGiPo`, vaddr `0x6840`)
  dispatches to vaddr `0x67e0`; its ioctl wrapper at `0x9180` submits
  `0x80108128` with a 16-byte payload containing ring address at `0x00`, size
  at `0x08`, and zero padding at `0x0C`. It requires 256-byte address alignment
  and dword size alignment. The Direct export (`16IjQxB-Heo`, vaddr `0x6950`)
  is a permission stub returning `0x8A6D0001` and performs no ioctl.
- SET_HS_OFFCHIP (`0xC010812C`) handler at `0x6ee6d2` copies the 16-byte user
  argument directly into `gc_pm4_clearstate_patch` (0xb7dd20) as a patch-list
  pointer and count:
    - `list_addr` at offset 0x00 (GPU/user pointer to 8-byte patch entries)
    - `num_entries` at offset 0x08 (max 0x400)
- QUEUE_CREATE (kernel-internal `0x8004812A`) packs the request into a 4-bit type
  (bits 9:10) and a 9-bit queue index (bits 0:8). However, the SPRX
  (`_sceAgcDriverCreateUserSpecialQueue` at vaddr 0x2c20) does NOT use this ioctl.
  Instead, it uses ioctl nr=0x21 (`0xC0408121`, 64-byte RW) with a struct
  containing three hardcoded magic authentication tokens (0xaf1e80b7,
  0x8b4cdd90, 0x99f68d6c), a secondary token (0xe5fcc174), ring buffer
  addresses carved from internal memory, pipe_id=0xc, and ring_size=0x1000.
- QUEUE_DESTROY (SPRX-confirmed) uses ioctl nr=0x0e (`0xC00C810E`, 12-byte RW)
  with only the three magic tokens. The kernel identifies the queue from
  process state. The kernel-internal nr=0x2b is NOT used by the SPRX.
- `sceAgcDriverSetupAsyncGraphics` (SPRX at vaddr 0x3ac0) uses ioctl nr=0x26
  (`0x80048126`, 4-byte READ) with arg=1, NOT nr=0x1f (SETUP_ASYNC) as
  previously assumed. The SPRX caches the initialized state and only calls
  the ioctl once. The pipe_id parameter controls a flag in SPRX global state.
- `NotifyDefaultStates` kernel consumption path remains unmapped; the current
  evidence is that the primary/internal register-defaults blobs are built in
  GPU-visible memory and consumed later via CLEAR_STATE patching or context
  setup, not through a single dedicated ioctl.
- Kernel analysis update for `NotifyDefaultStates`:
  - `gc_pm4_clearstate_patch` is at `0xb7dd20`; only direct caller found is the
    `SET_HS_OFFCHIP` ioctl handler at `0x6ee6d2` (confirmed via call-rel32 scan).
  - `CONTEXT_STATE` / `CONTEXT_STATE_OP` strings at `0xf929aa` and `0x100a8b1`
    are only used by the GPU-fault string-lookup routine at `0x52817f`, not by
    a dedicated kernel default-state loader.
  - Conclusion: there is no separate kernel ioctl for `NotifyDefaultStates`.
    The primary/internal register-defaults blobs built in GPU-visible memory are
    consumed by the GPU itself when the userspace driver later emits a
    `CLEAR_STATE` or `CONTEXT_STATE` PM4 packet referencing those blobs. The
    kernel's role is limited to validating/submitting the DCB that contains the
    packet.
- The NIDs for these entry points were recovered from `libSceAgcDriver.sprx`
  using `prospero-nid`:
  - `sceAgcDriverSuspendPointSubmitDirect`: `ZV04pRl7cWU`
  - `sceAgcDriverNotifyDefaultStates`: `nR6xhiFsOoc`
  - `sceAgcDriverSetupAsyncGraphics`: `Vlaj1gwmIFA`
  - `sceAgcDriverSetTFRing`: `XlNp7jzGiPo`
  - `sceAgcDriverSetTFRingDirect`: `16IjQxB-Heo`
  - `sceAgcDriverSetHsOffchipParamDirect`: `DPcAnsOlTQs`
  - `sce_agc_internal_suspend_point_submit_final`: `ZrN+92fezeA`
  - `_sceAgcDriverCreateUserSpecialQueue`: `031OFjGn0Eo`
  - `_sceAgcDriverDestroyUserSpecialQueue`: `4dk+ll564oM`
  - `sceAgcSuspendPoint`: `h9z6+0hEydk`

Licensing note:

The kernel dump is a RE reference only. Constants and layout facts recovered
from it are re-expressed as open data structures in the MIT openagc tree.

## PS5 AGC PM4 Packet Builders (ACB / DCB)

Reference path:

`/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc/src/agc_acb_native.c`
(NOT proven working — used for PM4 opcode cross-reference only)

Relevant openagc implementation files:

- `src/cb_builders.c` — cursor-based DCB / generic CB builders
- `src/acb.c` — old-style ACB builders (some still stubs)

Key findings recovered from the ps5-openagc native-PM4 reference and AMD RDNA2
PM4 convention (PM4 opcodes cross-verified against HLE reference and SPRX):

- `sceAgcAcbEventWrite` (ACB) emits `IT_EVENT_WRITE_EOP` (AGC opcode 0x47)
  with a 5-dword packet:
  - header (`PM4_TYPE3(0x47, 5)`)
  - `event_info` = `event_type[5:0] | gen_int[21] | int_ctx[24]`
  - 64-bit GPU address
  - 32-bit data value
- `sceAgcAcbAcquireMem` (ACB) emits the Ariel-specific `IT_AGC_0x58`
  (opcode 0x58) with an 8-dword packet:
  - coher_cntl, coher_size, 64-bit coher_base, engine_sel, reserved
- `sceAgcAcbWaitRegMem` (ACB) emits `IT_WAIT_REG_MEM` (opcode 0x3C) with
  a 6-dword packet:
  - wait_info, reference, mask, 64-bit poll address
- `sceAgcAcbWriteData` (ACB) emits `IT_WRITE_DATA` (opcode 0x37) with
  a 5+ dword packet:
  - control, 64-bit address, then data dwords
- `sceAgcAcbAtomicMem` (ACB) emits `IT_ATOMIC_MEM` (opcode 0x1B) with
  a 5-dword packet:
  - control, 64-bit address, 32-bit data
- `sceAgcAcbCopyData` (ACB) emits `IT_COPY_DATA` (opcode 0x40) with
  a 6-dword packet:
  - control (src_sel/dst_sel), src address, dst address
- `sceAgcAcbCondExec` (ACB) emits `IT_COND_EXEC` (opcode 0x22) with
  a 4-dword packet:
  - 64-bit condition address, execution count
- `sceAgcAcbJump` (ACB) emits `IT_INDIRECT_BUFFER` (opcode 0x3F) with
  a 4-dword packet:
  - 64-bit IB address, control word
- `sceAgcAcbMemSemaphore` (ACB) emits `IT_MEM_SEMAPHORE` (opcode 0x39) with
  a 4-dword packet:
  - 64-bit address, 32-bit data
- Implemented in `src/acb.c`:
  - `sceAgcAcbAcquireMem` — `IT_ACQUIRE_MEM` (0x58), 8 dwords
  - `sceAgcAcbEventWrite` — `IT_EVENT_WRITE_EOP` (0x47), 5 dwords
  - `sceAgcAcbAtomicMem` — `IT_ATOMIC_MEM` (0x1B), 5 dwords
  - `sceAgcAcbCondExec` — `IT_COND_EXEC` (0x22), 4 dwords
  - `sceAgcAcbWaitRegMem` — `IT_WAIT_REG_MEM` (0x3C), 6 dwords
  - `sceAgcAcbWriteData` — `IT_WRITE_DATA` (0x37), 5 dwords
  - `sceAgcAcbCopyData` — `IT_COPY_DATA` (0x40), 6 dwords
  - `sceAgcAcbMemSemaphore` — `IT_MEM_SEMAPHORE` (0x39), 4 dwords
  - `sceAgcAcbDmaData` — `IT_DMA_DATA` (0x50), 8 dwords
  - `sceAgcAcbResetQueue` — `IT_AGC_0x79` (0x79), 3 dwords
  - `sceAgcAcbRewind` — `IT_NOP` (0x10), 2 dwords
  - `sceAgcAcbSetFlip` — `IT_RELEASE_MEM` (0x49), 7 dwords
  - `sceAgcAcbJump` — `IT_INDIRECT_BUFFER` (0x3F), 4 dwords (already correct)
- Implemented in `src/dcb.c` (VSH DCB helpers):
  - `sceAgcDcbAtomicGds` — `IT_ATOMIC_GDS` (0x1D), 10 dwords
  - `sceAgcDcbContextStateOp` — variable-length `IT_SET_*_REG` (op selects
    register space), 1 header + 1 offset + `reg_count` data dwords
  - `sceAgcDcbResetQueue` — `IT_AGC_0x79` (0x79), 3 dwords
  - `sceAgcDcbSetWorkloadComplete` — exact 12-dword prefix plus
    `IT_SET_WORKLOAD` (0x1E), DCB control 0
  - `sceAgcDcbSetWorkloadStreamInactive` — exact 9-dword zero-mask prefix
  - `sceAgcDcbSetWorkloadsActive` — exact 18-dword prefix plus
    `IT_SET_WORKLOAD` (0x1E), DCB control 0
  - ACB workload wrappers share those layouts with control 1 and use the exact
    cursor ABI rather than the former raw-buffer approximation
  - `sceAgcDcbSetPreemption` — NOP placeholder (opcode pending), 2 dwords
  - `sceAgcDcbWaitUntilSafeForRendering` — NOP with `WAIT_FLIP_DONE`
    subcommand, 7 dwords
- All old-style ACB helpers in `include/agcdriver.h` are now implemented with
  real PM4 packets; no remaining NOP/placeholder stubs in that surface.
- `sceAgcDcbContextStateOp` is the key DCB entry point for the
  `NotifyDefaultStates` / `CLEAR_STATE` / `CONTEXT_STATE` userspace submission
  path: it emits the actual `SET_CONTEXT_REG` / `SET_SH_REG` / `SET_UCONFIG_REG`
  packets that load the GPU-visible register-default blobs built earlier by
  the ACB/CB builders.

DCB builders (`sceAgcDcb*`) are implemented in `src/cb_builders.c` and
follow the same Gen5 AGC type-3 PM4 header convention (`length_dwords - 2` in
bits 29:16). VSH DCB shell functions (`sceAgcDcb*`) are implemented in
`src/dcb.c`; all are now mapped in `include/agcdriver.h` and emit real packets
except `sceAgcDcbSetPreemption`, which is intentionally an unimplemented
VSH-only stub.

## `sceAgcDcbSetPreemption` — SPRX RE finding

- `sceAgcDcbSetPreemption` in `libSceAgcVsh.sprx` (vaddr 0x4140) is a stub
  that prints `line %d: %s() is not allowed to be called from agc vsh.` and
  executes `int 0x41` (crash). It is not exported via the NID table and has no
  ordinal.
- No "preempt" string appears in `libSceAgc.sprx` or `libSceAgcDriver.sprx`.
- Real GPU preemption is handled kernel-side by `gc_pm4_suspend_point_marker`
  (kernel 0xb7eaf0), which emits `IT_AGC_0x93` (8-dword packet). This opcode
  is exposed as `AGC_PM4_OP_SUSPEND_POINT_MARKER` (alias of the existing
  `AGC_PM4_OP_WAIT_REG_MEM64 = 0x93`).
- openagc `sceAgcDcbSetPreemption` returns `AGC_ERROR_INVALID_STATE` instead
  of crashing.

## `sceAgcDriverNotifyDefaultStates` userspace submission path

- `sceAgcDriverNotifyDefaultStates` (prospero backend, `src/driver_prospero.c`):
  1. Allocates GPU-visible memory for the primary and internal register-default
     blobs.
  2. Builds both blobs with `agcRegisterDefaultsBuild()`.
  3. Allocates a 2-dword DCB, emits `IT_CLEAR_STATE` (opcode 0x14), and submits
     it via `sceAgcDriverSubmitDcb()`.
  4. The kernel patches `CLEAR_STATE` via `gc_pm4_clearstate_patch` (0xb7dd20);
     the GPU consumes the GPU-visible blobs during context reset.
- A new public helper `sceAgcDcbClearState()` emits a standalone
  `IT_CLEAR_STATE` (0x14) packet for applications that need to trigger the
  reset outside of `NotifyDefaultStates`.

## Suspend points

- `sceAgcDriverSuspendPointSubmitDirect` submits a 16-byte suspend argument via
  `AGC_GC_IOCTL_SUSPEND_16` (nr=0x1c); the internal final variant uses
  `AGC_GC_IOCTL_SUSPEND_39` (nr=0x39).
- `sceAgcDriverIsSuspendPointInFlightDirect` now queries the gfx queue status
  via `AGC_GC_IOCTL_QUEUE_STAT_16` (nr=0x27) and returns whether the status is
  non-zero. The exact bit layout for the suspend-point in-flight state is still
  pending RE; the current implementation is a conservative placeholder.
- `sceAgcSuspendPointAndCheckStatus` combines a direct suspend-point submit with
  the in-flight query, returning `AGC_ERROR_BUSY` while in flight and `AGC_OK`
  once it is no longer in flight.

## NID Table Expansion (ps5-openagc cross-reference)

NID mappings from ps5-openagc were cross-verified against the actual FW 5.50
SPRX symbol tables. The NID-to-name mapping is reliable; ps5-openagc's
ioctl layouts and implementation code are NOT (see
`analysis/ps5_openagc_audit.md`).

- Cross-referenced our HLE reference-derived NID identifications with
  `ps5-openagc/include/ps5/internal/agc_nid.h` (auto-generated from FW 5.50
  SPRX NID matching).
- FW 5.50 export counts: libSceAgc=222, libSceAgcDriver=145, libSceAgcVsh=219.
- ps5-openagc identified: 43/222 (libSceAgc), 19/145 (libSceAgcDriver),
  43/219 (libSceAgcVsh).
- Combined with our HLE reference identifications, openagc now has 78 identified
  NIDs in `include/agc_nids.h` and `analysis/agc_known_nids.tsv`.
- Key new identifications from ps5-openagc:
  - All 22 ACB builders (sceAgcAcb* functions)
  - All 13 Vsh DCB builders (sceAgcDcb* functions)
  - All 18 libSceAgcDriver exports (sceAgcDriver* functions)
  - State queries: sceAgcGetDefaultCxStateFlat, sceAgcGetRegisterDefaults,
    sceAgcSuspendPointAndCheckStatus
- The remaining ~514 UNKNOWN exports are mostly small stub functions (3-10
  bytes) that return error codes or constants. Size-based guessing is
  unreliable — many small functions are genuinely unidentifiable without
  SDK headers or debug symbols.

## Shader Record Parser Deepening

- Added `AgcShaderSpecials` struct (16 bytes) with fields:
  - `ge_cntl` — GE_CNTL (geometry engine control)
  - `vgt_shader_stages_en` — VGT_SHADER_STAGES_EN (stage enable bits)
  - `vgt_gs_out_prim_type` — VGT_GS_OUT_PRIM_TYPE (GS output primitive)
  - `ge_user_vgpr_en` — GE_USER_VGPR_EN (user VGPR enable)
- Added `AgcShaderUserData` struct (40 bytes) with 5× 64-bit entries
  for resource descriptor pointers (T#, V#, S#) and inline constants.
- Added typed accessors: `agcShaderRecordGetSpecialsTyped`,
  `agcShaderRecordGetUserDataTyped`, `agcShaderRecordGetShRegisterValues`,
  `agcShaderRecordGetCxRegisterValues`.
- Key difference from PS4 GNM: PS5 AGC uses a pointer-based indirection
  model for shader records (header contains pointers to sub-blocks),
  unlike PS4 GNM's monolithic inline model.

## Texture/Surface Descriptor Format Expansion

- Cross-referenced descriptor formats with shadPS4, RPCSX, and freegnm.
- Added 18-value `AgcTileMode` enum matching shadPS4's TileMode (depth,
  display, thin, thick variants; LinearGeneral=31, LinearAligned=8).
- Added 8-value `AgcImageType` enum (1D, 2D, 3D, Cube, 1DArray, 2DArray,
  2DMSAA, 2DMSAAArray).
- Extended `AgcDataFormat` from 13 to 28 values: BC2/BC4/BC5/BC6,
  depth/stencil (8_24, 24_8, X24_8_32), Fmask (4 variants),
  subsampled (GbGr, BgRg), shared exponent (5_9_9_9, 10_11_11).
- Extended `AgcNumberType` with Srgb and UnormSnorm.
- Added `AgcClampMode` (5 values), `AgcFilterMode` (4 values),
  `AgcMipFilterMode` (3 values), `AgcBorderColor` (4 values).
- Existing T# (32-byte) and V# (16-byte) descriptor implementations
  are correct per cross-reference with all sources.
- Render target descriptor now has full 64-byte layout with CMASK/FMASK/DCC
  compression fields matching freegnm's `GnmRenderTarget`.

## SPRX Disassembly — Large UNKNOWN Functions (FW 5.50)

Disassembled and analyzed the largest unidentified functions in
`libSceAgc.sprx` and `libSceAgcDriver.sprx` using Capstone x86_64.

### libSceAgc.sprx findings

**Shader linking functions (HIGH confidence on behavior):**
- `fd5Bp5tGTgo` (ordinal 131, 1603B): Checks `shader_type` at offset 0x5A
  for HS(4)+CS(6) or LS(5)+CS(6) pairs. Copies 0x60-byte shader record,
  sets output type to GS(2). Accesses specials at offset 0x28.
  Error 0x8a6c0008. This is a **shader linking function** that combines
  Hull/Local shader + Compute shader into a Geometry shader record.
  Likely `sceAgcShaderLinkHsGs` / `sceAgcShaderLinkLsGs`.
- `nApJjpKNBl4` (ordinal 219, 1477B): Same pattern as above — likely a
  pre-0100 variant of the shader linking function.

**Shader query/extraction functions (MEDIUM confidence):**
- `MqAdbRMdNz4` (ordinal 132, 1105B): Writes to output at [rdi + 0x100],
  accesses shader specials via [rcx + 0x28]. 18-way branch (cmp ebp, 0x11).
  Likely `sceAgcShaderGetSpecials` or `sceAgcShaderGetUserData`.
- `NKIzURsgV7I` (ordinal 137, 1232B): No calls/syscalls. Manipulates bit
  fields, checks edx == -1 special case. Shader register/specials field
  extractor. LOW confidence on exact name.

**Packet builder variants (MEDIUM confidence):**
- `57labkp+rSQ` (ordinal 48, 687B): Writes 0xc0065800 = type3 PM4 header
  with opcode 0x58 (IT_ACQUIRE_MEM). DCB variant of `sceAgcAcbAcquireMem`.
- `03RZmELWWzw` (ordinal 38, 707B): Calls through function pointer
  [r8 + 0x20], processes array of dwords. Likely `sceAgcCbSetCxRegistersDirect`
  or similar batch register setter.

### libSceAgcDriver.sprx findings

**Flip/display functions (HIGH confidence):**
- `cwbxjPSJ7WQ` (ordinal 49, 585B): References strings
  "displayBufferIndex is invalid value" and "sceVideoOutSubmitEopFlip
  failed". Checks buffer index < 18 (0x12). Error 0x8029000a (VideoOut
  error code). This is `sceAgcDriverSubmitEopFlip` or a flip-related
  submit function.
- `u8BkdHb1+Po` (ordinal 50, 428B): Same string references as above.
  A variant of the EOP flip submit function.

**Submit path functions (MEDIUM confidence):**
- `lYz7vbL4W4A` (ordinal 20, 958B): Error 0x8a6d0003
  (AGC_ERROR_INVALID_ARG). Checks count > 0x400 (1024 max). Dynamic
  stack allocation (alloca) based on count. Calls malloc + multiple
  internal helpers. Likely `sceAgcDriverSubmitMultiCommandBuffers` or
  a batch submit function.
- `Fj7r9EHzF38` (ordinal 13, 579B): Uses `lock xadd` (atomic sequence
  number increment). References "Submit failed" strings. Likely
  `sceAgcDriverGetSubmitDone` or a submit sequence helper.
- `QcmHLO2n7mk` (ordinal 18, 767B): References "Submit failed - Unable
  to lock" and "Submit failed - Unlock error". Mutex lock/unlock pattern
  with 13 calls. Submit-with-lock helper.
- `F5ZTyyVUHTs` (ordinal 104, 449B): Checks edi >= 0x58 (88) or >= 0x20
  (32). Table lookups at 0x1a3e0 and 0x18460. Register/queue lookup
  function. LOW confidence on exact name.

### Key error codes discovered
- `0x8a6c0008` — AGC shader error (shader linking/validation)
- `0x8a6d0000` — AGC driver error base
- `0x8a6d0003` — AGC_ERROR_INVALID_ARG (argument validation)
- `0x8029000a` — VideoOut error (display/flip functions)

### Key string references found
- "[AgcDriver] Error - displayBufferIndex is invalid value."
- "[AgcDriver] Error - Submit failed - Unable to lock."
- "[AgcDriver] Error - Submit failed - Unlock error."
- "[AgcDriver] Error - sceVideoOutGetBufferLabelAddress failed."
- "[AgcDriver] Error - sceVideoOutSubmitEopFlip failed."

These confirm that libSceAgcDriver internally calls sceVideoOut functions
for flip operations, and uses mutex locking for submit serialization.

## Deep SPRX Capstone Disassembly (Session 2)

57 unknown export functions (>200 bytes) disassembled with Capstone across
all three SPRX files. Full findings report (from ps5-openagc, used for NID
identification only) at:
`/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc/analysis/nid_identification_findings.md`

### NID identification results

- **36 new NID identifications** (12 HIGH, 7 MEDIUM, 4 LOW confidence, 9 driver, 4 flip)
- libSceAgc: 43 → 82 identified exports
- libSceAgcDriver: 19 → 29 identified exports
- libSceAgcVsh: 43 → 78 identified exports (first deep analysis of this SPRX)

### New HIGH-confidence identifications

| NID | SPRX | Identified Name | Evidence |
|-----|------|-----------------|----------|
| `wr23dPKyWc0` | AGC/VSH | `sceAgcCbReleaseMem` | String "isReleaseMemValid", opcode 0x49 |
| `w1KFAHVqpaU` | AGC/VSH | `sceAgcDcbIndirectBuffer` | Opcode 0x3F, 14-dword IB |
| `xSAR0LTcRKM` | AGC/VSH | `sceAgcDcbJump` | Opcode 0x3F, 4-dword IB |
| `1rZSWUv1IRc` | AGC/VSH | `sceAgcDcbDrawIndirect` | Opcode 0x24, VGT_INDEX_TYPE ref |
| `q88lQ+GP5Yk` | AGC/VSH | `sceAgcDcbDrawIndex2` | Opcode 0x27 |
| `t1vNu082-jM` | AGC/VSH | `sceAgcDcbDrawIndexIndirect` | Opcode 0x25 |
| `kUlvghKs-mA` | AGC/VSH | `sceAgcDcbDrawIndirectMulti` | Opcode 0x2C |
| `ypVBz4uPKcQ` | AGC/VSH | `sceAgcDcbDrawIndexIndirectMulti` | Opcode 0x38 |
| `bbFueFP+J4k` | AGC/VSH | `sceAgcDcbSetPredication` | Opcode 0x20 |
| `aJf+j5yntiU` | AGC/VSH | `sceAgcDcbEventWrite` | Opcode 0x46 |
| `BVFg3CWU6Eo` | AGC/VSH | `sceAgcDcbSetConfigReg` | Opcode 0x68 |
| `n2fD4A+pb+g` | AGC/VSH | `sceAgcDcbSetShReg` | Opcode 0x76 |
| `MDLD5Ly94Xk` | AGC/VSH | `sceAgcDcbSetUconfigReg` | Opcode 0x79 |

### New driver function identifications

| NID | Identified Name | Evidence |
|-----|-----------------|----------|
| `UM9b9NunSrE` | `sceAgcDriverSetWorkloadsActive` | Multi-argument nine-dword SET_WORKLOAD 0x1E builder; not OpenAGC's one-ID ABI |
| `i6bfTi13ApA` | `sceAgcDriverSetWorkloadComplete` | Multi-argument nine-dword SET_WORKLOAD 0x1E builder; not OpenAGC's one-ID ABI |
| `Hj4eWnDektQ` | `sceAgcDriverSubmitCommandBuffers` | INDIRECT_BUFFER 0x3F, error 0x8a6d0001 |
| `XNbrdwCsZ9A` | `sceAgcDriverMapComputeQueue` | Error 0x8a6d0000, validation max 0x1f queues |
| `b4fpgH5ZXxQ` | `sceAgcDriverInitializeQueue` | Atomic counter, "[AgcDriver" string |
| `oFb2hMcoJa4` | `sceAgcDriverWaitIdle` | Spin-wait loops, error 0x8a6d0005 |

### New AGC-custom flip/display opcodes discovered

| Opcode | Tentative Name | Evidence |
|--------|----------------|----------|
| 0x4C | WAIT_FLIP (variant 1) | libSceAgc only, flip validation pattern |
| 0x4E | WAIT_FLIP (variant 2) | libSceAgc only |
| 0x4F | WAIT_FLIP (variant 3) | libSceAgc only, two opcodes emitted |
| 0x51 | WAIT_FLIP (variant 4) | libSceAgc only |
| 0x54 | INSERT_WAIT_FLIP_DONE | Sub-field 0x06 (WAIT_FLIP_DONE) |

### Error code patterns confirmed

- `0x8a6cNNNN` — AGC library errors (libSceAgc / libSceAgcVsh)
- `0x8a6dNNNN` — Driver errors (libSceAgcDriver)
- `0x8029000a` — VideoOut errors

### PM4 opcode verification

The SPRX disassembly confirmed that `agc_pm4.h` opcode values are correct:
SET_CONFIG_REG=0x68, SET_SH_REG=0x76, SET_UCONFIG_REG=0x79, WRITE_DATA=0x37,
RELEASE_MEM=0x49, INDIRECT_BUFFER=0x3F, EVENT_WRITE=0x46 — all match the SPRX.
