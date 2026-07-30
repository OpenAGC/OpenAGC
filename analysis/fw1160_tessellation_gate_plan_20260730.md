# FW 11.60 isolated tessellation gate plan (2026-07-30)

## Scope

This gate carries the FW 5.50-qualified Wave32 HS+TES+PS path to standard FW
11.60 without geometry-shader, HTILE, MSAA, or workload state. It exercises
the already-qualified FW 11.60 TF-ring carrier with real factor-ring and
offchip-ring GPU traffic rather than only accepting the ring address.

## Exact artifacts

| Profile | ELF | SHA-256 |
| --- | --- | --- |
| `0x1160` logged | `agc_tessellation_fw1160_logged.elf` | `765b7103f7a7ac3bc5c87ce8dd6e562ac194a5b504a1d9336ff5ff2934987039` |
| `0x0550` headless mirror | `agc_tessellation_fw550_headless.elf` | `99f5bbfc0ba7d1a897f6d1a9db24b30c96b725b1405d5aff68ff58bca31eb371` |

Both artifacts cross-build without warnings from the current source and
current `libopenagc.a`.

## Fail-closed oracle

The logged runner requires all ordinary exact-profile, Wave32, EOP fence,
marker, RGBA16F readback, shutdown, and final-verdict checks, plus:

- exact variant identity `tessellation`;
- `agcGfx1013DrawTessIndexAuto`/HS+TES+PS binder result `0x00000000`;
- positive offchip-ring mutation;
- exactly four nonzero factor-ring words across the entire ring, all equal to
  IEEE-754 `4.0f` (`0x40800000`).

The first two exploratory runs showed why the whole-ring scan is required:
both had exactly four changed factor words, but the active slot rotated away
from indices 0-3 on the second run. First-word sampling is therefore not a
stable oracle. Those exploratory runs do not count toward the strengthened
two-pass result.

Run the exact FW 11.60 artifact twice, with the process-cleanup ELF immediately
before each launch and a ps5debug-NG residual-process check afterward. Stop on
the first timeout, ring/readback mismatch, residual process, or console loss.
Only after two passes should combined TES-to-NGG geometry gates be prepared.

The current-source FW 5.50 mirror remains a separate regression requirement
when that console becomes reachable.
