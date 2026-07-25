# openagc Plan

## Goal

Build a clean open-source PS5 AGC implementation in stages:

1. Recover the AGC packet, command-buffer, shader, queue, and submit model.
2. Make that model host-testable before touching hardware.
3. Implement a native PS5 `/dev/gc` backend once ioctl and memory ownership are
   understood.
4. Expand from basic packet submission into native PS5 graphics features.

The project target is native PS5 AGC behavior, not PS4 GNM compatibility. GNM
is still a valuable reference because PS5 backward compatibility, GNM, AGC, and
AMD PM4 packet ancestry overlap in useful ways.

## Target Priority

Games built with openagc must run on **real PS5 hardware first**, then on
emulators (reference implementation, HLE reference) as secondary dev/testing targets on PC.

This priority determines the trust hierarchy for packet encodings:

1. **SPRX disassembly (ground truth)** — the actual SDK functions that run on
   real PS5 hardware. openagc's SPRX-confirmed encodings are authoritative.
2. **The reference implementation** — best secondary reference. Its packet builders reproduce what
   the SDK outputs (real opcodes like `IT_DRAW_INDEX_AUTO`,
   `IT_SET_UCONFIG_REG_INDEX`), and its full PM4 command processor validates
   them. Games run through the reference implementation, so its encodings are empirically tested
   against game expectations.
3. **openagc** — this project. SPRX RE-confirmed encodings match the reference. Some
   builders still use NOP-wrapped stubs that need switching to real opcodes.
4. **HLE reference** — useful for NID discovery and cross-reference, but many of its
   packet encodings would fail on real PS5 hardware. Its NOP-wrapped stubs work
   only because the HLE reference's inline interpreter doesn't validate packet format.
   Do NOT adopt the HLE reference's packet encodings without independent SPRX or reference
   confirmation. The HLE reference's NID discoveries are safe to adopt regardless of
   encoding correctness.

## Evidence Levels

Use these labels in docs, analysis notes, and code comments:

- **Implemented**: present in openagc, covered by host tests.
- **SPRX-confirmed**: verified against firmware 5.50 SPRX disassembly — highest
  confidence for real-PS5 correctness.
- **reference-confirmed**: verified against the reference implementation (working PS5 emulator
  with full PM4 command processor). High confidence for real-PS5 correctness.
- **Observed**: found in HLE reference, RPCSX, ps5-openagc notes (NID mapping only —
  ps5-openagc is NOT proven working and contains known ioctl errors; see
  `analysis/ps5_openagc_audit.md`), firmware strings, or local analysis, but
  not implemented yet. HLE reference packet encodings are NOT trusted without
  independent SPRX or reference confirmation.
- **Inferred**: likely from AMD/RDNA2 behavior or reference projects, but not
  confirmed in AGC firmware paths yet.
- **Speculative**: plausible roadmap item with no local implementation evidence
  yet.

Do not promote a feature from inferred/speculative to implemented until there
is a packet, structure, register, test, or hardware validation artifact.

## Architecture Context

PS5 can run PS4 games through hardware-level backward compatibility modes. In
that path, the GPU exposes behavior compatible with PS4 GNM/GNMX command
streams and legacy GCN assumptions.

Native PS5 software targets AGC. AGC should be treated as a Gen5/RDNA2-facing
model with its own command buffers, packet wrappers, shader records, queues,
cache synchronization, and `/dev/gc` ioctl ABI.

Practical rule:

- Use GNM/RPCSX/opengnm for packet ancestry, descriptor patterns, tiling, and
  queue interpretation.
- Use the reference implementation as the highest-priority emulator reference for PS5 AGC packet
  encodings, register defaults, and PM4 command processing. The reference implementation is a
  working emulator with a full PM4 command processor — its packet builders
  use real AMD/AGC opcodes and are empirically validated against real games.
- Use firmware 5.50 SPRX disassembly as ground truth for real-PS5 correctness.
  SPRX-confirmed encodings are authoritative.
- Use HLE reference for NID discovery and cross-reference only. The HLE reference's packet
  encodings are NOT trusted — many use NOP-wrapped stubs that would fail on
  real PS5 hardware. The HLE reference's NID findings are safe to adopt.
- Avoid assuming a PS4 GNM packet is valid AGC behavior unless AGC evidence
  confirms it.

## GNM to AGC Capability Map

| Area | PS4 GNM / GNMX | PS5 AGC target | Current evidence |
|---|---|---|---|
| GPU architecture | GCN | RDNA2 / Gen5 AGC | Observed from platform context |
| Wavefront model | Wave64-centric | Wave32/Wave64 metadata and register state | Inferred; shader header work needed |
| Geometry pipeline | VS/HS/DS/GS fixed-function path | AGC shader records and possible task/mesh-style paths | Speculative until firmware/shader evidence |
| Ray tracing | Software compute only | Ray acceleration/BVH state if exposed | Speculative until AGC evidence |
| Shading rate | Uniform rate | VRS/rate-image state if exposed | Speculative until register/packet evidence |
| Cache sync | Coarser GCN cache flush/invalidate model | AGC acquire/release/wait/cache-policy packets | Partially observed and partially implemented |
| Submission | GNM command buffers and PS4 ABI | AGC command buffers, submit descriptors, queues, `/dev/gc` ioctls | Submit descriptor implemented; ioctl model open |

