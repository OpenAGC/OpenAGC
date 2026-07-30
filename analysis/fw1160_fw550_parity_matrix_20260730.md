# FW 11.60 versus FW 5.50 capability parity matrix (2026-07-30)

Parity requires real FW 11.60 hardware evidence for every FW 5.50
hardware-qualified capability. Shared SPRX fingerprints, a compatibility-group
match, and host tests are supporting ABI evidence; they do not replace a
bounded hardware oracle. `Qualified` below means that the FW 11.60 path passed
its hardware gate. `Regression pending` means the FW 11.60 path passed but the
matching current-source FW 5.50 mirror still needs to run.

## Direct-driver ABI

| Capability | FW 11.60 status | Remaining gap |
|---|---|---|
| Standard model/profile selection | Qualified | None |
| `/dev/gc` initialization and nine internal regions | Qualified | None |
| Register defaults | Qualified with V12 | FW 5.50 remains qualified with V8 |
| Async setup | Qualified | None |
| Submit16 and multi-DCB submission | Qualified | None |
| Authenticated special queue | Qualified | None |
| Primary and final suspend-point submission | Qualified | None |
| TF ring, including real tessellation execution | Qualified | None |
| HS-offchip carrier | Zero-entry form qualified | Non-empty patch-list execution is missing |
| Workload packets | Fail closed | Every known direct and inline form stalls; recover a new prerequisite before another gate |
| EOP flip | Disabled | FW 11.60 hardware qualification is missing |

Suspend-point query is a disabled permission export on both profiles and is
not a parity gap.

## Compute, graphics, and copy

| Capability | FW 11.60 status | FW 5.50 mirror status |
|---|---|---|
| Wave32 compute | Qualified | Previously qualified |
| Baseline graphics and color formats | Qualified | Current-source format matrix pending |
| Indexed, indirect, and indexed-indirect draws | Qualified | Passed current-source mirrors on 2026-07-30 |
| NGG amplify, lines, and invocations | Qualified | Passed current-source mirrors on 2026-07-30 |
| Isolated tessellation | Qualified | Passed current-source mirror on 2026-07-30 |
| TES-to-NGG geometry matrix | Qualified | Passed all four current-source mirrors on 2026-07-30 |
| Standalone 8,294,400-byte DMA copy | Qualified | Passed twice on 2026-07-30 |

The FW 5.50 mirrors reproduced the FW 11.60 DCB sizes and exact output hashes:
draws `0x4a40c2eb4f12bc26`, NGG amplify/invocations
`0xac17a0b9c08e25d7`, NGG lines `0xf27532f1c0414783`, tessellation
`0x1754baabb2b216ca`, combined geometry `0xce4e39671f7448bc`, combined
invocations `0x1527e4785be7854a`, combined lines `0x929cf8dfd6a5b809`,
combined BGRA8 `0xe63963f065bd9a51`, and copy
`0xdd3702089b80f950`. Every run used the cleanup ELF immediately beforehand,
reached its completion fence, shut the driver down, and returned PASS.

## Depth, stencil, and sampling

| Capability | FW 11.60 status | Remaining gap |
|---|---|---|
| Uncompressed D32, D16, S8, and D16+S8 | Qualified | Current-source FW 5.50 mirrors pending |
| Ordinary D16 HTILE | Prepared | Hardware qualification missing |
| D16 HTILE expclear | Prepared | Hardware qualification missing |
| D32 HTILE ordinary/decompress/resummarize/expclear | Missing | Prepare bounded mirrors |
| Combined D32+S8 HTILE and aspect masks | Missing | Prepare bounded mirrors |
| HTILE mip and array subresources | Missing | Prepare bounded mirrors |
| 4x MSAA | Missing | Wait for the FW 5.50 baseline regression |
| Sample-rate shading | Missing | Add an exact invocation-count gate |

The first 2026-07-30 D32 mirror followed 11 successful graphics payloads and
kernel-panicked FW 5.50. Investigation found that every graphics launch leaked
its 65,536-byte command mapping and roughly 19 MiB graphics pool before
`SIGKILL`; the sequence accumulated about 219 MiB before D32. The simultaneous
shader-record byte change was not a command-stream delta because the sample
already derived and emitted the physical 11-entry CX block. Graphics and
compute now release all sample-owned flexible mappings, and ordinary depth
builds consume committed qualified shader records. Fresh-boot teardown stress
qualification is required before the uncompressed-depth mirror resumes. See
`analysis/fw550_headless_flexible_memory_panic_20260730.md`.

## Higher-level consumers

The following FW 5.50-qualified application-facing paths still need bounded
FW 11.60 gates: multi-viewport, cube arrays, dual-source blending, sample-rate
shading counters, application-neutral GPU authorization, VideoOut-integrated
graphics and compute, EOP flip, non-empty HS-offchip lists, and the standalone
cube consumer.

Occlusion queries, polygon modes, general MRT blending, and wide point/line
policy are not parity blockers yet because they are not hardware-qualified on
FW 5.50 either.

## Required execution order

1. On a fresh FW 5.50 boot, stress the fixed baseline past the old cumulative
   flexible-memory threshold and require zero cleanup results after every run.
2. Rerun the FW 5.50 color and uncompressed-depth mirrors using the committed
   qualified shader records.
3. Only after the uncompressed-depth baseline passes, run the FW 5.50 ordinary
   D16 HTILE mirror, then FW 11.60 ordinary and expclear twice each.
4. Prepare and qualify D32 HTILE operations, combined depth/stencil,
   subresources, and MSAA with the matching FW 5.50 mirror first.
5. Add bounded FW 11.60 gates for the higher-level consumer gaps.
6. Keep workload support fail closed and do not repeat stages 11-17 unchanged.
