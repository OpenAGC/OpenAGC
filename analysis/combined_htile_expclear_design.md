# Combined Stencil/HTILE Expclear Design

## Status

Design complete; hardware gate disabled.

`AGC_GFX1013_COMBINED_HTILE_EXPCLEAR_ENABLED` is zero. The dedicated
`AGC_STENCIL_HTILE_EXPCLEAR_VALIDATION` PS5 fixture gate cannot compile while
that constant is zero. This document does not claim that combined expclear is
safe or functional on PS5 hardware.

## gfx10 aspect ownership

For the non-VRS gfx10 combined depth/stencil HTILE word:

- Depth mask: `0xfffff00f`
- Stencil mask: `0x000003f0`
- Reserved bits 10-11: outside both masks and always preserved

The masks follow the gfx10 `zmask`, `zdelta`, `zbase`, `sr0`, `sr1`, and
`smem` field ownership used by Mesa. The actual S8 clear value is not encoded
in HTILE; it belongs in `DB_STENCIL_CLEAR`. HTILE stores the cleared stencil
pretest state (`0x000000f0` under the stencil mask).

Canonical combined values are:

| Aspects | Depth | Mask | Value before merge |
| --- | ---: | ---: | ---: |
| depth | 0.0 | `0xfffff00f` | `0x00000000` |
| depth | 1.0 | `0xfffff00f` | `0xfffc0000` |
| stencil | any | `0x000003f0` | `0x000000f0` |
| depth + stencil | 0.0 | `0xfffff3ff` | `0x000000f0` |
| depth + stencil | 1.0 | `0xfffff3ff` | `0xfffc00f0` |

Every combined update uses:

```c
new_word = (old_word & ~write_mask) | (write_value & write_mask);
```

This preserves an unselected aspect and reserved bits. For example, applying a
depth-one clear to the hardware-proven ordinary combined word `0xfffff30f`
produces `0xfffc0300`; applying only the stencil clear produces `0xfffff0ff`.

## Public plan contract

`agcGfx1013BuildHtileExpclearPlan` is a pure validation/planning helper. It
does not write metadata or emit PM4.

- A Z-only depth plan returns the existing hardware-proven full-word value,
  full mask, no RMW requirement, and `hardware_enabled=1`.
- Any plan for an HTILE surface containing stencil returns exact masked state,
  `requires_read_modify_write=1`, and `hardware_enabled=0`.
- Invalid aspects, noncanonical depth, an absent stencil aspect, or an S8 clear
  outside 0-255 reject without modifying the output plan.

## Implemented RMW stage

`agcGfx1013RmwHtile` and `htile_rmw.comp` now implement steps 2 through 4
below as one atomically preflighted command sequence. The composer accepts only
an exact ordinary-mip/layer range returned by the HTILE layout API; shared mip
tails and ranges outside the allocation reject. Its shader record is fixed to
seven user SGPRs plus TGID_X and receives address, word count, value, and mask.
This generic metadata operation does not change the combined expclear enable
gate by itself.

## Hardware sequence

Independent validation must use an isolated fixture and this order:

1. Bind a combined D32+S8 surface with ordinary combined HTILE already proven.
2. Release prior DB depth/stencil writes and make HTILE visible to compute.
3. Run a bounded compute RMW over only the exact selected HTILE subresource.
4. Flush compute writes and acquire them for DB metadata use.
5. Program `DB_DEPTH_CLEAR` and `DB_STENCIL_CLEAR` for the selected aspects.
6. Enable the matching depth/stencil expclear bits only after metadata is ready.
7. Draw independent depth and stencil probes.
8. Decompress and read back exact D32, S8, and HTILE results.

A CPU-side rewrite is not an acceptable final GPU API path because it does not
establish the required GPU cache and ordering contract.

## Activation criteria

The public enable constant may change to one only after all of these pass on a
fresh FW `0x0550` console session:

- Depth-only masked clear preserves every stencil bit and yields exact D32.
- Stencil-only masked clear preserves every depth bit and yields exact S8.
- Two-aspect clear yields exact D32 and S8 simultaneously.
- Reserved bits and metadata outside the selected mip/layer remain unchanged.
- EOP fence and all ordered markers complete within the bounded timeout.
- Each case passes twice sequentially with no GPU reset, page fault, kernel
  panic, UI crash, loader overlap, or instant close.
- The final display preview completes 1,800/1,800 flips.

Until that evidence exists, callers must receive and honor
`hardware_enabled=0`; ordinary combined compression and depth-only expclear do
not qualify this path.
