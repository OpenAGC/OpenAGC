# ps5-openagc Audit

Project at `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc` is **NOT proven working on hardware**. Used for NID mapping cross-reference ONLY. Contains known errors. See `analysis/ps5_openagc_audit.md`.

## Confirmed WRONG in ps5-openagc
1. **FRAME_OPEN (nr=0x00)** — claimed valid init ioctl with handler @ 0x6ed8dc. WRONG — kernel returns EINVAL. Handler @ 0x6ed8dc is for nr=0x16 (QUEUE_DISCONNECT). Real init uses CONTEXT_QUERY (nr=0x2e) + mmap. Hardware-confirmed.
2. **Queue create** — used nr=0x2a (4-byte READ) with simple packed struct. WRONG — real SPRX uses nr=0x21 (0xc0408121, 64-byte RW) with magic auth tokens (0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c, 0xe5fcc174), pipe_id=0xc, ring_size=0x1000.
3. **Queue destroy** — used nr=0x2b (4-byte WRITE). WRONG — real SPRX uses nr=0x0e (0xc00c810e, 12-byte RW) with 3 magic tokens.
4. **VMID mask** — documented 0x000FFFFF00000000 (transcription error). Correct 52-bit mask: 0x000FFFFFFFFFFFFF.
5. **Memory type constants** — had WB_ONION=0, WC_GARLIC=1, WB_GARLIC=2 (PS4 values). Correct PS5: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2. Hardware-confirmed (type=1 fails on exploited PS5).
6. **Internal memory region sizes** — 6 of 9 wrong (all 4KB-64KB). SPRX uses `sceKernelMapNamedSystemFlexibleMemory` (not `sceKernelAllocateDirectMemory`) with type=0x33. Correct sizes from SPRX: SceGnmGpuInfo=1MB, SceGnmDdid=1008KB, SceGnmEopFifo=240KB, SceGnmCwsr=16MB, SceGnmACQRB=1920KB. See `mem:sprx_re`.
7. **Queue create ioctl arg layout** — field order at offsets 0x10-0x28 completely wrong. Correct: pipe_id @ 0x10, caller_arg @ 0x18, mmio_base @ 0x20, queue_id @ 0x28. Ring carved from EOP FIFO base + 0x39000, not separately allocated.

## Independently verified (from our SPRX/kernel disassembly)
- Queue create/destroy ioctls (nr=0x21/0x0e) + magic tokens + pipe_id=0xc
- Submit ioctl nr=0x3b (cmd=0xc010813b, 16-byte RW), CB descriptor layout, VMID mask 0x000FFFFFFFFFFFFF
- Context query nr=0x2e (cmd=0xc004812e), kernel handler @ 0x6ee691
- 55 NIDs from ps5-openagc's agc_nid.h cross-referenced with SPRX symbol tables
- Setup async graphics nr=0x26 (cmd=0x80048126)
- PS5 memory type constants (hardware-confirmed)

## Inherited UNVERIFIED (could be wrong)
1. Suspend ioctl arg semantics (field meanings inferred, not from SPRX usage)
2. Makesysmap ioctl arg layouts (8/12/48-byte variants; 48-byte speculative)
3. SET_HS_OFFCHIP patch list entry format (unknown)
4. PADEBUG ioctl return value format (assumed)
5. Ioctl dispatch table entries (FRAME_OPEN was wrong; others may be too)

## Rule
Any time ps5-openagc is used as reference, mark finding INHERITED UNVERIFIED until confirmed by independent SPRX/kernel disassembly. Do NOT trust its ioctl layouts or memory region sizes without independent verification.