## Current State

Implemented and host-tested:

- Gen5 AGC/PM4 type-3 packet header helpers.
- AGC `IT_NOP` subcommand constants.
- Known NID table for mapped HLE reference exports.
- `SceAgcCb` cursor layout and allocation.
- Cursor-based `sceAgcCb*` and `sceAgcDcb*` packet builders:
  - `sceAgcCbNop`
  - `sceAgcCbDispatch`
  - `sceAgcCbSetShRegistersDirect`
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
  - `sceAgcDcbGetLodStatsGetSize`
  - `sceAgcDcbGetLodStats`
- In-place patchers:
  - `sceAgcDmaDataPatchSetDstAddressOrOffset`
  - `sceAgcWaitRegMemPatchAddress`
  - `sceAgcQueueEndOfPipeActionPatchAddress`
- DCB/ACB submit descriptor layout.
- Generic submit validation and debug capture.
- AGC shader record parser (magic, pointer fields, semantics counts, shader type).
- Shader linking (`agcShaderLinkHsGs`) and fused shader support
  (`sceAgcGetFusedShaderSize`, `sceAgcFuseShaderHalves` with register patching).
- Complete version 8 register defaults (703 public, 25 internal registers)
  from the reference implementation, replacing incomplete HLE-reference-derived data.
- ACB descriptor indirection (magic 0x5533ccaa) in prospero ACB submit.
- ACB packet builders for event write, atomic mem/GDS, cond exec, wait-reg-mem,
  write/copy/dma data, mem semaphore, acquire mem, queue reset, rewind, set
  flip, workload markers, and prime UTC L2.
- DCB/VSH packet builders for clear state, atomic GDS, context state ops, reset
  queue, set flip, workload markers, wait-safe, and preemption (SPRX-confirmed
  unimplemented VSH-only stub).
- Native prospero `/dev/gc` backend with ioctl submission, internal memory
  allocation, default-state `CLEAR_STATE` submission, and suspend-point
  submit/query. **Hardware-validated** on PS5 (FW 5.50, exploited).
- Hardware validation samples (`samples/hw_test/`) built as ELF and fake-SELF.
- **Compute shader execution verified on PS5 hardware** — 2,073,600 / 2,073,600
  pixels match expected output. GPU MMU memory mapping (flexible vs garlic),
  COMPUTE_STATIC_THREAD_MGMT_SE0..SE3, user data SGPR layout, and SH register
  defaults all confirmed.

Current expected host test result:

```text
1675 passed, 0 failed
```

## Phase 0: RE Groundwork

Status: complete.

Purpose:

Recover enough AGC packet and command-buffer behavior to make future work
measurable.

Done:

- Packet header model from HLE reference/RPCSX:

```c
0xC0000000u |
    (((length_dwords - 2u) & 0x3FFFu) << 16) |
    ((opcode & 0xFFu) << 8) |
    ((subcommand & 0x3Fu) << 2)
```

- `SceAgcCb` cursor offsets:
  - `0x10`: cursor-up/write cursor
  - `0x18`: cursor-down/end cursor
  - `0x20`: callback
  - `0x30`: reserved dwords
- Submit descriptor layout:
  - `0x00`: command buffer address
  - `0x08`: dword count
  - `0x0C`: reserved

Remaining:

- 12 SPRX NIDs remain unidentified (not in any known database — aerolib.csv
  154k entries, flatz ps5_symbols.txt, reference emulator, ps5-openagc, FW 3.20
  genstubs all exhausted). These are blocked on new external data sources.
- 32 NIDs in the TSV are unverified placeholders (`sceAgcUnknown_*` /
  `sceAgcDriverUnknown_*`) — names not in any database. Same blocker.
- Add structured notes for unknown packet IDs instead of scattering comments.

Acceptance criteria:

- NID table has source labels for each entry. ✅ Done.
- Packet model tests pass on host. ✅ Done.
- Unknowns are tracked in analysis docs with evidence level labels. ✅ Done.

## Phase 1: Packet Builder Completion

Status: implemented.

Purpose:

Cover the HLE-reference-confirmed AGC packet builders before implementing hardware
submission.

Already implemented:

- NOP, dispatch, SH register writes.
- Release memory (`sceAgcCbReleaseMem`).
- SH/CX/UC indirect register packet builders.
- Write-data, wait-reg-mem, DMA.
- Index buffer setup and indexed draw packets.
- DCB flip and wait-safe packets.
- In-place patchers for DMA-data, wait-reg-mem, and end-of-pipe addresses.
- LOD stats helpers.
- DCB/ACB submit descriptor validation.
- ACB event write, atomic mem/GDS, cond exec, acquire mem, queue reset, rewind,
  set flip, workload markers, and prime UTC L2.
