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

## Current Execution Order

This section is the authoritative completion plan. The later phase sections
retain detailed history and evidence; they do not override this order.

### Definition of a finished FW 5.50 core

OpenAGC's FW 5.50 core is release-ready when all of these conditions hold:

1. The library, rather than a hardware sample, owns every PM4 state builder
   required by the hardware-proven Wave32 graphics paths. Samples may retain
   memory allocation, VideoOut presentation, diagnostics, and visual oracles.
2. Exact host fixtures lock the validated shader, render-target, viewport,
   scissor, target-mask, depth-disabled, draw, and synchronization packet
   streams, including cursor advance and atomic short-buffer failure.
3. One revision passes the clean generic build, the complete retained host
   suite, the Prospero build, and the complete FW 5.500.008 websrv hardware
   qualification sequence without a GPU hang or kernel panic.
4. Exact four-digit firmware selection remains fail-closed. The implementation
   never calls absent or permission-only FW 5.50 driver operations as if they
   were supported capabilities.
5. A representative FW 5.50-compatible game corpus has 100% coverage for every
   observed AGC import and exercised ABI path. Unknown exports that no observed
   title uses are documented evidence gaps, not fabricated implementations.
6. Public declarations, calling conventions, firmware-layout static asserts,
   build instructions, deployment instructions, and supported/non-supported
   claims agree with the tested implementation.

This completion definition is for the stable FW 5.50 core, not a claim of full
official SDK parity, all-firmware support, VRS, or ray-tracing completeness.

### Audit of the proposed work

| Item | Status | Current evidence and remaining work |
| --- | --- | --- |
| 1. Harden FW 5.50 graphics and promote sample PM4 | **Complete** | Atomic reusable gfx1013 builders cover render-target, viewport, scissor, target-mask, depth-disabled, V8 defaults, and the baseline bind/index/instance/auto-index draw composition. Exact host fixtures and FW 5.50 RGBA16F/RGBA8 runs pass. |
| 2. Add exact Wave32 and fixed-function host fixtures | **Complete** | Exact fixtures cover the Wave32 VS/PS binder, hardware-proven RT/viewport/scissor/depth/default streams, and the 44-dword baseline draw wrapper, including post-bind overrides and atomic short-buffer rejection. |
| 3. Re-run the complete FW 5.50 websrv suite | **Complete** | Revision `c0633c7` passed the dependency-ordered base, compute, baseline/NGG, tessellation, and combined-stage matrix through curl/websrv. Required three-run repeats were deterministic, every applicable case completed 1,800/1,800 flips, and the sequential run had no hang, panic, or UI crash. |
| 4. Investigate FRAME_OPEN EINVAL and PA-debug EPERM | **Complete** | FW 5.50 `FRAME_OPEN` is absent from the kernel dispatcher. The PA-debug export is a userspace permission stub returning `0x8A6D0001`; neither result is an unresolved graphics blocker. |
| 5. Expand games and useful exports/NIDs | **Partial** | Four games expose 73 unique AGC functions and all 73 are implemented. PPSA02453 added a PS5 4.00 Unity workload and resolved `sceAgcCreateInterpolantMapping_0100`. The corpus must still expand to ten titles. Eleven unknown SPRX NIDs plus 32 placeholder mappings remain evidence gaps and are not guessed. |
| 6. Defer FW 3.20 | **Complete policy** | FW 3.20 remains the lowest active future target, but implementation and hardware work begin only after the FW 5.50 core satisfies the release gates above. |

### 1. Promote the hardware-proven graphics state into OpenAGC

Implement reusable gfx1013 state descriptions and atomic emitters for the
state that is still sample-only:

- Linear color targets covering the proven FP16 and RGBA8 formats, including
  base/base-ext, pitch, slice, info, attrib2/attrib3, color control, and target
  mask.
- Aspect-preserving viewport state, depth range, transform control, and full
  target scissor state.
- Depth-disabled color rendering state and the FW 5.50 graphics register
  defaults currently emitted by the sample.
- A baseline draw-state wrapper that composes the existing shader binder with
  primitive type, index type, instance count, and `DRAW_INDEX_AUTO` without
  hiding the lower-level packet builders.

Status: complete. The atomic gfx1013 builders cover the color target (RGBA16F
FLOAT/STD and RGBA8 UNORM/ALT), aspect-preserving viewport, all required
scissors, target mask, depth-disabled state, V8 SH/CX/UC defaults, and the
baseline draw-state wrapper. The wrapper composes the existing VS/PS binder,
primitive and index state, post-bind application overrides, instance count,
and `DRAW_INDEX_AUTO` without removing access to any lower-level builder.
Exact packet fixtures, short-buffer cursor-preservation tests, clean host
tests, and both FW 5.50 websrv hardware paths pass.

Each emitter must validate dimensions, alignment, format, and address range;
preflight its complete dword requirement; emit nothing on failure; and use
`agcPm4Header3*` plus the existing register helpers rather than hand-packed
headers. Public helper declarations use `PS5_SYSV_ABI`; firmware-layout structs
receive size and offset assertions.

Refactor `samples/hw_test/agc_graphics.c` to call these helpers. The sample may
still audit emitted registers, but it must no longer be the only implementation
of required render state. The exit gate is that deleting the sample-local
setup helpers does not change the hardware packet stream.

### 2. Lock exact host packet fixtures

Add golden host fixtures derived from the already validated OpenAGC output,
not copied firmware blobs. Normalize runtime addresses, then compare exact
dwords and cursor advance for:

- Wave32 fused NGG VS/PS binding and Wave32 HS/TES/NGG/PS binding.
- The 1536x1536 linear FP16 target and 1920x1080 linear RGBA8 display target.
- Square aspect-preserving viewports on square and 16:9 targets, depth range,
  full scissor, target mask, and disabled depth state.
- Primitive type, index type, one instance, `DRAW_INDEX_AUTO(3)`, completion
  marker, and the cache/synchronization tail used by the proven draw.
- FW 5.50 graphics-default emission counts and critical register sentinels.

Add negative fixtures for unaligned addresses, zero or excessive dimensions,
unsupported formats, invalid shader metadata, and every short-buffer boundary.
The exit gate is exact equality with the captured proven packet stream plus
zero cursor movement for every rejected state.

