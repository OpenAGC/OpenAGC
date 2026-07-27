# FW 5.50 compressed D16/HTILE qualification

Date: 2026-07-27

## Scope and prerequisite

Compressed D16/HTILE was deferred until an independent uncompressed D16 gate
proved the typed `D16_UNORM` layout, surface binding, depth comparisons, native
readback, completion fence, and presentation loop. That prerequisite passed
twice before HTILE was enabled. The compressed gate then kept stencil, MSAA,
and expclear disabled so it measured only D16 plus pipe-aligned HTILE and the
typed decompression/resummarization operations.

This is FW `0x05500008` gfx1013 hardware evidence. It does not generalize the
layout or pipe count to other firmware or GPU profiles.

## Independent runs

The two foreground curl/websrv logs are retained at:

- `samples/hw_test/conformance-logs/d16-htile-20260727/htile1.log`
- `samples/hw_test/conformance-logs/d16-htile-20260727/htile2.log`

Both runs independently reported:

- FW profile `0x05500008`, standard PS5, gfx1013.
- A 4,718,592-byte D16 `64KB_Z_X` allocation.
- A 196,608-byte, eight-pipe, pipe-aligned HTILE allocation initialized to
  depth-only uncompressed word `0xfffc000f`.
- Wave32 NGG and pixel shader validation passing.
- A 2,695-dword DCB accepted with `AGC_OK`.
- GPU completion fence arrival after 1,000 microseconds.
- All four ordered stage markers and the final `0xd16fffff` marker matching.
- 4,226 HTILE words changed after decompression and resummarization.
- Exact native D16 classes: 909,792 clear-one, 128,304 near, and 128,304 far.
- Exact color results: 128,304 green and 128,304 red pixels.
- 1,800 accepted and 1,800 completed VideoOut flips.

These native D16 counts are the decisive compressed-depth oracle: they are
measured only after the public typed decompression operation, rather than
inferring correctness from changed metadata or the displayed colors alone.

## Kernel-log boundary

Paired ps5debug-NG logs are retained beside the run logs. They contain no
kernel-panic, GPU bad-packet, GPU page-fault, GPU-reset, privilege-register, or
watchdog signature. Each log does contain a generic post-run `A user thread
receives a fatal signal` line and a `VM resource leak` warning for `0x4000`
bytes. Because the application had already printed `Done` after all 1,800
flips, these are tracked as teardown/lifecycle issues rather than evidence of a
failed D16/HTILE GPU command stream. They must not be summarized as entirely
clean kernel logs.

## Result

The original deferral condition is satisfied: uncompressed D16 passed first,
then two independent compressed D16/HTILE runs reproduced exact GPU, metadata,
native-depth, color, fence, and presentation oracles. The feature is qualified
for the FW `0x05500008` gfx1013 profile. Future changes to its layout, binder,
HTILE operations, synchronization, or sample must repeat this gate.
