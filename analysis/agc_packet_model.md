# AGC Packet Model

Current working model from HLE reference and RPCSX:

```c
header =
    0xC0000000u |
    (((length_dwords - 2u) & 0x3FFFu) << 16) |
    ((opcode & 0xFFu) << 8) |
    ((subcommand & 0x3Fu) << 2);
```

Decode:

```c
type = header >> 30;
length_dwords = ((header >> 16) & 0x3FFFu) + 2u;
opcode = (header >> 8) & 0xFFu;
subcommand = (header >> 2) & 0x3Fu;
```

Known AGC subcommands carried by `IT_NOP`:

| Subcommand | Meaning | Status |
|---:|---|---|
| `0x04` | Draw index auto | **DEPRECATED** — use `IT_DRAW_INDEX_AUTO (0x2D)` directly (reference-confirmed) |
| `0x05` | Draw reset | Active |
| `0x06` | Wait flip done | Active |
| `0x09` | ACB reset | Active |
| `0x0A` | Wait memory 32-bit | Active |
| `0x0B` | Push marker | Active |
| `0x0C` | Pop marker | Active |
| `0x11` | SH registers indirect | **DEPRECATED** — use `IT_SET_SH_REG_INDIRECT (0x63)` directly (SPRX+reference-confirmed) |
| `0x12` | CX registers indirect | **DEPRECATED** — use `IT_SET_CONTEXT_REG_INDIRECT (0x9F)` directly (SPRX+reference-confirmed) |
| `0x13` | UC registers indirect | **DEPRECATED** — use `IT_SET_UCONFIG_REG_INDIRECT (0x64)` directly (SPRX+reference-confirmed) |
| `0x14` | Acquire memory | Active |
| `0x15` | Write data | Active (but `IT_WRITE_DATA (0x37)` is also valid — the reference uses the direct opcode) |
| `0x16` | Wait memory 64-bit | Active |
| `0x17` | Flip | Active |
| `0x18` | Release memory | Active |
| `0x19` | DMA data | Active |

Immediate implementation rule:

- Use plain PM4 opcodes for packets confirmed as ordinary PM4 packets, such as
  `DRAW_INDEX_AUTO`, `DISPATCH_INDIRECT`, `EVENT_WRITE`, `SET_SH_REG`,
  `SET_SH_REG_INDIRECT`, `SET_CONTEXT_REG_INDIRECT`, `SET_UCONFIG_REG_INDIRECT`,
  `WRITE_DATA`, `PFP_SYNC_ME`, `SET_PREDICATION`, and `MEM_SEMAPHORE`.
  These are confirmed by SPRX disassembly and/or the reference implementation.
- Use `IT_NOP + subcommand` for AGC-custom wrapper packets that do not have
  standard PM4 opcode equivalents, such as flip, acquire memory, wait memory,
  release memory, DMA data, push/pop marker, and draw reset.
- Do NOT use `IT_NOP + subcommand` for packets that have real PM4 opcodes
  (e.g. DrawIndexAuto, indirect register setters, WriteData, StallParser).
  The real opcodes are required for real PS5 hardware compatibility.

## Command Buffer Cursor

Recovered cursor offsets used by `SceAgcCb`:

| Offset | Field |
|---:|---|
| `0x10` | cursor up / write cursor |
| `0x18` | cursor down / end cursor |
| `0x20` | callback |
| `0x30` | reserved dwords |

Allocation rule:

```c
remaining = ((cursor_down - cursor_up) / 4) - reserved_dwords;
command = cursor_up;
cursor_up += size_dwords * 4;
```

Implemented cursor-based builders:

- `sceAgcCbNop`
- `sceAgcCbDispatch`
- `sceAgcCbSetShRegistersDirect`
- `sceAgcDcbWriteData`
- `sceAgcDcbWaitRegMem`
- `sceAgcDcbDmaData`
- `sceAgcDcbSetBaseIndirectArgs`
- `sceAgcDcbDispatchIndirect`
- `sceAgcDcbSetIndexBuffer`
- `sceAgcDcbDrawIndexOffset`
- `sceAgcDcbDrawIndexAuto`
- `sceAgcDcbWaitUntilSafeForRendering`
- `sceAgcDcbPushMarker`
- `sceAgcDcbPopMarker`
- `sceAgcDcbSetFlip`

## Submit Descriptor

HLE reference parses DCB/ACB submit packets as:

| Offset | Field |
|---:|---|
| `0x00` | command buffer address |
| `0x08` | dword count |
| `0x0C` | reserved |

openagc represents this as:

```c
typedef struct {
    uintptr_t command_address;
    uint32_t dword_count;
    uint32_t reserved;
} AgcCommandBufferSubmit;
```

Implemented generic submit entry points:

- `sceAgcDriverSubmitDcb(const AgcCommandBufferSubmit *packet)`
- `sceAgcDriverSubmitAcb(uint32_t owner_handle, const AgcCommandBufferSubmit *packet)`

## Verification Coverage

Host tests assert:

- `SceAgcCb` offset and size layout
- PM4 type/opcode/length/subcommand decode helpers
- Known NID constants for mapped exports
- Cursor allocation and cursor advancement
- Packet shapes for implemented `sceAgcCb*` / `sceAgcDcb*` builders
- DCB/ACB submit descriptor offset and size layout
- Generic submit validation and debug capture
