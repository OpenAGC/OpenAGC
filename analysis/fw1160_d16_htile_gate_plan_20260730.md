# FW 11.60 D16 HTILE gate plan

## Prerequisite boundary

The next compressed-depth tier is isolated D16 plus HTILE, followed only after
success by D16 HTILE expclear. This preserves the FW 5.50 qualification order:
uncompressed D16, ordinary compressed D16/HTILE with expclear disabled, then
the expclear variant.

FW 11.60 uncompressed D16 already passed twice with the current full-rectangle
viewport, but project policy also requires the matching modern headless FW
5.50 D16 regression before any compressed FW 11.60 payload. The FW 5.50
console remains offline. The artifacts below are therefore build-qualified but
must not be launched yet.

## Exact artifacts

| Firmware | Gate | SHA-256 |
| --- | --- | --- |
| 11.60 | D16 + ordinary HTILE, logged | `e41556e119341376cdee8bfadfbc27a8be71e10ef60eb402955174bbf33a3354` |
| 11.60 | D16 + HTILE expclear, logged | `aa5f81c013a87ec9fe135853d2a4183cfcde19bf91127eb7e35611a151219a2d` |
| 5.50 | D16 + ordinary HTILE, headless mirror | `e6c79acc6cb4d96e67599c3a79d2ab0e4b971a6c8e7ac60774e14e2f4dbfb1ee` |
| 5.50 | D16 + HTILE expclear, headless mirror | `f5a6e1c2f621e9f00d98b088a951ed7787d9d215bb2624d6c56d386e9ea6ef47` |

All four compile without warnings. The FW 11.60 pair forces ABI key `0x1160`,
uses the standard version-12 defaults and file-backed daemon transport, rejects
Trinity, and self-terminates. The mirrors force `0x0550` and use the same
current headless allocation, viewport, PM4, native readback, and shutdown code.

## Isolated state

The ordinary gate enables `D16_UNORM`, pipe-aligned HTILE, typed
decompression/resummarization, and the canonical D16 uncompressed metadata
word `0xfffc000f`. It keeps stencil, MSAA, and expclear disabled.

The follow-up changes only the isolated expclear controls: canonical
depth-one metadata `0xfffffff0`, `DB_DEPTH_CLEAR=1.0`, D16
`ZRANGE_PRECISION=1`, and the proven typed HTILE operation sequence. Stencil
and MSAA remain disabled.

## Guarded oracles

The firmware-selectable depth runner now uses the same detached cleanup
preflight as the graphics runner. Logged FW 11.60 gates delete the previous
result, launch headlessly through daemon mode, poll FTP for a fresh final
verdict, and reject missing or partial evidence.

Both compressed gates must report:

- exact firmware profile selection and a bounded completion fence;
- all four ordered depth markers and final marker;
- exactly 228,096 green and 228,096 red pixels under the full rectangle;
- exact native D16 classes: 1,617,408 clear-one, 228,096 near, 228,096 far;
- positive HTILE mutation from the gate-specific initial word;
- driver shutdown PASS, final graphics PASS, and no failure text;
- no residual `eboot.elf` or kernel panic, bad-packet, page-fault, reset, or
  watchdog signature.

The first ordinary run must establish the new full-rectangle HTILE changed-word
count. After it succeeds, freeze that count in the runner before the second
run. Apply the same rule to expclear rather than assuming the retained
display-backed FW 5.50 counts (4,226 and 49,152) survive the viewport change.

## Hardware order

1. Bring FW 5.50 online and run the already-built exact uncompressed D16,
   S8-only, and D16+S8 headless regressions.
2. If those pass, run the exact FW 5.50 ordinary D16/HTILE mirror once to prove
   the current headless fixture did not regress compressed depth.
3. Reboot FW 11.60, reinject ps5debug-NG, and run ordinary D16/HTILE twice,
   freezing its exact HTILE count after pass 1.
4. Only then run D16 HTILE expclear twice with the same freeze-after-pass-1
   rule.
5. Run the exact FW 5.50 expclear mirror before cross-firmware promotion.

Stop on the first stall, mismatch, fault signature, or residual process. A GPU
stall requires a full reboot before any further gate.

The FW 5.50 uncompressed prerequisite has passed. The current-source ordinary
D16/HTILE ELF was then rebuilt twice with identical SHA-256
`a3f85b521571cd5249f00daffb94e4b5e1d3f32334fe48339694cb469c4c58df`,
verified to avoid both AGC SPRX dependencies, and preserved under its full hash
before execution. Its deploy recipe rejects changed bytes before network
access. This artifact is the next and only permitted compressed-depth launch.
