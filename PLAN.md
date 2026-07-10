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

## Evidence Levels

Use these labels in docs, analysis notes, and code comments:

- **Implemented**: present in openagc, covered by host tests.
- **Observed**: found in SharpEmu, RPCSX, ps5-openagc notes (NID mapping only —
  ps5-openagc is NOT proven working and contains known ioctl errors; see
  `analysis/ps5_openagc_audit.md`), firmware strings, or local analysis, but
  not implemented yet.
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
- Use SharpEmu and firmware 5.50 artifacts as higher-priority evidence for PS5
  AGC export names, NIDs, packet wrappers, command-buffer layout, and submit
  structures.
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
- Known NID table for mapped SharpEmu exports.
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
- ACB packet builders for event write, atomic mem/GDS, cond exec, wait-reg-mem,
  write/copy/dma data, mem semaphore, acquire mem, queue reset, rewind, set
  flip, workload markers, and prime UTC L2.
- DCB/VSH packet builders for clear state, atomic GDS, context state ops, reset
  queue, set flip, workload markers, wait-safe, and preemption (SPRX-confirmed
  unimplemented VSH-only stub).
- Native prospero `/dev/gc` backend skeleton with ioctl submission, internal memory
  allocation, default-state `CLEAR_STATE` submission, and suspend-point
  submit/query.
- Hardware validation samples (`samples/hw_test/`) built as ELF and fake-SELF.

Current expected host test result:

```text
581 passed, 0 failed
```

## Phase 0: RE Groundwork

Status: mostly implemented.

Purpose:

Recover enough AGC packet and command-buffer behavior to make future work
measurable.

Done:

- Packet header model from SharpEmu/RPCSX:

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

- Finish NID table expansion from firmware 5.50 and ps5-openagc analysis.
- Add structured notes for unknown packet IDs instead of scattering comments.

Acceptance criteria:

- NID table has source labels for each entry.
- Packet model tests pass on host.
- Unknowns are tracked in analysis docs with evidence level labels.

## Phase 1: Packet Builder Completion

Status: implemented.

Purpose:

Cover the SharpEmu-confirmed AGC packet builders before implementing hardware
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

Status: partially implemented (shader record parser done; register-block/Wave
parsing pending observed evidence).

Purpose:

Move shader support from placeholder headers toward AGC shader record
interpretation.

Current evidence:

- SharpEmu records shader header offsets:
  - user data
  - code pointer
  - CX/SH registers
  - specials
  - input/output semantics
  - shader type
  - SH register count
- openagc stores these offsets in `agc_re.h`.
- openagc now implements a read-only `AgcShaderRecord` parser with
  `_Static_assert` verified offsets and synthetic-record tests.

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

Acceptance criteria:

- Shader parser can read the known offsets without mutating input.
- Wave-size metadata is represented only when backed by observed fields.
- Tests cover malformed records and valid synthetic records.

## Phase 3: Register Defaults and State Builders

Status: implemented (FW 5.50 primary/internal groups embedded; prospero
`NotifyDefaultStates` builds the blobs in GPU memory and submits a `CLEAR_STATE`
DCB; hardware validation pending).

Purpose:

Recover AGC default register state and state-construction helpers.

Current evidence:

- SharpEmu has primary/internal register default groups.
- openagc embeds the 13 primary and 22 internal groups from SharpEmu in
  `src/register_defaults.c` and exposes them via `analysis/register_defaults_550.tsv`.
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

Status: implemented and built (prospero backend compiles and links; hardware
validation is the remaining gate).

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
7. Add hardware smoke tests. ✅ ELF + fake-SELF packages built in
   `samples/hw_test/`; deployment blocked on PS5 hardware access.

Acceptance criteria:

- Minimal DCB NOP submission does not fault on PS5 hardware.
- `NotifyDefaultStates` produces a valid `CLEAR_STATE` DCB.
- Suspend-point ioctls return expected behavior.
- Failure paths return stable error codes.

## Phase 6: Higher-Level AGC Features

Status: mostly speculative until more evidence is recovered.

These are long-term goals and should not block packet/ioctl work.

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
- SharpEmu AGC HLE: `/Users/bizkut/Downloads/PS5/homebrew/sharpemu`
- RPCSX GPU/PM4/GNM reference: `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`
- PS4 GNM clean rewrite reference: `../opengnm`

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
