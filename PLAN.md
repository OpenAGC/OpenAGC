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

## Primary Product Requirement: One Firmware-Neutral Homebrew Binary

The main deliverable is one homebrew game binary linked with OpenAGC that can
run unchanged on every supported PS5 firmware. A game built on or for FW 11.60
must not require recompilation for FW 5.50, and a game built on or for FW 5.50
must not require recompilation for FW 11.60 or another supported profile.

Acceptance requires all of the following:

1. The application-facing OpenAGC headers, symbols, structs, and calling
   conventions are firmware-independent.
2. Production application builds contain no compile-time expected-firmware
   key. Test-only macros may assert the console under test, but they must not
   select the implementation linked into the game.
3. OpenAGC reads the console's full runtime version, for example
   `0x11600005`, normalizes only the ABI key (`0x1160`), and selects every
   firmware-dependent driver, defaults, memory, queue, submission, VideoOut,
   and optional-feature detail internally.
4. Every supported key is an exact table entry backed by its own SPRX facts.
   Numeric ranges, nearest-version fallback, and compatibility-group inference
   do not constitute support. Unknown keys fail closed without corrupting
   process or GPU state.
5. One pinned portability ELF and one SHA-256 digest exercise the baseline
   game lifecycle: application-neutral GPU authorization, `/dev/gc` init,
   internal memory, register defaults, async graphics, real GPU execution,
   bounded VideoOut presentation, teardown, and relaunch.
6. That identical ELF must pass on both available endpoint consoles: standard
   PS5 FW 5.50 and standard PS5 FW 11.60. Rebuilding with a different firmware
   macro or comparing two source-equivalent ELFs does not satisfy this gate.
7. Intermediate exact firmware profiles may be enabled from reproducible SPRX
   evidence when hardware is unavailable, but documentation must label them
   hardware-unverified. Optional capabilities unavailable on a profile are
   discovered at runtime and disabled without preventing the common baseline
   game path.

The active support floor remains FW 3.20. FW 1.x, FW 2.x, and FW 3.00 remain
archival unless that policy is explicitly changed with matching evidence and
hardware need.

## Current Execution Order

### Firmware-neutral binary portability (highest priority)

The direct backend already recognizes 39 exact active ABI keys from FW 3.20
through FW 12.70 and selects them from the runtime version; submit, memory,
queue, primary suspend, TF-ring, HS-offchip, and async carriers have per-key
SPRX evidence. That is necessary but not yet sufficient for a portable game.
The compile-time audit found no production expected-firmware build input. The
39-profile VideoOut ledger and exact runtime table are complete. Register-
default recovery is also complete: the selector is the caller's
`sceAgcInit(version)` argument stored in the runtime record, not an unavailable
hardware choice. Every active key now carries its exact accepted upper bound;
OpenAGC preserves the caller-selected version instead of using that maximum as
a selected policy. FW5.50/V8 and FW11.60/V12 are hardware-qualified.

Execute in this order:

1. **Complete:** audit all installed public-library and application-consumer code for
   compile-time firmware constants, exact-version branches, and hidden
   firmware-specific shader, packet, memory, or VideoOut assumptions.
2. **Complete:** recover register-default selection for every active exact
   profile by proving the `sceAgcInit` caller-argument flow into the runtime
   record, recording each dispatcher bound, and keeping selection distinct
   from that bound in the direct backend. Explicit V0-V12 blob layouts and
   allocation-fit tests cover every accepted caller choice.
3. **Complete:** extract and verify the linear VideoOut registration branch offset and full
   original instruction signature from every active profile's own
   `libSceVideoOut.sprx`, then generate the runtime table used by the core.
4. **Complete:** define the common baseline capability contract a normal game can require.
   Keep workload packets, EOP flip, non-empty HS patch lists, and other narrow
   operations optional and runtime-queryable rather than allowing them to make
   the baseline binary firmware-specific.
5. **Complete:** build one unpinned portability payload. It prints the detected full
   version and selected four-digit key, but it must not be compiled with
   `AGC_EXPECT_FIRMWARE_ABI_KEY` or link a firmware SPRX.
6. **FW11.60 complete; FW5.50 pending:** the pinned ELF passed twice on
   FW11.60. Run the exact same bytes on FW5.50 when available, with the cleanup
   payload immediately before every launch and file-backed bounded verdicts.
7. **Complete:** preserve the same artifact for future intermediate-firmware testing. Until
   matching hardware exists, run corpus verifiers and host fixtures for every
   exact profile and report those rows as SPRX-qualified/hardware-unverified.
8. **Complete:** recover the Sony indirect-draw public ABI across all 39
   active profiles, select the sole FW 3.20 initiator difference by exact
   runtime key, and lock modifier, count-address, GetSize, cursor, and
   short-buffer behavior with fixtures. The application-facing indirect
   compositor now defaults to the Sony 10-dword multi form for both single and
   multiple draws. Fixed-count non-indexed and indexed forms each passed twice
   on FW11.60 through cleanup-first bounded gates. Non-indexed and indexed
   `draw_count=2` plus GPU count-buffer selection each subsequently passed
   twice with distinct second-draw geometry. Repeat all current paths on
   FW5.50 when that console is available.
9. Resume higher-level parity work only with firmware-neutral artifacts so
   each new game-facing capability strengthens the one-binary contract.

The FW 11.60 public VideoOut lifecycle and flexible-memory relaunch stress are
already hardware-qualified supporting components. They do not by themselves
prove binary portability because their existing qualification payloads contain
test-only expected-firmware macros and have not run as identical bytes on both
endpoint consoles.

The neutral target is now `samples/hw_test/agc_portability.elf`, pinned as
SHA-256 `e04004fee2254e6169805f153ce4812197726ed5f53a9295a4493f0d8ac9a9ce`
before hardware execution. It contains no expected-firmware macro or AGC SPRX
dependency and uses the common V7 caller ABI. The exact bytes passed 2/2 on
standard FW `0x11600005`, including live GPU execution, two flips, teardown,
and relaunch. The FW5.50 identical-byte run remains pending. See
`analysis/firmware_neutral_portability_elf_20260730.md`.

The offline endpoint audit is complete. Full nonzero-suffix raw versions now
exercise normalization, exact selection, and common-V7 acceptance for all 39
profiles; the clean host suite passes 6,454 assertions. All relevant SPRX
ledgers reproduce from `/Volumes/Untitled/unp`, and
`tools/verify_fw550_fw1160_compatibility.py` locks every shared layout and
classified endpoint difference. The FW5.50 target now uses only preserved
inputs: it authenticates the payload, cleanup, and kernel-only firmware probe,
kills a stale renderer, rejects any console key other than `0x0550`, then runs
cleanup immediately before each payload. No rebuild is part of the remaining
gate. See `analysis/fw550_fw1160_offline_portability_audit_20260730.md`.

### Existing FW 11.60 versus FW 5.50 capability work

The complete FW 11.60-versus-FW 5.50 capability inventory and required gate
order are maintained in
`analysis/fw1160_fw550_parity_matrix_20260730.md`. On 2026-07-30 the audited
FW 5.50 mirrors for buffer copy, draw variants, NGG, tessellation, and
TES-to-NGG geometry all passed and reproduced the FW 11.60 hashes. Color and
uncompressed-depth mirrors remain pending; do not advance to HTILE or MSAA
until those baselines pass from one pinned shader-compiler revision.

The first FW 5.50 D32 mirror then kernel-panicked after 11 successful headless
graphics launches. Those launches had accumulated about 219 MiB because the
sample never released its command and graphics-pool flexible mappings before
`SIGKILL`. Graphics and compute teardown now owns those releases and fails the
gate on a release error; depth shader regeneration is explicit rather than an
implicit build side effect. Start with the fresh-boot cleanup stress gate in
`analysis/fw550_headless_flexible_memory_panic_20260730.md`. Do not run depth,
HTILE, or MSAA until it passes. The committed `cleanup_stress_fw550` target
runs 14 file-backed launches and checks every release result; FW 5.50 hardware
is currently unavailable, so this gate and all dependent work remain pending.
The `cleanup_stress_fw1160` twin passed 14/14 launches after its canary, proving
the runner and shared teardown on FW 11.60 only.

### FW 11.60 workload parity gate

- Keep the public workload capability disabled while testing isolated causes
  of the standard-console inline `SET_WORKLOAD` stall.
- Stage 14 completed once and still stalled after its ordinary preflight
  marker passed. The complete 40-dword flush and reset timer therefore rule
  out cache coherency as the missing requirement; do not repeat it.
- Stage 15 completed once and reproduced the stall after its exact 2 MiB
  aperture, two descriptors, and Gn2/Gn3/Gn4 publications all returned
  `AGC_OK`; do not repeat it. The ordinary preflight marker still completed in
  50 ms, so the recovered constructor state is not sufficient.
- Full FW 5.50/FW 11.60 builder comparison found no semantic difference in
  Sony's prefix-plus-nine-dword form. Stage 16 is now built as an isolated
  counterfactual using the separate three-dword direct form actually proven on
  FW 5.50, with the qualified defaults/async/preflight sequence unchanged.
  Stage 16 was run once: its ordinary marker completed in 50 ms and the exact
  `0xc0011e80`/`0xc0011e84` DCB submitted, but both following markers remained
  zero for 5 seconds. Cleanup removed PID 109. Do not repeat it; both known
  workload packet forms now fail on FW 11.60. Keep the public capability fail
  closed.
- Full-constructor tracing recovered one concrete state difference that stages
  11-15 missed: Sony fills the `GpuInfo + 0x3a000` slot table with `0xff`,
  seeds its first 16 bytes from an all-ones `0xc010813b` request, and leaves
  zero there only when that request fails. OpenAGC had zeroed all `0x200`
  bytes. Stage 17 was run once as stage 15 plus only this exact lifecycle. The
  seed request succeeded with values
  `fff0ffe0/fff0ffe0/ffffffff/ffffffff`, and its ordinary marker completed in
  50 ms, but the inline workload still stalled before either following marker.
  Cleanup removed PID 101. Do not repeat stage 17 unchanged. Recover a new
  GPU-side queue/register prerequisite before another workload gate.
- After a successful FW 11.60 candidate passes twice, rerun the corresponding
  direct path on FW 5.50 before enabling any capability. Corpus extraction
  now proves the
  standard Gn2/Gn3/Gn4 constructor state on every exact active key from 6.00
  through 12.70 and the reduced Gn2-only Trinity branch from 9.00 onward. This
  is ABI evidence, not workload qualification; keep untested firmware gated.
- Evidence and artifact hashes are recorded in
  `analysis/fw1160_register_shadow_20260729.md` and
  `analysis/agc_driver_shadow_facts.md`. The stage-16 boundary and artifact
  hash are in `analysis/fw1160_workload_stage16_plan_20260729.md`; the corrected
  slot lifecycle and stage-17 artifact are in
  `analysis/fw1160_workload_stage17_plan_20260730.md`.

### FW 11.60 graphics and compute parity gates

- The headless graphics baseline passed twice on standard FW `0x11600005`:
  exact profile, Wave32 NGG/PS audit, indexed draw, bounded fence, RGBA16F
  readback, clean shutdown, and no residual process. Baseline graphics is now
  hardware-qualified for FW 11.60.
- The headless compute artifact is audited, rebuilt, and does not call any
  workload API. It uses the exact `0x1160` standard profile,
  version-12 defaults, async setup, bounded completion/readback oracles, clean
  shutdown, and forced self-termination.
- Headless compute subsequently passed twice: completion fences at 2 ms and
  1 ms, exact 2,073,600/2,073,600 shader output, clean shutdown, and no
  residual process. Baseline Wave32 compute is hardware-qualified for standard
  FW 11.60.
- Workload stages 11-17 are closed failed gates and remain independent of the
  now-qualified graphics and compute paths. Require new offline evidence before
  another workload payload.
- The first advanced graphics gates were headless `R16_FLOAT`, then
  `RG16_FLOAT`. Both reuse the qualified baseline draw and differ only in the
  typed color-target tuple and native readback width. Exact FW 11.60 artifacts
  and target-specific guarded runner support are built. Both formats passed
  twice on standard FW `0x11600005`, with reproducible native hashes, 1-4 ms
  fences, clean shutdowns, and no residual process. Build and run matching
  modern headless FW 5.50 artifacts before promotion. See
  `analysis/fw1160_narrow_fp16_gate_audit_20260730.md`.
- The next unqualified 16-bit tuple, `R16_UNORM`, is now implemented as an
  appended public typed format and a single firmware-neutral headless ELF.
  The exact SHA-256
  `c0a5ad4732bf13c41f96560cb2dbfa3c39dffb9a47958ccbd2bef5754523220a`
  passed twice on standard FW `0x11600005`, reproducing 255,217 validated
  pixels, the full native `0x0000..0xffff` conversion range, and FNV64
  `0x4f17d5e6b1c0d45b`. The guarded runner validates both local and uploaded
  bytes and creates the neutral result directory before cleanup-first launch.
  Replay those exact bytes on FW 5.50 when available; all other active
  profiles remain SPRX-qualified/hardware-unverified. See
  `analysis/fw1160_r16_unorm_portable_qualification_20260730.md`.
