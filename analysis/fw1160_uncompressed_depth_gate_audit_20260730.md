# FW 11.60 uncompressed depth/stencil gate audit (2026-07-30)

## Scope

This gate carries the three FW 5.50-qualified, uncompressed gfx1013
depth/stencil paths to a standard PS5 running exact firmware ABI `0x1160`:

1. `D16_UNORM` depth only;
2. `S8_UINT` stencil only;
3. combined `D16_UNORM + S8_UINT`.

The artifacts are headless and self-terminating. They use the exact standard
FW 11.60 backend profile, version-12 defaults, bounded GPU completion fence,
native tiled-memory readback, driver shutdown, and final process exit. HTILE,
expclear, and MSAA remain disabled so this gate isolates the base depth and
stencil surface ABIs.

## Artifacts

| Artifact | SHA-256 |
| --- | --- |
| `agc_depth_d16_fw1160.elf` | `c97e4335b84319cf8d62a324f6b78dfd988fb56f02d6924443817bf87e9ee01f` |
| `agc_stencil_s8_fw1160.elf` | `039b008c9669c0ab5bec4cbed11105c02550c0422cfb85ac349649fd9bb8819f` |
| `agc_depth_stencil_d16_s8_fw1160.elf` | `fdddd0da4c3efa599929449dcbd463b2fbf5aa6e6847f51c84d341915e10ccd6` |

`run_fw1160_depth.sh` uploads and runs the established process-cleanup ELF
immediately before each payload. It accepts a run only when the exact
`0x1160` profile, GPU completion fence, depth result, driver shutdown, and
final graphics verdict all pass, and rejects any failure, fatal, mismatch, or
timeout text.

The headless harness allocates a real 1920x1080 linear RGBA8 color-oracle
surface ahead of the aligned depth/stencil allocations and no longer copies
that result into an absent VideoOut preview buffer. This changes no GPU packet
or native oracle; it replaces the two VideoOut-owned addresses that are absent
from headless builds.

The first D16 launch was rejected before command construction or GPU
submission: the diagnostic printed a null color-target address and the process
exited before `DCB: ... submitting`. ps5debug-NG immediately reported no
residual `eboot`, and ports 744 and 8080 remained reachable. That run is not a
hardware attempt and led to the explicit headless color-oracle allocation in
the hashes above.

## Required native oracles

The gates retain the exact FW 5.50-qualified readback counts:

- D16: 909,792 clear-one samples, 128,304 near samples, 128,304 far samples,
  and exactly 128,304 green plus 128,304 red color pixels.
- S8-only: 2,364,832 zero bytes, 256,608 bytes equal to `0x5a`, no other
  stencil values, and exactly 128,304 green plus 128,304 red color pixels.
- D16+S8: both exact D16 and S8 distributions above, plus the same exact
  color counts.

These native distributions, the stage markers, and the completion fence are
the qualification oracle. A clean process exit or visible output alone is not
sufficient.

## Hardware order

The preceding stage-17 inline-workload experiment stalled the GPU queue, so a
full console reboot is required before this matrix. After reboot, inject
ps5debug-NG and run D16 twice, S8-only twice, then D16+S8 twice. Inspect the
live debugger and process list after every launch. Stop immediately on a
timeout, stale `eboot.elf`, UI degradation, GPU fault/reset, process-stop, or
panic signature; reboot before any further GPU payload.

After all six FW 11.60 runs pass identically, rerun the corresponding FW 5.50
artifacts to rule out regression before promoting these capabilities. HTILE,
expclear, compressed depth/stencil, and MSAA require later, separately bounded
gates.
