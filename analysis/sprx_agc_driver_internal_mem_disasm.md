# SPRX libSceAgcDriver — Internal Memory & Queue Create Disassembly (FW 5.50)

Disassembled from `libSceAgcDriver.sprx` (decrypted, FW 5.50).

## PLT Function Map (key imports from libkernel.prx)

| PLT addr | NID | Function |
|----------|-----|----------|
| 0xab70 | MpxhMh8QFro | (unknown — used in init) |
| 0xabc0 | 8zTFvBIAIN8 | `memset` |
| 0xabe0 | OMDRKKAZ8I4 | `sceKernelDebugRaiseException` |
| 0xace0 | RW-GEfpnsqg | `kevent` |
| 0xad70 | wuCroIGjt2g | `open` |
| 0xad80 | PfccT7qURYE | `ioctl` |
| 0xad90 | BPE9s9vQQXo | `mmap` |
| 0xada0 | DGMG3JshrZU | `sceKernelSetVirtualRangeName` |
| 0xadb0 | -W4xI5aVI8w | `sceKernelSetProcessProperty` |
| 0xadc0 | bY-PO6JhzhQ | `close` |
| 0xadd0 | kc+LEEIYakc | `sceKernelMapNamedSystemFlexibleMemory` |
| 0xade0 | YQ0navp+YIc | `puts` |

## Init Function (0x7cc0) — `sce_agc_initialize`

1. `open("/dev/gc", O_RDWR)` → fd
2. `ioctl(fd, 0xc004812e, &query_result)` — CONTEXT_QUERY
3. If query_result == 0 and global [0x1aae0] == 0:
   - `mmap(0xFE0200000, 0x4000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)` — map register aperture
   - Store mmap result at global [0x1aae0]
   - `sceKernelSetVirtualRangeName(mmap_addr, 0x4000, "SceGnmReg", 0)`
4. Store fd at output param
5. If global [0x1aba0] (SceGnmGpuInfo) is already set:
   - Clear and reinitialize the GPU info area
   - `sceKernelSetProcessProperty("Sce.Debug:Gnm", gpu_info_ptr, 0x100000,
     0, 0)` — exact five-argument GPU-info property carrier
   - `sceKernelSetVirtualRangeName(gpu_info_ptr, 0x100000,
     "SceGnmDumpArea")`

The earlier four-argument interpretation was incorrect. Cross-firmware
verification in `agc_driver_process_property_facts.md` confirms the five
arguments above for all 39 active drivers.
6. Call `sce_agc_initialize_internal_memory()` at 0x7e70

## Internal Memory Allocation (0x7e70) — `sce_agc_initialize_internal_memory`

**CRITICAL FINDING**: The SPRX uses `sceKernelMapNamedSystemFlexibleMemory` (not
`sceKernelAllocateDirectMemory`) for all internal memory regions. This is a
completely different allocation API.

### `sceKernelMapNamedSystemFlexibleMemory` signature
```c
int sceKernelMapNamedSystemFlexibleMemory(
    void **addr,      // rdi = &output (also input: search start hint)
    size_t size,      // rsi = size
    int type,         // edx = memory type
    int flags,        // rcx = flags (0 = none)
    const char *name  // r8 = name string
);
```

### Allocation 1: SceGnmGpuInfo
- **Search start**: `0xFE0300000` (register aperture area)
- **Size**: `0x100000` (1 MB)
- **Type**: `3` (WC_GARLIC)
- **Name**: `"SceGnmGpuInfo"`
- **Stored at globals**: `[0x1aba0]`, `[0x1ab40]`

### Allocations 2-9: all use search start `0xF00000000`, type `0x33`

Type `0x33` = `0b00110011` — this is a flexible memory type flag, NOT the same
as `sceKernelAllocateDirectMemory` memory types (1/2/3). It appears to mean
"WB+WC, garlic-coherent+garlic" — i.e., flexible memory accessible by both
CPU and GPU through garlic.