- `RG16_UNORM` is now the second firmware-neutral UNORM16 tuple. Its exact
  SHA-256 `d004a33d1d1245964b08ee22b577948d36537c68d9d8c5241ba9e78e4a39f2fd`
  passed twice on standard FW `0x11600005`. Both lanes independently
  reproduced full `0x0000..0xffff` ranges, bounded coverage, eight-or-more
  values, and distinct hashes; the packed native FNV64 was
  `0xf0866450a3c42b45`. The exact R16, RG16, base portability, and three
  10-dword indirect artifacts now live under hash-named local `pinned/` paths.
  Their FW 5.50 replay targets have no build prerequisites, so later endpoint
  qualification cannot silently recompile different bytes. The three neutral
  indirect artifacts also passed twice each on FW 11.60. See
  `analysis/fw1160_rg16_unorm_and_endpoint_replay_20260730.md`.
- `RGBA16_UNORM` completes the first firmware-neutral UNORM16 group. The
  appended tuple uses gfx1013 `16_16_16_16`, UNORM, standard swap, eight bytes
  per pixel, and the FP16_ABGR export. Exact SHA-256
  `13ca0dfaa743438301ecbe5d5255c0168bb89a80bf3ff0e68cdeae8a34908c88`
  passed twice on standard FW `0x11600005`: 255,744 pixels in exact `768x665`
  bounds, four independently validated near-full-range lanes, pairwise-
  distinct lane hashes, packed FNV64 `0xbad47fbdb2e3991e`, immediate fences,
  and zero-valued teardown. Preserve those exact bytes for FW 5.50 replay.
  See `analysis/fw1160_rgba16_unorm_portable_qualification_20260730.md`.
- `R16_SNORM` is now host-implemented as the next append-only tuple:
  gfx1013 `16`, SNORM, standard swap, two bytes per pixel, and FP16_ABGR
  export. Exact PM4, all-profile selection, 64-bit layout limits,
  invalid-enum behavior, and every 0-27-dword short-buffer boundary pass on
  the host. Its firmware-neutral hardware gate now uses a reusable signed
  four-lane fragment fixture and a sentinel-safe `int16_t` oracle with signed
  endpoint, diversity, coverage, independence, and raw-hash checks. The final
  firmware-neutral ELF is pinned before execution as SHA-256
  `e6aea5164b215d401244ebec13ace8e8ab95fe9e15a8e82d9a59310cfc09e1ef`;
  it is not yet hardware-qualified.
- Seven additional offscreen format gates now build under the same exact
  profile and bounded runner: R8, RG8, RGB10A2, R11G11B10, R32, RG32, and
  RGBA32. All seven passed twice on FW 11.60; logged RG32 reproduced FNV64
  `0x806171be9908c276` and logged RGBA32 reproduced
  `0x1e8771ed63381dce`, with zero invalid samples and clean shutdowns.
  RGBA8/BGRA8 UNORM
  and sRGB variants now have real headless flexible-memory targets, unchanged
  native oracles, exact FW 11.60 artifacts, and exact FW 5.50 regression
  mirrors. Run them after RG32/RGBA32 on the clean boot. See
  `analysis/fw1160_color_format_gate_matrix_20260730.md` and
  `analysis/fw1160_rgba8_srgb_headless_gate_audit_20260730.md`.
  FW 11.60 websrv stopped returning foreground stdout even though the payload
  self-terminated cleanly, then stopped entering `main`. An exact logged RG32
  daemon-loader probe passed the full GPU, readback, shutdown, and final
  verdict. File-backed variants and a stale-proof FTP polling runner now use
  that headless-only loader path for RG32, RGBA32, and the four RGBA8 gates;
  use the same logged artifact for both qualifying passes.
  The first RGBA8_UNORM gate found a stale centered-square coverage oracle:
  GPU execution, fence, marker, and shutdown passed, but the current headless
  full-rectangle viewport produced 224,640 pixels rather than the obsolete
  126,293 expectation. The validator now uses rectangular area in headless
  mode and retains the square formula for display fixtures. After that fix,
  RGBA8_UNORM, BGRA8_UNORM, RGBA8_SRGB, and BGRA8_SRGB each passed twice with
  reproducible native hashes, zero sRGB mismatches, clean shutdowns, and no
  residual process. Matching FW 5.50 mirrors remain pending because that
  console is offline.
- The direct-indexed, non-indexed indirect, and indexed-indirect draw variants
  now have exact logged FW 11.60 gates and current-source headless FW 5.50
  mirrors. The shared runner additionally requires the exact intended draw
  path in the verdict, so a generic baseline PASS cannot qualify the wrong
  compile-time variant. All three passed twice on FW `0x11600005`, with
  immediate fences, 255,744 complete FP16 pixels, exact hash
  `0x4a40c2eb4f12bc26`, clean shutdowns, and no residual process. Run the
  current-source FW 5.50 mirrors when that console returns. See
  `analysis/fw1160_indexed_indirect_gate_plan_20260730.md`.
- The next non-tessellation geometry tier is prepared: NGG amplification,
  line topology, and multiple invocations. Exact logged FW 11.60 artifacts and
  current-source headless FW 5.50 mirrors build without warnings. An explicit
  variant identity is now part of each verdict and the runner rejects a
  baseline payload under a variant gate. All three passed twice on FW
  `0x11600005` with immediate fences, exact variant-specific FP16 coverage and
  hashes, clean shutdowns, and no residual process. Prepare isolated
  tessellation next; keep the current-source FW 5.50 mirrors pending. See
  `analysis/fw1160_ngg_geometry_gate_plan_20260730.md`.
- The isolated HS+TES+PS gate is now prepared with exact logged FW 11.60 and
  headless FW 5.50 artifacts. Its runner requires the explicit tessellation
  identity, an `AGC_OK` reusable binder, positive offchip mutation, exactly
  four whole-ring `4.0f` factor values, the ordinary exact FP16 oracle,
  bounded fence, and clean shutdown. An exploratory pair proved that the
  factor-ring slot rotates, so first-word sampling was replaced by a complete
  ring scan. The strengthened artifact then passed twice on FW `0x11600005`:
  immediate fences, offchip mutation `24`, four valid factors, 255,744
  complete pixels, exact hash `0x1754baabb2b216ca`, clean shutdowns, and no
  residual process. Prepare combined TES-to-NGG geometry next; keep the
  current-source FW 5.50 mirror pending.
  `analysis/fw1160_tessellation_gate_plan_20260730.md`.
- The four combined TES-to-NGG gates are now prepared: ordinary geometry,
  invocations, line strip, and direct BGRA8. Exact logged FW 11.60 artifacts
  and current-source FW 5.50 mirrors build without warnings. Each inherits the
  whole-ring four-`4.0f` tessellation oracle, explicit variant identity,
  bounded fence, target-specific readback, and clean shutdown. All four passed
  twice on FW `0x11600005` with exact repeated FP16/BGRA8 hashes, immediate
  fences, offchip mutation `24`, four valid factors, clean shutdowns, and no
  residual process. Keep the current-source FW 5.50 mirrors pending. See
  `analysis/fw1160_tess_geometry_gate_plan_20260730.md`.
- A standalone buffer-copy parity gate now removes the prior SDL consumer's
  non-exact image oracle. It copies the same 8,294,400 bytes through four raw
  gfx1013 `DMA_DATA` packets, waits on an EOP fence, invalidates the
  destination, and requires zero word mismatches plus identical native FNV64
  hashes. Exact logged FW 11.60 and headless FW 5.50 artifacts build without
  warnings. FW 11.60 passed twice with a 38-dword DCB, exact 8,294,400-byte
  transfer, zero mismatches, reproducible hash `0xdd3702089b80f950`, clean
  shutdown, and no residual process. Keep the FW 5.50 mirror pending. See
  `analysis/fw1160_buffer_copy_gate_plan_20260730.md`.
- The first uncompressed depth/stencil tier is also built for exact FW 11.60:
  D32, D16, S8-only, then D16+S8. All four passed twice on standard FW
  `0x11600005`, with exact native distributions, immediate-to-3 ms fences,
  clean shutdowns, and no residual process. D32 was missing from the original
  matrix; its exact logged `0x1160` artifact reproduced the full-rectangle
  color and native-depth oracle twice, and its exact headless `0x0550` mirror
  is built. Matching modern headless FW 5.50 artifacts are built; run D32,
  D16, S8-only, and D16+S8 once before promotion. Do
  not advance to HTILE, expclear, compressed metadata, or MSAA until that
  regression passes.
  See `analysis/fw1160_uncompressed_depth_gate_audit_20260730.md`.
- The next isolated compressed-depth artifacts are prepared but hardware
  gated: ordinary D16/HTILE first, then D16 HTILE expclear. Exact logged
  `0x1160` artifacts and exact headless `0x0550` mirrors build without
  warnings. The runner requires full-rectangle color/D16 distributions,
  positive metadata mutation, bounded completion, shutdown, and final PASS.
  Do not launch them until the modern FW 5.50 uncompressed depth regressions
  pass. See `analysis/fw1160_d16_htile_gate_plan_20260730.md`.
- Require two identical passes per capability, then rerun the corresponding FW
  5.50 paths before promotion. See
  `analysis/fw1160_graphics_compute_gate_audit_20260729.md` and
  `analysis/fw1160_graphics_qualification_20260730.md` and
  `analysis/fw1160_compute_qualification_20260730.md`. Exact modern headless
  FW 5.50 mirrors for all nine color formats are built and recorded in
  `analysis/fw550_headless_color_regression_matrix_20260730.md`.

### Render-target format expansion through the 128-bit regular-color ceiling

Treat 128 bits per uncompressed format element (`RGBA32`, four 32-bit
components) as OpenAGC's regular color-buffer ceiling, then complete the useful
format matrix in increasing hardware-risk order. For a texture this element is
one texel; for a render target it is one color sample. MSAA multiplies the
per-pixel-location storage separately.

This boundary is supported independently by Mesa revision `44e18d3d` and Linux
revision `0ce37745d`: the largest regular-width gfx10.3 `ColorFormat` entry is
`COLOR_32_32_32_32=0x0e`, Mesa maps no wider ordinary color-buffer layout, and
RADV excludes 64-bit-component formats from color attachments. The evidence is
in `../mesa/src/amd/registers/gfx103.json`,
`../mesa/src/amd/common/ac_formats.c`,
`../mesa/src/amd/vulkan/radv_formats.c`, and
`../linux/drivers/gpu/drm/amd/include/navi10_enum.h`. This remains an OpenAGC
API boundary rather than a blanket claim about every special-purpose gfx1013
encoding, and each PS5 tuple still requires firmware evidence and hardware
qualification.

#### 1. Establish the format contract

Keep formats represented by four independent properties:

- Component layout: `16`, `16_16`, `16_16_16_16`, and so on.
- Number type: UNORM, SNORM, UINT, SINT, FLOAT, or SRGB.
- Component swap.
- Pixel size and matching pixel-shader export format.

Public enum values must only be appended so existing binaries retain their
ABI. Add a compile-time assertion ensuring every public format has exactly one
format-table entry.

Preserve the gfx10.3 usage boundaries recovered from Mesa and Linux:

- Plain three-channel `RGB8`, `RGB16`, and `RGB32` are not color-target
  layouts. In particular, Mesa exposes `32_32_32` as buffer-only.
- No format with a 64-bit component is a color attachment.
- Scaled number types are not regular color-buffer formats.
- UINT and SINT color attachments are valid but not blendable.
- Packed formats and component swaps remain distinct tuples even when their
  total element size matches a plain format.

#### 2. Complete 16-bit normalized formats

These are the best next targets because they reuse the already-qualified
16-bit layouts and FP16 pixel-shader export path. Qualify them in this order:

