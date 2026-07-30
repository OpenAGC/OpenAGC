# FW 11.60 Sony multi-indirect qualification (2026-07-30)

## Scope

This gate isolates the recovered Sony `sceAgcDcbDrawIndirectMulti` public ABI
from the historical failed Mesa-style 10-dword experiment. It tests the
fixed-count application default on the standard PS5 reporting raw firmware
`0x11600005`.

The initial diagnostic replaced only the former five-dword tail after the
ordinary composer built the complete prefix. After that isolated gate passed,
`agcGfx1013DrawBaselineIndirect` was promoted to call the Sony multi builder
directly. The current probe no longer rewinds or replaces anything: it invokes
the ordinary default compositor and audits its final ten dwords, which must
exactly equal:

```text
c0082c00 00000000 0000008f 00000090 00000280
00000001 00000000 00000000 00000010 00000002
```

This represents argument offset zero, GS user-data base-vertex and
start-instance locations `0x08f` and `0x090`, disabled draw-index/count-buffer
control, fixed count one, null ignored count address, 16-byte stride, and draw
initiator two.

## Guarded artifact

- ELF: `samples/hw_test/agc_graphics_sony_multi_indirect_fw1160_logged.elf`
- SHA-256: `e353d550b144d28062ef68bc0867b9f60cbcd47acdb8d61ab8b8c8808e1c68fe`
- Cleanup SHA-256: `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`
- Target: `deploy_agc_graphics_sony_multi_indirect_fw1160_logged`

The target uses the established detached process-cleanup ELF immediately
before every launch, a file-backed verdict, a 200 ms GPU-fence bound, complete
driver and memory teardown, and self-termination. The runner rejects a generic
graphics PASS unless the exact ten-dword audit line is also present.

## Hardware result

Two initial isolated replacement runs and two subsequent default-path runs
passed. The table records the two current default-path runs:

| Oracle | Run 1 | Run 2 |
| --- | ---: | ---: |
| Exact ten-dword audit | PASS | PASS |
| DCB size | 2,476 dwords | 2,476 dwords |
| Completion fence | immediate | immediate |
| Changed FP16 pixels | 255,744 | 255,744 |
| Coverage bounds | 768x665 | 768x665 |
| Complete samples | 255,744 | 255,744 |
| Native packed FNV64 | `0x4a40c2eb4f12bc26` | `0x4a40c2eb4f12bc26` |
| Driver/memory cleanup | PASS | PASS |

After the second run, TCP ports 8080 and 2121 remained responsive. There was
no fence timeout, stale process symptom, UI freeze, GPU reset, or kernel panic.

The fixed-count indexed default was then run twice through
`deploy_agc_graphics_indexed_indirect_fw1160_logged`. Both 2,484-dword DCBs
reached the fence immediately, changed the same 255,744 FP16 pixels, matched
FNV64 `0x4a40c2eb4f12bc26`, and shut down cleanly. Its artifact SHA-256 was
`c4cdc1ced26e3572235f5c5fca1a7b36f6f77f18de1423c80f562bbaf262d343`.

## Promotion boundary

The Sony ten-dword multi packet is now the application default for single and
multiple, indexed and non-indexed indirect draws. Hardware evidence currently
qualifies fixed-count non-indexed and indexed forms on FW 11.60. The subsequent
`draw_count=2` and count-buffer gates are recorded in
`fw1160_multi_indirect_qualification_20260730.md`. Repeat all exact current
artifacts on FW 5.50 when available.
