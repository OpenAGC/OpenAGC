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
| `0x1160` logged | non-indexed indirect | `agc_graphics_indirect_fw1160_logged.elf` | `fc7d66a21223648db06a34bc38f5136208d95c9fc2c5444e57aef36101852896` |
| `0x1160` logged | u16 indexed-indirect | `agc_graphics_indexed_indirect_fw1160_logged.elf` | `c4cdc1ced26e3572235f5c5fca1a7b36f6f77f18de1423c80f562bbaf262d343` |
| `0x0550` headless mirror | direct u16 indexed | `agc_graphics_indexed_fw550_headless.elf` | `79e6bc94b23911b4d23c13ff78908c1f563b20944ed14d4885de2b1643955703` |
| `0x0550` headless mirror | non-indexed indirect | `agc_graphics_indirect_fw550_headless.elf` | `e2bbf7b1bd8f0d1f476147b9c2d9baf43fbe650df7e7cdd569e784da882d47f3` |
| `0x0550` headless mirror | u16 indexed-indirect | `agc_graphics_indexed_indirect_fw550_headless.elf` | `1ca79d68c0cbda0ce4be5a938d0b67556d749cabf0ea0a3796cb8eed8c300e65` |

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

## FW 11.60 hardware result

All three variants passed twice on the standard PS5 reporting raw firmware
`0x11600005`:

| Draw path | DCB dwords | Fence, both runs | Complete FP16 pixels | Native FNV64 |
| --- | ---: | ---: | ---: | --- |
| direct u16 indexed | 2,473 | immediate | 255,744 | `0x4a40c2eb4f12bc26` |
| non-indexed indirect, Sony default | 2,476 | immediate | 255,744 | `0x4a40c2eb4f12bc26` |
| u16 indexed-indirect, Sony default | 2,484 | immediate | 255,744 | `0x4a40c2eb4f12bc26` |

Every run also reproduced the exact 768x665 coverage bounds, eight sampled
colors, zero incomplete or out-of-range components, the Wave32 PM4 audit,
`0xdeadcafe` marker, clean driver shutdown, and final graphics PASS.
ps5debug-NG found no residual `eboot` between or after the runs; ports 744 and
8080 remained reachable.

This independently hardware-qualifies the three current draw-composition
variants on the tested FW 11.60 console. The indirect rows now use the
recovered, byte-distinct Sony 10-dword default. This does not qualify
count-buffer multi-draw, draw counts greater than one, or the rejected
Mesa-style 10-dword packet form.

## Execution policy

The completed run order was direct indexed twice, non-indexed indirect twice,
then indexed-indirect twice. The historical noncanonical `SET_BASE` low
control modifier must not be reintroduced; current exact host fixtures require
control zero.

The matching current-source FW 5.50 mirrors remain a separate regression
requirement when that console is available. FW 11.60 evidence alone does not
promote cross-firmware support.