1. `R16_UNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
2. `RG16_UNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
3. `RGBA16_UNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
4. `R16_SNORM` — next implementation and FW 11.60 qualification gate.
5. `RG16_SNORM`.
6. `RGBA16_SNORM`.

For every format, test:

- Exact CB format and number-type fields.
- Correct bytes per pixel.
- Correct shader-export selection.
- Surface pitch, padding, size, and alignment.
- Integer-overflow rejection.
- Short command-buffer behavior.
- Exact emitted PM4 stream.
- Invalid enum rejection.

UNORM hardware readback must prove:

- Expected triangle coverage.
- Multiple distinct values independently in every stored component.
- Values approaching both `0x0000` and `0xffff` independently in every stored
  component.
- Per-component coverage within the expected bounded window and unchanged
  sentinels outside it.
- Reproducible per-component and packed native-memory hashes.

Do not require every covered component to differ from its initialization
sentinel: every 16-bit bit pattern is a legal UNORM value, so interpolation can
legitimately reproduce the sentinel. Qualification must instead combine the
bounded coverage, range, diversity, hash, and, where applicable,
component-independence oracles.

SNORM must similarly demonstrate both negative and positive ranges in every
stored component, with exact native signed interpretation and independently
reproducible component hashes.

#### 3. Add 16-bit integer formats

After normalized formats pass, qualify:

1. `R16_UINT`.
2. `RG16_UINT`.
3. `RGBA16_UINT`.
4. `R16_SINT`.
5. `RG16_SINT`.
6. `RGBA16_SINT`.

These formats need dedicated integer-output pixel shaders. Do not qualify them
using a floating-point shader and assume the conversion is correct. Their
oracles must validate exact integer values rather than approximate
interpolation. Integer color targets must be qualified with blending disabled;
attempting to enable blending must fail validation without emitting commands.

#### 4. Complete 32-bit integer formats

The float forms already reach the maximum widths. Add:

- `R32_UINT`, `RG32_UINT`, and `RGBA32_UINT`.
- `R32_SINT`, `RG32_SINT`, and `RGBA32_SINT`.

`RGBA32_*` remains the regular color-buffer maximum:

| Format | Bits per element/sample |
| --- | ---: |
| `R32` | 32 |
| `RG32` | 64 |
| `RGBA32` | 128 |

Surface-layout arithmetic must safely handle 16 bytes per element/color sample
and MSAA multiplication without overflow. Do not add `RGB32_*` render-target tuples:
Mesa marks the 96-bit layout buffer-only. Do not infer `R64_*` color-target
support from its 64-bit element size; RADV explicitly excludes 64-bit-component
formats from color attachments.

Treat fast-clear and auxiliary-compression support as separate capabilities.
Mesa disables CMASK fast clear above 64 bits per element, so an `RGBA32_*`
color-target PASS must not imply CMASK fast-clear qualification. DCC, CMASK,
FMASK, and MSAA combinations require their own bounded gates.

#### 5. Consider packed formats separately

After the regular matrix is stable, evaluate evidence and homebrew demand for:

- `RGB10A2_UINT`.
- Additional packed 16-bit formats such as `RGB565` or `RGBA5551`.
- gfx10.3 `R9G9B9E5_FLOAT`, which Mesa exposes as a regular color-buffer tuple
  only from gfx10.3 onward.
- Alternate component swaps.
- Any firmware-observed special-purpose formats.

Do not add obscure hardware encodings merely to make the enum larger.

#### 6. Qualify block-compressed textures separately

The 128-bit regular-color ceiling applies only to uncompressed color surfaces
and uncompressed texture texels. Block-compressed textures use a separate
storage contract measured in bits per block. For the currently declared BC
formats, OpenAGC's product-scope ceiling is 128 bits per compressed 4x4 block,
not 128 bits per texel:

| Format family | Bytes per 4x4 block | Nominal full-block storage |
| --- | ---: | ---: |
| BC1, BC4 | 8 | 4 bits per pixel |
| BC2, BC3, BC5, BC6, BC7 | 16 | 8 bits per pixel |

Keep block-compressed textures distinct from DCC, CMASK, FMASK, and HTILE.
BC1-BC7 are application-visible sampled texture formats. DCC/CMASK/FMASK and
HTILE are auxiliary GPU metadata for otherwise ordinary color or depth
surfaces and do not change the surface's logical format or bits per pixel.

The Mesa gfx10 resource table
(`../mesa/src/amd/registers/gfx10-rsrc.json`) explicitly contains native image
encodings for BC1 UNORM/SRGB, BC2 UNORM/SRGB, BC3 UNORM/SRGB, BC4 UNORM/SNORM,
BC5 UNORM/SNORM, BC6 UFLOAT/SFLOAT, and BC7 UNORM/SRGB. OpenAGC's corresponding
`kAgcDataFormatBc1` through `kAgcDataFormatBc7` values already exist in the
public texture enum, but register, enum, and descriptor coverage alone are not
PS5 hardware qualification. Before advertising a BC format, implement and test
a complete layout and sampling path with these independent properties:

- Block width and height.
- Bytes per block.
- Compatible number type, including UNORM, SNORM, SRGB, or the signed/unsigned
  BC6 floating-point interpretation where applicable.
- Tile mode, row pitch, slice size, alignment, mip offsets, array layers, and
  cube faces.
- Component selection and exact texture-descriptor encoding.

Every BC layout must use checked arithmetic and ceiling-divided block counts.
Widths and heights below four texels still occupy at least one complete block.
Tests must cover partial edge blocks, 1x1 through 4x4 mip levels, non-multiple-
of-four dimensions, complete mip chains, arrays, cube faces, maximum accepted
dimensions, and overflow rejection.

Qualify the existing BC families in increasing decoding and oracle risk:

1. BC1 UNORM and SRGB.
2. BC4 UNORM and SNORM.
3. BC2 UNORM and SRGB.
4. BC3 UNORM and SRGB.
5. BC5 UNORM and SNORM.
6. BC7 UNORM and SRGB.
7. BC6 unsigned and signed floating point.

Use dedicated, deterministic source blocks containing endpoint, index,
alpha, signed-range, and edge-block cases appropriate to each format. A
hardware gate must sample the compressed texture into an already-qualified
uncompressed render target, wait on a bounded fence, and validate exact or
format-tolerant decoded texels as appropriate plus a reproducible native
render-target hash. It must also prove mip and layer selection rather than
qualifying only base level zero.

Mesa records a GFX9-through-GFX11.5 limitation for indirect image copies of
BC mip chains. Therefore the first BC gates must use a direct, deterministic
upload followed by shader sampling. Direct image-copy qualification comes
after sampling; indirect BC image-copy and mip-copy behavior is an independent
late gate and must not be inferred from a successful sampling result.

BC formats are sampled-texture formats, not color-render-target formats; do
not route them through the color-target tuple table or assign them a
pixel-shader export format. Mesa's gfx10 native resource-format table contains
BC1-BC7 but no native ASTC entries. Do not add ASTC or another compression
family without primary PS5 firmware evidence and a concrete homebrew
requirement.

#### 7. Use firmware-neutral qualification artifacts

Every new hardware gate must:

- Contain no `AGC_EXPECT_FIRMWARE_ABI_KEY`.
- Have no dynamic dependency on either `libSceAgc.sprx` or
  `libSceAgcDriver.sprx`.
- Detect and normalize the full runtime version.
- Use the selected `/dev/gc` profile.
- Write a bounded, file-backed verdict.
- Shut down and self-terminate.
- Be copied to an immutable hash-named local path before its first hardware
  run, with the runner verifying both the local and uploaded hashes.

Use the identical ELF twice on FW 11.60. Later, run those exact bytes on FW
5.50; never rebuild between endpoint tests. A source-equivalent rebuild does
not satisfy the endpoint-portability gate.

#### 8. Qualification labels

Track three states independently:

- Host-tested.
- SPRX/profile-qualified, hardware-unverified.
- Hardware-qualified on an exact firmware.

A format passing FW 11.60 must not automatically be advertised as
hardware-qualified on FW 5.50 or the other 37 profiles. Identical SPRX/profile
findings strengthen ABI evidence but do not promote a profile to
hardware-qualified status.

#### 9. Guarded hardware sequence

For each new tuple:

1. Confirm websrv and debugger connectivity.
2. Run the process-cleanup ELF.
3. Verify no stale renderer remains.
4. Launch one bounded format gate.
5. Require fence, readback, shutdown, and final verdict.
6. Check for residual processes and kernel faults.
7. Repeat once using the identical ELF.
8. Stop immediately on a timeout, UI stall, reset, or unexpected hash.

#### Recommended immediate milestone

Complete and qualify this first group:

1. `R16_UNORM` — FW 11.60 complete.
2. `RG16_UNORM` — FW 11.60 complete.
3. `RGBA16_UNORM` — FW 11.60 complete.

All three are now stable on FW 11.60, so the next implementation milestone is
`R16_SNORM`, followed by `RG16_SNORM` and `RGBA16_SNORM`. This gives
OpenAGC the most useful missing 16-bit coverage while reusing the already-proven
16-bit layouts and floating-point shader path. Exact FW 5.50 replay remains a
separate endpoint gate for all three artifacts.

### Gfx1013 multi-viewport state

- Provide one application-neutral typed array for up to 16 Vulkan-style
  viewport transforms and matching per-slot scissors.
- Reapply this state after baseline and tessellation shader binding so shader
  records cannot restore stale viewport registers.
- Exact packet, range, capacity, and draw-order host coverage is complete;
  FW 5.50 shader routing qualification proceeds through Vulkan-PS5.

### Gfx1013 array and cube image descriptors

- Add application-neutral 2D-array and cube-array resource types plus base and
  last accessible layers to the typed SQ image descriptor helper.
- Exact host tests cover full and subrange cube arrays, ordinary 2D arrays,
  and malformed cube face counts.
- Host implementation is complete; FW 5.50 sampling qualification proceeds
  through the Vulkan-PS5 `imageCubeArray` gate.

### Gfx1013 dual-source blend state

- Detect SRC1 factors in the application-neutral blend builder and reject
  their use outside MRT0.
- Disable RB+ dual-quad mode and clear all SX blend optimizations while dual
  source blending is active.
- Exact host tests pass, and two bounded FW 5.50 Vulkan probes passed the
  18,432-pixel green SRC1 readback oracle with clean process exit.
- This OpenAGC portion is complete; higher-level Vulkan feature exposure is
  tracked in Vulkan-PS5.

### Gfx1013 tessellation offchip concurrency gate

- Replaced the tessellation sample's single global HS offchip buffer with an
  Oberon-wide 160-buffer profile: four shader engines, two shader arrays per
  engine, five physical CUs per array, and four workgroups per CU.
- Mesa's gfx10.3 path confirms that `VGT_HS_OFFCHIP_PARAM` encodes the global
  count (`159`), unlike gfx11's per-shader-engine interpretation. Public
  constants and tests preserve the 40-per-engine/160-global distinction.
- The ring-table builder rejects state whose allocation cannot cover its
  encoded `VGT_HS_OFFCHIP_PARAM`, and the hardware sample derives
  non-overlapping pool offsets from the public ring sizes.
- This is the next qualification candidate for Vulkan's nondeterministic
  HS-store/TES-load path. Host tests pass. Two independent bounded Vulkan FW
  5.500.008 runs passed the hull, TES offchip-read, and 7200-pixel image oracles
  (`20260728T043915Z-tessellation-run1.log` and
  `20260728T044035Z-tessellation-run1.log`) and left the console responsive.
  The ring-concurrency correction is hardware-qualified at this scope.

### FW 5.50 application-neutral initialization gate

- Prospero initialization now owns the GPU ucred preparation required before
  `/dev/gc` access. The public ABI and generic backend remain firmware-neutral;
  the FW 5.50 thread-ucred offsets live only in `driver_prospero.c`.
- Higher-level consumers such as Vulkan-PS5 no longer need a sample-only
  credential header. The next gate is repeated FW 5.50 compute and graphics
  execution through foreground websrv.

### Hardware-validated gate: gfx1013 D32 depth surface

- `agcGfx1013GetDepthSurfaceLayout` calculates separate depth and stencil
  plane layouts for gfx1013 `64KB_Z_X`, including pitch, padded height,
  64 KiB alignment, per-layer mip-chain size, full array allocation, macroblock
  dimensions, and the first packed mip-tail level.
- Exact host fixtures cover D32, 4x MSAA arrays, D32 mip tails, split D16/S8
  planes, the largest bindable 64-bit allocation, invalid large dimensions,
  unsupported swizzles, and invalid multisampled mip chains.
- `agc_depth.elf` now consumes the query instead of reserving a hardcoded
  16 MiB depth image.
- FW `0x05500008` hardware passed the isolated D32 gate through curl/websrv:
  all markers, color coverage, raw depth values, and 1,800/1,800 flips passed
  without a hang or kernel panic.

### Hardware-validated: gfx1013 HTILE layout and compressed-depth gate

- `agcGfx1013GetHtileLayout` models non-RB+ gfx1013 pipe-aligned
  `64KB_Z_X` metadata. Address-pipe count remains an explicit input; the FW
  `0x0550` sample uses the hardware-proven eight-pipe layout.
- Exact fixtures cover metadata pitch, padded height, block geometry,
  alignment, slice size, layers, packed mip-tail accounting, multiple pipe
  profiles, the largest bindable 64-bit allocation, and atomic rejection.
- `agcGfx1013SetDepthSurface` explicitly emits
  `DB_HTILE_SURFACE.PIPE_ALIGNED`. The isolated sample initializes depth-only
  metadata to the gfx10.3 uncompressed value `0xfffc000f`.
- FW `0x05500008` passed `agc_depth_htile.elf`: all draw/fence/color checks,
  18,013 changed HTILE words, and 1,800/1,800 flips completed. The screen
  showed the expected green/red triangles on dark gray.
- User-ring `COPY_DATA` reads of privileged `GB_ADDR_CONFIG` are prohibited.
  The attempted diagnostic produced `GPU Bad packet error: Privilege reg` for
  register `0x13de`; FW reset and recovered the graphics rings automatically.

The earlier compressed-D16 deferral is closed for the FW `0x05500008`
gfx1013 profile. Uncompressed D16 passed twice before HTILE was enabled, and
two subsequent D16/HTILE runs independently reproduced exact decompressed D16,
metadata, color, fence, and flip oracles. The bounded evidence, including the
remaining teardown warnings, is recorded in
`analysis/fw550_d16_htile_qualification_20260727.md`.

### Hardware-validated: isolated stencil gate

- `agc_depth_stencil.elf` allocates separate typed D32 and S8 `64KB_Z_X`
  planes and leaves MSAA, HTILE, and expclear disabled.
- The gate uses front-face `ALWAYS` compare, `0xff` compare/write masks, and
  `REPLACE 0x5a` only on depth pass. Exact host fixtures lock the D32/S8
  layouts and the full 14-dword stencil packet stream.
- Completion requires the existing deterministic marker/color/D32 checks plus
  raw S8 containing only zero and the written `0x5a` reference.
- FW `0x05500008` websrv execution passed with 256,608 `0x5a` stencil bytes,
  2,364,832 zero bytes, no unexpected values, all depth/color checks, and
  1,800/1,800 completed flips without a hang or kernel panic.

### Hardware-validated: isolated 4x MSAA gate

- `agc_depth_msaa.elf` binds typed 4x RGBA8 `64KB_R_X` color and D32
  `64KB_Z_X` surfaces while leaving stencil, HTILE, expclear, CMASK, FMASK,
  and DCC disabled.
- The shader resolve transitions the multisample target to shader-read,
  restores 1x raster state, samples all four fragments, and draws into the
  registered VideoOut target. The sample compensates the source `ALT`
  red/blue storage and composites coverage over dark gray.
- Repeated FW `0x05500008` runs accepted the 5,131-dword DCB. All stage and
  completion markers, exact green/red interiors, raw 4x D32 classes, and
  1,800/1,800 flips passed without a hang or kernel panic.
- The captured framebuffer showed green and red triangles with resolved edges
  on dark gray. Black side pillars in the wider capture are outside the
  registered 1920x1080 framebuffer.

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
5. A representative homebrew application outside `samples/hw_test` builds
   against the installed public API and exercises initialization, uploaded
   shaders/resources, drawing, synchronization, and presentation without
   copying qualification-sample PM4 setup. Retail imports are evidence only and
   are never a release-completion metric.
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
| 5. Publish a homebrew-facing example | **Complete** | `samples/triangle` retains the minimal command-recording example. `examples/cube` is a separate installed-package consumer that owns allocation, shader upload, resource tables, triple-buffered frame resources, bounded fences, continuous vertex/index updates, VideoOut presentation, and cleanup. Its staged Prospero install/consumer build passes without repository include or library paths. Two FW `0x05500008` curl/websrv runs presented 3,600 rotating-cube frames and exited cleanly. Retail import audits remain bounded ABI evidence only. |
| 6. Add cross-firmware backend profiles | **Complete for the SPRX-qualified common subset** | All 39 exact active keys from FW 3.20 through FW 12.70 are runtime-selectable for their submit16, internal-memory, authenticated-queue, primary-suspend, public TF-ring, HS-offchip, and async carrier evidence. FW 5.50 and standard-PS5 FW 11.60 are hardware-qualified; other exact profiles remain hardware-unverified, and firmware-specific operations stay independently gated. |

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

### 5. Deliver OpenAGC as a homebrew GPU API

OpenAGC's primary product is a clean, usable GPU API and shader toolchain for
native homebrew applications and games running on jailbroken PS5 hardware. The
FW `0x0550` console in hand is the first qualification target. Compatibility
with official `sceAgc*` / `sceAgcDriver*` entry points remains important, but
retail-title import counts are supporting ABI evidence rather than the product
goal or a release gate.

Work in this order:

1. **Public-API vertical-slice audit.** Inventory the hardware samples and move
   every generally useful shader upload, resource binding, render-target,
   viewport/scissor, draw/dispatch, barrier, synchronization, and queue-submit
   operation out of sample-local raw PM4 into public OpenAGC builders or
   documented low-level escape hatches. VideoOut lifecycle remains platform
   integration rather than AGC command construction.
2. **Graphics and compute application path.** Provide one minimal graphics path
   and one compute path that use installed OpenAGC headers and `libopenagc.a`
   only. The application must not reproduce private register sequences from
   `samples/hw_test`. Cover shader records, GPU memory ownership/alignment,
   Wave32 VS/PS, render targets, viewport/scissor, indexed and non-indexed draw,
   dispatch, cache visibility, suspend points, and error propagation.
3. **Shader toolchain usability.** Make `openagc-psbc` a documented part of the
   homebrew SDK flow: GLSL or SPIR-V to gfx1013 shader records, deterministic
   artifacts, stage/link diagnostics, and examples for compute, VS/PS, NGG
   geometry, and tessellation. Keep PS5 gfx1013 behavior distinct from generic
   gfx1030 assumptions.
4. **SDK packaging.** Install public headers, `libopenagc.a`, CMake package
   metadata, and compiler tooling through one supported workflow. This now
   exports `OpenAGC::openagc`, `OpenAGC::psbc`, a relocatable package config,
   version metadata, `openagc_compile_shader()`, and TGZ generation. Document the
   public API boundary, ownership/lifetime rules, alignment requirements,
   numeric firmware profiles, error codes, raw-PM4 escape hatch, and websrv
   deployment without requiring proprietary SDK headers or firmware blobs.
5. **FW 5.50 conformance suite.** Run the display, initialization, compute,
   graphics, indexed draw, indirect draw, NGG geometry, and tessellation tests
   through curl/websrv. Add bounded waits and fail-closed state validation so a
   malformed builder input cannot submit a kernel-panic-prone packet. Record
   visual expectations and console results separately from host packet tests.
6. **Representative homebrew proof.** Build and run a small application or game
   outside `samples/hw_test` that uses the installed OpenAGC SDK. It must render
   continuously, upload/update resources, compile/load shaders, recover cleanly
   from application errors, and contain no copied sample-private PM4 setup.
   **Complete:** `examples/cube` is an
   independent installed-package consumer with application-owned PS5 memory,
   Wave32 shader upload/fusion, a public descriptor-backed indexed draw,
   three frame slots, two-second fence bounds, per-frame rotating-cube updates,
   finite VideoOut presentation, and teardown. The separate Prospero consumer
   build passes. Two consecutive FW `0x05500008` websrv runs presented all
   3,600 frames, showed the rotating colored cube, and exited cleanly without a
   hang, reset, or panic. Qualification evidence is in
   `analysis/fw550_standalone_cube_qualification_20260727.md`.
7. **Broaden capability after the vertical slice.** Reusable standard-swap
   RGBA8, RGB10A2, and R11G11B10 color targets are now host-tested and FW 5.50
   hardware-qualified, alongside the earlier alternate-swap BGRA8 and RGBA16F
   paths. RGBA8/BGRA8 sRGB encode behavior is also hardware-qualified. Next
   qualify additional 16-bit color tuples. After the color matrix is stable,
   D16, S8-only, D16+S8, compressed D16/HTILE, and D16 HTILE expclear are now
   hardware-qualified. Continue with blending, resource transitions,
   multi-buffer frame scheduling, timestamps/queries, and stable NGG
   geometry/tessellation APIs according to homebrew needs.
8. **Firmware and retail ABI evidence.** Preserve numeric firmware profiles and
   expand below/above FW `0x0550` only after the primary path is mature. Analyze
   retail binaries when they reveal an API contract needed by homebrew; do not
   chase dead imports, guess prototypes, or make a ten-title corpus a release
   requirement. FW `0x0320` remains the lowest active compatibility target;
   FW `0x0100`, 2.x, and `0x0300` remain archival evidence only and are
   rejected by runtime profile selection. FW `0x0320` is the lowest active
   compatibility target.

The sample-only PM4 audit is complete in
`analysis/sample_pm4_public_api_audit.md`. It classifies the exact reusable,
partial, missing, platform-only, and diagnostic operations in the FW 5.50
hardware samples.

The reusable compute vertical slice now has typed `CONTEXT_CONTROL`, gfx1013
compute validation/binding/dispatch state, and FW 5.50 compute-default emission.
`agc_compute.c` uses those APIs plus public `WRITE_DATA`, `ACQUIRE_MEM`, NOP,
and submission calls exclusively; the curl/websrv FW 5.50 run produced
2,073,600/2,073,600 matching pixels and completed the display flip.

The compute sample now places a public `ACQUIRE_MEM` before an ordered
GPU-written completion marker and polls that CPU-visible fence with a bounded
timeout. FW 5.50 reached the fence after 1 ms and retained full pixel coverage.

The public resource API now has byte-exact gfx1013 structured-buffer,
zero-record structured-buffer, byte-bounded raw-buffer, 2D-image, and 64-byte combined image/sampler
descriptor encoders matching the proven
sample layouts. Resource-table binding now resolves compiler placeholders
inside shader records, validates every required table atomically, enforces the
PS5 address32 range, and selects graphics/compute packet type internally. The
baseline draw state owns primitive and pixel resource tables so their resolved
values are emitted after shader placeholders and before draw packets. The VS/PS
state also owns the primitive back-program address and resolves the compiler
continuation placeholder internally.

The graphics sample conversion is complete for command construction. Baseline
and tessellation builds use typed resource encoders, state-owned placeholder
resolution, public context/default/fixed-function/draw calls, public cache and
marker packets, and bounded completion. FW 5.50 validated the baseline fence at
4 ms and tessellation at 9 ms; both passed FP16 validation, and tessellation
produced the expected ring writes. Tessellation ring promotion is now complete:
the public API owns the 128-byte descriptor table, ring sizes/slots, four UC
ring registers, and five post-bind CX registers. The sample-local ring header
and its Makefile dependencies are removed. A repeated FW 5.50 run retained the
9 ms fence, 24 offchip changes, four factor changes, and passing FP16 output.
Each closed goal must update the
documentation, pass clean generic and Prospero builds, pass host fixtures, and
be committed before hardware promotion.

#### Retail ABI evidence retained by the project

Subnautica (`PPSA02453`) content `01.022.394` is now re-audited from the
decrypted executable: all 63 AGC imports pass the strict coverage gate, with 58
direct implementations and five intentional versioned wrappers. The artifact
is pinned by SHA-256 in `analysis/subnautica_ppsa02453_audit.md`. Its metadata
requires system software `0x1120`, so this result proves SDK `0x0400` API
coverage rather than native package launch compatibility on FW `0x0550`.

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
The completed cross-version audit confirms that FW `1.00` through `12.70` all
use the same six-byte `0x8A6C9018` return stub. Dragon Quest and the two FW
`0x0550` system applications that import it contain only dead local thunks, with
no code or relocated-data callers. See
`analysis/find_resources_public_audit.md`. Keep the NID unresolved until a live
PS5 caller or typed PS5 header becomes available; do not use the PS4 placeholder
prototype. Corpus expansion therefore requires another decrypted PS5 title
binary rather than more firmware variants of this stub.

When useful to homebrew API work, grow the optional evidence corpus with
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

Per-operation direct-backend gates are complete for the recovered operations.
All 39 active SPRX pairs are grouped
by normalized wrapper and private-carrier fingerprints. Submit16, authenticated
queue management, primary suspend, public TF ring, HS offchip, async setup,
and standard/Trinity memory facts are exact-RE-qualified per firmware. FW 11.60
is the modern/Trinity reference and FW 3.20 remains the lowest active target.
Workloads remain disabled unless their exact direct contract is separately
proven. Register defaults are exact-profile gated: FW 5.50 version 8 and FW
11.60 version 12 are hardware-qualified. Version 12 maps to the recovered V10
tables and uses its exact larger internal DDID slot; two FW 11.60 runs built
both blobs, executed the post-default GPU markers, and shut down cleanly.
FW 11.60's separately guarded headless compute artifact passed twice with the
exact 2,073,600-pixel output oracle, driver shutdown, and forced process
termination. Both runs executed the gfx1013 shader and filled every pixel with
`0xff00ff00`; the original FW 5.50 compute-plus-VideoOut conformance sample then
passed unchanged. Presentation remains isolated because FW 11.60 rejects the
FW 5.50 `libSceVideoOut` linear-buffer patch offset; its VideoOut contract will
be qualified independently rather than guessed during compute testing.
The shared compute sample removes its flip event and closes VideoOut before
driver shutdown. It also reports best-effort unregister, event-queue, unmap,
and direct-memory results. FW 5.50 keeps the currently scanned buffer set
`RESOURCE_BUSY`, closes the event queue with the port (`EBADF` on a second
delete), and leaves the allocation process-owned (`EINVAL` on explicit
release); those diagnostic returns are not AGC qualification failures because
process exit reclaims them. Exact GPU output, completed presentation, event
removal, VideoOut close, and driver shutdown remain mandatory.
The FW 11.60 graphics gate reused the proven baseline NGG+PS shaders, 2,470-
dword PM4 path, flexible-memory FP16 target, completion fence, and exact
coverage/color oracle. It passed twice with 255,744 changed pixels, eight
sampled colors, no invalid components, and packed hash `0x4a40c2eb4f12bc26`.
The same revision then passed FW 5.50's 1,800-flip graphics conformance sample.
Only FW 11.60 presentation is skipped, keeping proven graphics execution
independent from its still-unqualified VideoOut linear-buffer contract.
FW 11.60 rejected OpenAGC's explicitly separate one-ID convenience extension:
both three-dword calls returned `AGC_OK`, but the ordered post-workload
`WRITE_DATA` marker timed out after five seconds. The capability is disabled
again. The strengthened marker oracle remains active for FW 5.50, and any FW
11.60 adapter must reproduce the recovered nine-dword Sony contract plus its
registered-stream state rather than treating submit acceptance as execution.
The registration blocker is now resolved at the ABI level: FW 5.50 and FW
11.60 both use the 32-entry GPU-visible table at standard-console
`SceGnmGpuInfo + 0x3a000`, with one 64-bit slot per stream; public registration
only maintains a parallel 32-byte userspace descriptor and bitmask. The exact
18-dword active and 12-dword complete standalone-buffer forms, including their
private `0x79`/`WRITE_DATA` prefixes, are recorded in
`analysis/agc_driver_workload_facts.md`. A bounded standard-PS5 FW 11.60
adapter using OpenAGC-owned stream slot 1 and separate 18/12-dword DCBs matches
the exact host fixtures, but its ordered hardware marker still timed out after
both calls returned `AGC_OK`. Attempting the separately observed GPU-info
process-property step then caused a kernel panic before a verdict. That call
used the wrong four-argument order and is removed. All 39 active SPRXs now
reproducibly prove the correct five-argument carrier as
`("Sce.Debug:Gnm", gpu_info_base, gpu_info_span, 0, 0)`, with a `0x100000`
standard span and `0x180000` Trinity span. The isolated
`agc_fw1160_stage10.elf` gate used that exact five-argument call, applied the
proven `SceGnmDumpArea` range name, performed no submission, returned `AGC_OK`,
shut down, and self-terminated on standard-PS5 FW `0x11600005`. The standard
FW 11.60 stage 11 then made that registration an idempotent prerequisite of
the exact Sony stream adapter and submitted active ID 1, complete ID 1, then
an ordered `WRITE_DATA` marker. Both workload submissions returned `AGC_OK`,
but the process and PS5 UI stalled before the bounded polling loop could print
its five-second verdict. ps5debug-NG still enumerated PID 104 but could not
attach; the process-cleanup ELF removed it and restored a no-stale-process
state. The FW 11.60 workload capability is disabled again. Next RE must recover
the missing registered-stream state or lifecycle beyond the already proven
process property, address table, and packet bytes; do not rerun stage 11
unchanged.
The recovered `libSceAgc` cursor wrappers show that Sony's lifecycle is
caller-owned and inline: DCB control 0, ACB control 1, with active and complete
appended to one command stream rather than separately submitted. Stage 12
tested that distinct sequence by registering stream 1 and building active →
marker A → complete → marker B in one 40-dword DCB. The single submit returned
`AGC_OK`, but neither marker verdict nor shutdown was reached and the PS5 UI
became unresponsive. The cleanup ELF removed the stale payload and ps5debug-NG
confirmed that no `eboot.elf` remained. Do not rerun stage 12 unchanged.
FW 11.60 workload remains disabled. Next, recover the Sony driver/module
initialization that precedes workload use—particularly GPU/register enable
state or kernel operations beyond the already proven process property, stream
table address, packet bytes, and inline cursor lifecycle—before constructing a
new isolated gate. Reboot the console before that future GPU test.
Offline tracing now proves the workload initializer itself contains no hidden
ioctl or GPU write: it only selects GPU-info region 2, validates its span and
alignment, reserves stream 0, initializes descriptors, and creates a mutex.
The builders also carry the address of the selected 64-bit slot, exactly as
OpenAGC does. Stage 13 tested the remaining evidenced difference from the FW
5.50 qualified path after a clean reboot. FW 11.60 default-state notification,
async setup, the exact process property, stream registration, and a normal
preflight marker all succeeded; the preflight marker completed in 50 ms. The
unchanged inline workload DCB then returned `AGC_OK` but produced no following
verdict before the 20-second transport timeout. The cleanup payload found no
stale `eboot.elf`, while websrv and ps5debug-NG port 744 remained reachable.
This rules out those surrounding prerequisites as the missing state. Do not
rerun stage 13 unchanged. Keep FW 11.60 workload disabled and recover the
GPU-side `SET_WORKLOAD` state transition or required queue/register
programming before constructing another gate.
The first opt-in installed-driver oracle was run once after a clean reboot. FW
11.60's matching module loaded, all exact exports resolved, the 18/12-dword
sizes matched, and async setup returned `AGC_OK`. Its ordinary `WRITE_DATA`
preflight returned `AGC_OK` but left the marker zero after 5,000 ms, so the
safety gate prevented stream registration and workload emission. Static review
then found that the preflight itself omitted the already hardware-proven NOP
trailer required to advance the final graphics descriptor in this payload
context. The failure is therefore inconclusive, not a Sony-backend rejection.
The revised oracle submits two observable DCBs plus a 16-dword NOP trailer
through Sony's multi-DCB export and flushes all cache lines occupied by the
40-dword workload DCB. It is build-qualified for one fresh-boot attempt; do not
rerun the original artifact. See
`analysis/fw1160_sony_workload_attempt_20260729.md`.
The revised artifact was subsequently run after another clean reboot. Sony's
multi-DCB export returned `AGC_OK`, but neither observable marker executed in
5,000 ms. The safety gate again prevented workload emission. This rules out
final-descriptor deferral and incomplete cache flushing, and proves that the
installed module cannot be the execution oracle under websrv. Do not rerun
either artifact. Resume direct `/dev/gc` recovery of the GPU-side
`SET_WORKLOAD` queue/register transition before constructing a new gate.

The Sony workload contract itself is recovered for all active firmware:
seven active-wrapper and three complete-wrapper groups converge on the same
nine-dword `0xc0071e00` packet and 18/12-dword maximum reservations. OpenAGC's
public DCB and ACB cursor ABIs now match those exact active/complete forms plus
the nine-dword inactive prefix. DCB passes control 0 and ACB control 1, matching
the FW 11.60 wrappers; registered stream state supplies the backend GPU slot
address. This fixes the userspace builders without re-enabling the distinct
one-ID submit-owning convenience operation on FW 11.60.

The fail-closed audit also separates FW 5.50-only EOP-flip evidence from the
common direct-operation group. Unimplemented target-ring and Razor/capture
operations must return `AGC_ERROR_NOT_SUPPORTED`; export-table presence or a
placeholder backend function is never a capability grant.

TF-ring and HS-offchip grouping now includes semantic payload verification:
all active images use `u64@0,u32@8` with commands `0x80108128` and
`0xc010812c`. FW 12.x explicitly zeroes reserved offset `0xc`; OpenAGC's typed
zero-initialized structures preserve that stricter form across the common
carrier groups.

Submission grouping is also semantic rather than hash-only: every active DCB,
ACB, and multi-DCB export group converges on the instruction-identical
`0xc0108102` carrier with a typed `u32@0,u32@4,u64@8` request.

Cache synchronization semantics follow this audit and become an earlier
blocker only if the expanded game corpus exercises an unresolved path. VRS and
ray tracing remain later feature tracks driven by verified FW 5.50 ABI evidence
and real-title demand.

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
- Other registered FW 4.00-12.70 builds: exact selection, submit16,
  authenticated queues, primary suspend, public TF ring,
  HS offchip, async setup, and standard/Trinity memory layouts are RE-verified.
  They remain hardware-pending and are not a complete support claim.
- FW 1.00 and 2.x: archival RE profiles only. Known submit/EOP evidence is
  retained, including FW 1.00's `0x38000` offset, but missing legacy queue or
  optional-request ABIs will not be recovered. Unsupported operations remain
  explicitly fail-closed and these versions are not advertised as supported.
- FW 3.20: lowest active target, with local firmware references available for
  exact userspace ABI recovery. Hardware validation remains pending.
- PS5 Pro: FW 9+ resolves `sceKernelHasTrinityMode` and selects the firmware-
  proven 22 MiB CWSR allocation and related offsets. Hardware validation is
  still required on a PS5 Pro.

The next compatibility work is the safety correction and per-operation
qualification ladder below. Evidence and exact aliases are tracked in
`analysis/agc_driver_abi_families.tsv`,
`analysis/agc_driver_operation_facts.tsv`,
`analysis/agc_register_defaults_facts.tsv`, and
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
5137 passed, 0 failed
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
   ✅ `driver_prospero.c` implements private primary/final suspend carriers.
   Both public Direct exports preserve Sony's `0x8a6d0001` permission-stub ABI
   and never substitute the internal `QUEUE_STAT_16` operation.

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
6. Submit suspend points and preserve the public Direct ABI. ✅ The private
   `sce_agc_internal_suspend_point_submit_primary` / `_final` carriers use the
   recovered ioctls; both Sony Direct exports return the exact permission error.
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
   a 64-byte GPU-visible NOP IB trailer carved from unused `SceGnmDdid` space,
   making the deferred descriptor harmless without a standalone 16 KiB VM
   resource. Standalone `sceAgcDriverSubmitDcb` also emits the frame-state
   operation and trailer so its caller DCB executes in the current submit. Two
   immediate deployments each passed three repeated two-DCB
   iterations with unique ordered markers and zero polling delay. Vulkan-PS5
   additionally passed two standalone compute and two standalone triangle
   EOP/readback runs through this path on FW `0x05500008`.
   A later VideoOut teardown gate proved the standalone allocation was not the
   source of its final `0x4000` VM warning. The VideoOut patch had restored its
   originally execute-only text range as read/execute; the Prospero backend now
   restores the exact execute-only protection so the kernel can coalesce the
   temporary text mapping. A follow-up reproduced the warning, falsifying that
   hypothesis too. All 27 retained OpenAGC graphics klogs contain the same
   single-page warning; the remaining unmatched lifecycle object is the flip
   event. Teardown now explicitly unregisters it before closing VideoOut and
   deleting its still-live equeue. The Vulkan-PS5 SystemService-only baseline at
   `20260728T064628Z-system-exit-probe-target.klog` then reproduced exactly one
   `set:1, res:0, amount:0x4000` warning without loading OpenAGC or using GPU,
   VideoOut, equeues, or custom memory. This proves the warning belongs to FW
   5.50/raw-ELF container teardown rather than OpenAGC's VideoOut lifecycle.
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

Status: common direct-backend profile selection complete. All 39 exact active
keys from FW 3.20 through FW 12.70 are runtime-selectable with per-operation
gates. FW 5.50 and standard-PS5 FW 11.60 are hardware-qualified; other
firmware/model profiles, including PS5 Pro, remain hardware-unverified. FW 1.00,
2.x, and 3.00 remain archival RE profiles, and Sony-export GPU submission is
not selected automatically.

Purpose:

Allow a single game-facing OpenAGC ABI to run across supported PS5 firmware
versions without exposing firmware-private ioctl layouts to applications.
Keep OpenAGC independent of the installed userspace driver: select an exact
firmware profile and issue only statically verified `/dev/gc` operations.

Architecture:

```text
Game -> stable OpenAGC public ABI
     -> exact per-firmware /dev/gc backend
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

