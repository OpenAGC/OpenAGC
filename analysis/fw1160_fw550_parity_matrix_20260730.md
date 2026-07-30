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
| D32 HTILE ordinary/decompress/resummarize/expclear | Qualified | Qualified on both endpoints |
| Combined D32+S8 HTILE and aspect masks | Qualified | Qualified on both endpoints |
| HTILE mip and array subresources | Current-source endpoint artifacts build | Pin and hardware-qualify both mirrors |
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
changed bytes before network access. The D16 prerequisites completed before
the pinned FW 5.50 ordinary D32 HTILE mirror was launched.

The pinned FW 5.50 ordinary mirror has now completed two cleanup-first passes.
Both selected `0x0550`, reached the fence, reproduced exact native D32 classes
(`1617408` clear-one, `228096` near, and `228096` far), changed exactly `7408`
HTILE words from `0xfffc000f`, shut down cleanly, and returned final PASS. The
first wrapper output was filtered before the numeric line could be retained;
the identical count-capture replay recovered and reproduced `7408`. The recipe
now freezes that value. Run one enforcement replay before promoting the FW
5.50 ordinary tuple and moving to its exact FW 11.60 mirror.

The frozen enforcement replay also reproduced `7408`, exact D32 classes,
bounded completion, driver shutdown, final PASS, and no residual `eboot.bin`.
Ordinary D32 HTILE is therefore hardware-qualified on FW 5.50. The exact
FW 11.60 ordinary artifact is the next permitted launch; establish its count,
freeze it, and replay before any D32 expclear artifact.

FW 11.60 ordinary pass 1 selected `0x1160`, reproduced the same `7408` changed
HTILE words and exact D32 classes as FW 5.50, reached its fence immediately,
shut down cleanly, returned final PASS, and left no residual `eboot.bin`. The
logged recipe now freezes `7408`. One identical cleanup-first replay remains
before ordinary D32 HTILE can be promoted across both endpoints.

The identical FW 11.60 replay reproduced `7408`, exact D32 classes, immediate
completion, clean shutdown, final PASS, and no residual process. Ordinary D32
HTILE is hardware-qualified on both endpoints. The pinned FW 11.60 D32
expclear artifact is now the active gate; its first pass must establish and
freeze the exact changed-word count before replay, followed by the pinned FW
5.50 expclear mirror.

FW 11.60 D32 expclear pass 1 selected `0x1160`, changed exactly `49152`
HTILE words from `0xfffffff0`, reproduced the exact D32 classes, reached its
fence immediately, shut down cleanly, returned final PASS, and left no
residual `eboot.bin`. The logged recipe now freezes `49152`; run its identical
replay before permitting the FW 5.50 expclear mirror.

The identical FW 11.60 expclear replay reproduced `49152`, exact D32 classes,
immediate completion, clean shutdown, final PASS, and no residual process. The
pinned FW 5.50 expclear recipe now requires the same exact count and is the
next permitted hardware launch.

The pinned FW 5.50 expclear mirror reproduced `49152`, exact D32 classes,
bounded completion, clean shutdown, final PASS, and no residual process. The
D32 HTILE ordinary/decompress/resummarize and expclear tier is therefore
hardware-qualified on both endpoints. Proceed to combined D32+S8 ordinary
HTILE; keep aspect-specific expclear behind that combined baseline.

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

Current-source exact-key headless builds now exist for mip 1 and array layer 1
on FW `0x1160` and FW `0x0550`. The guarded depth runner can require positive
selected-subresource mutation, zero outside mutation, an exact frozen selected
count, and exact green/red coverage. Its host fixture accepts the intended
selected/outside/color tuple and rejects wrong selected, outside, and color
values. All four ELFs build without warnings. They have no deploy target yet:
relink twice, audit dependencies, preserve the exact bytes, and wire hashes
before the first current-source subresource launch.

Two committed-shader relinks reproduced exact SHA-256 values: FW 11.60 mip
`a266bc5c03554842a21a9c8bf5b34cbeb9d50ba5a646e7002353712167fd8d04`, FW
11.60 array `bbde10976bd64f2a56d81ac0fc0fb01f9000c7e0680835edd5f1ed85b4fa49cf`,
FW 5.50 mip `b8e0c004b995ee4c670d54e7bdabf5f7ebae09f100bd6c6ed689b8683806d6dd`,
and FW 5.50 array
`ef1936c6399e77261b5eacb68c02bd41c795113b88a45b3b3f4a7eedd584b2f8`.
All four depend only on VideoOut, kernel, libc, and networking, are preserved
under their full hashes, and now have cleanup-first deploy targets that reject
byte drift before network access. Mip requires exact `56832/56832` color
coverage; array requires `228096/228096`; both require positive selected and
zero outside metadata mutation. Run the pinned FW 5.50 mip gate first and
freeze its selected count before replay.

