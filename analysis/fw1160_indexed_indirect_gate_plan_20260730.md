# FW 11.60 indexed and indirect draw gate plan (2026-07-30)

## Scope

FW 5.50 hardware-qualified three application-facing gfx1013 draw paths that
are not yet independently qualified on FW 11.60:

1. direct u16 indexed (`DRAW_INDEX_2`);
2. non-indexed indirect (`SET_BASE` plus indirect arguments);
3. u16 indexed-indirect (`SET_BASE`, index state, and indexed arguments).

The ordinary FW 11.60 baseline already proves shader binding, direct queue
submission, an EOP fence, and RGBA16F readback. These gates isolate only the
draw-composition variants. They remain headless, use the same 1536x1536
RGBA16F target and exact coverage/value oracle, and self-terminate after a
clean driver shutdown.

## Exact artifacts

| Profile | Draw path | ELF | SHA-256 |
| --- | --- | --- | --- |
| `0x1160` logged | direct u16 indexed | `agc_graphics_indexed_fw1160_logged.elf` | `7d30374aa51c01b167ef2b5d2fee1269ccf1f55319da949ffd8dc1c4949b7d81` |
| `0x1160` logged | non-indexed indirect | `agc_graphics_indirect_fw1160_logged.elf` | `79f9c3a1a4097b5ee1f58fa6c38c46ed1ff6def9fff2633dde431ad47de2d41b` |
| `0x1160` logged | u16 indexed-indirect | `agc_graphics_indexed_indirect_fw1160_logged.elf` | `096fc7ecf8c49f74c79152a93c2155f32fcf9960014b3c74225fa39deda3acfe` |
| `0x0550` headless mirror | direct u16 indexed | `agc_graphics_indexed_fw550_headless.elf` | `79e6bc94b23911b4d23c13ff78908c1f563b20944ed14d4885de2b1643955703` |
| `0x0550` headless mirror | non-indexed indirect | `agc_graphics_indirect_fw550_headless.elf` | `ebd70a634f44d8c6f0040ad2a2b7d9bc214f5f22c40e3c91c39e6ffcf5d6a758` |
| `0x0550` headless mirror | u16 indexed-indirect | `agc_graphics_indexed_indirect_fw550_headless.elf` | `c223f3901ec6f25a77614f3dca7687b3bf3fb1c1e0904ce793b280165c1e3262` |

All six artifacts cross-build without warnings from the current source and
current `libopenagc.a`. Firmware blobs are neither linked nor embedded.

## Fail-closed runner contract

`run_fw1160_graphics.sh` now accepts `EXPECTED_DRAW_MODE`. When present, the
verdict must contain the exact intended draw-composition line with
`0x00000000`; a generic graphics PASS from a wrong compile-time path is
rejected. Existing checks still require:

- the exact four-digit runtime ABI key;
- a bounded GPU completion fence;
- the RGBA16F target and full coverage/value PASS oracle;
- driver shutdown PASS and final graphics PASS;
- no failure, fatal, mismatch, or timeout text.

The FW 11.60 targets use the proven file-backed daemon-loader verdict. Every
launch is immediately preceded by the established process-cleanup ELF. After
each verdict, ps5debug-NG must report no residual `eboot` and ports 744 and
8080 must remain responsive.

## Execution order

Run direct indexed twice, then non-indexed indirect twice, then
indexed-indirect twice. Stop on the first mismatch, fence timeout, residual
process, or loss of console responsiveness. The historical noncanonical
`SET_BASE` low control modifier must not be reintroduced; current exact host
fixtures require control zero.

The matching current-source FW 5.50 mirrors remain a separate regression
requirement when that console is available. FW 11.60 evidence alone does not
promote cross-firmware support.
