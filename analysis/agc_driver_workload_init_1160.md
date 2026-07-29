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
and caller-owned inline DCB lifecycle, yet stalled. Stage 13 ran after a clean
reboot and restored the two remaining surrounding prerequisites from the
qualified FW 5.50 path: register-default notification and async-graphics setup.
It also repeated the exact process-property call and stream registration. A
normal preflight DCB returned `AGC_OK` and its `0x1160f013` marker completed in
50 ms, proving that ordinary submission and marker visibility were healthy at
that point.

The unchanged 40-dword inline active/marker/complete/marker DCB then returned
`AGC_OK`, but no verdict or shutdown text followed before websrv timed out at
20 seconds. The cleanup ELF subsequently found no stale `eboot.elf`; websrv and
ps5debug-NG port 744 remained reachable. Stage 13 must not be rerun unchanged.
The remaining investigation is the GPU-side `SET_WORKLOAD` state transition or
required queue/register programming, not defaults, async setup, process
property, stream registration, slot address, packet bytes, or cursor lifecycle.

Subsequent constructor tracing recovered a separate standard-console
register-shadow publication path. The corrected ABI, including an ELF
file-offset/vaddr mapping error caught before hardware, is documented in
`analysis/fw1160_register_shadow_20260729.md`. Stage 14 first isolates the
previously partial 40-dword DCB cache flush. Stage 15 adds the recovered
Gn2/Gn3/Gn4 state. Stage 14 reproduced the stall despite the complete flush;
do not repeat it. Corpus extraction subsequently proved the same standard
Gn2/Gn3/Gn4 constructor contract on exact FW 6.00–12.70 keys and the reduced
Gn2-only Trinity branch, as documented in
`analysis/agc_driver_shadow_facts.md`. Stage 15 remains an isolated FW 11.60
hardware gate and does not change the public fail-closed workload capability.
Stage 15 was subsequently run once: every shadow publication returned
`AGC_OK`, the ordinary preflight marker completed, and the workload submit
returned `AGC_OK`, but the inline marker still never advanced. The cleanup ELF
removed the stalled PID afterward. The recovered constructor state is not
sufficient; do not repeat stage 15 unchanged.
