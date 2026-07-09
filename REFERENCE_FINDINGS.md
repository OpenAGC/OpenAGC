# openagc Reference Findings

This document records findings from local PS5 emulator/reference projects used
to guide openagc development.

## SharpEmu

Reference path:

`/Users/bizkut/Downloads/PS5/homebrew/sharpemu`

Relevant files:

- `src/SharpEmu.Libs/Agc/AgcExports.cs`
- `src/SharpEmu.Libs/Agc/Gen5ShaderTranslator.cs`
- `src/SharpEmu.Libs/VideoOut/VideoOutExports.cs`
- `src/SharpEmu.Libs/VideoOut/VulkanVideoPresenter.cs`
- `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs`

Key findings:

- SharpEmu has the most directly useful PS5 AGC HLE material found so far.
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
- SharpEmu models many Gen5 AGC command-buffer functions as type-3 PM4
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
  the SharpEmu/RPCSX length-field layout.
- SharpEmu has concrete command encodings for:
  - DCB flip packets
  - DCB/ACB write-data packets
  - DCB/ACB wait-reg-mem packets
  - ACB acquire-mem packets
  - DCB DMA packets
  - DCB draw/index packets
  - marker push/pop packets
  - submit packet parsing
- SharpEmu tracks submitted GPU state enough to observe:
  - SH/CX/UC register writes
  - texture descriptors used for presentation
  - release-mem/write-data/DMA side effects
  - flip submission into VideoOut
- Shader handling includes header validation, pointer relocation, shader
  register patching, primitive-state creation, interpolant mapping, and a
  small Gen5 shader translation path for known fullscreen presentation shaders.

Licensing note:

SharpEmu is GPL-2.0-or-later. Treat it as a behavioral/reference source unless
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
  used by SharpEmu:

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

- Done: replaced `agcPm4Header3()` with an AGC/RPCSX/SharpEmu-compatible
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
- Added SharpEmu-confirmed `sceAgcCb*` and `sceAgcDcb*` declarations/builders
  for NOP, dispatch, SH registers, write-data, wait-reg-mem, push/pop marker,
  and flip.
- Added additional SharpEmu-confirmed DCB builders for DMA data, base indirect
  args, indirect dispatch, index buffer setup, draw index offset, draw index
  auto, and wait-until-safe-for-rendering.
- Added recovered submit descriptor layout and generic
  `sceAgcDriverSubmitDcb` / `sceAgcDriverSubmitAcb` validation/debug capture.
- Verified host-generic build with 342 passing assertions.

Remaining corrections:

- Cover any ACB/DCB packet variants recovered from firmware that are not in
  SharpEmu.
- Keep native `/dev/gc` submission separate from host packet construction until
  hardware validation is complete.

Longer-term references to mine:

- SharpEmu register default groups and shader header offsets.
- SharpEmu DCB flip and VideoOut integration behavior.
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
- Async/TF/HS setup handler `sub_06ee43c` at `0x6ee43c` for ioctls
  `0xC004811F` (SETUP_ASYNC), `0xC0108120` (SET_TF_RING), and related commands
- Queue create handler `sub_06ee502` at `0x6ee502` for ioctl `0x8004812A`

Key findings:

- The `gc_ioctl_internal` switch uses multiple binary-search ranges and jump
  tables. A full dispatch map is available in the sibling ps5-openagc project
  at `analysis/ioctl_dispatch.tsv`.
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
- SET_TF_RING (`0xC0108120`) takes a 16-byte argument but the FW 5.50 handler
  at `0x6ee476` ignores the user buffer; it only writes constant/derived values
  into device offsets `0x178` and `0x180`.
- SET_HS_OFFCHIP (`0xC010812C`) handler at `0x6ee6d2` copies the 16-byte user
  argument directly into `gc_pm4_clearstate_patch` (0xb7dd20) as a patch-list
  pointer and count:
    - `list_addr` at offset 0x00 (GPU/user pointer to 8-byte patch entries)
    - `num_entries` at offset 0x08 (max 0x400)
- QUEUE_CREATE (`0x8004812A`) packs the request into a 4-bit type (bits 9:10)
  and a 9-bit queue index (bits 0:8), matching the existing `AgcOrbisQueueArg`
  layout in `src/driver_orbis.c`.
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

