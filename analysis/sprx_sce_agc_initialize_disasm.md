# SPRX sce_agc_initialize + libSceAgcDriver module_start Disassembly Analysis

## Summary

The PS5 AGC initialization sequence does **NOT** use a `FRAME_OPEN` ioctl
(nr=0x00). The kernel's `/dev/gc` driver has no handler for nr=0x00 — it
falls through to the default case which returns `EINVAL` (22).

The real initialization is performed by `libSceAgcDriver.sprx`'s
**module_start** function, which runs automatically when the SPRX is loaded
by the dynamic linker. It opens `/dev/gc`, queries the context state with
ioctl `0xc004812e` (nr=0x2e), and if the context is not yet initialized,
mmaps the GPU register space at a fixed address.

`sce_agc_initialize` in `libSceAgc.sprx` (NID `23LRUSvYu1M`) does NOT open
`/dev/gc` or call any ioctl — it only locks a mutex, queries system info,
allocates an 8-byte context, and calls internal resource table initializers.

## Detailed Findings

### 1. libSceAgc.sprx — sce_agc_initialize (NID: 23LRUSvYu1M)

**Location:** vaddr 0x75e0 (thunk at 0x84e0)

The function:
1. Locks `SceAgcMutex` (call to `9UK1vLZQft4CD`)
2. Calls `GGeRJk1XdWcCD` — queries system configuration
3. Checks a byte field `(field << 0x18) == 0x4000000` — GPU capability check
4. Calls `HoLVWNanBBcCD` + `G_MYv5erXaUCD` — more system queries
5. Calls `1yca4VvfcNACD` twice (with args 0x52 and 0x53) — resource table queries
6. Stores results in global state at `[0x33038]`
7. If `[0x33110]` is NULL, allocates 8 bytes via `memset`-style allocator
8. Calls `0xe8f0` — initializes resource table (FS table, not /dev/gc)
9. Calls `0x119a0` — initializes another resource table
10. Calls `0x7779` → `0xe8f0` (resource init) and `0x119a0` (resource init)
11. Unlocks mutex and returns

**Key point:** `sce_agc_initialize` does NOT open `/dev/gc` or call any ioctl.
It only sets up userspace resource tables and queries system configuration.

### 2. libSceAgcDriver.sprx — module_start (the real /dev/gc initializer)

**Location:** vaddr 0x77f0 (file offset 0xb7f0)
**Called via:** INIT function at vaddr 0x10 → trampoline at 0x7ca0 → jmp 0x77f0

The module_start function:

1. **Zeroes global state** (ymmword stores at `[rip + 0x13246]` and `[rip + 0x13257]`)
2. **Calls `0xb580`** — internal initialization
3. **Calls `0xbcc0`** (vaddr 0x7cc0) — the /dev/gc open + query function:
   - Opens `/dev/gc` with `open("/dev/gc", O_RDWR)` (call to PLT `wuCroIGjt2g`)
   - Calls `ioctl(fd, 0xc004812e, &result)` — **CONTEXT_QUERY** ioctl
     - Arg: 4-byte RW (IOC encoding: dir=RW, type=0x81, nr=0x2e, size=4)
     - Returns: 16-bit capability mask in lower 2 bytes
     - If result == 0: context is NOT initialized
     - If result != 0: context is already initialized (skip mmap)
   - If result == 0 AND mmap not yet done:
     - `mmap(0xfe0200000, 0x4000, MAP_SHARED, fd)` — maps GPU registers
     - `mlock(mapped_addr, 0x4000)` — locks the mapping
   - Stores fd in output parameter
   - If capability is still 0:
     - Calls `0x7e70` — likely `sceAgcDriverNotifyDefaultStates` equivalent
     - Calls `0xa8a0` (vaddr 0x68a0) — resource setup function
   - Initializes internal state structure (zeroes 0x600 bytes, etc.)
   - Sets up memory regions via `memset` + `mlock` calls

### 3. Kernel handler for ioctl 0xc004812e (nr=0x2e)

**Location:** 0x6ee691 in kernel_550_merged_by_offset.bin
**Dispatch path:** gc_ioctl_internal (0x6ed39c) → BST at 0x6edcdf → jump table
at 0x14db1e4 → index 0x1b → handler at 0x6ee691