The first current-source FW 5.50 mip attempt executed successfully but was
rejected by that inherited color count. It changed `7982` selected metadata
words and zero outside, reached its fence, returned application PASS, shut down
cleanly, and left no residual process. The current headless full-rectangle
viewport produces `56832/56832`, not the historical retained-VideoOut
`31968/31968`. Both mip recipes now require the current exact count. The
attempt is not promoted because its pre-launch wrapper contract was wrong;
rerun the same pinned FW 5.50 artifact before freezing the selected count.

The corrected-wrapper FW 5.50 mip run passed exact `56832/56832` color,
positive selected metadata with zero outside, bounded completion, application
PASS, clean shutdown, and no residual process. Because the identical pinned
artifact had already exposed the exact selected count `7982`, the recipe now
freezes `7982`. Run one enforcement replay before promoting the FW 5.50 mip
tuple and moving to its exact FW 11.60 mirror.

The frozen FW 5.50 mip replay reproduced `7982` selected words, zero outside
change, exact `56832/56832` color, bounded completion, clean shutdown, final
PASS, and no residual process. Current-source mip-1 HTILE isolation is
hardware-qualified on FW 5.50. Run the pinned FW 11.60 mip artifact next,
establish and freeze its selected count, and replay before array-layer work.

FW 11.60 mip pass 1 reproduced `7982` selected words, zero outside change,
exact `56832/56832` color, immediate completion, clean shutdown, final PASS,
and no residual process. Its logged recipe now freezes `7982`; one identical
replay remains before mip isolation can be promoted across endpoints.

The identical FW 11.60 mip replay reproduced `7982` selected words, zero
outside change, exact `56832/56832` color, immediate completion, clean
shutdown, final PASS, and no residual process. Current-source mip-1 HTILE
isolation is hardware-qualified on both endpoints. Run the pinned FW 5.50
array-layer-1 gate next and freeze its selected count before replay.

That prerequisite sequence is now complete. Two relinks against the committed
shader records reproduced all eight artifacts byte-for-byte, and dependency
inspection found only VideoOut, kernel, libc, and networking. The exact FW
11.60 hashes are ordinary
`636bd2dd304d5edb62f9213c40cfec80870060baa98f6aa044e5800681afc120`, depth
expclear `48057d05dfa531fcb65958c3a2645bdcda709b211bc9520f2cbde0801e6261fb`,
stencil expclear
`edbb8672c26b7f1eb1aedb57c9c27a77da89edba5d05435ed06bb68d65498a7a`, and
both expclear `70a65a0f19f474ed5b1521700087bebdb97e0126ac0a6b0596338b389071a1ef`.
The FW 5.50 hashes are ordinary
`8cdfdfe8073a6f567949e259081eb7ade0b5e2857ad8755d028b87da07e9ad5c`, depth
expclear `4aa1a5bf0eb0e7e8f41ec057009f02e279030498138f6f1d534a8c54944b8d46`,
stencil expclear
`a08270e7bde95ff1406c86644ab692e192bb547e4515e193e02e6eb830150b6f`, and both
expclear `43c9c150cbd0f464a19ddb9b3bb3c4415a5fa4a9dbd2d79e739dcbb29fb3efdc`.
Every artifact is preserved under its full hash and every deploy recipe checks
that hash before network access. The pinned FW 5.50 ordinary combined gate is
the next permitted launch.

FW 5.50 ordinary combined pass 1 selected `0x0550`, changed exactly `49152`
HTILE words from `0xfffff30f`, reproduced exact native D32 classes and the
allocation-aware S8 distribution (`2165248` zero, `456192` replace-`0x5a`, no
other values), reached its fence, shut down cleanly, returned final PASS, and
left no residual process. The recipe now freezes `49152`; run the identical
artifact once more before permitting its FW 11.60 mirror.

The identical FW 5.50 replay reproduced `49152`, the exact D32 and S8
distributions, bounded completion, clean shutdown, final PASS, and no residual
process. Ordinary combined D32+S8 HTILE is hardware-qualified on FW 5.50. Run
the pinned FW 11.60 ordinary mirror next, establish and freeze its exact count,
then replay before any aspect-specific expclear gate.

FW 11.60 ordinary combined pass 1 selected `0x1160` and reproduced the FW 5.50
oracle exactly: `49152` changed HTILE words, the full D32 and allocation-aware
S8 distributions, immediate fence completion, clean shutdown, final PASS, and
no residual process. The logged recipe now freezes `49152`; one identical
replay remains before combined ordinary promotion.

The identical FW 11.60 replay reproduced `49152`, exact D32 and S8
distributions, immediate completion, clean shutdown, final PASS, and no
residual process. Ordinary combined D32+S8 HTILE is hardware-qualified on both
endpoints. Run the pinned FW 11.60 depth-only expclear gate next; freeze and
replay it before its FW 5.50 mirror, then repeat that sequence for stencil-only
and both-aspect gates.