- DCB/VSH clear state, atomic GDS, context state ops, reset queue, workload
  markers, and preemption (SPRX-confirmed unimplemented VSH-only stub).

Remaining packet builders:

1. None identified; new firmware-derived variants will be added as they are
   discovered.

Acceptance criteria:

- Each builder has:
  - a declaration in `agcdriver.h`
  - an implementation in `src/cb_builders.c`, `src/acb.c`, or `src/dcb.c`
  - at least one test asserting opcode, subcommand, length, and key payload
    fields
  - a row in `analysis/agc_known_nids.tsv` when a NID is known

## Phase 2: Shader Records and Wavefront Metadata

Status: mostly implemented (shader record parser done; fused shader support
done; register-block/Wave parsing pending observed evidence).

Purpose:

Move shader support from placeholder headers toward AGC shader record
interpretation.

Current evidence:

- HLE reference records shader header offsets:
  - user data
  - code pointer
  - CX/SH registers
  - specials
  - input/output semantics
  - shader type
  - SH register count
- openagc stores these offsets in `agc_re.h`.
- openagc implements a read-only `AgcShaderRecord` parser with
  `_Static_assert` verified offsets and synthetic-record tests.
- openagc implements `agcShaderLinkHsGs` (SPRX-confirmed HS/LS + CS → GS
  shader record linking).
- openagc implements fused shader support: `sceAgcGetFusedShaderSize` and
  `sceAgcFuseShaderHalves` (reference-confirmed GS/HS front+back half fusion
  with SH register copy, SPI_SHADER_PGM_CHKSUM_GS/LO_ES/LO_LS address
  patching, and vgt_shader_stages_en mismatch validation).

Work:

1. Replace placeholder shader magic assumptions with documented AGC shader
   record structures. ✅ Done.
2. Add parser helpers for shader pointer fields and register blocks.
   Pointer-field accessors done; register-block parsing waits for observed
   block layout.
3. Track Wave32/Wave64 metadata without guessing missing fields.
   No observed Wave32/Wave64 offset yet — keep as analysis-only.
4. Add tests from synthetic records first, then captured records when available.
   ✅ Synthetic tests added.
5. Add fused shader support (GetFusedShaderSize / FuseShaderHalves).
   ✅ Done — reference-confirmed implementation with register patching.

Acceptance criteria:

- Shader parser can read the known offsets without mutating input.
- Wave-size metadata is represented only when backed by observed fields.
- Tests cover malformed records and valid synthetic records.

## Phase 3: Register Defaults and State Builders

Status: implemented (FW 5.50 primary/internal groups embedded; complete v8
register defaults from reference implementation; prospero `NotifyDefaultStates`
builds the blobs in GPU memory and submits a `CLEAR_STATE` DCB; hardware
validation pending).

Purpose:

Recover AGC default register state and state-construction helpers.

Current evidence:

- HLE reference has primary/internal register default groups (incomplete: 38
  public, 22 internal registers with many zero-placeholder values).
- Complete version 8 register defaults extracted from the reference
  implementation: 703 public registers across 127 groups, 25 internal
  registers across 22 groups. Stored in `src/register_defaults_v8.c` and
  exposed via `agcRegisterDefaultsV8GetPrimaryGroups()` /
  `agcRegisterDefaultsV8GetInternalGroups()`.
- openagc implements the `AgcRegisterDefaults` blob builder/parser with
  `_Static_assert` verified layout and tests.
- openagc implements `sceAgcDriverNotifyDefaultStates` to allocate GPU memory
  for the primary and internal blobs, build them with the correct GPU VA
  pointers, and submit an `IT_CLEAR_STATE` (0x14) DCB.

Work:

1. Convert register default groups into openagc analysis tables. ✅ Done.
2. Add typed structures for register default records. ✅ Done.
3. Implement read-only helpers first. ✅ Done.
4. Implement state builders only after tests lock down expected records.
   ✅ Builder and parser tested; prospero backend uses the builder.
5. Wire default-state submission via `IT_CLEAR_STATE`. ✅ Done.

Acceptance criteria:

- Default groups are represented in data tables with source labels.
- Tests verify register offsets, values, and group sizes.
- Builders do not silently drop unknown records.
- Hardware validation confirms the kernel accepts the `CLEAR_STATE` DCB.

## Phase 4: `/dev/gc` Ioctl and Queue Model

Status: implemented (ioctl table + struct size/alignment tests + suspend arg
struct; no hardware validation yet).

Purpose:

Recover the native PS5 backend boundary before writing hardware submission
code.

Inputs:

- Firmware 5.50 dump.
- ps5-openagc NID tables (ioctl tables from ps5-openagc are NOT trusted —
  contains known errors; see `analysis/ps5_openagc_audit.md`).
