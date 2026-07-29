# PM4 dword packet comparison: SharpEmu, KytyPS5, and OpenAGC

Compared locally on 2026-07-29:

- SharpEmu: `src/SharpEmu.Libs/Agc/AgcExports.cs`
- KytyPS5: `src/libs/agc.cpp` and `src/graphics/guest_gpu/pm4.h`
- OpenAGC: `src/cb_builders.c`, `src/acb.c`, and `src/cb.c`

The external projects are independent ABI references. No source was copied.

## Shared packet and cursor model

All three encode a Gen5 type-3 header as:

```text
0xc0000000 | ((packet_dwords - 2) << 16) | (opcode << 8) | subcommand
```

SharpEmu allocates `packet_dwords`, writes the header with that same count,
then advances `cursor_up` by `packet_dwords * 4`. KytyPS5's `AllocateDW()`
performs the equivalent pointer advance. Both subtract the command buffer's
reserved-dword allowance before permitting an allocation. OpenAGC's
`agcCbAllocDwords()` follows the same rule.

## Multi-argument nine-dword builders

Both references implement the 64-bit form of `sceAgcDcbWaitRegMem` as a
nine-dword NOP-wrapped packet. `sceAgcAcbWaitRegMem` uses the same packet with
the operation fixed to zero.

| Dword | 64-bit wait field |
|---:|---|
| 0 | NOP type-3 header, WAIT_MEM64 subcommand, length 9 |
| 1 | aligned address low (`& ~7`) |
| 2 | address high (`& 0x3ffff`) |
| 3 | mask low |
| 4 | mask high |
| 5 | reference low |
| 6 | reference high |
| 7 | compare/operation/cache control |
| 8 | `min(poll_cycles >> 4, 0xffff)` |

The arguments beyond the sixth SysV integer register are read from the stack;
this does not change the packet dword count. The returned pointer is the old
cursor and the cursor advances by nine dwords.

KytyPS5 also has an unrelated nine-dword `DISPATCH_DRAW_PREAMBLE` builder.
That confirms its general allocation/header convention but is not evidence
that OpenAGC exposes the corresponding Sony export.

## Regressions found and corrected

OpenAGC already had the correct nine-dword DCB layout, but two branches did not
match either reference:

- DCB operations 2 and 3 diverted to a lossy seven-dword raw
  `WAIT_REG_MEM` packet. They now retain the NOP-wrapped 7/9-dword layout for
  all valid operation values 0 through 4.
- The poll field used a bit mask and wrapped large values. It now saturates at
  `0xffff`.

OpenAGC's `sceAgcAcbWaitRegMem` symbol had a fixed-buffer seven-argument ABI
and emitted an unrelated six-dword packet. This conflicted with SharpEmu,
KytyPS5, and OpenAGC's own SharpEmu hardware-facing sample declaration. It now
uses the Sony-style eight-argument cursor ABI and delegates to the DCB encoder
with operation zero.

Host fixtures cover both sizes, operations 1 and 2, 64-bit mask/reference
preservation, poll saturation, header length, and exact nine-dword cursor
advance. These are host/reference-qualified; no new real-console claim is
made.