| # | Name | Size | Hex | Global ptr | Global size |
|---|------|------|-----|------------|-------------|
| 2 | SceGnmTrapCode | 16 KB | 0x4000 | [0x1aaf0] | [0x1aaf8]=0x4000 |
| 3 | SceGnmTrapData | 16 KB | 0x4000 | [0x1ab00] | [0x1ab08]=0x4000 |
| 4 | SceGnmDdid | 1008 KB | 0xFC000 | [0x1ab20] | [0x1ab28]=0xFC000 |
| 5 | SceGnmEopFifo | 240 KB | 0x3C000 | [0x1ab30] | [0x1ab38]=0x3C000 |
| 6 | SceGnmShadowReg | 16 KB | 0x4000 | [0x1ab50] | [0x1ab58]=0x4000 |
| 7 | SceGnmCwsr | 16 MB | 0x1000000 | [0x1ab50] | [0x1ab48]=0x100000 |
| 8 | SceGnmMisc | 16 KB | 0x4000 | [0x1ab60] | [0x1ab58]=0x4000 |
| 9 | SceGnmACQRB | 1920 KB | 0x1E0000 | [0x1ab80] | [0x1ab78]=0xA00000 |

**Total**: ~19.3 MB (vs our current ~70 KB from ps5-openagc)

### Additional globals set after allocation
- `[0x1ab10]` = `0xC80008000` — computed value (not a pointer)
- `[0x1ab18]` = `0x40000` (256 KB) — sub-region size?
- `[0x1ab48]` = `0x100000` (1 MB) — sub-region size within CWSR?
- `[0x1ab68]` = `0x1000000` — CWSR size
- `[0x1ab78]` = `0xA00000` (10 MB) — sub-region size within ACQRB?
- `[0x1aae8]` = `1` — initialization flag
- `[0x1afb0]` = SceGnmDdid ptr
- `[0x1afb8]` = SceGnmMisc ptr
- `[0x1afc0]` = SceGnmShadowReg ptr

### Comparison with ps5-openagc (our current code)

| Region | SPRX size | Our size | Match? |
|--------|-----------|----------|--------|
| SceGnmGpuInfo | 0x100000 (1MB) | 0x1000 (4KB) | **WRONG** |
| SceGnmTrapCode | 0x4000 (16KB) | 0x4000 (16KB) | OK |
| SceGnmTrapData | 0x4000 (16KB) | 0x4000 (16KB) | OK |
| SceGnmDdid | 0xFC000 (1008KB) | 0x1000 (4KB) | **WRONG** |
| SceGnmEopFifo | 0x3C000 (240KB) | 0x1000 (4KB) | **WRONG** |
| SceGnmShadowReg | 0x4000 (16KB) | 0x4000 (16KB) | OK |
| SceGnmCwsr | 0x1000000 (16MB) | 0x10000 (64KB) | **WRONG** |
| SceGnmMisc | 0x4000 (16KB) | 0x1000 (4KB) | **WRONG** |
| SceGnmACQRB | 0x1E0000 (1920KB) | 0x1000 (4KB) | **WRONG** |

**6 out of 9 region sizes are wrong.** The total is off by ~275x.

### Allocation API mismatch

Our code uses `sceKernelAllocateDirectMemory` + `sceKernelMapDirectMemory`.
The SPRX uses `sceKernelMapNamedSystemFlexibleMemory` — a single-call API
that both allocates and maps. This is a fundamentally different approach.

## Suspend Point (0x95d0) — `sceAgcDriverSuspendPointSubmitDirect`