### 3. Qualify one revision on FW 5.50 hardware

After sections 1 and 2 land, build one clean revision and run the websrv suite
in dependency order through curl:

1. `videoout_linear.elf`.
2. `agc_init.elf`.
3. `agc_videoout.elf`.
4. `agc_compute.elf`.
5. Baseline `agc_graphics.elf` plus the vertex-fetch, indexed-draw, texture,
   repeated-submit, geometry invocation/amplification/topology, FP16, and RGBA8
   variants.
6. Isolated tessellation, TES-to-NGG geometry, combined invocation-count,
   combined line-strip, and combined RGBA8 variants.

Record the git revision, raw firmware value `0x05500008`, PM4 audit, readback
metrics, completion marker, flip count, and physical-display result for every
case. Run the baseline reusable binder and the combined tessellation path three
consecutive times from identical ELFs. The exit gate is all deterministic
oracles passing, 1,800/1,800 flips where applicable, and no hang, panic, or UI
crash. Use curl/websrv only; do not use `prospero-deploy`.

Qualification runners must wait for a successful foreground HTTP completion
before launching the next ELF. A timeout or disconnected `/hbldr` request is a
hard stop, not permission to overlap another homebrew process. The standalone
VideoOut smoke test patches the FW 5.50 linear-buffer check internally, runs a
finite 600-flip window, cleans up, and exits. The compute test submits and
waits for its display flip before returning. These lifecycle rules prevent a
successful persistent display case from occupying websrv during the next
qualification launch.

Status: complete on revision `c0633c7`, raw firmware `0x05500008`. All 14
qualification ELFs passed their deterministic oracles and physical-display
checks. The baseline binder and combined TES-to-NGG geometry paths each passed
three consecutive runs from identical hashed ELFs. See
`analysis/fw550_qualification_c0633c7.md` for the full matrix, readback metrics,
markers, flip counts, hashes, and lifecycle-incident analysis.

### 4. Preserve the closed FW 5.50 driver-gap results

Do not reopen PA-debug or `FRAME_OPEN` without contradictory firmware evidence.
Convert the conclusions into permanent capability/profile regressions:

- FW 5.50 must not issue `FRAME_OPEN`; its absence is expected behavior.
- `sceAgcDriverGetPaDebugInterfaceVersion` remains a permission-only userspace
  stub result, not a required ioctl or release gate.
- Unsupported optional operations return a stable fail-closed AGC error and do
  not mutate backend state.

### 5. Expand FW 5.50 game compatibility

Dragon Quest VII Reimagined (`PPSA17942`) is the fifth target in progress. It
is hardware-proven on FW `0x0550` and bundles AGC compatibility SPRXs despite
declaring `0x1202`. Its 253 imports currently have 252 covered and 1 unresolved
after completing the FW 5.50 GetSize imports, seven packet patchers, and the
data-packet payload-range, primitive-state update, and constant driver-status
ABIs. Its FW 5.50 workload-stream register/unregister pair and AGR multi-DCB
status path are also covered.
The analyzer merges named compatibility NIDs from the version-variant table,
including the three exact WriteData patch helpers used by the bundled SPRX.
The compatibility cursor ABI for `sceAgcAcbAtomicGds_0900` is also implemented
as an exact 11-dword packet builder.
The three FW 5.50 owner-management exports are covered with their exact
single-owner signatures and firmware `AGC_ERROR_NOT_SUPPORTED` behavior.
The compatibility-only `sceAgcGetIsTrinityMode` ABI is also recovered: it
writes the `sceKernelHasTrinityMode` result through a one-byte output pointer,
and the standard FW `0x0550` PS5 path reports false. The sole Dragon Quest call
site ignores the residual return register and consumes the stored byte.
The executed shader-instrumentation getter/setter and AMM semaphore-memory
path are recovered as well. The latter enforces 16 KiB base/size alignment,
returns 32-byte label records by index, and preserves the firmware's
already-initialized, not-initialized, and out-of-range errors.
The compatibility `ACQUIRE_MEM` engine patch and three async `WRITE_DATA`
patchers are recovered with their exact packet validation, masks, and
`0x8A6C000C` wrong-packet error behavior.
The complete ACB/DCB marker family now matches the cursor-based firmware ABI,
including explicit-length set/push span exports, color words, and distinct
`SET_MARKER`/`PUSH_MARKER` NOP subcommands.
The compatibility GS primitive-payload query scans the shader record's
firmware-counted CX register pairs and returns eight bytes only for register
`0x1C2` mode 2.
The compatibility GS-oversubscription query now reproduces the bundled
SPRX's `GE_PC_ALLOC` and `SPI_SHADER_PGM_RSRC4_GS` occupancy calculation,
including zero-limit defaults, forced-maximum state, shader-register-derived
bounds, and the fourth-argument floating-point interpolation factor.
`sceAgcCbMemsetExclusive` now follows the firmware's compute-dispatch
architecture rather than substituting DMA. It binds an aligned OpenAGC-owned
gfx1013 kernel compiled from `shaders/memset_exclusive.comp`, programs the
hardware-proven ring-offset/push-constant SGPR layout, and emits a 32-dword
Wave32 dispatch sequence. Host packet coverage is complete; execute the kernel
against GPU-visible memory on FW `0x0550` before promoting it to
hardware-validated status.
The five compatibility submit-validation controls reproduce the bundled
driver's unconditional `0x8A6C1000` debug-unavailable stubs without modifying
caller outputs or runtime state.
Eight resource/GDS exports reproduce the FW 5.50 userspace `0x8A6C9018`
status-only behavior with recovered SysV signatures, and capture start, stop,
and trigger reproduce their unconditional `0x8A6C1000` status. These stubs do
not inspect arguments or modify output storage.
`sceAgcDriverFindResourcesPublic` remains unresolved because the firmware body
proves only its constant status, not its public prototype; do not guess that
ABI.
Three otherwise-unknown NIDs (`7Wa3aeJgeVU`, `rP5xLdOf26k`, and
`Ikfdt-rIqCE`) now implement their exact FW 5.50/11.60 `IT_INDIRECT_BUFFER`
field-patcher ABIs. They validate opcode `0x3F`, patch the firmware-specific
dword offsets, preserve reserved bits, and return `0x8A6C000C` without mutation
for the wrong packet. Their NID-derived public labels are intentionally not
presented as recovered Sony names.
Two version-specific builder ABIs are also covered without changing the older
named source-compatible entry points. `-KRzWekV120` emits the exact four-argument
11.60 `SET_INDEX_SIZE` form, including its extra control bit, while
`zARR5aCmkoY` emits the exact 12-argument, 11-dword full atomic-GDS packet.
Both retain NID-derived public labels because matching firmware bodies prove
their packet layouts and SysV signatures but not their official names.
The NID-specific `qj7QZpgr9Uw` context-state transition ABI is recovered as an
exact two-argument builder. Its four operations emit the firmware-sized
`5/27/27/32`-dword sequences composed of `CONTEXT_CONTROL`, `COND_EXEC`,
`RELEASE_MEM`, `ATOMIC_MEM`, clear-state, and optional indirect CX restore
packets. The Prospero backend initializes the firmware-format `{0,1}`
GPU-visible synchronization label and flattened FW 5.50 v8 CX restore list in
`SceGnmMisc`. Host packet fixtures cover every mode and atomic short-buffer
failure; a focused FW `0x0550` hardware run remains required before this path
is labeled hardware-validated.
AcquireMem sizes follow the
firmware title-workaround mode: mode 1 returns 64 bytes and modes 0/2 return 32,
instead of hard-coding the emulator's 32-byte path. The FW 11.60
`dbOlWdppb4o` and `vieBRwlh1Lw` imports now use their recovered three-argument
interpolant-mapping ABI and exact enhanced descriptor transform. The create
variant fills the 32-entry identity tail, while the update variant leaves the
tail untouched. These are CPU-side shader metadata helpers covered by exact
host fixtures; game-runtime validation remains pending. Resolve the sole
remaining import, `sceAgcDriverFindResourcesPublic`, only from a game call site
or independently corroborated public prototype; its constant firmware stub is
not sufficient evidence to guess the ABI.