- RPCSX queue/ring model for conceptual comparison.
- freegnm/opengnm and shadPS4 as lower-priority structural references.

Work:

1. Build an ioctl table with command IDs, input/output structure sizes, and
   firmware source references. ✅ Done (`analysis/ioctl_550.tsv`).
2. Identify memory objects required for AGC:
   - DDID
   - register shadow
   - CWSR/EOP/trap regions where applicable
   - queue ring buffers
   - doorbell/read-pointer areas
   ✅ Documented in `STATUS.md` and `driver_prospero.c`.
3. Model queue creation and destruction as host-testable structs first.
   ✅ `AgcProsperoQueue` modeled; `sceAgcDriverSubmitMultiCommandBuffersDirect` uses
   the submit descriptor layout.
4. Add native backend stubs only after structure sizes are known.
   ✅ `driver_prospero.c` skeleton implemented; `sceAgcDriverSuspendPointSubmitDirect`
   now calls the suspend ioctl; in-flight query uses `QUEUE_STAT_16`.

Acceptance criteria:

- `analysis/ioctl_550.tsv` or equivalent exists.
- Every ioctl struct has size/alignment tests.
- Native submit path is implemented and builds; hardware validation is the
  remaining gate.

## Phase 5: Native PS5 Backend

Status: **implemented and hardware-validated** (NOP submit, compute dispatch
with 100% pixel output, queue create/destroy, suspend point, workload
tracking all confirmed on PS5 hardware).

Purpose:

Turn host packet builders into real native PS5 submission.

Work:

1. Open and validate `/dev/gc`. ✅ Done in `sce_agc_initialize`.
2. Allocate required kernel/direct memory regions.
   ✅ Done in `sce_agc_initialize_internal_memory`.
3. Create graphics and compute queues. ✅ Done — `sceAgcDriverSetupAsyncGraphics`
   uses `QUEUE_STATUS` ioctl (nr=0x26); `_sceAgcDriverCreateUserSpecialQueue`
   uses `QUEUE_CREATE` ioctl (nr=0x21, 64-byte RW with magic auth tokens);
   `_sceAgcDriverDestroyUserSpecialQueue` uses `QUEUE_DESTROY` ioctl (nr=0x0e,
   12-byte RW). All SPRX-confirmed.
4. Submit DCB/ACB buffers using recovered descriptors.
   ✅ `sceAgcDriverSubmitDcb` / `sceAgcDriverSubmitMultiCommandBuffersDirect`
   use the recovered descriptor layout.
5. Submit default state via `CLEAR_STATE`. ✅ Done in `sceAgcDriverNotifyDefaultStates`.
6. Submit suspend points and query in-flight status. ✅ Done in
   `sceAgcDriverSuspendPointSubmitDirect` / `sceAgcDriverIsSuspendPointInFlightDirect`.
7. Add hardware smoke tests. ✅ Four ELF samples in `samples/hw_test/`:
   `videoout_linear.elf`, `agc_init.elf`, `agc_videoout.elf`, `agc_compute.elf`.
   All deployed and validated on PS5 hardware (FW 5.50, exploited).
8. Submit a compute dispatch with a real shader. ✅ Done — `agc_compute.elf`
   loads a psbc-compiled compute shader, sets SH registers (PGM_LO/HI,
   RSRC1/2/3, NUM_THREAD), sets user data (buffer pointer + push constants),
   dispatches via `DISPATCH_DIRECT`, and flips the display. GPU accepts the DCB
   (`SubmitDcb: 0x00000000`).
9. Verify compute shader pixel output. ✅ Done — 2,073,600 / 2,073,600 pixels
   match `0xFF00FF00` (solid green GPU-rendered frame). See "Key architectural
   discoveries" below.

### Key architectural discoveries (Phase 5 hardware validation)

These were the root causes of the compute shader not writing to the display
buffer. All four had to be fixed before the shader executed correctly:

1. **GPU MMU virtual memory mapping — flexible vs garlic memory.**
   Garlic memory (`sceKernelMapDirectMemory`) is NOT automatically mapped
   in the GPU's VMID address space. Only **flexible memory**
   (`sceKernelMapNamedSystemFlexibleMemory`) is automatically mapped in
   both CPU and GPU MMU spaces. This is why the AGC SPRX uses flexible
   memory for all internal GPU memory regions. The compute shader output
   buffer and shader code must be in flexible memory, not garlic memory.
   The display buffer (garlic) is registered with VideoOut separately and
   is GPU-accessible for display scanout, but not for general compute
   writes. Fix: allocate a 16MB flexible memory pool for compute output.

2. **Compute Unit enable — `COMPUTE_STATIC_THREAD_MGMT_SE0..SE3`.**
   Registers `0x216, 0x217, 0x219, 0x21A` must be set to `0xFFFFFFFF` to
   enable compute units on all shader engines (SE0-SE3). Without these,
   `DISPATCH_DIRECT` is processed but no workgroups actually execute on
   the CUs. Register `0x218` (`COMPUTE_TMPRING_SIZE`) is skipped in this
   contiguous block. This was the missing register that prevented shader
   execution even after all other state was correct.