| Capability | Direct `/dev/gc` status |
| --- | --- |
| Firmware selection | Exact four-digit aliases; incomplete profiles fail closed |
| Submission | Submit16 command and layout recovered across inspected families; hardware-qualified on FW 5.50 |
| Queue management | Per-key request, token, and layout evidence required before enabling |
| Suspend points | Primary/final submit and query are independently capability-gated |
| Workloads | The one-ID OpenAGC convenience submit is distinct from Sony's multi-argument nine-dword builders and is enabled only with direct evidence |
| TF ring | Public `0x80108128` and privileged `0xC0108120` roles are separate |
| HS offchip | FW 5.50 and 11.60 use `0xC010812C`; unrelated `0xC008812D` assumptions are rejected |
| Internal memory | Standard and Trinity sizes require exact per-key facts |
| Default states | Register-defaults version is selected only from an exact profile fact |

### Priority 0: Correct unsafe or overstated assumptions

1. Rename the `0xC0108139` verifier check to its actual suspend-final role.
2. Add independent TF-ring checks for the public and privileged commands,
   including direction, immediate size, field offsets, and call semantics.
3. Resolve the HS-offchip `0xC008812D` versus `0xC010812C` discrepancy from
   named wrapper disassembly before enabling either command for another key.
4. Remove the hardcoded defaults-version 8 selection. Add an exact
   `register_defaults_version` fact to each qualified firmware profile; return
   `AGC_ERROR_NOT_SUPPORTED` when it is unknown.
