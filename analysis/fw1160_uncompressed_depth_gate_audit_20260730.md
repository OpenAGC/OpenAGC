# FW 11.60 uncompressed depth/stencil gate audit (2026-07-30)

## Scope

This gate carries the four FW 5.50-qualified, uncompressed gfx1013
depth/stencil paths to a standard PS5 running exact firmware ABI `0x1160`:

1. `D32_FLOAT` depth only;
2. `D16_UNORM` depth only;
3. `S8_UINT` stencil only;
4. combined `D16_UNORM + S8_UINT`.

The artifacts are headless and self-terminating. They use the exact standard
FW 11.60 backend profile, version-12 defaults, bounded GPU completion fence,
native tiled-memory readback, driver shutdown, and final process exit. HTILE,
expclear, and MSAA remain disabled so this gate isolates the base depth and
stencil surface ABIs.

## Artifacts

| Artifact | SHA-256 |
| --- | --- |
| `agc_depth_d32_fw1160_logged.elf` | `84b82e4ad6cc9d658e9af710d91ad0b344cfe50d9c99298907c9b45d8a1a8274` |
| `agc_depth_d16_fw1160.elf` | `f9c26bd6373ecb5ce87fedb492e95a3646d0506665b464cf06eb44ccb4ae2949` |
| `agc_stencil_s8_fw1160.elf` | `c2c679296601e93857fb62ce0e7fc1a764dfba31246884bc15d5f265907d2fca` |
| `agc_depth_stencil_d16_s8_fw1160.elf` | `561f0acc085666eccd9c83c417203d300f85da9a66cf61504585b2cf642d9ced` |

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

The next D16 launch completed its 2,633-dword DCB in 1 ms, passed all five
markers, and shut the driver down cleanly. It was deliberately rejected by the
old centered-square counts: the exact full-rectangle result was 228,096 green,
228,096 red, 1,617,408 clear-one D16, 228,096 near D16, and 228,096 far D16.
The 16/9 change from the retained counts is exactly the viewport-area change,
not a firmware-dependent ABI difference. ps5debug-NG reported no residual
`eboot`. The hashes above must be refreshed after rebuilding the corrected
oracle, and this rejected run does not count toward qualification.

## Required native oracles

The original FW 5.50 display-backed fixtures used the former centered-square
viewport semantics and produced the retained 128,304-pixel triangle counts.
The current public viewport API intentionally covers the full 1920x1080
rectangle. The FW 11.60 headless gates therefore require the exact current
geometry observed by the first completed D16 submission:

- D16: 1,617,408 clear-one samples, 228,096 near samples, 228,096 far samples,
  and exactly 228,096 green plus 228,096 red color pixels.
- D32: the same exact logical classes using native float encodings:
  1,617,408 clear-one, 228,096 near, and 228,096 far samples, plus the same
  exact color counts.
- S8-only: 2,165,248 zero bytes, 456,192 bytes equal to `0x5a`, no other
  stencil values, and exactly 228,096 green plus 228,096 red color pixels.
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

After all eight FW 11.60 runs pass identically, rerun the corresponding FW 5.50
artifacts to rule out regression before promoting these capabilities. HTILE,
expclear, compressed depth/stencil, and MSAA require later, separately bounded
gates.

## FW 11.60 hardware result

All four uncompressed gates passed twice on the standard PS5 reporting raw
firmware `0x11600005`:

| Gate | Run 1 fence | Run 2 fence | Exact native result |
| --- | ---: | ---: | --- |
| D32 | immediate | immediate | color `228096/228096`; D32 `1617408/228096/228096` |
| D16 | 1 ms | 1 ms | color `228096/228096`; D16 `1617408/228096/228096` |
| S8-only | 1 ms | 1 ms | color `228096/228096`; S8 `2165248/456192/0` |
| D16+S8 | 1 ms | 3 ms | both exact D16 and S8 distributions above |

Every qualifying run reported the exact `0x1160` profile, Wave32 NGG and PS,
all four stage markers plus the completion marker, `SubmitDcb: AGC_OK`, driver
shutdown PASS, and final graphics PASS. ps5debug-NG found no residual `eboot`
after every run; its port 744 and websrv port 8080 remained responsive after
the matrix.

This hardware-qualifies all four base uncompressed paths on the tested FW
11.60 console. Project-wide promotion remains pending the matching modern
headless regression on FW 5.50. The two earlier rejected harness-development
runs do not count toward the two-pass result.

The original matrix inadvertently omitted the separately FW 5.50-qualified
uncompressed D32 path. An exact logged FW 11.60 artifact and exact headless
FW 5.50 mirror were built to close that gap. Two FW 11.60 runs reproduced the
same full-rectangle color counts and exact native D32 classes listed above,
with immediate fences, clean driver shutdown, final PASS verdicts, and no
residual `eboot`. Its FW 5.50 mirror remains part of the pending regression
matrix.

## FW 5.50 regression artifacts

The matching modern headless artifacts force exact ABI key `0x0550` and use
the same current viewport, allocation, packet, native readback, bounded fence,
shutdown, and self-termination paths as the FW 11.60 gates:

| Artifact | SHA-256 |
| --- | --- |
| `agc_depth_d32_fw550_headless.elf` | `54ca186bc6aac7e3c335b03bd62cf07084f7a0f15ea48631524df1d25566e9de` |
| `agc_depth_d16_fw550_headless.elf` | `e3ac54d0edcd003246a9517f03879ed37f22cdf573b1202ade2c699b6881ebf0` |
| `agc_stencil_s8_fw550_headless.elf` | `8059abd5a68c44f8ea1d05215f374550063ba552b500f6f9a3050e565553eef3` |
| `agc_depth_stencil_d16_s8_fw550_headless.elf` | `3866dd4dcf82e1a5425e23c401e7794702dac8b359009f70852cb0583fd6b001` |

`run_fw1160_depth.sh` now accepts an `EXPECTED_FW_ABI` selector and derives a
firmware-specific remote path. The FW 5.50 deploy targets set it to `0x0550`;
the default remains exact `0x1160`. Run D32, D16, S8-only, and D16+S8 once each on
the FW 5.50 console. Any mismatch blocks promotion and requires stopping the
matrix.
