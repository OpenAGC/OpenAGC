# FW 11.60 Sony multi-indirect qualification (2026-07-30)

## Scope

This gate isolates the recovered Sony `sceAgcDcbDrawIndirectMulti` public ABI
from both OpenAGC's established application-facing 5/7-dword path and the
historical failed Mesa-style 10-dword experiment. It tests one fixed-count,
non-indexed draw on the standard PS5 reporting raw firmware `0x11600005`.

The ordinary proven graphics composer first emits the complete shader, frame,
resource, `SET_BASE`, and five-dword indirect stream. The probe verifies the
final packet header, rewinds only that packet, and replaces it through the
public Sony-compatible builder. The resulting ten dwords must exactly equal:

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
- SHA-256: `bb577a3616c1820278322b1e0b1563f230265672bbbb50e5a5cc1d834cd2734e`
- Cleanup SHA-256: `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`
- Target: `deploy_agc_graphics_sony_multi_indirect_fw1160_logged`

The target uses the established detached process-cleanup ELF immediately
before every launch, a file-backed verdict, a 200 ms GPU-fence bound, complete
driver and memory teardown, and self-termination. The runner rejects a generic
graphics PASS unless the exact ten-dword audit line is also present.

## Hardware result

Two independent cleanup-first runs passed:

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

## Promotion boundary

This qualifies only fixed-count non-indexed Sony multi-indirect on FW 11.60.
It does not qualify the indexed opcode, indirect count-buffer control, more
than one draw, or another firmware. Keep the established application-facing
5/7-dword path as the default until the exact fixed-count packet passes on FW
5.50. Test indexed and count-buffer variants as separate gates afterward.