5. Split broad backend eligibility from operation support. An exact firmware
   match may select a backend, but each direct operation must check its own
   capability bit before issuing an ioctl or PM4 submission.
6. Correct status text and verifier success messages so installed export
   presence, RE-qualified direct ABI, and hardware qualification are reported
   as distinct evidence levels.

Acceptance criteria:

- No verifier label names a different ioctl than the command it checks.
- No direct operation runs on a firmware key without an exact capability fact.
- Unknown defaults, TF-ring, HS-offchip, workload, or internal suspend-query ABIs fail
  with `AGC_ERROR_NOT_SUPPORTED` rather than reusing FW 5.50 behavior.
- Generic and Prospero builds pass with no new warnings, and host tests cover
  each disabled/enabled capability boundary.

### Per-operation firmware profile model

Each active four-digit key should resolve to a record containing, at minimum:

- direct capability flags for submission, queue, suspend-submit,
  suspend-query, workload, TF-ring, HS-offchip, memory, and default states;
- ioctl command words and typed argument-layout identifiers for every enabled
  private operation;
- queue tokens, ring/read-pointer/metadata offsets, and authentication policy;
- standard and Trinity memory sizes and working offsets where applicable;
- exact register-default table version;
- provenance identifying the SPRX wrapper, unique fingerprint group, and
  hardware status.