3. **User data SGPR layout — `s2..s5` (confirmed by RDNA2 disassembly).**
   The openagc-psbc NIR postprocess shows `@load_scalar_arg_amd` base
   values, but the actual SGPR mapping was confirmed by disassembling the
   shader binary:
   - `s0`: unused (0)
   - `s1`: unused (0)
   - `s2`: buffer ptr low 32 bits
   - `s3`: buffer ptr high 32 bits
   - `s4`: total_pixels
   - `s5`: fill color (RGBA8 packed)

4. **FW 5.50 SH register defaults in the compute command buffer.**
   Applying the primary and internal SH register default groups (from
   `register_defaults_v8.c`) in the compute command buffer, with the
   compute shader type bit (bit 0) set on each `SET_SH_REG` packet header.
   This provides the baseline GPU state that the compute dispatch expects.

Acceptance criteria:

- ✅ Minimal DCB NOP submission does not fault on PS5 hardware.
- ✅ `NotifyDefaultStates` produces a valid `CLEAR_STATE` DCB and returns
  `AGC_OK` on hardware.
- ✅ Suspend-point ioctls return expected behavior.
- ✅ Failure paths return stable error codes.
- ✅ Compute dispatch accepted by GPU (non-NOP command buffer).
- ✅ Compute shader writes 100% of pixels correctly (2,073,600 / 2,073,600).

## Phase 6: Shader Compiler (openagc-psbc)

Status: **implemented and hardware-validated for compute shaders.**
User data SGPR layout confirmed by RDNA2 disassembly.

Purpose:

Compile GLSL → SPIR-V → NIR → ACO → PS5 `AgcShaderRecord` binary without
proprietary SDK tools. The `openagc-psbc` compiler (in `../openagc-psbc/`)
uses Mesa's NIR and ACO backends with a custom `AgcShaderRecord` emitter.

Work:

1. Build Mesa NIR + ACO subset as a standalone library. ✅ Done.
2. SPIR-V → NIR frontend. ✅ Done (Mesa `spirv_to_nir`).
3. NIR → ACO instruction selection. ✅ Done (Mesa `aco_select_nir`).
4. ACO → machine code assembly. ✅ Done (Mesa `aco_assembler`).
5. Emit `AgcShaderRecord` with SH/CX register blocks. ✅ Done.
6. Compute shader support. ✅ Done — `fill_color.comp` compiles to
   `fill_color.sb` and runs on PS5 hardware (100% pixel output verified).
7. Graphics shader support (VS/PS/GS/HS/LS). ⬜ Not yet tested on hardware.
   The RSRC1/2/3 offset functions are implemented for all shader types but
   only compute has been hardware-validated.

Bugs found and fixed during compute shader validation:

- **SH register offsets for compute were wrong.** The psbc compiler used
  `pgm_lo + 2/3/4` for RSRC1/2/3, but compute shaders have a different
  register layout: `COMPUTE_DISPATCH_PKT_ADDR_LO` sits at `pgm_lo + 2`,
  not `RSRC1`. Fixed to use explicit per-stage offset functions:
  CS: 0x212/0x213/0x228, PS: 0x00A/0x00B/0x007, etc.
  (Confirmed by sharpemu and AMD register headers.)

- **Shader type byte encoding was wrong.** The `AgcShaderType` enum had
  CS=6, but the firmware expects CS=0 at offset 0x5A of the shader record.
  Fixed to match sharpemu's confirmed encoding:
  CS=0, PS=1, ES=2, VS=3, GS=4, HS=5, ES-alt=6, LS=7.
  This matters because `sceAgcCreateShader` uses this byte to select which
  PGM_LO/HI register pair to patch with the shader code address.

- **User data SGPR layout confirmed.** The NIR postprocess output shows
  `@load_scalar_arg_amd` base values, but the actual SGPR mapping was
  confirmed by RDNA2 disassembly of the shader binary. The layout is:
  s0=unused, s1=unused, s2=buf_lo, s3=buf_hi, s4=total_pixels, s5=color.
  See "Key architectural discoveries" in Phase 5 for details.

Acceptance criteria:

- ✅ Compute shader compiles and executes on PS5 hardware.
- ✅ User data layout matches ACO's SGPR arg mapping (confirmed by RDNA2
  disassembly — 100% pixel output).
- ⬜ Graphics shaders (VS/PS) compile and execute on PS5 hardware.
  VS+PS compile via psbc and the GPU executes past the draw (WRITE_DATA
  marker confirmed), but render target output is still black — see Phase 7.

## Phase 7: Graphics Pipeline (Draw Calls)

Status: **in progress.** The DCB is accepted, the GPU executes past the
draw (WRITE_DATA marker confirmed), but the render target remains black.
This is the current critical-path milestone.

Purpose:

