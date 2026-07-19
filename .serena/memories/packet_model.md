# AGC PM4 Packet Model

## Type-3 header (Gen5 AGC, from SharpEmu/RPCSX)
```c
header = 0xC0000000u |
    (((length_dwords - 2u) & 0x3FFFu) << 16) |
    ((opcode & 0xFFu) << 8) |
    ((subcommand & 0x3Fu) << 2);
```
Decode:
```c
type = header >> 30;            // always 3
length_dwords = ((header >> 16) & 0x3FFFu) + 2u;
opcode = (header >> 8) & 0xFFu;
subcommand = (header >> 2) & 0x3Fu;
```
**CRITICAL**: `length_dwords - 2` goes in bits 29:16. Always use `agcPm4Header3()` / `agcPm4Header3Sub()` from `agc_pm4.h` — never hand-pack.

## IT_NOP subcommands (AGC wrappers)
| Sub | Meaning |
|---:|---|
| 0x04 | Draw index auto |
| 0x05 | Draw reset |
| 0x06 | Wait flip done |
| 0x09 | ACB reset |
| 0x0A | Wait memory 32-bit |
| 0x0B | Push marker |
| 0x0C | Pop marker |
| 0x11 | SH registers indirect |
| 0x12 | CX registers indirect |
| 0x13 | UC registers indirect |
| 0x14 | Acquire memory |
| 0x15 | Write data |
| 0x16 | Wait memory 64-bit |
| 0x17 | Flip |
| 0x18 | Release memory |
| 0x19 | DMA data |

Rule: plain PM4 opcodes for confirmed ordinary packets (DISPATCH_INDIRECT, EVENT_WRITE, SET_SH_REG, MEM_SEMAPHORE). `IT_NOP + subcommand` for SharpEmu-confirmed AGC wrappers (flip, acquire mem, write data, wait mem, release mem, DMA data).

## Cursor model (SceAgcCb)
| Offset | Field |
|---:|---|
| 0x10 | cursor up / write cursor |
| 0x18 | cursor down / end cursor |
| 0x20 | callback |
| 0x30 | reserved dwords |

`agcCbAllocDwords(cb, n)` advances cursor and returns write ptr. Builders must NULL-check and bail cleanly.
```c
remaining = ((cursor_down - cursor_up) / 4) - reserved_dwords;
command = cursor_up;
cursor_up += size_dwords * 4;
```

## Submit descriptor (AgcCommandBufferSubmit)
| Offset | Field |
|---:|---|
| 0x00 | command buffer address |
| 0x08 | dword count |
| 0x0C | reserved |

Generic submit: `sceAgcDriverSubmitDcb(const AgcCommandBufferSubmit *)`, `sceAgcDriverSubmitAcb(uint32_t owner_handle, const AgcCommandBufferSubmit *)`.

## Verified PM4 opcodes (SPRX-confirmed)
SET_CONFIG_REG=0x68, SET_SH_REG=0x76, SET_UCONFIG_REG=0x79, WRITE_DATA=0x37, RELEASE_MEM=0x49, INDIRECT_BUFFER=0x3F, EVENT_WRITE=0x46, ACQUIRE_MEM=0x58, WAIT_REG_MEM=0x3C, COPY_DATA=0x40, DMA_DATA=0x50, COND_EXEC=0x22, MEM_SEMAPHORE=0x39, ATOMIC_MEM=0x1B, ATOMIC_GDS=0x1D, SET_WORKLOAD=0x1E, CLEAR_STATE=0x14, REWIND=0x59, PRIME_UTCL2=0x5D, DRAW_INDEX_2=0x27, NUM_INSTANCES=0x2F, INDEX_BUFFER_SIZE=0x13.

## AGC-custom flip opcodes (libSceAgc only)
0x4C=WAIT_FLIP v1, 0x4E=WAIT_FLIP v2, 0x4F=WAIT_FLIP v3 (two opcodes emitted), 0x51=WAIT_FLIP v4, 0x54=INSERT_WAIT_FLIP_DONE (sub-field 0x06).