Grow the corpus from three games to at least ten representative,
FW 5.50-compatible binaries spanning multiple engines, SDK vintages, and
graphics workloads. For each title:

- Extract imported AGC NIDs and named functions into the analysis tables.
- Classify each import as implemented, forwarding wrapper, intentional
  fail-closed optional feature, or unresolved.
- Implement observed missing functions with ABI declarations, static asserts,
  packet/layout fixtures, and a game-provenance note.
- Add runtime fixtures for newly observed state combinations instead of
  declaring compatibility from symbol presence alone.

Prioritize functions used by real titles. The 12 unknown SPRX exports and 32
placeholder mappings are not release blockers unless a corpus title imports
or exercises them; if that occurs, obtain new evidence rather than guessing a
name or ABI. The exit gate is 100% named-import implementation across the
expanded corpus and no exercised path relying on a silent success stub.

Candidate selection is fail-closed. `PPSA01325` (ASTRO's PLAYROOM) is
explicitly excluded by project scope. Metadata-only candidates that have not
run on FW `0x0550` are ineligible. A hardware-proven backport is eligible even
when its metadata names newer firmware, provided its executable provenance and
bundled compatibility ABI are recorded. Record rejected candidates in
`analysis/game_compat_exclusions.tsv` so they are not counted or repeatedly
reinvestigated.

### 6. Run the FW 5.50 release audit

Before calling the FW 5.50 core complete:

- Audit all public symbols against FW 5.50 names, signatures, calling
  conventions, and NID provenance.
- Audit every firmware ABI struct for size and relevant offset assertions.
- Pass clean CMake and Make generic builds with no new warnings, the complete
  host suite, and a clean Prospero build.
- Ensure samples build only from public headers plus explicitly private sample
  support, and contain no second implementation of reusable PM4 state.
- Update `STATUS.md`, API/build/deployment documentation, the support matrix,
  and non-goals from the final qualification evidence.

### 7. Work deliberately deferred until after the FW 5.50 core

FW 3.20 exact-profile implementation is first after the FW 5.50 release gates
are satisfied; it remains the lowest active compatibility target. Other
firmware families require matching hardware before support claims. Cache
synchronization semantics follow the core audit and become an earlier blocker
only if the expanded game corpus exercises an unresolved path. VRS and ray
tracing remain later feature tracks driven by verified FW 5.50 ABI evidence and
real-title demand.

Every completed goal requires updated documentation, host regression coverage,
the relevant Prospero build, hardware validation through curl/websrv when
hardware is available, and a focused git commit. Do not use `prospero-deploy`.

## Firmware Compatibility Strategy

OpenAGC's public API is firmware-agnostic. Private `/dev/gc` behavior is
represented by versioned ABI families selected from exact inspected firmware
aliases, never by assuming that every version in a numeric range is compatible.

Current backend coverage:

- Exact inspected builds from FW 1.00 through 12.70 remain registered as RE
  data through submit16 ABI profiles. Registration is not a support claim.
- FW 3.20 is the lowest active compatibility target. OpenAGC's hardware-proven
  submission request is `0xc0108102`; the later PID request is not required.
- FW 5.50: RE-verified and fully hardware-validated on a standard PS5. The
  console reports raw build `0x05500008` (`5.500.008`); profile selection uses
  its four-digit `0x0550` ABI key while diagnostics retain the complete value.
- Other registered FW 4.00-12.70 builds: RE-verified, awaiting per-firmware
  hardware validation.
- FW 1.00 and 2.x: archival RE profiles only. Known submit/EOP evidence is
  retained, including FW 1.00's `0x38000` offset, but missing legacy queue or
  optional-request ABIs will not be recovered. Unsupported operations remain
  explicitly fail-closed and these versions are not advertised as supported.
- FW 3.20: lowest active target, with local firmware references available for
  exact userspace ABI recovery. Hardware validation remains pending.
- PS5 Pro: FW 9+ resolves `sceKernelHasTrinityMode` and selects the firmware-
  proven 22 MiB CWSR allocation and related offsets. Hardware validation is
  still required on a PS5 Pro.

The next compatibility work is completion of the FW 5.50 graphics and driver
path on the available hardware. FW 3.20 exact-profile work follows after the
FW 5.50 implementation is mature. Evidence and exact
aliases are tracked in `analysis/agc_driver_abi_families.tsv` and
`analysis/agc_driver_abi_1160.md`.

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
| Wavefront model | Wave64-centric | Wave32/Wave64 metadata and register state | Wave32 NGG+PS compiler records, PM4, readback, and display hardware-validated on gfx1013 |
| Geometry pipeline | VS/HS/DS/GS fixed-function path | AGC shader records and possible task/mesh-style paths | Speculative until firmware/shader evidence |
| Ray tracing | Software compute only | Ray acceleration/BVH state if exposed | Speculative until AGC evidence |
| Shading rate | Uniform rate | VRS/rate-image state if exposed | Speculative until register/packet evidence |
| Cache sync | Coarser GCN cache flush/invalidate model | AGC acquire/release/wait/cache-policy packets | Partially observed and partially implemented |
| Submission | GNM command buffers and PS4 ABI | AGC command buffers, submit descriptors, queues, `/dev/gc` ioctls | FW 5.50 hardware-validated; exact registry implemented; FW 3.20 is the lowest active target |

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
2110 passed, 0 failed
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

Status: implemented for the currently exercised Gen5 records. Shader parsing,
specials, fusion, semantic mapping, NGG/PS Wave32 record state, and final-PM4
validation are host-tested and hardware-validated on gfx1013. Unobserved record
extensions remain evidence-gated.

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

Status: implemented and hardware-validated on FW 5.50. Primary/internal groups
are embedded, the complete v8 defaults are available, and Prospero
`NotifyDefaultStates` builds the GPU blobs and submits `CLEAR_STATE`.

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

Status: implemented and hardware-validated for FW 5.50 submit, queue lifecycle,
suspend points, workloads, and multi-DCB submission. Exact firmware profiles
fail closed. Remaining FW 5.50 ioctl issues are non-blocking follow-up work;
cross-firmware ABI expansion is deferred until advanced graphics is stable.

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

Status: **implemented and hardware-validated for compute and graphics
shaders.** User-data SGPR layout, no-GS NGG fusion, vertex/texture descriptors,
varying exports, and Wave32 NGG+PS state are confirmed by compiler fixtures and
real gfx1013 execution.

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
7. Graphics shader support (VS/PS/GS/HS/LS). ✅ VS+PS tested on hardware.
   The gfx1013 no-GS NGG VS+PS path, RGB interpolants, and interleaved
   position/color vertex fetch are validated; GS, HS, and LS execution remain
   untested.

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
- ✅ Graphics shaders (VS/PS) compile and execute on PS5 hardware.
  The gfx1013 NGG front-entry probe executes, and the real ACO VS+PS path
  rasterizes an interpolated RGB triangle (1,036,796 changed pixels and eight
  distinct colors sampled by readback).

## Phase 7: Graphics Pipeline (Draw Calls)

Status: **complete on real PS5 hardware, including interpolants.** The DCB is
accepted, the gfx1013 NGG front program executes, and the pixel shader consumes
the vertex shader's `v_color` output to render a smooth RGB triangle. Readback
finds 1,036,796 changed pixels and eight distinct colors; visual confirmation
on the PS5 display matches the expected gradient. The post-draw WRITE_DATA
marker also confirms CP progress.

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

10. **PS CX block re-enables depth after explicit disable.** The PS
    shader's CX register block writes `DB_DEPTH_INFO` (0x00F) = 0x0F and
    `DB_SHADER_CONTROL` (0x203) = 0x10 *after* the code disabled depth.
    With no depth buffer bound, depth testing discards all fragments.
    **Fix:** override `DB_DEPTH_INFO`, `DB_Z_INFO`, `DB_STENCIL_INFO`,
    `DB_SHADER_CONTROL`, and `DB_DEPTH_CONTROL` to 0 *after* the PS CX
    block is written.

11. **`SPI_PS_INPUT_CNTL_0` register offset was wrong.** The code wrote
    to `0x1B8` (`AGC_REG_SPI_BARYC_CNTL`) instead of the correct
    `0x191` (`AGC_REG_SPI_PS_INPUT_CNTL_0`). **Fix:** use
    `AGC_REG_SPI_PS_INPUT_CNTL_0` with `OFFSET=0`.

12. **`VGT_SHADER_STAGES_EN` bit layout corrected.** Bit 8 is
    `dynamicHs`, not `ES_EN`. The real `esEn` field is at bits [4:3]
    (EsReal=2 → 0x10). For VS at ES PGM (0x0C8, as psbc outputs), set
    `VGT_SHADER_STAGES_EN = 0x10` (EsReal). Does not kernel panic.

13. **`SPI_SHADER_COL_FORMAT` must match `CB_COLOR0_INFO` format.**
    Set `SPI_SHADER_COL_FORMAT = 1` (8_8_8_8) to match
    `CB_COLOR0_INFO` format 1 (COLOR_8_8_8_8).

14. **`CB_COLOR0_ATTRIB2` (0x3B0) was missing.** Without MIP0 dimensions,
    the CB clips all writes to 0x0. **Fix:** set
    `CB_COLOR0_ATTRIB2 = ((height-1) & 0x3FFF) | (((width-1) & 0x3FFF) << 14)`
    and `CB_COLOR0_ATTRIB3 = 0`.

15. **Removed invalid partial NGG state.** A plain VS record has no
    GS/NGG `specials` block. `VGT_GS_OUT_PRIM_TYPE=4` was also wrong because
    that register uses the GS-output enum (`triangles=2`), not the input
    primitive enum, and the speculative `GE_CNTL` values were unsupported.

16. **`PA_CL_VS_OUT_CNTL` and `PA_CL_CLIP_CNTL` set to 0.** The previous
    non-zero values (USE_VTX_POINT_SIZE, CLIP_DISABLE) may interfere with
    NGG rasterization. Set both to 0 (defaults).

### Current hardware-validation candidate: corrected plain VS path

The generated vertex record declares `shader_type=VS` but stores its four
program registers at ES offsets. The sample now remaps those registers to
the VS quartet and uses `VGT_SHADER_STAGES_EN=0`. It also fixes four
independent state errors found by cross-checking KytyPS5 and sharpemu:

- `CB_COLOR0_INFO.FORMAT` is `10` for `8_8_8_8`, not `2`.
- `CB_COLOR_CONTROL.MODE` must be Normal (`0x00CC0010` with ROP3 copy),
  rather than zero/disabled (`0x00CC000F`).
- `SPI_SHADER_COL_FORMAT=4` selects FP16 ABGR shader export; it is not the
  same enum as the render-target channel layout.
- `VGT_PRIMITIVE_TYPE` is UCONFIG-only and must not also be written through
  `SET_CONTEXT_REG`.

A production PS5 NGG path still requires a real GS/NGG shader record and
its populated `specials` block. KytyPS5 and sharpemu both source stage,
GS-output, `GE_CNTL`, and `GE_USER_VGPR_EN` state from that block; partial
NGG state must not be synthesized around a plain VS.

### Real-PS5 NGG implementation direction

The plain-VS path is only a diagnostic experiment. The target architecture
for real PS5 hardware is the ES+GS/NGG pipeline indicated by both reference
emulators and must be validated against FW 5.50 SPRX behavior, game command
buffers, and hardware results.

Reference roles:

- **KytyPS5 is the primary low-level state reference.** Its recognized PS5
  vertex path uses an ES program, valid GS metadata/checksum, and
  `VGT_SHADER_STAGES_EN = 0x02002000`. It is the better reference for PM4
  register relationships and the minimum coherent NGG state.
- **sharpemu is the primary AGC ABI reference.** Its
  `sceAgcCreatePrimState` implementation requires an ES/geometry shader with
  a non-null `specials` block and copies `VGT_SHADER_STAGES_EN`,
  `VGT_GS_OUT_PRIM_TYPE`, `GE_CNTL`, and `GE_USER_VGPR_EN` from that shader.
  Its interpolant builder confirms that ES output semantics must be linked
  to PS input semantics.
- **Neither emulator is authoritative by itself.** Final constants and
  layouts must be confirmed from the FW 5.50 SPRX, captured game state, and
  real-PS5 execution. Do not replace shader-derived state with guessed
  constants merely because an emulator accepts them.

FW 5.50 SPRX verification (`libSceAgc.sprx`):

- `sceAgcCreatePrimState` (`D9sr1xGUriE`, `0xE2D0`, 255 bytes) confirms the
  five-argument ABI: CX output, UCONFIG output, optional hull shader,
  geometry/fused shader, and input primitive type.
- The firmware copies register/value pairs from the geometry shader
  `specials` block at `+0x00` (`GE_CNTL`), `+0x08`
  (`VGT_SHADER_STAGES_EN`), `+0x20` (`VGT_GS_OUT_PRIM_TYPE`), and `+0x28`
  (`GE_USER_VGPR_EN`). These are 8-byte register/value entries, not four
  packed `uint32_t` values. The former 16-byte `AgcShaderSpecials` model has
  been replaced with the verified 0x30-byte sparse register-pair layout.
- `CreatePrimState` tests GS-enable bit 5 in the stage-mask value. When GS is
  enabled it uses the shader-provided GS-output pair at `specials+0x20`;
  otherwise it derives the low three GS-output primitive bits from the
  firmware primitive lookup table. An optional hull shader contributes stage
  bits and replaces `GE_USER_VGPR_EN` with its own specials value.
- The SPRX does not hard-code `VGT_SHADER_STAGES_EN=0x02002000` in
  `CreatePrimState`; it copies the compiler-produced value. `0x02002000`
  remains a useful KytyPS5/game-state observation, not a constant OpenAGC
  should synthesize unconditionally.
- `sceAgcCreateInterpolantMapping` (`pdEV7bI6COI`, `0xD7F0`, 758 bytes)
  builds 32 register/value entries by matching PS input semantics at
  `PS+0x30` against geometry output semantics at `GS+0x38`. It preserves and
  transforms interpolation flags rather than emitting a simple identity map;
  OpenAGC must reproduce this firmware behavior for a production draw path.
- Both FW 5.50 fusion exports (`0xC770` and `0xCD40`) accept half-type pairs
  `4+6` and `5+7`, copy the back record, and emit fused record types `2` and
  `3` respectively. They compare stage-mask value bits 22/21, merge multiple
  SH resource fields, and patch the front program address into the fused SH
  register set. OpenAGC's current simplified fusion/type model requires an
  SPRX-accurate correction before it can produce real NGG shaders.

Implementation work:

1. ~~Correct `AgcShaderSpecials` to the verified sparse register-pair layout,
   add size/offset static assertions, and update all typed accessors/users.~~
   Done.
2. ~~Correct fused-shader half validation, output types (`2`/`3`), stage-bit
   checks, SH resource merging, and front-program address patching against
   the `0xC770` and `0xCD40` firmware implementations.~~ Done.
3. ~~Implement the firmware `sceAgcCreatePrimState` behavior with tests
   covering exact output register pairs, hull merging, and primitive lookup.~~
   Done.
4. ~~Implement `sceAgcCreateInterpolantMapping` semantic mapping with exact
   FW 5.50 flag/default transformations and all 32 output entries.~~ Done.
5. ~~Extend `openagc-psbc` to emit a real PS5 ES+GS/NGG shader record rather
   than a plain VS record containing ES register offsets.~~ Done.
6. ~~Generate or fuse the required GS front/back halves, including a valid
   `SPI_SHADER_PGM_CHKSUM_GS` and the complete shader `specials` block.
   ~~ Done.
7. ~~Bind vertex/export code through `SPI_SHADER_PGM_LO/HI_ES` and bind all
   required GS program/resource state from the generated record.~~ Done.
8. ~~Apply the shader-provided `VGT_SHADER_STAGES_EN`. Preserve
   `0x02002000` as a captured/reference value only; never substitute it for
   the compiler-produced specials entry without matching shader metadata.~~
   Done.
9. ~~Apply shader-provided `GE_CNTL` and `GE_USER_VGPR_EN`.~~ Done.
10. ~~Set `VGT_GS_OUT_PRIM_TYPE=2` and input
    `VGT_PRIMITIVE_TYPE=4`.~~ Done.
11. ~~Generate ES-to-PS interpolant registers from shader semantics.~~ Done.
12. ~~Validate with post-draw markers and render-target readback.~~ Done on
    real PS5 hardware.

Safety constraints:

- Do not enable `GE_NGG_SUBGRP_CNTL` or an NGG stage mask without a valid
  GS/NGG shader and checksum; the partial configuration already caused a
  kernel panic.
- Do not dual-bind the same program to ES and VS register spaces.
- Keep compute and graphics work in separate DCB submissions during NGG
  bring-up.

### Immediate Phase 7 execution order

The next work should proceed in this order. Do not begin another round of
manual PM4 tuning before steps 1-4 are complete.

1. **Fix the shader ABI model.** Done. `AgcShaderSpecials` now models the
   0x30-byte FW 5.50 sparse register/value layout and has size and offset
   assertions for the entries at `0x00`, `0x08`, `0x20`, and `0x28`.
2. **Make shader fusion SPRX-accurate.** Done for both FW 5.50 exports,
   including half pairs `4+6` and `5+7`, fused types `2` and `3`, stage-mask
   checks, version-specific SH resource merging and user-data behavior,
   scratch copying, checksum transfer, and front-address patching.
3. **Implement `sceAgcCreatePrimState`.** Done. It emits the exact two
   context and three UCONFIG register/value entries recovered from FW 5.50
   at `0xE2D0`, including optional hull-stage merging and primitive lookup.
4. ~~**Implement `sceAgcCreateInterpolantMapping`.** Reproduce the semantic
   matching and flag transformation recovered from the FW 5.50 function at
   `0xD7F0`. Test missing semantics, identity fallback, flat interpolation,
   defaults, and all 32 output entries.~~ Done.
5. ~~**Add a compiler fixture before changing `psbc`.** Use synthetic shader
   records and metadata only; do not introduce proprietary firmware or
   shader data.~~ Done. The host fixture fuses GS-front/GS-back records, then
   derives primitive and interpolant state from the fused record.
6. ~~**Extend `openagc-psbc` for the real NGG path.** Emit the required shader
   halves and fuse them through OpenAGC rather than inventing another record
   format in the sample.~~ Done for the RDNA2 no-GS NGG vertex path. The
   compiler runs RADV NGG lowering, computes subgroup/LDS state, emits the
   monolithic ACO program plus fusion-compatible front/back records, complete
   specials, semantic maps, and corrected named CX register offsets.
7. ~~**Replace the graphics sample's plain-VS binding.** Consume the fused
   shader record plus the primitive-state and interpolant builders, removing
   remaining guessed stage and semantic state.~~ Done. The sample relocates
   file records, uploads both halves, fuses with the real GPU addresses, and
   binds executable code through the ES program pair and resources through
   the GS register block.
8. ~~**Validate in layers.**~~ Done. The front-entry probe wrote
   `0x4E474721`; the real ACO path changed 1,036,800 pixels and produced the
   expected magenta triangle on PS5 hardware.

**Interpolant validation is complete.** The fragment shader consumes
`vec4(v_color, 1.0)` and renders an RGB gradient on real PS5 hardware. This
proves that compiler-generated ES/NGG output semantics,
`sceAgcCreateInterpolantMapping`, PS input registers, and parameter exports
work together. The compiler fix assigns standalone vertex-stage user varyings
to RADV parameter-export slots before NGG lowering; parameter 0 now exports
all three `v_color` channels instead of falling back to constant ones.

After interpolants pass, proceed in this order:

1. Maintain compiler regression coverage for NGG record placement and
   multi-component parameter exports.
2. Keep `AGC_NGG_ENTRY_PROBE` as an optional diagnostic and reduce temporary
   PM4 audit output in the normal hardware sample.
3. ✅ Validate vertex-buffer input. A 20-byte interleaved `float2` position +
   `float3` color binding executes on real gfx1013 hardware through RADV-style
   buffer descriptors and the compiler-recorded descriptor-table user SGPR;
   GPU readback and the displayed RGB-gradient triangle both pass.
4. ✅ Validate index-buffer binding and indexed drawing. A bound 16-bit
   `{1,2,3}` index buffer skips a decoy vertex 0 and reproduces the exact
   1,036,796-pixel RGB triangle on real gfx1013 hardware using
   `DRAW_INDEX_OFFSET_2`; GPU readback and the PS5 display both pass.
5. ✅ Validate texture and sampler binding. A 2x2 linear RGBA8 image and
   bilinear clamp sampler execute through a RADV combined descriptor on real
   gfx1013 hardware; exact readback, sampled-color variation, and the PS5
   display all pass.
6. ✅ Validate an additional render-target format. A 1536x1536 linear
   `R16G16B16A16_FLOAT` target executes on real gfx1013 hardware with CB
   format `0x0c`, FLOAT number type `7`, and standard component swap. Readback
   finds 255,744 covered pixels, eight distinct FP16 colors, opaque samples,
   zero components outside `[0,1]`, and a live completion marker. The 1:1
   RGBA8 preview was visually confirmed as a centered, smoothly textured
   equilateral triangle.
7. ✅ Resolve repeated multiple-DCB execution. The public wrappers issue one
   descriptor-array submit after the SPRX-confirmed nr=1 frame-state ioctl.
   Unique-marker runs proved the FW 5.50 exploited-payload graphics ring defers
   the final descriptor until the next submit. The Prospero backend now appends
   a dedicated GPU-visible NOP IB trailer, making the deferred descriptor
   harmless. Two immediate deployments each passed three repeated two-DCB
   iterations with unique ordered markers and zero polling delay.
8. ✅ Validate isolated Wave32 tessellation. Fused HS and TES records execute
   through the reusable gfx1013 binder and the recovered non-Direct TF-ring
   ABI. The factor ring contains four `4.0` factors, readback contains 255,744
   valid FP16 pixels within the expected equilateral bounds, and the PS5
   displays the centered interpolated triangle with equal sides without a hang
   or kernel panic.
9. ✅ Combine the validated tessellation path with a real NGG geometry shader.
   The centroid-shrink GS executes after TES, producing 155,321 changed FP16
   pixels versus 155,419 expected, eight sampled colors, zero out-of-range
   components, a live completion marker, and 1,800/1,800 display flips. The
   physical display shows the expected equal-sided tessellated triangle with
   dark seams around each colorful microtriangle. The isolated tessellation
   and geometry samples remain controls.
10. Expand Wave32 graphics coverage, then VRS and ray tracing where supported
   by gfx1013 and the PS5 AGC ABI. Combined GS `invocations=2` is
   hardware-validated with two half-scale tessellated copies, deterministic
   127,488-pixel FP16 coverage, and `VGT_GS_INSTANCE_CNT=0x9`. Combined
   line-strip output is also hardware-validated with `out_prim=1`, a colorful
   6,749-pixel wire grid, and retained tessellation state. The isolated direct
   RGBA8 target completes the matrix with repeatable 76,803-pixel coverage and
   physical confirmation of the subdivided centroid-shrink triangle.
11. ✅ Close PA-debug and FRAME_OPEN RE for FW 5.50. The PA-debug version
   export is a userspace permission stub returning `0x8A6D0001` without an
   ioctl, while `FRAME_OPEN` is absent from the kernel dispatcher.

Work:

1. ~~Compile a vertex + pixel shader pair~~ ✅ Done.
2. ~~Set up render target state~~ ✅ Done (CB_COLOR0, tile mode 31, BASE_EXT 0x390, ATTRIB2/3).
3. ~~Set up graphics state~~ ✅ Done (viewport, scissor, blend, prim type, GE_CNTL).
4. ~~Submit a draw call~~ ✅ Done (DCB accepted, GPU alive after draw).
5. ~~Copy render target to display buffer~~ ✅ Done (flexible memory RT → garlic display buffer).
6. ~~Fix VS PGM_LO register patching~~ ✅ Done (0x048/0x049 VS PGM_LO/HI matched).
7. ~~Fix depth re-enable by PS CX block~~ ✅ Done (override after PS CX).
8. ~~Fix SPI_PS_INPUT_CNTL_0 offset~~ ✅ Done (0x191 not 0x1B8).
9. ~~Fix VGT_SHADER_STAGES_EN bit layout~~ ✅ Done (esEn=EsReal 0x10).
10. ~~Fix SPI_SHADER_COL_FORMAT / CB_COLOR0_INFO mismatch~~ ✅ Done (both 8_8_8_8).
11. ~~Add CB_COLOR0_ATTRIB2 for MIP0 dimensions~~ ✅ Done.
12. ~~Remove invalid partial NGG state and correct color/primitive state~~ ✅ Done.
13. **Optionally test the plain-VS diagnostic on hardware.** The corrected
    sample may still determine whether a usable legacy path exists, but this
    is no longer the Phase 7 critical path and must not delay the real NGG
    implementation.

Acceptance criteria:

- ✅ A triangle is visible on the PS5 display, rendered by the GPU.
- ✅ The draw call DCB is accepted by `sceAgcDriverSubmitDcb` without error.
- ✅ Vertex and pixel shaders execute correctly (correct position and RGB gradient).
- ✅ Render target is written correctly by the GPU.
- ✅ RGB vertex-to-pixel interpolation is confirmed by readback and display.

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

## Phase 8: Firmware Forward Compatibility

Status: in progress. The stable operations table, exact-match runtime registry,
FW 5.50 direct backend, and collision-safe Sony-export candidate are present.
FW 5.50 is the active hardware target. FW 3.20 remains the intended lowest
active cross-firmware target but its implementation is deferred until the FW
5.50 graphics path is mature. FW 1.00 and 2.x are archival RE profiles only;
FW 11.60, PS5 Pro, and Sony-export GPU submission remain pending.

Purpose:

Allow a single game-facing OpenAGC ABI to run across supported PS5 firmware
versions without exposing firmware-private ioctl layouts to applications.
Match the compatibility model used by retail titles where practical: prefer
the installed userspace driver that matches the running kernel, while keeping
validated direct backends as fallbacks.

Architecture:

```text
Game -> stable OpenAGC public ABI
     -> installed Sony driver-export backend (preferred)
     -> validated per-firmware /dev/gc backend (fallback)
     -> safe AGC_ERROR_UNSUPPORTED result for unknown interfaces
```

### Deferred: FW 3.20 lowest active cross-firmware profile

Recovery and implementation sequence:

1. Inventory FW 3.20 `libSceAgc.sprx` and `libSceAgcDriver.sprx` exports,
   versions, initialization calls, and firmware-sensitive wrappers.
2. Decode queue create/destroy, submit16, suspend, workload, TF-ring, HS
   offchip, internal-memory sizes, and default-state selection from the local
   FW 3.20 references.
3. Compare every private request and structure against FW 5.50. Share code only
   where request values, sizes, field offsets, and semantics match.
4. Add or refine an exact FW 3.20 runtime profile using its four-digit ABI key.
   Keep absent optional operations fail-closed.
5. Add `_Static_assert` checks and byte-exact host fixtures for every differing
   private structure or ioctl argument.
6. Build the generic and Prospero targets. Without matching FW 3.20 hardware,
   label the result **SPRX-confirmed, hardware pending**.

Acceptance criteria:

- FW 3.20 is the documented lowest active target and has a complete provenance
  record for every enabled private operation.
- No FW 5.50 private request is reused solely because FW 3.20 is nearby.
- Exact FW 3.20 aliases select only capabilities proven by its firmware.
- Unknown, FW 1.00, and FW 2.x missing operations continue to fail closed.
- Hardware support is not claimed until the ordered websrv smoke tests pass on
  a matching FW 3.20 console.

### Deferred archival profiles: FW 1.00 and 2.x

- Preserve exact aliases, known submit-family data, FW 1.00's `0x38000` EOP
  evidence, and existing regression fixtures for research value.
- Do not recover the FW 1.00 pre-authentication special queue or other missing
  legacy-only optional ABIs unless real users and matching hardware appear.
- Keep explicit `AGC_ERROR_NOT_SUPPORTED` behavior and do not advertise these
  profiles as supported.

### Priority 2: Stable backend dispatch and Sony exports

- ✅ `AgcDriverOps` preserves the public ABI across generic and Prospero
  implementations.
- ✅ Exact firmware detection and capability selection fail closed.
- ✅ The Sony-export candidate resolves privately without symbol recursion.
- Keep the installed-Sony candidate ineligible for automatic GPU submission
  until a non-destructive probe proves execution. Never follow a mutating Sony
  probe with direct fallback in the same boot session.
- Maintain per-firmware NID/module aliases without assuming NID stability.

### Priority 3: Additional firmware families

- Recover FW 11.60 exports, initialization, ioctls, memory sizes, defaults, and
  permissions from local references without committing firmware binaries.
- Add a direct backend only after independent structure and behavior proof.
- Validate in layers on matching hardware: version query, initialization, NOP,
  queue lifecycle, compute readback, graphics readback, and display.
- Keep deterministic PM4 builders, descriptors, shader parsing/fusion,
  primitive state, and interpolant mapping inside OpenAGC.

Safety and compatibility rules:

- OpenAGC's public ABI remains firmware-independent; games must not include
  private ioctl structures or firmware-specific backend headers.
- Runtime export resolution must use private function-pointer names. Directly
  importing a firmware symbol that OpenAGC also defines risks self-resolution
  or duplicate-symbol behavior.
- Loading the installed Sony driver does not bypass GPU credentials,
  `cr_sceAuthId`, module-global initialization, or other permission checks.
- Unknown firmware must fail safely with `AGC_ERROR_UNSUPPORTED`; it must not
  guess an ioctl ABI.
- Firmware support is declared only after real-hardware validation. Matching
  RDNA2 PM4 packets alone is not sufficient evidence that the userspace/kernel
  submission ABI is compatible.
- Keep firmware binaries, generated proprietary stubs, and microcode outside
  the repository.

Acceptance criteria:

- Existing generic and FW 5.50 behavior is preserved behind `AgcDriverOps`.
- A Sony-export backend resolves and calls installed driver exports without
  colliding with OpenAGC's public symbols.
- Backend selection is deterministic, capability-tested, and fails safely.
- FW 5.50 continues to complete the ordered websrv hardware smoke tests.
- Other firmware families are not called supported until the same tests pass
  on matching hardware.
- Each supported firmware has recorded export provenance, structure
  size/offset assertions, and explicit hardware-validation results.

The internal operations-table migration is complete. The next bounded change
is the FW 5.50 NGG geometry milestone described below. The FW 3.20 exact-profile
audit remains the first cross-firmware task after FW 5.50 matures.

## Phase 9: Higher-Level AGC Features

Status: reusable Wave32 VS+PS is complete on FW 5.50 gfx1013 hardware.
The library validates fused Gs(2)+PS records, patches program addresses, derives
primitive/interpolant state, preflights command-buffer capacity, and emits the
hardware-proven state. Advanced graphics proceeds in this order: NGG geometry,
tessellation, combined tessellation-plus-geometry, cache/synchronization
hardening, VRS, and ray tracing. Each rendering stage must have host fixtures
and a websrv hardware test before the next stage begins.

These are long-term goals and should not block draw-call work.

### Wave32 / Wave64

Status: Wave32 graphics is hardware-validated on real PS5 gfx1013 hardware.
`openagc-psbc --wave32` compiles the no-GS NGG stage with 32-lane waves while
gfx1013 pixel and compute stages remain Wave32 by default. The generated
records carry `VGT_SHADER_STAGES_EN.GS_W32_EN` and
`SPI_PS_IN_CONTROL.PS_W32_EN`; the hardware sample rejects missing bits both
before fusion and in the final PM4 stream.

Three FW 5.50 runs programmed NGG stage state `0x02412010` and PS control
`0x00008001`, submitted successfully, executed the post-draw marker, and
passed FP16 readback with 255,744 changed pixels, eight sampled colors, and
zero out-of-range components. The display path now matches the hardware-proven
VideoOut contract: a 1920x1080 linear scanout, a centered 768x768 downsampled
preview mirrored into both registered buffers, and one flip-completion wait per
frame. The websrv run completed 1,800/1,800 vsync flips over 30 seconds and was
visually confirmed as a dark-gray background with a centered blended-color
triangle. The validation also corrected the sample's
direct-memory physical offset from a truncating `int32_t` to the ABI-correct
`off_t` and decoupled the FP16 render-pool size from the scanout dimensions.

Goal:

Represent wave-size metadata and shader register state accurately.

Rule:

Do not hard-code performance claims into API behavior. Track wave mode only
where AGC shader records or registers expose it.

### Geometry / Mesh-Style Processing

Goal:

Recover native PS5 geometry setup beyond the classic GNM-style pipeline.

Current FW 5.50 gfx1013 sequence:

1. **Pass-through visual gate closed.** The split-GS ESGS ABI and GFX10
   `GE_CNTL` programming are fixed. Three identical FW 5.50 runs produced
   255,744 covered FP16 pixels, centered bounds, eight sampled colors, no
   out-of-range components, a live marker, and 1,800/1,800 completed flips.
   The physical display confirmed the centered colorful triangle on a gray
   background.
2. **Probe-free path promoted.** The compiler NGG-record regression and OpenAGC
   generic suite pass, and the documentation records the hardware-confirmed
   implementation.
3. **Geometry amplification validated.** The `triangle_amplify.geom` path emits
   two distinct half-scale textured triangles. Captured runs consistently
   produce 127,488 changed pixels, bounds `x=346..1189, y=602..933`, eight
   sampled colors, zero out-of-range components, a live marker, and
   1,800/1,800 completed flips; the physical display was confirmed three times.
4. **Extended geometry coverage validated.** A line-list input (`VGT_PRIMITIVE_TYPE=2`,
   two input vertices) reconstructs the full triangle; `invocations=2` emits two
   half-scale copies with deterministic 127,488-pixel FP16 coverage; and the
   isolated RGBA8 target produces 126,360 changed pixels against a
   dimension-derived expectation of 126,293 +/- 1,024. All three cases preserve
   eight sampled colors, a live marker, and 1,800/1,800 flips, and were confirmed
   on the physical display. The centered square viewport prevents 16:9 scanout
   stretching. Keep render-target variants isolated to one draw per process until
   same-process second-submit sequencing is characterized independently.
5. **Proceed to tessellation only after geometry passes.** Geometry remains
   ahead of tessellation and combined tessellation-plus-geometry so later
   failures do not conflate hull/domain ABI work with geometry ABI work.

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
  from GLSL/SPIR-V input. Hardware-validated for compute and Wave32 NGG+PS
  graphics shaders.

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
