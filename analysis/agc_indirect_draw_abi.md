# Indirect-draw public ABI recovery

OpenAGC's original low-level indirect builders exposed the argument fields of
an application-specific packet path directly. Primary `libSceAgc.sprx`
disassembly shows that those declarations were not the Sony public ABI. The
recovered public exports are now kept separate from the already
hardware-qualified application-facing gfx1013 path.

## Reproducible corpus evidence

Run:

```sh
tools/verify_agc_indirect_draw_facts.sh /Volumes/Untitled/unp
```

The verifier reads the 39 active profiles in `agc_firmware_versions.tsv`,
locates each profile's matching `libSceAgc.sprx` and
`libSceAgcDriver.sprx`, validates semantic instruction patterns, and compares
the result with `agc_indirect_draw_facts.tsv`. Firmware binaries remain
external inputs and are not copied into the repository.

All 39 profiles from FW 3.20 through FW 12.70 export the same NIDs and public
signatures:

| Export | NID | Sony arguments after `cb` | Core | GetSize |
| --- | --- | --- | ---: | ---: |
| `sceAgcDcbDrawIndirect` | `1q1titRBL6o` | `u32 data_offset, u64 modifier` | 5 dwords | 20 bytes |
| `sceAgcDcbDrawIndexIndirect` | `t1vNu082-jM` | `u32 data_offset, u64 modifier` | 5 dwords | 20 bytes |
| `sceAgcDcbDrawIndirectMulti` | `kUlvghKs-mA` | `u32 data_offset, u32 count_indirect, u32 max_count_or_count, ptr count_address, u32 stride, u64 modifier` | 10 dwords | 64 bytes |
| `sceAgcDcbDrawIndexIndirectMulti` | `ypVBz4uPKcQ` | same multi arguments | 10 dwords | 64 bytes |

The former `sceAgcDcbDrawIndirect` comment incorrectly assigned NID
`1rZSWUv1IRc`; that NID belongs to `sceAgcDcbCopyData`. The corrected NID is
present in the existing FW 5.50 NID table.

## Modifier and packet layout

Let `base` be `0x10c` when modifier bits 29-31 equal 3 or 5, otherwise
`0x08c`. An enabled register-location field is `base` plus the corresponding
five-bit value; a disabled field is `0x280`.

- bit 0 enables modifier bits 9-13 for the first register location;
- bit 1 enables bits 14-18 for the indexed second location;
- bit 2 enables bits 19-23 for the high register location;
- bit 3 enables bits 24-28 in the multi control dword;
- bit 4 maps to multi control bit 27;
- indexed bit 1 also sets the DrawIndex control bit;
- `count_indirect & 1` maps to multi control bit 30;
- modifier bit 3 maps to multi control bit 31;
- modifier bit 8 maps to initiator bit 5 unless modifier bit 32 suppresses
  modifier-controlled initiator fields.

The multi packet stores `max_count_or_count` in dword 5, the four-byte-aligned
count address in dwords 6-7, stride in dword 8, and initiator in dword 9.
Both headers encode ten dwords: `0xc0082c00` and `0xc0083800`.

FW 3.20 has one recovered semantic difference: when modifier bit 32 is clear,
modifier bits 5-7 are additionally copied to initiator bits 29-31. FW 4.00
through FW 12.70 omit those three legacy bits. OpenAGC selects this distinction
from the normalized runtime ABI key; it does not use a numeric range fallback.

## User-data wrappers and cursor accounting

Sony's multi builders reserve
`2 * sceAgcDriverUserDataGetPacketSize(0) + 10` dwords, which is 16 dwords or
64 bytes for every active profile. They call the user-data immediate-packet
entry before and after the core with tags 8/5 and 0 respectively. That driver
entry is an unconditional zero-dword stub throughout the active corpus, so a
successful call advances the cursor by ten dwords, not sixteen. OpenAGC
therefore preflights the full 16-dword maximum atomically and emits the exact
ten-dword core. A 10-15-dword destination is rejected without changing its
cursor or contents.

## Qualification boundary

The fixed-count non-indexed Sony-compatible ten-dword export is
hardware-qualified on FW 11.60. Its isolated gate passed twice with exact
packet auditing, an immediate completion fence, the complete FP16 render
oracle, clean teardown, and responsive loader services afterward. Indexed and
count-buffer forms remain SPRX-qualified and host-tested only.

The earlier FW 5.50 failure involved a different, Mesa-style ten-dword
experiment and does not validate or invalidate these recovered Sony bytes.
Until the exact Sony form also passes on FW 5.50, the application-facing
`agcGfx1013DrawBaselineIndirect` path deliberately retains its separately
qualified fixed-count 5/7-dword packets. Existing exact 45/55-dword stream
fixtures protect that separation from regression. See
`fw1160_sony_multi_indirect_qualification_20260730.md` for the hardware record.
