# AGC Packet Model

Current working model from SharpEmu and RPCSX:

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

| Subcommand | Meaning |
|---:|---|
| `0x04` | Draw index auto |
| `0x05` | Draw reset |
| `0x06` | Wait flip done |
| `0x09` | ACB reset |
| `0x0A` | Wait memory 32-bit |
| `0x0B` | Push marker |
| `0x0C` | Pop marker |
| `0x11` | SH registers indirect |
| `0x12` | CX registers indirect |
| `0x13` | UC registers indirect |
| `0x14` | Acquire memory |
| `0x15` | Write data |
| `0x16` | Wait memory 64-bit |
| `0x17` | Flip |
| `0x18` | Release memory |
| `0x19` | DMA data |

Immediate implementation rule:

- Use plain PM4 opcodes for packets confirmed as ordinary packets, such as
  `DISPATCH_INDIRECT`, `EVENT_WRITE`, `SET_SH_REG`, and `MEM_SEMAPHORE`.
- Use `IT_NOP + subcommand` for SharpEmu-confirmed AGC wrapper packets, such as
  flip, acquire memory, write data, wait memory, release memory, and DMA data.

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

SharpEmu parses DCB/ACB submit packets as:

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