```asm
95d0: push rbp
95d1: mov rsp, rbp
95d4: push rbx
95d5: sub rsp, 0x18
95d9: rbx = [0x140e8]       ; global context (contains fd)
95e0: rax = [rbx]            ; stack canary
95e3: [rbp-0x10] = rax       ; save canary
95e7: [rbp-0x20] = esi       ; field0 (arg2)
95ea: [rbp-0x1c] = edx       ; field1 (arg3)
95ed: rdx = &[rbp-0x20]      ; arg struct pointer
95f1: esi = 0xc010811c       ; ioctl cmd (SUSPEND_16)
95f6: [rbp-0x18] = ecx       ; field2 (arg4)
95f9: eax = 0                ; return value init
95fb: [rbp-0x14] = r8d       ; field3 (arg5)
95ff: call ioctl             ; ioctl(rdi=fd, esi=cmd, rdx=&arg)
```

**Key finding**: `rdi` (the fd) is NOT set in this function. It must be
passed by the caller. The function takes 5 args: `(fd, field0, field1, field2, field3)`.
The 4 fields are packed into a 16-byte struct on the stack and passed to
`ioctl(fd, 0xc010811c, &struct)`.

### Kernel handler (0xd8f66ff0) — HW-validated

The kernel handler uses the SAME credential check (0xd8e70400) and the
SAME magic triple (0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c) as the queue
create handler (0xd8f66bb0). When credentials pass (cr_sceAuthId =
0x4801000000000000), the handler falls through to magic checks. The
magic triple selects config table at 0xd9d5b360, mapping to slot
(field0=2, field1=3, field2=5) at ctx offset 0x158 — the SAME slot as
the queue create.

Non-magic values like (1,0,0) compute a different slot (0x64) and fail
with 0x804C0001 (no queue at that slot).

The tail-called function at 0xd8e57700 checks
`(field3 >> queue->shift_amount) == 0` where shift_amount is read from
queue+0x48. If field3 is non-zero and shift_amount is 0, returns EINVAL.
Passing field3=0 works.

**HW-validated**: sceAgcDriverSuspendPointSubmitDirect(0xaf1e80b7,
0x8b4cdd90, 0x99f68d6c, 0) succeeds on PS5 FW 5.50 with the GPU
credential bypass.

## Queue Create (0x8900) — `_sceAgcDriverCreateUserSpecialQueue`

### Function signature
```c
int sceAgcDriverCreateUserSpecialQueue(
    uint32_t magic1,    // esi (ebx) — must be 0xaf1e80b7 for known path
    uint32_t magic2,    // edx (r15d) — must be 0x8b4cdd90
    uint32_t magic3,    // ecx (r14d) — must be 0x99f68d6c
    uint32_t magic4,    // stack arg (0x10(%rbp))
    uint64_t *out_queue, // stack arg (0x18(%rbp))
    uint32_t pipe_id,   // r8d
    uint32_t queue_id   // r9d
);
```

### Ring buffer address computation

The ring buffer address is computed from the SceGnmEopFifo base (global
`[0x1ab30]`) plus an offset that depends on the magic tokens:

```c
uint64_t ring_addr;
if (magic1 == 0xaf1e80b7 && magic2 == 0x8b4cdd90 && magic3 == 0x99f68d6c) {
    ring_addr = eop_fifo_base + 0x39000;
} else if (xor_check_passes) {
    ring_addr = eop_fifo_base + 0x38000;
} else {
    uint32_t offset = (magic2 + magic1*4 + magic3*8) << 12;
    offset += 0xFFFE0000;
    ring_addr = eop_fifo_base + offset;
}
```

The XOR check at 0x895b-0x8971:
```c
bool xor_match = (magic1 ^ 0x769C766) | (magic2 ^ 0x72E8E3C1) | (magic3 ^ 0xDB72AF28) == 0;
```

### Ioctl argument layout (64 bytes at rbp-0x70)

```
offset  field
-0x70   magic1 (esi/ebx)
-0x6c   magic2 (r15d)
-0x68   magic3 (r14d)
-0x64   magic4 (stack arg)
-0x60   r8 (pipe_id as qword)
-0x58   rax (stack arg — out_queue?)
-0x50   eop_fifo_base global
-0x48   r9d (queue_id)
-0x44   0 (padding)
-0x40   computed ring_addr
-0x38   0x1000 (ring buffer entry size?)
```

