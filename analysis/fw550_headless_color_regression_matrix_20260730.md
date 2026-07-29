# FW 5.50 modern headless color regression matrix (2026-07-30)

## Purpose

The retained FW 5.50 color qualifications used older display-backed artifacts.
FW 11.60 parity promotion requires matching the current headless allocation,
viewport, shader, native readback, bounded fence, shutdown, and termination
paths rather than comparing unlike historical executables.

All artifacts below force exact ABI key `0x0550`, reject any other runtime,
and cross-built without warnings:

| Target | Artifact SHA-256 |
| --- | --- |
| R16_FLOAT | `16f391c5c3ea55ced18d20c237a14f5a4b9bd739beda38da011f7b8335f78bdf` |
| RG16_FLOAT | `6f0fcc728e4058f5bf2a89d4ed59c32c4413c6ee54a6b3dad4efa447b1587f1a` |
| R8_UNORM | `4dbf417980b11a1e4ad9a86e543e5f69c93a9b32f9cb704ebfe9fbcdb0ea10fe` |
| RG8_UNORM | `fcb24063b028bca7e3c6bbd871136032bdca6e44a449d4cc0a6aa0788ba28c07` |
| RGB10A2_UNORM | `37ef714ecbba60942fa8f08023ecdd40eb3d13ff2cb12e4e6495e75fa69bf8c5` |
| R11G11B10_FLOAT | `6d60d2e747edb513905c022963c5c7173f6ff7f276db3247259707270ddfbb8d` |
| R32_FLOAT | `aec9e75909c3817413f9344eaa9c89be3a55e2b835f6df2e198fedafce6be3c6` |
| RG32_FLOAT | `c994572a21e9fa8a36b4a2478abe356a2344fd1675dbc63fa4ec94544ed52801` |
| RGBA32_FLOAT | `e93d4d580722da8d4e714af89d4a299023fe722e42c48ee6a71581b4b487b2c4` |

`run_fw1160_graphics.sh` now accepts `EXPECTED_FW_ABI`; its default remains
`0x1160`, while FW 5.50 regression calls set `EXPECTED_FW_ABI=0x0550`. It
derives a firmware-specific remote directory and requires the correct native
oracle for each target family.

Run one matching FW 5.50 pass for every twice-qualified FW 11.60 format. RG32
and RGBA32 stay behind completion of their FW 11.60 two-pass gates. Each launch
still requires the cleanup ELF immediately beforehand and a ps5debug-NG
residual-process check afterward. Stop on any differing native hash/histogram,
timeout, stale process, fault, reset, or UI degradation.

The FW 5.50 console at `10.0.1.41` refused both ports 8080 and 744 when these
artifacts were prepared, so none is hardware-run yet.
