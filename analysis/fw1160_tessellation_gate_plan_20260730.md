# FW 11.60 isolated tessellation gate plan (2026-07-30)

## Scope

This gate carries the FW 5.50-qualified Wave32 HS+TES+PS path to standard FW
11.60 without geometry-shader, HTILE, MSAA, or workload state. It exercises
the already-qualified FW 11.60 TF-ring carrier with real factor-ring and
offchip-ring GPU traffic rather than only accepting the ring address.

## Exact artifacts

| Profile | ELF | SHA-256 |
| --- | --- | --- |
| `0x1160` logged | `agc_tessellation_fw1160_logged.elf` | `3d6e8457fcc1f8581293b8f212af3ae0035b1cfaafefe7cc54b52bdb281cca10` |
| `0x0550` headless mirror | `agc_tessellation_fw550_headless.elf` | `8c497359b76c8a93fb0e2867995a56b4377bba4b33174e73ccf12b00a52ff74f` |

Both artifacts cross-build without warnings from the current source and
current `libopenagc.a`.

## Fail-closed oracle

The logged runner requires all ordinary exact-profile, Wave32, EOP fence,
marker, RGBA16F readback, shutdown, and final-verdict checks, plus:

- exact variant identity `tessellation`;
- `agcGfx1013DrawTessIndexAuto`/HS+TES+PS binder result `0x00000000`;
- positive mutation counts in both offchip and factor rings.

Run the exact FW 11.60 artifact twice, with the process-cleanup ELF immediately
before each launch and a ps5debug-NG residual-process check afterward. Stop on
the first timeout, ring/readback mismatch, residual process, or console loss.
Only after two passes should combined TES-to-NGG geometry gates be prepared.

The current-source FW 5.50 mirror remains a separate regression requirement
when that console becomes reachable.
