# SPRX Disassembly Findings (FW 5.50)

SPRX files at `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50/sprx/common_lib/`. RE-only — never copy/embed/commit.

## Init sequence (libSceAgcDriver module_start @ vaddr 0x77f0)
**CRITICAL**: Real /dev/gc init is in libSceAgcDriver **module_start**, NOT `sce_agc_initialize` (NID 23LRUSvYu1M @ 0x75e0 in libSceAgc). `sce_agc_initialize` only locks mutex, queries system info, allocates 8-byte context, calls resource table inits — NO /dev/gc open, NO ioctl.

module_start → 0x7cc0:
1. `open("/dev/gc", O_RDWR)` → fd
2. `ioctl(fd, 0xc004812e, &result)` — CONTEXT_QUERY (nr=0x2e, 4-byte RW). Result = `(ctx->field40 != 0) << 16 | (ctx->field30 != 0)`.
3. If result==0 AND mmap not done: `mmap(0xfe0200000, 0x4000, PROT_RW|MAP_SHARED, fd, 0)` + `mlock` + `sceKernelSetVirtualRangeName(addr, 0x4000, "SceGnmReg", 0)`
4. Store fd. If capability still 0: call 0x7e70 (internal mem) + 0xa8a0 (resource setup)

**FRAME_OPEN (0xc0088100, nr=0x00) does NOT exist in FW 5.50 kernel BST** — returns EINVAL. Handler at 0x6ed8dc is for nr=0x16 (QUEUE_DISCONNECT), not FRAME_OPEN.

## Internal memory (0x7e70) — sce_agc_initialize_internal_memory
Uses `sceKernelMapNamedSystemFlexibleMemory(addr&, size, type, flags, name)` — NOT `sceKernelAllocateDirectMemory`.

| # | Name | Size | Type | Search start |
|---|------|------|------|--------------|
| 1 | SceGnmGpuInfo | 0x100000 (1MB) | 3 (WC_GARLIC) | 0xFE0300000 |
| 2 | SceGnmTrapCode | 0x4000 (16KB) | 0x33 | 0xF00000000 |
| 3 | SceGnmTrapData | 0x4000 (16KB) | 0x33 | 0xF00000000 |
| 4 | SceGnmDdid | 0xFC000 (1008KB) | 0x33 | 0xF00000000 |
| 5 | SceGnmEopFifo | 0x3C000 (240KB) | 0x33 | 0xF00000000 |
| 6 | SceGnmShadowReg | 0x4000 (16KB) | 0x33 | 0xF00000000 |
| 7 | SceGnmCwsr | 0x1000000 (16MB) | 0x33 | 0xF00000000 |
| 8 | SceGnmMisc | 0x4000 (16KB) | 0x33 | 0xF00000000 |
| 9 | SceGnmACQRB | 0x1E0000 (1920KB) | 0x33 | 0xF00000000 |

Type 0x33 = `0b00110011` — flexible memory flag (WB+WC, garlic-coherent+garlic), NOT same as sceKernelAllocateDirectMemory types (1/2/3).
Total ~19.3MB. ps5-openagc had 6/9 sizes wrong (all 4KB-64KB).

## Queue create (0x8900) — _sceAgcDriverCreateUserSpecialQueue
Signature: `(magic1=0xaf1e80b7, magic2=0x8b4cdd90, magic3=0x99f68d6c, magic4, out_queue*, pipe_id, queue_id)`. Ioctl nr=0x21 (cmd=0xc0408121, 64-byte RW).

Ring buffer carved from SceGnmEopFifo base + 0x39000 (for standard magic triple). Read ptr from ACQRB base + 0x1C8000, metadata from ACQRB base + 0x1CC000.

64-byte ioctl arg layout (rbp-0x70):
- 0x00 magic1, 0x04 magic2, 0x08 magic3, 0x0C magic4
- 0x10 pipe_id (qword), 0x18 caller_arg, 0x20 mmio_base (eop_fifo_base global)
- 0x28 queue_id, 0x2C padding, 0x30 ring_addr, 0x38 0x1000 (entry size)

Post-ioctl: magic4 checked vs 0xD245ED58 / 0xE5FCC174. Queue metadata ptr = mmap_base + 0x1E0 (or 0x1E8 if magic4=0xE5FCC174).

## Suspend point (0x95d0) — sceAgcDriverSuspendPointSubmitDirect
Takes 5 args: `(fd, field0, field1, field2, field3)`. Packs 4 fields into 16-byte stack struct, calls `ioctl(fd, 0xc010811c, &struct)` (SUSPEND_16, nr=0x1c). Final variant uses nr=0x39 (SUSPEND_39), same layout.

Kernel handler 0xd8f66ff0 uses SAME credential check + SAME magic triple as queue create. Same config table 0xd9d5b360 → same slot (2,3,5) at ctx offset 0x158. Tail-call 0xd8e57700 checks `(field3 >> queue->shift_amount) == 0`; field3=0 works.

## NID table
148 identified exports (78 original + 36 deep SPRX + 34 batch 2). FW 5.50 export counts: libSceAgc=222, libSceAgcDriver=145, libSceAgcVsh=219.

Key driver NIDs (recovered via prospero-nid):
- sceAgcDriverSuspendPointSubmitDirect: ZV04pRl7cWU
- sceAgcDriverNotifyDefaultStates: nR6xhiFsOoc
- sceAgcDriverSetupAsyncGraphics: Vlaj1gwmIFA
- sceAgcDriverSetTFRingDirect: 16IjQxB-Heo
- sceAgcDriverSetHsOffchipParamDirect: DPcAnsOlTQs
- sce_agc_internal_suspend_point_submit_final: ZrN+92fezeA
- _sceAgcDriverCreateUserSpecialQueue: 031OFjGn0Eo
- _sceAgcDriverDestroyUserSpecialQueue: 4dk+ll564oM
- sceAgcSuspendPoint: h9z6+0hEydk

See `mem:kernel_re` for kernel-side handlers, `mem:ps5_openagc_audit` for what NOT to trust.