The first FW 11.60 depth-only expclear attempt reached both fences, passed the
aspect-`0x1` masked-RMW oracle, reproduced exact logical color, near, far, and
stencil values, shut down cleanly, and left no residual process, but the host
wrapper rejected its D32 clear-one count. Investigation showed the sample
intentionally pre-fills the entire 2,211,840-element swizzled D32 allocation to
`1.0` for combined expclear, including 138,240 padding elements; after the two
triangles its exact allocation-aware distribution is therefore `1755648`
clear-one, `228096` near, and `228096` far. The wrapper previously hard-coded
the ordinary logical-surface clear count `1617408`. It now accepts an explicit,
numeric `EXPECTED_D32_ONE_COUNT`, defaults to the ordinary value, and all six
combined expclear recipes require `1755648`. The host fixture accepts the
allocation-aware count and rejects the ordinary count. This attempt is not
promoted because the pre-launch wrapper contract was wrong; rerun the identical
pinned artifact with the corrected fail-closed oracle.

The corrected-wrapper FW 11.60 depth-only pass succeeded. Its aspect-`0x1`
RMW selected all `49152` metadata words with zero mismatch, zero outside
change, and preserved reserved bits; the final HTILE readback also changed
exactly `49152`. It reproduced the allocation-aware D32 and exact S8
distributions, completed immediately, shut down cleanly, returned final PASS,
and left no residual process. The recipe now freezes `49152`; one identical
replay remains before the FW 5.50 depth-only mirror.

The identical FW 11.60 depth-only replay reproduced the aspect-`0x1` RMW,
`49152` changed words, allocation-aware D32 and exact S8 distributions,
immediate completion, clean shutdown, final PASS, and no residual process. The
pinned FW 5.50 depth-only mirror now requires `49152` and is the next permitted
launch.

The pinned FW 5.50 depth-only mirror reproduced aspect `0x1`, zero RMW
mismatches and outside changes, preserved reserved bits, `49152` changed
words, exact allocation-aware D32 and S8 distributions, bounded completion,
clean shutdown, final PASS, and no residual process. Depth-only combined
expclear is hardware-qualified on both endpoints. Proceed to pinned FW 11.60
stencil-only pass 1; both-aspect remains blocked.

FW 11.60 stencil-only pass 1 selected aspect `0x2`, produced expected metadata
`0xfffff0ff` across all `49152` selected words with zero mismatch, zero outside
change, and preserved reserved bits, and reproduced the exact allocation-aware
D32 and S8 distributions. It completed immediately, shut down cleanly, returned
final PASS, and left no residual process. The recipe now freezes `49152`; one
identical replay remains before the FW 5.50 stencil-only mirror.

The identical FW 11.60 stencil-only replay reproduced aspect `0x2`, expected
metadata `0xfffff0ff`, `49152` changed words, the exact allocation-aware D32
and S8 distributions, immediate completion, clean shutdown, final PASS, and no
residual process. The pinned FW 5.50 stencil-only mirror now freezes `49152`
and is the next permitted launch.

The pinned FW 5.50 stencil-only mirror reproduced aspect `0x2`, expected
metadata `0xfffff0ff`, zero RMW mismatch or outside change, preserved reserved
bits, `49152` changed words, exact allocation-aware D32 and S8 distributions,
bounded completion, clean shutdown, final PASS, and no residual process.
Stencil-only combined expclear is hardware-qualified on both endpoints. The
pinned FW 11.60 both-aspect gate is now active.

FW 11.60 both-aspect pass 1 selected aspect `0x3`, produced expected metadata
`0xfffc00f0` across all `49152` selected words with zero mismatch, zero outside
change, and preserved reserved bits, and reproduced exact allocation-aware D32
and S8 distributions. It completed immediately, shut down cleanly, returned
final PASS, and left no residual process. The recipe now freezes `49152`; one
identical replay remains before the FW 5.50 both-aspect mirror.

The identical FW 11.60 both-aspect replay reproduced aspect `0x3`, expected
metadata `0xfffc00f0`, `49152` changed words, exact allocation-aware D32 and S8
distributions, immediate completion, clean shutdown, final PASS, and no
residual process. The pinned FW 5.50 both-aspect mirror now freezes `49152` and
is the final combined-tier endpoint gate.

The pinned FW 5.50 both-aspect mirror reproduced aspect `0x3`, expected
metadata `0xfffc00f0`, zero RMW mismatch or outside change, preserved reserved
bits, `49152` changed words, exact allocation-aware D32 and S8 distributions,
bounded completion, clean shutdown, final PASS, and no residual process.
Combined D32+S8 ordinary HTILE and depth-only, stencil-only, and both-aspect
expclear are hardware-qualified on both endpoints. The next tier is current-
source HTILE mip and array isolation; the older FW 5.50 VideoOut evidence does
not replace new hash-pinned endpoint mirrors.

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
