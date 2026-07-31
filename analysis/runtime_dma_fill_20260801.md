# Runtime DMA fill lowering (2026-08-01)

`agcCmdFillBuffer` now lowers a repeated 32-bit value to the canonical
seven-dword gfx1013 `DMA_DATA` immediate-source packet instead of embedding the
value once per destination dword through `WRITE_DATA`.

The packet uses source select `DATA`, destination select `ADDR`, and `CP_SYNC`:
its control dword is `0xc0000000`. Transfers are split at the qualified
`0x1ffffc`-byte packet limit, and the complete `7 * packet_count` command-space
requirement is checked before any packet is committed. Buffer bounds, typed
copy-destination state, queue ownership, and resource retention continue to be
validated by the existing native runtime path.

This makes a 1920x1080 four-byte surface fill four packets (28 dwords) rather
than roughly two million embedded payload dwords. The generic suite verifies
the exact header, control, immediate value, zero high source word, and byte
count. The optimized packet form remains hardware-unverified until the guarded
FW 5.50 SDL/Zink qualification run after reboot; it must not be described as
hardware-qualified before that gate passes.