A function name or NID must never enable a direct ioctl without independent
command, layout, and behavior evidence.

### Cross-firmware recovery workflow

1. Extract the relevant named wrapper bodies from all active SPRXs and compute
   normalized fingerprints that ignore load addresses and relocation noise.
2. Group identical implementations. Inspect one representative of every
   unique body, then verify that every profile assigned to the group matches
   the complete command/layout/constant set.
3. Record positive and negative evidence per operation. Missing wrappers and
   permission stubs become explicit unsupported capabilities.
4. Generate verifier fixtures from the recorded facts; do not use a single
   incidental hexadecimal constant as proof of an operation.
5. Add host tests for profile lookup, capability gating, typed structure
   layout, command selection, and nearby-key rejection.
6. Run the clean generic and Prospero builds. Mark the result
   **RE-verified, hardware pending** until matching hardware passes the ordered
   smoke tests.

The generated `analysis/agc_driver_operation_facts.tsv` now provides the
39-key operation ledger and normalized wrapper-group mapping. It is
deliberately conservative: all keys receive only the common carrier-proven
submit16, internal-memory, authenticated-queue, primary-suspend, public TF-ring,
HS-offchip, and async subset. FW 5.50 and standard-PS5 FW 11.60 add only their
separately hardware-qualified operations; remaining capabilities require
exact layout recovery and matching hardware evidence.
The companion `analysis/agc_driver_command_carriers.tsv` now groups the full
private ioctl carrier functions. It proves one common submit16, primary
suspend, and privileged-TF carrier, while preserving the multiple queue,
public-TF, HS, final-suspend, and async groups for explicit review.

### Completed: FW 11.60 modern standard-console reference

FW 11.60 provides the later-firmware reference and includes the runtime
`sceKernelHasTrinityMode` branch. Its queue, suspend, TF-ring, HS-offchip,
memory, default-state, and workload boundaries are recovered. The standard
console passed the complete staged and public-path ladders; Trinity remains
SPRX-qualified and hardware-unverified.

Acceptance criteria:

- Every enabled FW 11.60 direct operation has named-wrapper disassembly,
  command/layout fixtures, and explicit standard/Trinity memory facts.
- Standard and Trinity direct profiles expose distinct, accurate diagnostics.
- Standard-PS5 status is hardware-qualified after two complete public-path
  runs; Trinity stays hardware-unverified until matching hardware is tested.

### Priority 2: FW 3.20 lowest active cross-firmware profile

Recovery and implementation status:

1. FW 3.20 `libSceAgc.sprx` and `libSceAgcDriver.sprx` exports and
   firmware-sensitive wrappers are inventoried.
2. Submit16 and a subset of queue/memory constants are recovered as the
   legacy-v3 family. Suspend, TF-ring, HS-offchip, workload, and default-state
   details still require the workflow above.
3. Exact key `0x0320` selects the legacy-v3 direct family; missing operations
   remain unavailable instead of inheriting the FW 4.00+ surface.
4. Profile-selection tests and the current subset verifier pass. This is not a
   complete direct-backend qualification. Matching FW 3.20 hardware is still
   required before any hardware-supported claim.

Acceptance criteria:

- FW 3.20 is the documented lowest active target and gains a complete
  provenance record for every enabled private operation.
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

### Priority 2: Stable direct-backend dispatch

- ✅ `AgcDriverOps` preserves the public ABI across generic and Prospero
  implementations.
- ✅ Exact firmware detection, backend selection, and per-operation direct
  capability gates fail closed. FW 5.50 retains its hardware-qualified one-ID
  workload convenience path without claiming Sony export ABI compatibility;
  Standard-PS5 FW 11.60 passed its wrapper-proven operation set twice, plus
  version-12/V10 defaults and real compute execution. Workloads, suspend-query,
  EOP flip, and non-empty HS patch lists remain disabled.
- ✅ The primary Prospero target links no `SceAgcDriver` stub and the
  resulting hardware-test ELF has no `libSceAgcDriver.sprx` dependency.
- ✅ The direct `/dev/gc` backend passed the clean FW 5.500.008 init,
  multi-DCB marker, async queue, suspend-point, and workload sequence
  (`20260729T091752Z-72225`).
- ✅ All direct extractors use `analysis/agc_firmware_versions.tsv`; no
  installed-driver profile is required for runtime selection or RE validation.
- Maintain per-firmware NID/module aliases without assuming NID stability.

### Completed hardware gate: standard-PS5 FW 11.60

- ✅ The original 2026-07-29 power-off was followed by an operation-at-a-time
  staged ladder, with the process-cleanup ELF immediately before every payload.
- ✅ Added unbuffered `agc_fw1160_stage0.elf` (identity only) and
  `agc_fw1160_stage1.elf` (corrected init plus immediate shutdown). Their runner
  launches the process-cleanup ELF before every stage.
- ✅ FW 11.60 stages 0 and 1 each passed twice on standard hardware reporting
  raw `0x11600005` and SoC `0x00840f60`.
- ✅ FW 11.60 stage 2 passed twice: exact internal-memory mappings, DDID
  initialization, and shutdown completed without a freeze or power-off.
- ✅ FW 11.60 stage 3 passed twice: the shared standard-group submit policy
  executed a five-dword `WRITE_DATA` marker after 50 ms and 0 ms, then released
  its memory and shut down cleanly. The same revision passed FW 5.50's complete
  init/multi-DCB/wait/async/queue/suspend/workload regression lifecycle first.
- ✅ FW 11.60 stage 4 passed twice: async setup and direct shutdown returned
  `AGC_OK`. The foreground app's black screen was a loader lifecycle artifact;
  probes now flush PASS and self-terminate instead of requiring manual UI kill.
- ✅ FW 11.60 stage 5 passed twice: authenticated queue create returned handle
  0 and queue destroy plus shutdown returned `AGC_OK`.
- ✅ FW 11.60 stage 6 passed twice: primary suspend returned `AGC_OK` while the
  qualified queue was active; teardown succeeded and ps5debug-NG confirmed no
  residual `eboot.elf` process.
- ✅ FW 11.60 stage 7 passed twice: final suspend and the complete queue
  teardown returned `AGC_OK`.
- ✅ FW 11.60 stage 8 passed twice: an aligned mapped 16 KiB TF ring was
  accepted, followed by clean context shutdown and memory release.
- ✅ FW 11.60 stage 9 passed twice: the HS-offchip zero-entry carrier accepted
  an aligned list pointer. This qualifies the ABI boundary, not non-empty
  patch-list execution.
- ✅ Exact runtime key `0x1160` passed the ordinary public init, memory,
  multi-DCB marker, nine-dword wait64, async, queue, suspend, release, and
  shutdown lifecycle twice. ps5debug-NG found no residual process.
- ✅ The same teardown revision passed the complete FW 5.50 lifecycle,
  including V8 defaults and the FW 5.50 workload extension.
- ✅ FW 11.60 version-12/V10 register defaults passed twice with exact
  79/29/20 primary and 9/15/3 internal dimensions, post-default GPU markers,
  and clean shutdown. FW 5.50 retained its original V8 DDID layout.
- ✅ FW 11.60 headless compute passed twice: the gfx1013 dispatch reached its
  completion fence in 1-2 ms and exactly 2,073,600/2,073,600 pixels matched
  `0xff00ff00`. The original FW 5.50 compute-plus-VideoOut sample then passed.
- ✅ FW 11.60 headless graphics passed twice with Wave32 NGG+PS execution,
  255,744 FP16 pixels, eight sampled colors, no invalid components, and exact
  FNV64 `0x4a40c2eb4f12bc26`. FW 5.50 then passed the same draw and all 1,800
  VideoOut flips.
- Workloads, EOP flip, and non-empty HS patch-list execution remain
  fail-closed or unadvertised.
- FW 11.60 VideoOut preparation no longer reuses the unsafe FW 5.50
  `+0x7e61` patch. Exact SPRX comparison recovered the evolved branch at
  `+0x9922`; the core verifies a firmware-keyed six-byte signature and fails
  closed for other layouts. The pinned public gate now combines live V12
  defaults/async state, a GPU marker, two bounded flips, complete teardown,
  file-backed verdicts, and self-termination. It passed twice on standard FW
  `0x11600005`, including 50 ms GPU markers and zero-valued teardown results;
  ports 8080 and 744 remained reachable. Graphics/compute scanout content is
  still a separate gate; see `analysis/videoout_linear_patch_versions_20260730.md`.
- The public `sceAgcDriverIsSuspendPointInFlightDirect` gap is closed: all 39
  active drivers return userspace permission error `0x8a6d0001` without an
  ioctl. OpenAGC preserves the 32-bit return ABI instead of truncating it to
  `bool` or substituting internal `QUEUE_STAT`. The separate CDBG helper
  remains fail-closed pending its private carrier.
- Other exact active firmware/model profiles are enabled from reproducible
  SPRX evidence but remain hardware-unverified until matching consoles exist.
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

Compute dispatch now exposes named Wave32 and Wave64 initiator modifiers.
Wave32 ACO compute records must be dispatched with
`AGC_GFX1013_COMPUTE_DISPATCH_WAVE32`; leaving the modifier at its zero
Wave64 default causes divergent Wave32 state to execute with the wrong lane
model. Exact host packet coverage locks the Wave32 initiator to `0x8041`.

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
# Product Roadmap: OpenAGC as a PS5 Homebrew GPU API

## Current FW 5.50 conformance checkpoint (2026-07-27)

The sample completion path now uses a gfx1013 `RELEASE_MEM` EOP fence rather
than treating a later `WRITE_DATA` marker as proof that shader writes are
globally visible. Compute repeated with 2,073,600/2,073,600 matching pixels;
the corrected-fence NGG baseline, amplification, line, invocation, and
tessellation-geometry-line cases also passed. The line case has direct Chiaki
evidence that the white outer and internal edges connect at the intended
vertices. The complete generic suite passes.

The hardware-proven completion tail and remaining tessellation draw tail are
now public typed APIs. `agcGfx1013SignalEopFence` owns the exact gfx1013 EOP
event/GCR/cache-policy sequence, while `agcGfx1013DrawTessIndexAuto` owns the
validated HS/TES/GS/PS bind, resource, post-bind context, override, instance,
and draw ordering. Hardware samples no longer open-code either sequence.

The remaining frame/launch sequence is also promoted. A single
`AgcGfx1013FrameState` now drives the atomic context-control, clear-state, V8
defaults, color-target, viewport, scissor, target-mask, vertex-bound, and NGG
launch prologue. Both baseline and tessellation draw composers apply its
depth-disabled and clip/raster state after shader binding, preserving the
hardware-proven overwrite order.

The complete 17-sample invocation of
`make -C samples/hw_test conformance_fw550` now passes on a fresh FW `0x0550`
console session. The runner preserves logs and fails closed on a transport
timeout, instant close, firmware mismatch, missing output gate, or application
failure. Revision `03b43f2` completed 17/17 sequential launches in 434 seconds;
retained evidence is summarized in
`analysis/fw550_qualification_03b43f2.md`.

