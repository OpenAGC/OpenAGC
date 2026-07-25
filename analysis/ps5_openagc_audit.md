# ps5-openagc Inherited Assumptions Audit

## Background

The ps5-openagc project (`/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc`) is
**NOT proven working on hardware**. It was used as a reference for NID mapping
and initial ioctl analysis, but its implementation has known errors.

The most critical error: ps5-openagc claimed `FRAME_OPEN` (ioctl nr=0x00) was
the initialization ioctl, but the FW 5.50 kernel has no handler for it and
returns `EINVAL`. The real init uses `CONTEXT_QUERY` (nr=0x2e) + `mmap`.
See `sprx_sce_agc_initialize_disasm.md` for details.

## Audit Results

### INDEPENDENTLY VERIFIED (from SPRX/kernel disassembly)

These were confirmed by our own disassembly of the FW 5.50 SPRX files and
kernel dump, NOT just inherited from ps5-openagc:

1. **Queue create ioctl**: nr=0x21, cmd=0xc0408121, 64-byte RW arg
   - Confirmed: SPRX `_sceAgcDriverCreateUserSpecialQueue` at vaddr 0x2c20
     calls internal helper at 0x8d80 which uses `mov esi, 0xc0408121`
   - Magic tokens confirmed: 0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c, 0xe5fcc174
   - pipe_id=0xc confirmed
   - **ps5-openagc was WRONG**: it used nr=0x2a (4-byte READ) with a simple
     packed struct

2. **Queue destroy ioctl**: nr=0x0e, cmd=0xc00c810e, 12-byte RW arg
   - Confirmed: SPRX `_sceAgcDriverDestroyUserSpecialQueue` at vaddr 0x2e30
     calls internal helper at 0x8fc0 which uses `mov esi, 0xc00c810e`
   - Same three magic tokens confirmed
   - **ps5-openagc was WRONG**: it used nr=0x2b (4-byte WRITE)

3. **Submit ioctl**: nr=0x3b, cmd=0xc010813b, 16-byte RW arg
   - Confirmed from kernel dump `gc_submit_with_pid` at 0x6e65c0
   - CB descriptor layout (16 bytes: header + ib_base) confirmed
   - VMID mask: our code uses 0x000FFFFFFFFFFFFF (52-bit), ps5-openagc had
     a transcription error (0x000FFFFF00000000)

4. **Context query ioctl**: nr=0x2e, cmd=0xc004812e, 4-byte RW
   - Confirmed: SPRX module_start at vaddr 0x7cc0 uses this ioctl
   - Kernel handler at 0x6ee691 confirmed

5. **NID mappings**: 55 NIDs from ps5-openagc's agc_nid.h were cross-referenced
   with the SPRX symbol tables and confirmed present

6. **Setup async graphics**: nr=0x26, cmd=0x80048126
   - Confirmed from SPRX `sceAgcDriverSetupAsyncGraphics` disassembly

7. **Memory type constants** (hardware-confirmed):
   - PS5: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2
   - ps5-openagc had: WB_ONION=0, WC_GARLIC=1, WB_GARLIC=2 (WRONG)
   - Confirmed by deploying to PS5 FW 5.50: type=1 (onion) returns EINVAL
     in the exploited payload context, type=3 (garlic) works for all regions
   - Cross-referenced with PS5_DEV_HOMEBREW/examples/ps5_sdk which uses
     type=1 for command buffers and type=3 for framebuffers

### INHERITED UNVERIFIED (from ps5-openagc, not independently confirmed)

These came from ps5-openagc and have NOT been independently verified against
SPRX or kernel disassembly. They could be wrong:

1. **Internal memory region sizes** (driver_prospero.c `sce_agc_initialize_internal_memory`):
   - DDID: 0x1000 (4KB), WC_GARLIC
   - CWSR: 0x10000 (64KB), WC_GARLIC
   - EOP FIFO: 0x1000 (4KB), WC_GARLIC
   - Shadow regs: 0x4000 (16KB), WC_GARLIC
   - Trap code: 0x4000 (16KB), WC_GARLIC
   - Trap data: 0x4000 (16KB), WC_GARLIC
   - GPU info: 0x1000 (4KB), WC_GARLIC
   - Workload: 0x1000 (4KB), WC_GARLIC
   - **Source**: ps5-openagc src/agc_driver.c, claimed "from GNM driver string
     analysis"
   - **Hardware test**: All 8 regions allocate successfully on PS5 FW 5.50
     with WC_GARLIC (type=3). WB_ONION (type=1) returns EINVAL on the
     exploited PS5, so all regions now use garlic. The actual SPRX sizes
     are still unconfirmed — all 8 allocate but may be larger/smaller than
     what the real driver uses.
   - **Risk**: If sizes are wrong, the kernel may reject the MAKESYSMAP ioctls
     or the GPU may malfunction. The SPRX module_start uses 0x100000 (1MB) total
     locked memory, which doesn't directly match these individual sizes.
   - **Action needed**: Disassemble the actual `sce_agc_initialize_internal_memory`
     function from libSceAgc.sprx to verify region sizes and memory types.

2. **Suspend ioctl argument layout** (nr=0x1c, nr=0x39):
   - 4-dword struct: field0 (type/selector), field1 (<=3), field2 (<=7), field3 (value)
   - **Source**: kernel handler at 0x6e6ff0 (partially verified from kernel dump)
   - **Risk**: The field semantics are inferred, not confirmed from SPRX usage
   - **Action needed**: Find SPRX calls to the suspend ioctl to verify arg layout

3. **Makesysmap ioctl argument layouts** (nr=0x09, 0x0c, 0x0d):
   - 8-byte, 12-byte, and 48-byte variants
   - **Source**: ps5-openagc + kernel dump cross-reference
   - **Risk**: The 48-byte variant layout is speculative
   - **Action needed**: Verify from SPRX makesysmap calls

4. **SET_HS_OFFCHIP argument** (nr=0x2c):
   - 16-byte: list_addr (8 bytes) + num_entries (4 bytes) + reserved (4 bytes)
   - **Source**: kernel handler at 0x6ee6d2
   - **Risk**: The exact patch list entry format is unknown

5. **PADEBUG ioctl** (nr=0x38):
   - It is **not** used by FW 5.50 `sceAgcDriverGetPaDebugInterfaceVersion`.
   - The export at VA 0x2b0 logs `permission insufficient` and returns the
     constant `0x8a6d0001`; it performs no ioctl.
   - `sceAgcDriverIsPaDebug` at VA 0x2e0 is a constant-zero stub.
   - **Source**: independently disassembled FW 5.50 libSceAgcDriver.sprx.

6. **Ioctl dispatch table entries** (from ps5-openagc's ioctl_dispatch.md):
   - Many ioctl handler addresses were copied from ps5-openagc
   - **Known error**: FRAME_OPEN (nr=0x00) was listed as valid but isn't
   - **Risk**: Other entries may also be misidentified
   - **Action needed**: Re-verify the full BST dispatch from the kernel dump

### CONFIRMED WRONG in ps5-openagc

1. **FRAME_OPEN (nr=0x00)**: ps5-openagc claimed this was a valid init ioctl
   with handler at 0x6ed8dc. **WRONG** — the kernel returns EINVAL for this.
   The handler at 0x6ed8dc is for a different ioctl (nr=0x16, QUEUE_DISCONNECT).

2. **Queue create/destroy**: ps5-openagc used nr=0x2a/0x2b with simple 4-byte
   packed args. **WRONG** — the real SPRX uses nr=0x21/0x0e with 64-byte/12-byte
   args containing magic authentication tokens.

3. **VMID mask**: ps5-openagc documented 0x000FFFFF00000000. **WRONG** — the
   correct 52-bit mask is 0x000FFFFFFFFFFFFF.

## Recommendations

1. **High priority**: Disassemble `sce_agc_initialize_internal_memory` from
   libSceAgc.sprx to verify the 8 region sizes and memory types. This is the
   most likely source of hardware failure after the FRAME_OPEN fix.

2. **Medium priority**: Re-verify the full ioctl dispatch table from the kernel
   dump BST, starting from 0x6ed39c. Don't trust ps5-openagc's ioctl_dispatch.md.

3. **Low priority**: Verify suspend ioctl argument semantics from SPRX calls.

4. **Ongoing**: Any time ps5-openagc is used as a reference, mark the finding
   as INHERITED UNVERIFIED until confirmed by independent disassembly.