The handler:
```asm
mov rax, [rbp - 0x38]       ; ctx (gc context struct)
xor ecx, ecx
cmp dword [rax + 0x30], 0   ; check ctx->field_30
setne cl                    ; cl = (field_30 != 0)
xor edx, edx
mov word [r12], cx          ; write 16-bit result to user
cmp dword [rax + 0x40], 0   ; check ctx->field_40
setne dl                    ; dl = (field_40 != 0)
shl edx, 0x10               ; dl << 16
or edx, ecx                 ; edx = (field_40 != 0) << 16 | (field_30 != 0)
mov dword [r12], edx        ; write 32-bit result to user
jmp 0x6eec2e                ; success (r14d = 0)
```

This is a **capability/version query** — it reads two fields from the kernel's
gc context struct and returns a bitmask indicating whether the GPU context
has been initialized. It does NOT perform any initialization itself.

### 4. Why FRAME_OPEN (nr=0x00) returns EINVAL

The kernel's `gc_ioctl_internal` at 0x6ed39c uses a BST (binary search tree)
dispatch. The ioctl cmd 0xc0088100 (nr=0x00) is NOT in any handled range:

- BST at 0x6edcdf: handles 0xc0048113 to 0xc004812e (index 0x00 to 0x1b)
- BST at 0x6edbe7: handles 0xc0048113 to 0xc004812d
- Various direct comparisons: 0xc0048112, 0xc0088101, 0xc0088111, etc.

None of these match 0xc0088100. The default handler at 0x6edbb4 sets
`r14d = 0x16` (22 = EINVAL) and falls through to the cleanup at 0x6eec31.

**Conclusion:** There is no FRAME_OPEN ioctl in FW 5.50. The `/dev/gc`
file descriptor is opened and the GPU context is queried via ioctl
0xc004812e. The actual GPU context initialization is done by the kernel
at boot time or when the first process opens `/dev/gc`.

### 5. The mmap at fixed address 0xfe0200000

The SPRX maps GPU register space at the fixed virtual address `0xfe0200000`
with size `0x4000` (16KB). This is `MAP_SHARED` with the `/dev/gc` file
descriptor, giving userspace direct access to GPU register MMIO space.

The `mlock` call ensures the mapping stays resident in memory (no page-out).

### 6. Other /dev/gc open functions in libSceAgcDriver.sprx

There are three functions that open `/dev/gc`:

1. **0xbcc0 (vaddr 0x7cc0)** — called from module_start (0xb7f0)
   - Opens /dev/gc, calls ioctl 0xc004812e, mmaps at 0xfe0200000
   - This is the primary initialization path

2. **0xc3e0 (vaddr 0x83e0)** — not directly called (may be via function pointer)
   - Opens /dev/gc, mmaps at 0xfe0200000
   - Also mmaps at another fixed address with `mmap(0, 0x4000, ...)`
   - Calls `0xe8a0` (resource setup)

3. **0xc5a0 (vaddr 0x85a0)** — not directly called
   - Opens /dev/gc, calls `0xedf0` (some query)
   - If query returns non-zero AND global pointer is NULL:
     - `mmap(0, 0x4000, MAP_SHARED, fd)` — maps at any address
   - Simpler than the other two

## Corrected Initialization Sequence

For the orbis backend, the correct sequence is:

1. `open("/dev/gc", O_RDWR)` — get file descriptor
2. `ioctl(fd, 0xc004812e, &result)` — query context state (4-byte RW)
   - result is a 32-bit value: `(field40 != 0) << 16 | (field30 != 0)`
   - If result != 0: context is already initialized, skip to step 4
3. `mmap(0xfe0200000, 0x4000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)`
   - Map GPU register space at fixed address
   - `mlock(mapped, 0x4000)` — lock the mapping
4. Store fd for later use
5. If context was not initialized (result == 0):
   - Call default state notification (CLEAR_STATE submission)
   - Set up internal resource tables

## Ioctl Number Reference

| Ioctl Command | nr | Direction | Size | Purpose |
|--------------|-----|-----------|------|---------|
| 0xc004812e | 0x2e | RW | 4 | Context capability query |
| 0xc0088101 | 0x01 | RW | 8 | Close / cleanup |
| 0xc00c810e | 0x0e | RW | 12 | Queue destroy |
| 0xc0408121 | 0x21 | RW | 64 | Queue create |
| 0x80048126 | 0x26 | R | 4 | Setup async graphics |
| 0xc0088100 | 0x00 | — | — | **NOT HANDLED** (returns EINVAL) |

## Files

- SPRX: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50/sprx/common_lib/libSceAgcDriver.sprx`
- SPRX: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50/sprx/common_lib/libSceAgc.sprx`
- Kernel: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50/5.50-kv-dump/merged/kernel_550_merged_by_offset.bin`