The additional render-target host milestone is implemented as a typed gfx1013
format table rather than more sample-local CB constants. Eleven linear UNORM
and FLOAT presets resolve their exact CB format, number type, component swap,
pixel size, and SPI export format, and every resulting 28-dword color-target
stream is locked by a host fixture. New presets beyond the hardware-proven
RGBA8/BGRA8 and RGBA16 FLOAT paths remain explicitly hardware-unqualified;
future websrv runs should advance them individually before enabling compression
or multisampling.

The host-side synchronization milestone is implemented as typed gfx1013
resource transitions spanning render, compute, copy, shader-read, presentation,
and host-read usage. Producer completion preserves the hardware-proven
`RELEASE_MEM + NOP` stream, while GPU consumers append the authoritative full
gfx103 `ACQUIRE_MEM` packet and cache controls. Exact order, no-op, and atomic
failure fixtures are required gates. The acquire-bearing transitions remain
hardware-unqualified until the FW `0x0550` websrv matrix can exercise
render-to-shader, compute-to-copy, copy-to-shader, and present-to-render cases.

Typed blend and depth/stencil control is implemented as deterministic gfx1013
packet groups. Blend covers eight MRT equations, write masks, and constants;
depth/stencil covers depth test/write/bounds plus independent front/back
compare, operations, reference, compare mask, and write mask. Host fixtures
lock the full streams and atomic failures. This completes control-state
encoding only: depth-surface allocation/binding and FW `0x0550` execution must
be qualified before depth or stencil support is advertised as hardware-ready.

This is the authoritative execution order for OpenAGC. The product goal is a
clean, redistributable GPU API that lets homebrew applications and game ports
compile shaders, allocate GPU resources, build command buffers, submit work,
and present frames on a jailbroken PS5 without proprietary SDK headers. FW
5.50 and the available gfx1013 hardware are the primary implementation and
validation target. Older roadmap sections remain as technical history; when
their ordering conflicts with this section, this section wins.

Keep implementation and validation work narrowly focused on GPU ABI
compatibility, shader compilation, PM4 state, resource management, submission,
and display integration. Exploit setup and security-bypass mechanics belong to
the launcher environment and hardware-test scaffolding, not the public OpenAGC
API.

## Definition of done

OpenAGC reaches its first usable release when a third-party homebrew project
can install the SDK, use documented public headers and CMake targets, compile
shaders with `openagc-psbc`, create the required GPU resources, record and
submit compute or graphics work, synchronize it, and present stable frames on
FW 5.50. The same application must build against the generic host backend for
packet and ABI tests. Failures must return documented errors rather than hang
the GPU, freeze the UI, or panic the kernel.

## Execution plan

### Phase 1: Ship a consumable SDK (complete)

1. Finish and validate install/export support for both `generic` and
   `prospero` builds.
2. Install public headers, `libopenagc.a`, the host `openagc-psbc` compiler,
   CMake package metadata, namespaced targets, license, and documentation.
3. Prove a clean downstream project can use `find_package(OpenAGC)`, link
   `OpenAGC::openagc`, invoke `OpenAGC::psbc`, and compile a shader through the
   provided CMake helper.
4. Produce a relocatable versioned archive suitable for a homebrew toolchain.
5. Keep the public package free of firmware blobs, decrypted modules,
   launcher-specific credential code, and host-machine paths.

Exit criteria met: clean generic and Prospero installs and downstream builds
pass; the installed host compiler compiles the real compute SPIR-V fixture from
both consumer configurations; and versioned generic and Prospero TGZ archives
are generated. The package contains the public headers, `libopenagc.a`,
`openagc-psbc`, relocatable CMake metadata, license, and documentation.

### Phase 2: Stabilize the FW 5.50 runtime boundary

1. Turn the hardware-proven `/dev/gc` initialization, internal-memory,
   default-state, queue, submit, suspend-point, and workload paths into a
   coherent runtime lifecycle.
2. Define explicit ownership and teardown rules for contexts, queues, mapped
   memory, command buffers, shaders, and display buffers.
3. Validate all sizes, alignments, firmware-profile fields, packet capacities,
   and user pointers before issuing ioctls or submissions.
4. Add bounded waits, submission diagnostics, and recovery-safe error paths so
   malformed or unsupported state fails before reaching the kernel.
5. Treat `FRAME_OPEN` returning `EINVAL` and PA debug returning `EPERM` as
   documented FW 5.50 capability results unless new firmware evidence proves a
   supported userspace path.
6. Audit every path associated with prior UI freezes and kernel panics,
   especially NGG state, queue lifetime, command-buffer bounds, synchronization,
   and register programming. Never expose uncertain state as a default builder.

Exit criteria: repeated websrv runs of init, compute, graphics, queue teardown,
and relaunch complete without a GPU hang, UI crash, or kernel panic.

### Phase 3: Complete reusable command and resource APIs

1. Replace sample-only PM4 sequences with typed OpenAGC builders for shader
   binding, render targets, viewport, scissor, draw, dispatch, barriers,
   cache management, events, and synchronization.
2. Provide resource helpers for garlic/onion memory, GPU virtual addresses,
   alignment, buffers, textures, render targets, depth targets, and shader
   records without hiding required hardware constraints.
3. Add state objects or descriptor structs that separate validation from PM4
   emission and make command-buffer capacity requirements predictable.
4. Preserve low-level builders for expert users while offering a documented
   minimal path for ordinary homebrew applications.
5. Add host fixtures for every hardware-proven packet stream, including exact
   Wave32 VS/PS, compute, render-target, viewport, scissor, draw, NGG, geometry,
   and tessellation state where validated.

Exit criteria: hardware samples use public library builders rather than local
packet assembly, and generic fixtures lock down headers, payloads, cursor
advance, rejected inputs, and ABI layouts.

### Phase 4: Make shader compilation a supported pipeline

1. Define and version the `AgcShaderRecord` contract shared by
   `openagc-psbc`, the public headers, and runtime binders.
2. Validate gfx1013 explicitly. Do not silently treat the PS5 as gfx1030 or
   infer behavior solely from a generic `gfx103.json` profile.
3. Support documented compute, vertex, pixel, NGG, geometry, hull, and domain
   stage inputs in an incremental feature ladder.
4. Validate register metadata, user-SGPR layout, Wave32/Wave64 requirements,
   fused-stage records, resource limits, scratch/LDS use, and stage linkage at
   compile time whenever possible.
5. Emit actionable compiler diagnostics for unsupported SPIR-V capabilities,
   stage combinations, interpolation, tessellation modes, and resource use.
6. Add compiler fixtures and end-to-end tests from source shader to installed
   compiler output to runtime binding.

Exit criteria: shader records are reproducible, version checked, rejected when
incompatible, and consumed without sample-specific register knowledge.

### Phase 5: Finish the graphics feature ladder on gfx1013

Advance only after the preceding rung is stable under repeated FW 5.50 runs:

1. Compute dispatch and memory visibility.
2. Wave32 VS/PS triangle, indexed and non-indexed draws, multiple draws, and
   correct render-target synchronization.
3. Additional render-target, vertex, index, texture, sampler, blend, depth,
   stencil, and multisample formats/states.
4. Stable pass-through NGG geometry with conservative, validated defaults.
5. NGG geometry-shader amplification and stream behavior.
6. Tessellation control/evaluation, ring allocation, factor buffers, patch
   constants, and HS/DS linkage.
7. Offscreen rendering, render-to-texture, mip levels, and copy/resolve paths.
8. HDR rendering and presentation, kept as two independently qualified gates:
   add `R11G11B10_FLOAT` and other HDR-capable typed targets; implement FP16
   or 10-bit presentation buffers; add Rec.2020/PQ conversion and any required
   VideoOut HDR metadata; lock the color pipeline with deterministic host and
   GPU-readback fixtures; then verify on FW 5.50 that the connected display
   enters HDR mode. HDR-range offscreen rendering must not be reported as HDR
   presentation until the VideoOut signal is hardware-confirmed.

Every rung needs a deterministic host fixture, a minimal hardware sample, a
documented expected screen/result, repeated websrv validation, and a negative
test that rejects unsafe configuration. A single successful frame is evidence,
not completion.

### Phase 6: Stabilize the homebrew-facing API from real requirements

1. Stabilize homebrew initialization, memory, shader, resource, draw, compute,
   synchronization, and presentation APIs as coherent vertical slices.
2. Move generally useful gfx1013 state and PM4 construction into reusable
   builders; do not add title-specific runtime paths.
3. Give every feature an exact host fixture and a focused FW 5.50 hardware
   sample before calling it supported.
4. Maintain buildable examples and public documentation for homebrew authors.
5. Retain Sony-compatible names when they improve source portability, while
   keeping unsupported behavior explicit rather than adding success stubs.
6. Treat Subnautica, DRAGON QUEST VII Reimagined, and other retail binaries as
   bounded ABI/NID evidence only. Audit one only when it resolves a missing
   structure, calling convention, firmware variant, or common API shape.

Exit criteria: representative homebrew samples build against documented public
APIs and pass deterministic host plus focused PS5 hardware gates. Retail import
counts are never a release criterion.

### Phase 7: Establish a release-grade validation matrix

1. Keep the generic clean build and complete host suite as the mandatory gate
   for every goal.
2. Add ABI size/offset assertions, packet golden fixtures, malformed-input
   tests, compiler fixtures, and downstream package-consumer tests.
3. Maintain FW 5.50 websrv tests for VideoOut, initialization, compute,
   graphics, NGG, geometry, tessellation, stress/relaunch, and teardown.
4. Record each hardware run with build commit, firmware ID (`0x0550`), sample,
   expected result, observed result, iteration count, and any crash/hang.
5. Require repeated cold-launch and relaunch success before marking a hardware
   feature stable.
6. Keep hardware-test-only launcher setup outside reusable OpenAGC code.

Exit criteria: a release checklist can be executed from a clean checkout and
produces traceable host, packaging, compiler, and FW 5.50 hardware evidence.

### Phase 8: Add firmware profiles without destabilizing FW 5.50

1. Identify firmware numerically with four hex digits such as `0x0550` and
   `0x0320`; aliases are optional display metadata, never lookup keys.
2. Isolate firmware-dependent ioctls, queue layouts, memory regions, exports,
   and capabilities behind explicit profiles selected at runtime.
3. Keep FW 5.50 as the reference profile until the API and graphics ladder are
   mature.
4. Bring up FW 3.20 next using the same host fixtures and hardware-validation
   gates where hardware becomes available.
5. Preserve FW 1.00 and 2.x evidence as archival data only. Do not spend release
   work on their legacy special-queue ABI or advertise them as supported.
6. Refuse unknown firmware conservatively instead of guessing a close profile.

Exit criteria: firmware variation is data-driven, unsupported versions fail
safely, and adding a profile does not fork the public API.

### Phase 9: Release and maintain

1. Publish versioned API/ABI and shader-record compatibility policies.
2. Provide minimal compute and graphics starter applications using only the
   installed SDK surface.
3. Document supported firmware, hardware, shader stages, formats, known errors,
   and validation evidence without overclaiming game compatibility.
4. Add semantic release notes and an upgrade guide for behavior or ABI changes.
5. Keep `README.md`, `STATUS.md`, this plan, installed package metadata, and
   samples synchronized at every completed goal.

## Immediate next goals

### Completed host goal: typed gfx1013 depth-surface binding

OpenAGC now has a reusable, atomic `agcGfx1013SetDepthSurface` builder for
gfx1013 depth/stencil memory state. It covers typed D16, D32, S8, D16+S8, and
D32+S8 surfaces; independent depth/stencil read and write bases; gfx103
swizzle modes; mip and array-layer views; 1x/2x/4x/8x samples; read-only
aspects; and optional HTILE/expclear state. Exact host fixtures lock the
27-dword register stream, including explicit `DB_HTILE_SURFACE`, 48-bit
address splitting, and negative
fixtures prove that malformed formats, aspect/address combinations, sample
counts, alignment, and undersized command buffers fail atomically.

The isolated FW `0x0550` D32, stencil, 4x MSAA, compressed HTILE, and typed
HTILE decompress/resummarize qualifications are complete. Compressed depth is
validated through deterministic color outcomes and changed metadata. The
typed operation gate then expands it to exact host-readable D32 and rebuilds
nontrivial HTILE ranges in a separate full-surface raster pass.

The first hardware sample is `samples/hw_test/agc_depth.elf`.
It uses an uncompressed D32-only 64KB-Z-X surface with HTILE disabled, performs
GPU initialization plus deterministic near-pass, overlap-fail, and independent
far-pass draws, and checks four stage markers, an EOP completion marker, RGBA8
samples, coverage, and raw D32 values. FW `0x05500008` produced all four stage
markers, 128,304 green and 128,304 red pixels, expected raw depth values, and
1,800/1,800 completed flips without a hang or kernel panic.

