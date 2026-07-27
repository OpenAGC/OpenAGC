# FW 5.50 Hardware Qualification: 03b43f2

Date: 2026-07-27

## Environment and retained evidence

- OpenAGC artifact revision: `03b43f25728256d0f5f346589234916cbc9ed9db`
- Raw system software: `0x05500008` (`5.500.008`)
- GPU: standard PS5 gfx1013
- Deployment: sequential FTP upload and foreground curl/websrv `/hbldr`
- Result: 17/17 samples passed in 434 seconds
- Per-sample timeout: 90 seconds
- Raw evidence directory:
  `samples/hw_test/conformance-logs/fw550-03b43f2-20260727/`
- Evidence files: `run.env`, `artifacts.sha256`, `logs.sha256`, and 17 raw
  per-sample logs

The runner used a unique remote directory for every ELF and did not launch the
next sample until the current foreground request returned and its exact log
gates passed. No timeout, instant close, firmware mismatch, loader overlap,
GPU hang, reset, kernel panic, or UI crash occurred.

## Ordered matrix

| # | Sample | Required machine oracle | Result |
| ---: | --- | --- | --- |
| 1 | `videoout_linear` | 600/600 VideoOut flips | PASS |
| 2 | `agc_init` | FW profile, defaults, multi-DCB, queue and suspend lifecycle | PASS |
| 3 | `agc_videoout` | NOP submits and 600 color-bar frames | PASS |
| 4 | `agc_compute` | 2,073,600/2,073,600 pixels and completed flip | PASS |
| 5 | `agc_graphics` | Wave32, EOP, FP16 target, 1,800 flips | PASS |
| 6 | `agc_graphics_rgba8` | vertex/index/texture checks and 1,800 flips | PASS |
| 7 | `agc_graphics_amplify` | Wave32 amplified geometry and 1,800 flips | PASS |
| 8 | `agc_graphics_lines` | Wave32 line topology and 1,800 flips | PASS |
| 9 | `agc_graphics_invocations` | Wave32 GS invocations and 1,800 flips | PASS |
| 10 | `agc_tessellation` | TF ring, typed HS+TES+PS bind, EOP, 1,800 flips | PASS |
| 11 | `agc_tess_geometry` | combined tessellation/geometry path | PASS |
| 12 | `agc_tess_geometry_invocations` | combined invocation path | PASS |
| 13 | `agc_tess_geometry_lines` | combined line-strip path | PASS |
| 14 | `agc_tess_geometry_rgba8` | combined direct-RGBA8 resource path | PASS |
| 15 | `agc_depth_htile_ops` | exact decompressed D32 and resummarized HTILE | PASS |
| 16 | `agc_depth_expclear` | metadata-only clear and exact expanded D32 | PASS |
| 17 | `agc_depth_stencil_htile` | exact D32, S8, and combined HTILE | PASS |

## Depth evidence

- HTILE operations: 909,792 clear, 128,304 near, and 128,304 far D32 words;
  4,226 changed metadata words.
- Depth expclear: 918,432 clear, 128,304 near, and 128,304 far D32 words; all
  49,152 metadata words changed from `0xfffffff0`.
- Combined stencil/HTILE: 909,792 clear, 128,304 near, and 128,304 far D32
  words; 2,364,832 zero S8 bytes, 256,608 `0x5a` bytes, no other S8 values;
  all 49,152 metadata words changed from `0xfffff30f`.

All three depth cases reached the EOP fence in 1 ms, passed every stage and
color oracle, and completed 1,800/1,800 VideoOut flips.

## ELF SHA-256 evidence

The complete absolute-path manifest is retained as `artifacts.sha256`. The
artifact hashes, in matrix order, are:

```text
881a0c51bbe7fbf47733d952aebabdbc80d04ea6a85c1a3c5adb76eb437a0709  videoout_linear.elf
109635bdb51d3df872366d8159d6b8f18b2d1df4c31944e939dcad639e095787  agc_init.elf
d0e04b2bdae95cfc0af435779e0658649b83cd95dddbce19d0e4235a31d0e4d8  agc_videoout.elf
83f59fe7e6d656cea71335e32d3666a02d4345b9c3f489d2c7c1c40327e923ec  agc_compute.elf
652f5d501b07e3919e5f0ea21c7ee2e68550d01a428ce25e4412353aa72fc1d6  agc_graphics.elf
344e84e718a2171e267f4a9eab4203043e6dc16f1ab87737ed21ec82c4b489dc  agc_graphics_rgba8.elf
a1e9852db73688f6e0910616c109642ee858b4bb836a8cd4384ee0d65a2d0b82  agc_graphics_amplify.elf
3feb8b4e89c07b99474ba20860718243a111267bfcb194e655232dd60f828f46  agc_graphics_lines.elf
d1f6ff1a12432f60a97df9c5e4da80bbda956166d31f718f3dfa2df42cfdc7fc  agc_graphics_invocations.elf
92127faf65776a0e75abb1b7ccac6a2bbf361e77927fef11acf59e0b77e5847c  agc_tessellation.elf
18ef24c2fffeeca686fc829f03b60e97ab6132fe9b6c10868033d5d0557b384f  agc_tess_geometry.elf
87361d4a6d500fcf5a7d0e842e907c2592ba30e6db47cfac69a186359ee09424  agc_tess_geometry_invocations.elf
3b9a44fd5278e111374ac00b3bb6da8397db6f48005a2e2f4a75a5b6765e3d2a  agc_tess_geometry_lines.elf
b1c0a72d49ed66929090ad5d667b651bb97451e156d226ef4762c2f31380925d  agc_tess_geometry_rgba8.elf
f099f85d2d958bbfa76c5ba55c8731c5d97135d09223e2f5fab2785998cc278e  agc_depth_htile_ops.elf
54ee52e14705b59b47eb293363c720dac9d504a83e44db1dfdea83f0be0ebfae  agc_depth_expclear.elf
956c3186491bd5c9ff7d797a16d9150c036e7adf11b547ab4319ea85c8524a53  agc_depth_stencil_htile.elf
```