Submit a real graphics draw call — vertex shader + pixel shader + render
target + viewport + blend state + indexed draw — and display the result.

Prerequisites:

- ✅ Compute dispatch works with 100% pixel output (Phase 5).
- ✅ Shader compiler produces valid compute binaries (Phase 6).
- ✅ GPU MMU memory mapping understood (flexible memory for GPU writes).
- ✅ `NotifyDefaultStates` returns `AGC_OK` on hardware.
- ✅ Graphics shader compilation (Phase 6) — VS+PS compiled via psbc.

Key learnings from compute validation that apply to graphics:

- **Render targets can be in garlic memory** — the compute sample writes
  to garlic memory (display buffer) successfully. The graphics sample now
  renders directly to the display buffer (garlic) to avoid a copy step.
- **SET_SH_REG packets need the shader type bit** (bit 0): 0=graphics,
  1=compute. For VS/PS draw calls, use bit 0 = 0 (graphics engine).
- **Apply FW 5.50 SH register defaults** in the command buffer before
  setting shader-specific state. This provides the baseline GPU state.
- **CONTEXT_CONTROL packet is required** — opcode 0x28, 3 dwords,
  `LOAD_ENABLE_CONTEXT=0x80000000`. Same as compute.

### Critical issues discovered during hardware validation

1. **Non-contiguous register default groups corrupt GPU state.** Five
   register-default groups in `register_defaults_v8.c` have non-contiguous
   offsets but were being written as batch `SET_SH_REG`/`SET_CX_REG`
   packets (which assume contiguous offsets). Group 72 (128 CB_COLOR0
   registers) has offsets 0x318, 0x31b, 0x31c, 0x31d, 0x31e, 0x31f,
   0x321, 0x323... — writing contiguously overwrites unrelated registers
   and causes a GPU hang. **Fix:** write each register individually with
   `register_count=1`. The 5 non-contiguous groups: `_64`, `_72`, `_76`,
   `_90`, `internal_regs_21`.

2. **Tile mode 0 is Depth_2DThin_64, NOT linear.** For a linear color
   render target, use `kAgcTileDisplay_LinearGeneral` (31).
   `CB_COLOR0_ATTRIB = 0x0000001F` (tile_mode_index=31).

3. **SPI_SHADER_COL_FORMAT (0x1C5) and SPI_SHADER_POS_FORMAT (0x1C3) are
   NOT in shader records or register defaults.** They default to 0 (no
   export). Must set manually:
   - `SPI_SHADER_POS_FORMAT (0x1C3) = 1` (vec4 position)
   - `SPI_SHADER_Z_FORMAT (0x1C4) = 0` (no Z export)
   - `SPI_SHADER_COL_FORMAT (0x1C5) = 1` (8_8_8_8 color)
   - `CB_SHADER_MASK (0x08F) = 0x0F` (all RGBA to RT0)

4. **VGT_SHADER_STAGES_EN should be 0 (default) for VS+PS.** Do NOT set
   `ES_EN` — that routes VS through the ES stage, wrong for a type-3 VS.

5. **DB_Z_INFO must be explicitly disabled.** Default is `0x80000000`
   (depth enabled). Set `DB_Z_INFO = 0` and `DB_STENCIL_INFO = 0`.

6. **CB_COLOR0_PITCH uses 8-element tiles for linear mode.**
   `TILE_MAX = (pitch_elements / 8) - 1`. `SLICE = (tiles_per_row * height) - 1`.

7. **VS PGM_LO register offset mismatch (CRITICAL FIX).** `write_shader_sh_regs`
   previously only checked for `0x0C8` (ES) and `0x008` (PS). For a type-3
   (VS) vertex shader compiled by `openagc-psbc`, `SPI_SHADER_PGM_LO_VS` is
   at offset `0x048` (`0x049` for `PGM_HI`). Because `0x048` was not matched,
   the vertex shader code address was never patched, leaving `PGM_LO = 0`.
   The GPU attempted to execute the VS from address 0x0 (NULL), yielding 0
   vertices and leaving all pixels black. **Fix:** added `is_pgm_lo_off()` and
   `is_pgm_hi_off()` to match and patch `PGM_LO`/`HI` across all stages
   (PS `0x008`, VS `0x048`, GS `0x088`, ES `0x0C8`, HS `0x108`, LS `0x148`, CS `0x20C`).

8. **CB_COLOR0_BASE_EXT (0x390) required for 64-bit high bits.** The
   render target base address is in 64-bit GPU virtual memory. Writing only
   `CB_COLOR0_BASE` (`addr >> 8`) at 0x318 left high bits unpopulated.
   **Fix:** write `CB_COLOR0_BASE_EXT` at 0x390 with `(addr >> 40)`.

9. **Flexible memory required for render target writes.** Garlic memory
   direct writes from the Color Buffer engine are not GPU MMU-mapped in the
   same way flexible memory is. **Fix:** render into flexible memory
   (`compute_buffer + 0x10000`) and copy to garlic display buffer post-draw.