Depth/stencil synchronization is also typed. `DEPTH_STENCIL_WRITE` releases
gfx1013 DB metadata with event `0x2c`, releases DB data with timestamp event
`0x2b`, and uses the shared GCR acquire when the consumer remains on-GPU.
`DEPTH_STENCIL_READ` participates as a read-only usage, so read-to-read changes
remain explicit zero-dword transitions. Exact host fixtures cover DB-to-DB,
DB-to-host, read-to-read, and undersized atomic failure paths. The depth sample
uses separate color and depth-to-host transitions before CPU readback.

The reusable `agcGfx1013SetHtileOperation` builder emits an atomic three-dword
`DB_RENDER_CONTROL` packet. Depth decompression uses
`DEPTH_COMPRESS_DISABLE | DECOMPRESS_ENABLE` (`0x1040`), depth
resummarization uses `RESUMMARIZE_ENABLE` (`0x0010`), and the neutral state
restores zero. The hardware sample disables ordinary depth and color writes,
uses full-surface raster passes, brackets the modes with typed DB
release/acquire transitions, and restores neutral state before completion.
FW `0x05500008` accepted the 2,695-dword DCB, reached its fence in 1 ms,
recovered exact D32 counts of 909,792 clear, 128,304 near, and 128,304 far
words, produced 4,226 non-initial HTILE words after resummarization, and
completed 1,800/1,800 flips.

Depth-only expclear is also hardware-validated. The typed
`agcGfx1013SetDepthExpclear` builder accepts only the gfx10.3 canonical 0.0 or
1.0 clear values and emits `DB_DEPTH_CLEAR` atomically. The isolated gate
initializes HTILE to the depth-one clear encoding `0xfffffff0`, enables
`DB_Z_INFO.ALLOW_EXPCLEAR`, and deliberately omits the old full-surface depth
initialization draw. Near-pass, overlap-fail, and independent far-pass draws
therefore consume only metadata-backed clear depth. The proven decompression
pass recovered 918,432 exact 1.0 D32 words, 128,304 near words, and 128,304 far
words; all 49,152 HTILE words changed after resummarization; and the FW
`0x05500008` run completed 1,800/1,800 flips. The physical display showed the
expected green and red triangles on dark gray.

Combined D32+S8 HTILE is hardware-validated separately with expclear disabled.
Shared metadata starts at gfx10.3 combined uncompressed `0xfffff30f`. The
typed combined decompression mode emits
`STENCIL_COMPRESS_DISABLE | DEPTH_COMPRESS_DISABLE | DECOMPRESS_ENABLE`
(`0x1060`), followed by the proven resummarization and neutral-state restore.
FW `0x05500008` recovered 909,792 clear, 128,304 near, and 128,304 far D32
words; raw S8 contained 2,364,832 zero bytes, 256,608 `0x5a` bytes, and no
other values; all 49,152 HTILE words changed; and VideoOut completed
1,800/1,800 flips without a hang or reset.

The isolated HTILE subresource milestone is complete on FW `0x05500008`.
Mip 1 of a two-level D32 image and layer 1 of a two-layer D32 image both passed
exact selected-versus-untouched metadata readback gates and sustained preview.
The mip run also established that viewport/scissor must match the selected mip
extent; using the base extent legitimately rasterized beyond the mip attachment
and changed adjacent metadata. See
`analysis/fw550_htile_subresources_20260727.md`.

The hardware-sample PM4 promotion audit is also complete. Normal real-PS5
sample paths contain no hand-packed headers, direct command allocation, or raw
register emission. Intentional markers, repeated diagnostic draws, the PM4
decoder, and emulator export-conformance calls remain low-level by design; see
`analysis/sample_pm4_public_api_audit.md`.

Combined stencil/HTILE expclear is complete and hardware-enabled. The typed
plan, exact-range Wave32 compute RMW, selective clear-register and depth-surface
state, and DB/compute synchronization are covered by exact host fixtures.
Depth-only, stencil-only, and both-aspect FW `0x0550` cases each passed twice
with the public gate off: exact selected-range values, zero metadata spill,
reserved-bit preservation, D32/S8 readback, fences, draw markers, and
1,800/1,800 flips all passed without a reset or panic. See
`analysis/fw550_combined_expclear_qualification_20260727.md`.

Next execution order:

4. **Host complete:** application-facing typed indexed drawing now composes
   u16/u32 buffers, first-index address adjustment, instance count, and
   `DRAW_INDEX_2`. Exact fixtures lock packet order, range encoding, and atomic
   short-buffer rejection. Base-vertex values remain shader ABI state; the PM4
   packet exposes no direct base-vertex value field.
5. **Host complete:** application-facing single/multi indirect composition now
   covers indexed and non-indexed arguments, argument-base and offset
   alignment, stride validation, register locations, and exact golden streams.
   The hardware-qualified consumer retains its PS5 7-dword fixed-count form.
   The separately recovered Sony public exports now expose their native
   ten-dword count-address layout across all 39 active profiles, but remain
   SPRX-qualified/hardware-unverified and are not substituted into this path.
   A bounded 2026-07-28 test of a different Mesa-style gfx10+ ten-dword form
   caused a GPU fault and reset. DrawIndex control stays reserved in the typed
   consumer until the Sony form is independently hardware-qualified.
6. **Hardware complete:** direct u16 indexed, non-indexed indirect, and u16
   indexed-indirect each passed separately on FW `0x0550` through curl/websrv
   with exact FP16 coverage, completion fence, Wave32 audit, and 1,800 flips.
   The recovered `SET_BASE` wrapper requires canonical header control zero;
   passing one timed out and is now covered by an exact regression fixture.
7. **Hardware complete:** RGBA8 SRGB and BGRA8 SRGB append public enum values
   12 and 13 without renumbering existing formats. Exact fixtures lock CB
   format `0x0a`, CB sRGB number type `6`, standard/alternate component swaps,
   FP16_ABGR export, and `CB_COLOR0_INFO` values `0x00010628`/`0x00010e28`.
   Both identical ELFs passed twice on FW 5.50 with native packed-memory
   transfer oracles, exact coverage, Wave32 PM4 state, two completion fences,
   and 1,800/1,800 flips.
8. **Hardware complete:** R16_FLOAT and RG16_FLOAT use typed CB formats `0x02`
   and `0x05`, FLOAT number type `7`, standard swap, and FP16_ABGR export.
   Each format passed twice on FW `0x05500008` with deterministic native
   packed-memory hashes, exact complete-component checks, Wave32 audits,
   completion fences, and 1,800/1,800 flips. Live ps5debug-NG logs contained
   no GPU fault or reset signatures. One earlier R16 launch coincided with an
   unlogged console shutdown, so its cause remains unproven; event setup and
   VideoOut teardown are now checked explicitly.
9. **Hardware complete:** uncompressed D16 depth-only uses the typed
   `D16_UNORM` `64KB_Z_X` layout and surface binder with HTILE, stencil, MSAA,
   and expclear disabled. Two identical FW `0x05500008` runs produced exact
   128,304-pixel near/far color outcomes and exact 909,792/128,304/128,304
   native clear/near/far D16 counts, passed all markers and fences, completed
   1,800/1,800 flips, and left clean live kernel logs.
10. **Hardware complete:** uncompressed S8-only uses the typed `S8_UINT`
   `64KB_Z_X` layout and binder with no depth plane, zero depth addresses, and
   depth testing/writes disabled. Two identical FW `0x05500008` runs proved
   stencil replace and compare rejection with exact 128,304 green/red color
   counts, 2,364,832 zero plus 256,608 `0x5a` S8 bytes, all markers/fences,
   1,800/1,800 flips, and clean live kernel logs.
11. **Hardware complete:** uncompressed D16+S8 uses separate typed
   `64KB_Z_X` planes with HTILE, MSAA, and expclear disabled. Two identical FW
   `0x05500008` runs reproduced the exact D16 and S8 native counts from the
   independent gates, exact green/red color counts, all markers/fences,
   1,800/1,800 flips, and clean live kernel logs.
12. **Hardware complete:** compressed D16/HTILE uses the recovered gfx1013
   2048x1152 D16 and 2048x1536 metadata layouts, depth-only `0xfffc000f`
   initialization, D16 `ZRANGE_PRECISION`, and typed depth decompression plus
   resummarization. Two identical FW `0x05500008` runs each changed 4,226
   metadata words, recovered exact 909,792/128,304/128,304 native D16 counts,
   produced exact 128,304 green/red color counts, passed all markers and 1 ms
   fences, completed 1,800/1,800 flips, and left clean live kernel logs.
13. **Hardware complete:** D16 HTILE expclear uses the exact host-locked
   `DB_Z_INFO` word `0xaf800181` (`ALLOW_EXPCLEAR=1`,
   `DECOMPRESS_ON_N_ZPLANES=15`, and `ZRANGE_PRECISION=1`) and canonical
   depth-one metadata `0xfffffff0`. Two identical FW `0x05500008` runs each
   changed all 49,152 metadata words, recovered exact
   918,432/128,304/128,304 native D16 counts, produced exact 128,304 green/red
   color counts, passed every marker and 1 ms fence, completed 1,800/1,800
   flips, and left clean live kernel logs.
14. **Hardware complete:** linear R8 and RG8 UNORM use CB formats `0x01` and
   `0x03`, number type `0`, standard swap, and FP16_ABGR export selected by the
   reusable frame post-bind builder. Two identical runs per format produced
   stable 255,043/255,744-pixel coverage and FNV64
   `0x6fe253259c7b0455`/`0x6babce1afaa81b2c`, passed Wave32/marker/fence checks,
   completed 1,800/1,800 flips, and left clean live kernel logs.
15. **Hardware complete:** linear R32 FLOAT uses CB format `0x04`, FLOAT number
   type `7`, standard swap, and the format-derived 32_R shader export. Two
   identical runs each stored 255,744 complete pixels, eight distinct values,
   no invalid float components, and FNV64 `0x43e0f1986c4ec883`; all checks and
   1,800/1,800 flips passed with clean kernel logs.
16. **Hardware complete:** linear RG32 FLOAT uses CB format `0x0b`, FLOAT
   number type `7`, standard swap, and the format-derived 32_GR export. Two
   identical runs each stored two complete components for 255,744 pixels,
   eight distinct values, no invalid floats, and FNV64 `0x806171be9908c276`;
   all checks and 1,800/1,800 flips passed with clean logs.
17. **Hardware complete:** linear RGBA32 FLOAT uses CB format `0x0e`, FLOAT
   number type `7`, standard swap, the format-derived 32_ABGR export, and a
   16-byte-per-pixel allocation. Two identical runs each stored four complete
   components for 255,744 pixels, eight distinct values, no invalid floats,
   and FNV64 `0x1e8771ed63381dce`; all checks and 1,800/1,800 flips passed with
   clean logs. The R8/RG8/R32/RG32/RGBA32 format-expansion goal is complete.
18. Complete the bounded Subnautica evidence cleanup, then return to stable
   homebrew-facing vertical slices and examples. Do not expand Subnautica or
   DRAGON QUEST VII into retail-runtime compatibility goals; consult those
   binaries only when a missing ABI contract blocks reusable homebrew work.

## Gfx1013 4x MSAA depth gate (hardware validated)

The reusable FW `0x0550` preparation for the isolated 4x gate is complete:

- Typed `agcGfx1013SetSampleState` emits `PA_SC_AA_CONFIG`, `DB_EQAA`,
  `PA_SC_MODE_CNTL_0`, `PA_SC_MODE_CNTL_1`, four sample-location registers,
  centroid priority, and both coverage masks for exact 1x/4x state. Baseline
  and tessellation draw states apply it after shader binding; 1/2/4 pixel
  iterations are host-locked and the 2x/4x modes are FW 5.50-qualified.
- Typed `agcGfx1013GetColorSurfaceLayout` covers 4x `64KB_R_X` color
  allocations, while the existing D32 layout covers 4x `64KB_Z_X`.
- Color-target binding now types the sample/fragment counts and swizzle mode;
  gfx1013 image descriptors type 2D-MSAA, log2 sample count, and R_X swizzle.
- `agcGfx1013ResolveColor4x` performs the render-to-shader transition, builds
  the 1x destination frame, restores 1x state after defaults, and executes a
  caller-supplied fullscreen shader draw. It intentionally does not use the
  unsupported gfx10 legacy `CB_RESOLVE` mode.
- `agc_depth_msaa.elf` renders the D32 pass/fail fixture into separate 4x
  color/depth images and averages four samples into the 1x VideoOut buffer.
  Stencil and HTILE are explicitly disabled.

FW `0x05500008` passed this gate repeatedly through curl/websrv on 2026-07-27.
The 5,131-dword DCB produced 127,818 exact green and 127,818 exact red pixels,
all four draw-stage markers, the completion fence, and nonzero initialization,
near, and far raw D32 classes. VideoOut completed 1,800/1,800 flips. The
captured physical result showed the expected green/red triangles and resolved
edges on the dark-gray framebuffer without a hang or kernel panic.