Relevant openagc implementation files:

- `src/cb_builders.c` — cursor-based DCB / generic CB builders
- `src/acb.c` — old-style ACB builders (some still stubs)

Key findings recovered from the ps5-openagc native-PM4 reference and AMD RDNA2
PM4 convention:

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
  - `sceAgcVshDcbAtomicGds` — `IT_ATOMIC_GDS` (0x1D), 10 dwords
  - `sceAgcVshDcbContextStateOp` — variable-length `IT_SET_*_REG` (op selects
    register space), 1 header + 1 offset + `reg_count` data dwords
  - `sceAgcVshDcbResetQueue` — `IT_AGC_0x79` (0x79), 3 dwords
  - `sceAgcVshDcbSetWorkloadComplete` — `IT_SET_WORKLOAD` (0x1E), 8 dwords
  - `sceAgcVshDcbSetWorkloadStreamInactive` — `IT_AGC_0x79` (0x79), 3 dwords
  - `sceAgcVshDcbSetWorkloadsActive` — `IT_SET_WORKLOAD` (0x1E), 8 dwords
  - `sceAgcVshDcbSetPreemption` — NOP placeholder (opcode pending), 2 dwords
  - `sceAgcVshDcbWaitUntilSafeForRendering` — NOP with `WAIT_FLIP_DONE`
    subcommand, 7 dwords
- All old-style ACB helpers in `include/agcdriver.h` are now implemented with
  real PM4 packets; no remaining NOP/placeholder stubs in that surface.
- `sceAgcVshDcbContextStateOp` is the key DCB entry point for the
  `NotifyDefaultStates` / `CLEAR_STATE` / `CONTEXT_STATE` userspace submission
  path: it emits the actual `SET_CONTEXT_REG` / `SET_SH_REG` / `SET_UCONFIG_REG`
  packets that load the GPU-visible register-default blobs built earlier by
  the ACB/CB builders.

DCB builders (`sceAgcDcb*`) are implemented in `src/cb_builders.c` and
follow the same Gen5 AGC type-3 PM4 header convention (`length_dwords - 2` in
bits 29:16). VSH DCB shell functions (`sceAgcVshDcb*`) are implemented in
`src/dcb.c`; all are now mapped in `include/agcdriver.h` and emit real packets
except `sceAgcVshDcbSetPreemption`, which is intentionally an unimplemented
VSH-only stub.

## `sceAgcVshDcbSetPreemption` — SPRX RE finding

- `sceAgcVshDcbSetPreemption` in `libSceAgcVsh.sprx` (vaddr 0x4140) is a stub
  that prints `line %d: %s() is not allowed to be called from agc vsh.` and
  executes `int 0x41` (crash). It is not exported via the NID table and has no
  ordinal.
- No "preempt" string appears in `libSceAgc.sprx` or `libSceAgcDriver.sprx`.
- Real GPU preemption is handled kernel-side by `gc_pm4_suspend_point_marker`
  (kernel 0xb7eaf0), which emits `IT_AGC_0x93` (8-dword packet). This opcode
  is exposed as `AGC_PM4_OP_SUSPEND_POINT_MARKER` (alias of the existing
  `AGC_PM4_OP_WAIT_REG_MEM64 = 0x93`).
- openagc `sceAgcVshDcbSetPreemption` returns `AGC_ERROR_INVALID_STATE` instead
  of crashing.

## `sceAgcDriverNotifyDefaultStates` userspace submission path

- `sceAgcDriverNotifyDefaultStates` (orbis backend, `src/driver_orbis.c`):
  1. Allocates GPU-visible memory for the primary and internal register-default
     blobs.
  2. Builds both blobs with `agcRegisterDefaultsBuild()`.
  3. Allocates a 2-dword DCB, emits `IT_CLEAR_STATE` (opcode 0x14), and submits
     it via `sceAgcDriverSubmitDcb()`.
  4. The kernel patches `CLEAR_STATE` via `gc_pm4_clearstate_patch` (0xb7dd20);
     the GPU consumes the GPU-visible blobs during context reset.
- A new public helper `sceAgcVshDcbClearState()` emits a standalone
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