Work:

1. ~~Compile a vertex + pixel shader pair~~ ✅ Done.
2. ~~Set up render target state~~ ✅ Done (CB_COLOR0, tile mode 31, BASE_EXT 0x390).
3. ~~Set up graphics state~~ ✅ Done (viewport, scissor, blend, prim type).
4. ~~Submit a draw call~~ ✅ Done (DCB accepted, GPU alive after draw).
5. ~~Copy render target to display buffer~~ ✅ Done (flexible memory RT → garlic display buffer).
6. ~~Fix VS PGM_LO register patching~~ ✅ Done (0x048/0x049 VS PGM_LO/HI matched).
7. **Verify visual output on hardware** 🔄 Ready for hardware testing (`agc_graphics.elf`).

Acceptance criteria:

- A triangle is visible on the PS5 display, rendered by the GPU.
- The draw call DCB is accepted by `sceAgcDriverSubmitDcb` without error.
- Vertex and pixel shaders execute correctly (correct position + color).
- Render target is written correctly by the GPU.

### Experimental approaches that caused a kernel panic (DO NOT RETRY)

These were attempted and caused a PS5 kernel panic (system freeze +
reboot). Do not re-apply these changes without careful analysis:

1. **Mixing compute dispatch into a graphics DCB.** Inserting a
   `DISPATCH_DIRECT` (compute) packet into the same DCB as a graphics
   `IT_DRAW_INDEX_AUTO` draw call, before the graphics state setup,
   caused a kernel panic. The CP may not support switching between
   compute and graphics modes within a single DCB submission. Keep
   compute and graphics in separate DCB submissions.

2. **Enabling RDNA2 NGG (Next-Gen Geometry) mode.** Setting
   `GE_NGG_SUBGRP_CNTL (0x2D3) = 1` and `VGT_SHADER_STAGES_EN (0x2D5) =
   0x8110` (NGG_EN bit + ES_STAGE_REAL) caused a kernel panic. The NGG
   mode requires a valid GS (geometry shader) or NGG passthrough shader
   to be bound; without one, the geometry pipeline crashes the GPU. Do
   not enable NGG mode without a proper NGG-compatible shader setup.

3. **Dual-binding ES and VS SH registers.** Copying VS shader registers
   to both the VS (0x048-0x04B) and ES (0x0C8-0x0CB) stage register
   spaces simultaneously caused instability. The PS5 GPU expects a
   single active vertex-processing stage; dual-binding confuses the
   shader scheduler.

## Phase 8: Higher-Level AGC Features

Status: mostly speculative until more evidence is recovered.

These are long-term goals and should not block draw-call work.

### Wave32 / Wave64

Goal:

Represent wave-size metadata and shader register state accurately.

Rule:

Do not hard-code performance claims into API behavior. Track wave mode only
where AGC shader records or registers expose it.

### Geometry / Mesh-Style Processing

Goal:

Recover native PS5 geometry setup beyond the classic GNM-style pipeline.

Rule:

Do not add task/mesh public APIs until firmware exports, shader metadata, or
register evidence is found.

### Ray Tracing

Goal:

Identify whether AGC exposes ray acceleration state through exports, packets,
registers, shader records, or compiler intrinsics.

Rule:

Keep this as analysis-only until concrete evidence exists.

### Cache Synchronization

Goal:

Map AGC acquire/release/wait/cache-policy fields to observed behavior.

Rule:

Preserve raw fields in builders even before their full meaning is known.

### Variable Rate Shading

Goal:

Recover VRS/rate-image state if firmware/register evidence exists.

Rule:

No placeholder VRS enums in public headers until evidence is found.

## Reference Inputs

