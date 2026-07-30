# FW 11.60 multi-indirect qualification (2026-07-30)

## Scope

This sequence extends the fixed-count Sony ten-dword qualification to three
application-facing cases on the standard PS5 reporting raw firmware
`0x11600005`:

1. non-indexed `draw_count=2`;
2. indexed `draw_count=2`;
3. non-indexed GPU count-buffer selection with maximum two and count two.

Every artifact contains two argument records. Record zero draws the established
centered triangle. Record one selects vertices 3-5, a right-shifted triangle.
The gate therefore cannot pass if the GPU executes only record zero: it
requires changed coverage above the single-draw baseline and a maximum X bound
beyond three quarters of the 1536-pixel target.

## Hardware-tested artifacts

| Gate | ELF SHA-256 | Header | Control | Count/max | Stride |
| --- | --- | ---: | ---: | ---: | ---: |
| non-indexed fixed two | `7153aedcb8ad04aef59318e30efef001604d9f257ca672268336342fb39996cd` | `0xc0082c00` | `0x00000280` | 2 | 16 |
| indexed fixed two | `4af02201846b959426a64fa385fbaf47b40651a7ea004bcb7af1d2afa8ebb926` | `0xc0083800` | `0x00000280` | 2 | 20 |
| non-indexed count buffer | `024bc5f44f43272b4367f2e69fc400c257348378b262d11184a9c56c761508ae` | `0xc0082c00` | `0x40000280` | 2 | 16 |

The count-buffer artifact stored value two at GPU-visible flexible-memory
address `0x200099100`. Its audited packet encoded low/high address dwords
`0x00099100` and `0x00000002`. `draw_count` remained the hardware maximum.

## Guard and oracle

The three deployment targets use the existing file-backed bounded graphics
runner and launch the authenticated process-cleanup ELF immediately before
every payload:

```sh
make -C samples/hw_test deploy_agc_graphics_multi_indirect_fw1160_logged \
  PS5_HOST=10.0.1.39
make -C samples/hw_test deploy_agc_graphics_multi_indexed_indirect_fw1160_logged \
  PS5_HOST=10.0.1.39
make -C samples/hw_test deploy_agc_graphics_count_indirect_fw1160_logged \
  PS5_HOST=10.0.1.39
```

The runner requires the intended draw-path identity, all ten audited dwords,
the distinct second-geometry line, immediate/bounded completion, complete FP16
samples, clean driver and memory teardown, and final PASS. The count-buffer
target additionally requires the GPU-selected-records verdict.

## Result

Each gate passed twice with identical output:

| Oracle | Result, all six runs |
| --- | ---: |
| DCB size, non-indexed/indexed | 2,476 / 2,484 dwords |
| Completion fence | immediate |
| Changed FP16 pixels | 463,430 |
| Coverage bounds | `x=384..1535`, `y=436..1100` |
| Complete samples | 463,430 |
| Out-of-range components | 0 |
| Native packed FNV64 | `0x4352dc6d19dc690f` |
| Driver and memory cleanup | PASS |

Ports 8080 and 2121 remained responsive after the final count-buffer run.
There was no timeout, stale process, UI freeze, GPU reset, or kernel panic.

## Remaining boundary

These results qualify the three tested modes on FW 11.60. Their current-source
FW 5.50 mirrors still require cleanup-first hardware runs when that console is
available. Larger maximum counts, indexed count buffers, and GPU-written count
production in a preceding command buffer are not implied by this result.