### Post-ioctl processing

After the ioctl succeeds, the SPRX:
1. Reads `magic4` from `[rbp+0x10]` and checks it against `0xD245ED58` and `0xE5FCC174`
2. Computes a queue metadata pointer from `[0x1aae0]` (mmap base) + offset
3. The offset depends on the magic tokens:
   - `(0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c)` → offset 0x1E0
   - `(0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c)` with magic4=0xE5FCC174 → offset 0x1E8
   - Otherwise: computed from tokens
4. Writes the queue metadata pointer to the output queue struct
5. If global `[0x1aba0]` (SceGnmGpuInfo) is set, writes queue info to the GPU info area

### Why our queue create fails

Our implementation:
1. Allocates a **separate** ring buffer region — the SPRX uses the EOP FIFO region
2. Computes ring buffer address as `eop_fifo_base + offset` — we use a standalone allocation
3. The 64-byte ioctl argument layout may not match exactly

The fix: use the EOP FIFO allocation as the ring buffer base, and compute
the ring buffer address as `eop_fifo_base + 0x39000` (for the standard magic
tokens).

### Kernel RE: actual root cause of queue create failure

After fixing the ioctl arg layout (ring from EOP FIFO, read_ptr/metadata
from ACQRB), the queue create still fails with `0x804C000B` (EAGAIN from
GC subsystem). Kernel disassembly of FW 5.50 reveals the actual root cause:

**Kernel handler**: `0xffffffffd8f66bb0` (called from ioctl dispatch at
`0xffffffffd8f6e74d` for ioctl `0xc0408121`).

The handler flow:
1. Reads magic tokens from the ioctl arg (offsets 0x00-0x0C)
2. Calls `0xffffffffd8e4b9d0` (some check) and `0xffffffffd92e3a80(0)`
3. Gets `curthread` from `gs:[0]`, gets `proc` from `[curthread + 0x140]`
4. Calls `0xffffffffd8e70400(proc)` — **GPU process credential check**
5. If the check returns 0 → returns `0x804C000B` (EAGAIN)
6. If the check returns non-zero → validates magic tokens and proceeds

**Credential check function** at `0xffffffffd8e70400`:
```c
int gpu_process_check(proc_t *proc) {
    uint64_t rax = 0xff0f000000000000ULL & proc->creds;  // [proc + 0x58]
    uint64_t rcx = 0xb7ff000000000000ULL + rax;
    return (rcx >> 49) == 0;  // 1 if pass, 0 if fail
}
```

For the check to pass: `(proc->creds & 0xff0f000000000000)` must equal
`0x4901000000000000` (so that `0xb7ff... + 0x4901... = 0` with overflow).

Our homebrew payload (uid=1, euid=0) does not have this GPU process
credential, so the check fails. This is a **kernel-level permission check**
that cannot be bypassed from userspace. Options:
- Patch the kernel check at `0xffffffffd8e70400` (on exploited PS5)
- Set `proc->creds` at `[proc + 0x58]` to include `0x4901000000000000`
- Run the payload as a process that already has GPU credentials (game/shell)

### Kernel RE: suspend point failure root cause

The suspend point ioctl (`0xc010811c`) handler at `0xffffffffd8f665c0`:

1. Reads `[device_context + 0x10]` — the frame/render context pointer
2. If NULL → returns `0x804C0001` (EPERM)
3. If non-NULL → looks up process by field0, gets VMID, validates VMID in [2,14]

An earlier hypothesis attributed `[device_context + 0x10]` to a
`FRAME_OPEN` ioctl. FW 5.50 dispatch RE disproves that hypothesis:
`0xc0088100` has no handler and always reaches the `EINVAL` default path.
The field is established through another driver lifecycle path; validated
suspend-point submission does not depend on a nonexistent FRAME_OPEN call.