- Firmware dump: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50`
- Existing open AGC notes: `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc`
  (NOT proven working — NID mapping only; see `analysis/ps5_openagc_audit.md`)
- RPCSX GPU/PM4/GNM reference: `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`
- PS4 GNM clean rewrite reference: `../opengnm`
- SharpEmu PS5 emulator (AGC HLE reference): `/Users/bizkut/Downloads/PS5/homebrew/sharpemu`
  — C# PS5 emulator with detailed AGC implementation in
  `src/SharpEmu.Libs/Agc/AgcExports.cs`. Confirmed-correct for:
  - Shader type byte encoding at offset 0x5A (CS=0, PS=1, ES=2, VS=3, GS=4,
    HS=5, ES-alt=6, LS=7)
  - Compute dispatch initiator: `(modifier & 0xA038) | 0x41`
  - PGM_LO/HI address encoding: `addr = (HI << 40) | (LO << 8)`
  - Compute register offsets (PGM_LO=0x20C, RSRC2=0x213, NUM_THREAD=0x207-9,
    USER_DATA_0=0x240)
  - RSRC2 USER_SGPR field (bits [5:1]) and system SGPR layout
  - CreateShader PGM_LO/HI patching (scans SH table, handles missing pairs)
- openagc-psbc shader compiler: `../openagc-psbc/`
  — Mesa NIR + ACO based compiler that produces PS5 AgcShaderRecord binaries
  from GLSL/SPIR-V input. Hardware-validated for compute shaders.

## Reference Alignment Action Items

Completed:
- [x] Fix `sceAgcDcbDrawIndexAuto` to use `IT_DRAW_INDEX_AUTO (0x2D)` directly
- [x] Fix `sceAgcDcbWaitRegMem` 32-bit variant to 7 dwords with proper control word
- [x] Add reference-confirmed patchers (GetPacketSize, SetPacketPredication,
      SetRangePredication, CondExecPatch*, WriteDataPatchSetAddressOrOffset,
      JumpPatchSetTarget, SetNumRegisters variants)
- [x] Add reference-confirmed GetSize helpers (WriteData, Jump, Rewind, CondExec,
      WaitOnAddress)
- [x] Add missing driver functions (IsCaptureInProgress, DeleteEqEvent,
      GetEqEventType, GetDefaultOwner, InitResourceRegistration, etc.)
- [x] Add ACB descriptor indirection (magic 0x5533ccaa) to prospero backend
- [x] Fix PM4 opcodes (DISPATCH_DRAW_PREAMBLE 0x3A, SET_CONTEXT_REG_INDIRECT 0x9F)
- [x] Update register defaults from reference v8 (703 public registers across
      127 groups, 25 internal registers across 22 groups; replaces incomplete
      HLE-reference-derived data that had only 38/703 public registers with
      wrong zero-placeholder values)
- [x] Add fused shader support (sceAgcGetFusedShaderSize / sceAgcFuseShaderHalves
      with SH register patching, SPI_SHADER_PGM_CHKSUM_GS/LO_ES/LO_LS address
      patching, and vgt_shader_stages_en mismatch validation)
- [x] Replace HLE-reference-derived register defaults with complete v8 data
      as default for NotifyDefaultStates
- [x] Add SubmitMultiDcbs, SubmitCommandBuffer, SubmitMultiCommandBuffers,
      SubmitMultiAcbs convenience wrappers
- [x] Cross-check builder encodings against the reference — fixed 5 builders:
      WriteData (direct IT_WRITE_DATA 0x37), ReleaseMem (cmd[1]/cmd[2] fields),
      CondExec (5 dwords, count at cmd[4]), EventWrite (IT_EVENT_WRITE 0x46,
      variable length), DrawIndexOffset (decode_draw_index_initiator)
- [x] Switch from MIT to Apache 2.0 license (LICENSE file, SPDX headers)

Pending:
- [x] Add version selection for register defaults (v0, v4, v5, v7, v8, v9, v10, v11)

Completed (Phase 5-6, hardware validation):
- [x] Build openagc-psbc shader compiler (Mesa NIR + ACO → AgcShaderRecord)
- [x] Compute dispatch on PS5 hardware (agc_compute.elf — GPU accepts DCB)
- [x] Fix psbc compute SH register offsets (RSRC1=0x212, RSRC2=0x213, RSRC3=0x228)
- [x] Fix AgcShaderType enum encoding (CS=0, PS=1, ES=2, VS=3, GS=4, HS=5, LS=7)
  — confirmed by sharpemu PatchShaderProgramRegisters
- [x] libSceVideoOut.sprx runtime patch (NOP linear tiling check at 0x7e61)
- [x] Websrv homebrew deployment (FTP upload + HTTP /hbldr launch)
- [x] Cross-reference sharpemu AGC implementation for compute dispatch encoding
- [x] Fix DCB submit descriptor (SUBMIT_16 format, 0xC0108102)
- [x] Fix DDID allocation sizes for NotifyDefaultStates (primary=0x41000, internal=0xc000)
- [x] Set compute shader type bit on SET_SH_REG and DISPATCH_DIRECT headers
- [x] Fix PGM_LO address format (shader_addr >> 8, confirmed from KytyPS5)
- [x] Fix WRITE_DATA packet length (5 dwords, was corrupting shader code)
- [x] Discover GPU MMU mapping: flexible memory is GPU-visible, garlic is not
- [x] Enable Compute Units via COMPUTE_STATIC_THREAD_MGMT_SE0..SE3 (0x216/0x217/0x219/0x21A)
- [x] Apply FW 5.50 SH register defaults in compute command buffer
- [x] Confirm user data SGPR layout by RDNA2 disassembly (s2..s5)
- [x] **100% compute shader pixel output verified on PS5 hardware** (2,073,600 / 2,073,600 pixels match 0xFF00FF00)

## Working Rules

- Prefer source-backed packet fields over guessed abstractions.
- Keep host tests ahead of native backend work.
- Keep analysis artifacts next to code:
  - `analysis/agc_known_nids.tsv`
  - `analysis/agc_packet_model.md`
  - future ioctl/register/shader tables
- Separate raw recovered APIs from convenience wrappers.
- Treat PS5 hardware validation as a separate milestone with explicit smoke
  tests.
