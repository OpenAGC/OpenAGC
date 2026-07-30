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
| Public linear VideoOut lifecycle | Qualified | FW 11.60 `+0x9922` patch/restore, live AGC marker, and bounded flips passed twice |

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
| Uncompressed D32, D16, S8, and D16+S8 | Qualified | Qualified on both endpoints |
| Ordinary D16 HTILE | Qualified | Qualified on both endpoints |
| D16 HTILE expclear | Qualified | Qualified on both endpoints |
| D32 HTILE ordinary/decompress/resummarize/expclear | Prepared | Hardware qualification missing |
| Combined D32+S8 HTILE and aspect masks | Prepared | Hardware qualification missing |
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
has now passed; the uncompressed-depth mirror is the active gate. See
`analysis/fw550_headless_flexible_memory_panic_20260730.md`.

The reproducible gate is `make cleanup_stress_fw550`: it runs 14 file-backed
baseline launches, calls the cleanup ELF immediately before every payload, and
requires all four sample cleanup results plus driver shutdown to pass on every
iteration. Its FW 11.60 twin can validate the runner and shared teardown, but
does not close the FW 5.50 regression. FW 5.50 hardware is currently
available again. The FW 11.60 twin passed a one-launch canary followed by all
14/14 stress iterations on 2026-07-30. FW 5.50 then passed the corrected-path
one-launch canary and its full 14/14 threshold run with the pinned
current-source ELF. Both endpoints reached their fence, shut the driver down,
and reported all four cleanup results as zero on every accepted launch. The FW
5.50 teardown regression is closed. The current-source uncompressed D32, D16,
S8, and D16+S8 mirrors then passed with exact native distributions, completion,
shutdown, cleanup, and no residual process. Ordinary D16 HTILE is now the
active hardware gate.

The D32 compressed-depth tier now has four bounded source-built artifacts:
ordinary/decompress/resummarize and expclear variants for exact FW `0x1160`,
plus matching headless FW `0x0550` mirrors. Their guarded recipes require the
cleanup ELF immediately before launch, exact firmware selection, bounded fence
completion, full-rectangle D32 distributions, positive HTILE mutation, driver
shutdown, and final PASS. All four build without warnings and have no dynamic
dependency on `libSceAgc.sprx` or `libSceAgcDriver.sprx`. Two relinks against
the committed, hardware-qualified depth shader record reproduced these exact
SHA-256 values: FW 11.60 ordinary
`f67d8d2087cd463e60e118cd9aa6813b4b63162d8c88a61bb1882fc96d9a8a0d`, FW
11.60 expclear
`e91430172cba028cfa8379b9aa0e87876107b6fb6a81096d22038c59869448d9`, FW 5.50
ordinary `599fd135cac34e6e94c63cee18171e80c3eefc321b6da47c095789bd8196ed13`,
and FW 5.50 expclear
`a10eb1985e99822210102fc3a5eec3039fe79f5bf10a67d2c737a2fadf77fce3`.
Each artifact is preserved under its full hash and every deploy recipe rejects
changed bytes before network access. None has been run on hardware. The D16
prerequisites are complete, so the pinned FW 5.50 ordinary D32 HTILE mirror is
the next permitted launch.

The combined D32+S8 tier is likewise prepared without a hardware claim. Eight
artifacts cover ordinary HTILE and expclear of depth-only, stencil-only, or
both aspects for exact FW `0x1160` and matching headless FW `0x0550`. The
runner now additionally requires the exact allocation-aware stencil
distribution (`456192` replaced and `2165248` zero bytes, including swizzle
padding) and, for expclear, an enabled
aspect-specific RMW plan with zero selected mismatches, zero outside changes,
preserved reserved bits, and its bounded fence. A host fixture proves that the
runner accepts the intended aspect and rejects the wrong one. All eight ELFs
build without warnings and avoid both AGC SPRX dependencies. They remain
behind the FW 5.50 uncompressed-depth, D16 HTILE, and D32 HTILE qualification
sequence.

## Higher-level consumers

The following FW 5.50-qualified application-facing paths still need bounded
FW 11.60 gates: multi-viewport, cube arrays, dual-source blending, sample-rate
shading counters, application-neutral GPU authorization, VideoOut-integrated
graphics and compute output, EOP flip, non-empty HS-offchip lists, and the
standalone cube consumer. The public VideoOut lifecycle underneath those gates
is now hardware-qualified twice with a live direct AGC marker; see
`analysis/videoout_linear_patch_versions_20260730.md`.

Occlusion queries, polygon modes, general MRT blending, and wide point/line
policy are not parity blockers yet because they are not hardware-qualified on
FW 5.50 either.

## Required execution order

1. On a fresh FW 5.50 boot, stress the fixed baseline past the old cumulative
   flexible-memory threshold and require zero cleanup results after every run.
2. Rerun the FW 5.50 color and uncompressed-depth mirrors using the committed
   qualified shader records.
3. The uncompressed-depth, ordinary D16 HTILE, and D16 expclear endpoint
   sequences are complete.
4. Prepare and qualify D32 HTILE operations, combined depth/stencil,
   subresources, and MSAA with the matching FW 5.50 mirror first.
5. Add bounded FW 11.60 gates for the higher-level consumer gaps.
6. Keep workload support fail closed and do not repeat stages 11-17 unchanged.

The current-source D16 expclear artifacts completed that endpoint sequence:
FW 11.60 passed twice and FW 5.50 passed the pinned mirror with exact D16
classes and `49152` changed HTILE words from `0xfffffff0`. Every accepted run
used cleanup first, reached its fence, shut down the driver, returned PASS,
and left no residual process. D32 HTILE is now the active depth tier.
