# FW 11.60 workload initialization

Source: external FW 11.60 `libSceAgcDriver.sprx` (SHA-256
`8314b5d388445a3b9a23f787c1b752f84fd35887b9bb2af4def4d49c77bfab3c`).
The firmware image is reference-only and is not part of this repository.

## Module memory setup

The helper at vaddr `0x4f0` creates the driver's large shared direct-memory
mapping. Its relevant imports are:

| PLT | Import |
|---:|---|
| `0xad30` | `sceKernelGetDirectMemorySize` |
| `0xad40` | `sceKernelAllocateDirectMemory` |
| `0xad50` | `sceKernelMapNamedDirectMemory` |
| `0xad60` | `memset` |

This is general module memory setup, not a workload enable path. There is no
workload-specific ioctl in it.

## Workload-state initializer

The helper at vaddr `0xa50` is the complete workload-state initializer:

1. Calls the internal region-address helper at `0x690` with region index 2.
2. Calls the internal region-size helper at `0x6c0` with region index 2.
3. Rejects a size below `0x100`, a null base, or a base not aligned to 16
   bytes with `0x8a6c003b`.
4. Stores the region base as the workload table base.
5. Sets registration bit 0 and clears the userspace descriptor storage.
6. Writes the 32-byte stream-0 descriptor beginning with `"System"`.
7. Initializes a userspace mutex with `scePthreadMutexInit`.

For the standard-console FW 11.60 layout, region 2 is the `0x200` bytes at
`SceGnmGpuInfo + 0x3a000`. No ioctl, process-property call, GPU register write,
or allocation occurs in this initializer.

## Builder address operand

The active builder at `0xd10` computes:

```text
table_base + stream_id * 8
```

at `0xdfe..0xe0b` and stores that address directly in dwords 2 and 3 of the
final nine-dword `SET_WORKLOAD` packet. The complete builder repeats the same
calculation at `0xf09..0xf20`. Neither builder dereferences the 64-bit slot
before emitting the packet.

Therefore OpenAGC's address operand is correct: stream 1 uses the GPU virtual
address `SceGnmGpuInfo + 0x3a008`. The zero-initialized value in that slot is
GPU output/state, not a pointer that userspace must populate.

## Consequence for requalification

Stage 12 already matched the table address, registration state, packet bytes,
and caller-owned inline DCB lifecycle, yet stalled. The remaining observable
difference from the qualified FW 5.50 full-path sequence is surrounding driver
state. Stage 13 therefore restores two independently FW 11.60-qualified
prerequisites—register-default notification and async-graphics setup—and proves
ordinary marker execution before submitting the unchanged inline workload
DCB. It must not run until the console has rebooted after the stage-12 stall.
