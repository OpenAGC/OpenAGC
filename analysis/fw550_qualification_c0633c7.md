# FW 5.50 Hardware Qualification: c0633c7

Date: 2026-07-26

## Qualified revision and environment

- OpenAGC revision: `c0633c7`
- Raw system software: `0x05500008` (`5.500.008`)
- GPU: PS5 gfx1013
- Deployment: FTP upload plus foreground curl/websrv `/hbldr`
- Host gate: clean generic configure/build and complete retained test suite pass
- Target gate: clean Prospero library and all 14 qualification ELFs build
- Loader gate: every case returned successfully before the next ELF launched

The first qualification attempt exposed a harness lifecycle bug rather than a
PM4 failure. The old compute sample passed its complete GPU readback, then
waited for a flip event without submitting a flip. Attempting another hbldr
launch while that process remained active eventually panicked the console; the
new graphics ELF produced no output and its log was empty. Revision `c0633c7`
adds the missing compute flip, makes the VideoOut smoke test finite and
self-cleaning, and treats every curl timeout as a hard stop. The complete suite
below ran after reboot with no overlapping loaders, hang, panic, or UI crash.

## Base qualification

| ELF | Machine oracle | Physical result |
| --- | --- | --- |
| `videoout_linear.elf` | Internal FW 5.50 VideoOut patch; 600/600 flips; clean exit | Solid colors cycled correctly |
| `agc_init.elf` | FW profile, permission stub, three batched multi-DCB marker runs, queue create/destroy, and suspend-point path pass | Non-graphical case completed with responsive UI |
| `agc_videoout.elf` | AGC init/defaults/async setup and periodic NOP submits return `AGC_OK`; 600 color-bar frames | Color bars cycled correctly |
| `agc_compute.elf` | 2,073,600/2,073,600 pixels equal `0xFF00FF00`; display flip completed; clean exit | GPU output displayed correctly |

## Baseline and NGG graphics qualification

Every case returned `AGC_OK` from the reusable baseline draw wrapper, passed
the NGG+PS PM4 audit, executed the post-draw `0xDEADCAFE` marker, sampled eight
colors, and completed 1,800/1,800 flips.

| Case | Changed pixels and bounds | Physical result |
| --- | --- | --- |
| Baseline run 1 | 255,744; x=384..1151, y=436..1100 | Gray background, colorful triangle |
| Baseline run 2 | 255,744; x=384..1151, y=436..1100 | Identical colorful triangle |
| Baseline run 3 | 255,744; x=384..1151, y=436..1100 | Identical colorful triangle |
| NGG amplification | 127,488; x=346..1189, y=602..933 | Two colorful triangles |
| NGG line topology | 255,744; x=384..1151, y=436..1100 | Colorful triangle/topology result |
| NGG invocations | 127,488; x=346..1189, y=602..933 | Two colorful triangles |
| Direct RGBA8 | 126,360; full vertex/index/texture checks pass | Green/dark-red colorful triangle |

The three baseline runs used the identical ELF:
`4bc18b4b2cb6812a50217d03b1bc586efa8c8942b52193e8ef4859d845fdf8eb`.

## Tessellation and combined-stage qualification

Every case configured the FW 5.50 TF ring successfully, returned `AGC_OK` from
the reusable HS+TES+PS binder, passed the NGG+PS PM4 audit, executed the
post-draw `0xDEADCAFE` marker, sampled eight colors, and completed 1,800/1,800
flips.

| Case | Changed pixels and bounds | Physical result |
| --- | --- | --- |
| Isolated tessellation | 255,744; x=384..1151, y=436..1100 | Tessellated colorful triangle displayed |
| TES-to-NGG geometry run 1 | 155,321; x=406..1129, y=465..1091 | Combined triangle displayed |
| TES-to-NGG geometry run 2 | 155,321; x=406..1129, y=465..1091 | Identical combined triangle |
| TES-to-NGG geometry run 3 | 155,321; x=406..1129, y=465..1091 | Identical combined triangle |
| Combined invocations | 127,488; x=346..1189, y=602..933 | Invocation geometry displayed |
| Combined line-strip | 6,749; x=384..1151, y=435..1100 | Sparse subdivided/line result displayed |
| Combined RGBA8 | 76,803; vertex/index/texture checks pass | Combined RGBA8 result displayed |

The three combined geometry runs used the identical ELF:
`30983c736b5666f26228da780243707e1b4ee048a34e189749fac8e31a9478a0`.

## ELF SHA-256 evidence

| ELF | SHA-256 |
| --- | --- |
| `agc_graphics.elf` | `4bc18b4b2cb6812a50217d03b1bc586efa8c8942b52193e8ef4859d845fdf8eb` |
| `agc_graphics_amplify.elf` | `69d86df4168c04ba24b8d1755faa10197b6b42e69b5786e6082d7c0732ed75aa` |
| `agc_graphics_lines.elf` | `bd73366cf0cd09bbacfbc014b096a2dbbec904a806e61a230c5285038999669a` |
| `agc_graphics_invocations.elf` | `a136535e9cc0bb17a19e2c6d7dbfecba8ed83af5ad503b4f201d28076e6ff594` |
| `agc_graphics_rgba8.elf` | `8d33e27b0a8321a2093598124bf3204ae4a08ef41b03314081c9bd9e7b9bdcf2` |
| `agc_tessellation.elf` | `92fae0686cf25a6919c0db86389c0a16f7cc44dd38cce5d13582b1ca3d9c5db1` |
| `agc_tess_geometry.elf` | `30983c736b5666f26228da780243707e1b4ee048a34e189749fac8e31a9478a0` |
| `agc_tess_geometry_invocations.elf` | `350ea04761bec9da7eb7f5512bf554a1bfb338f07f30668012a7f8d99455b9f8` |
| `agc_tess_geometry_lines.elf` | `d7283b239310350db29f859300bf117894b6ae4af4adef87fbbcf99294df301b` |
| `agc_tess_geometry_rgba8.elf` | `2d311dfa0055a49b783b6451149f6dc60ce4f8cc55b4eb7ff189705c1908ed9c` |

## Result

The complete post-PM4-promotion FW 5.50 websrv qualification gate passes on
revision `c0633c7`. All deterministic software oracles and physical-display
checks pass. Repeated baseline and combined tessellation results are identical,
all applicable cases complete 1,800/1,800 flips, and the corrected sequential
run has no GPU hang, kernel panic, or UI crash.
