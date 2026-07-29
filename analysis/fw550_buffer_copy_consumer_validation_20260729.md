# FW 5.50 buffer-copy consumer validation (2026-07-29)

## Scope

This run validated the command format and completion behavior used by
`agcGfx1013CopyBuffer` on a standard PS5 running system software raw
`0x05500008`. SDL's `ps5agc` renderer supplied the consumer path:

1. render into a 1920×1080 flexible-memory BGRA8 target;
2. transition it to copy-source usage;
3. copy 8,294,400 bytes into a direct-memory VideoOut buffer;
4. transition the destination for host read/presentation;
5. wait on a bounded fence, present, invalidate, and read back.

The copy is split into four raw gfx1013 `DMA_DATA` packets: three
`0x1ffffc`-byte packets and one `0x1e900c`-byte packet. Each packet is seven
dwords (`PKT3_DMA_DATA` count 5), uses L2 source/destination selectors, and
sets CP synchronization.

## Rejected encodings

Two host-tested encodings were disproved before the passing run:

- The eight-dword ACB-form packet placed byte count and addresses in the wrong
  DCB words. FW 5.50 reported a bad packet and an unmapped read at low virtual
  address `0x7e9000`, then reset the graphics queue.
- The recovered `sceAgcDcbDmaData` compatibility wrapper reached the PFP as a
  reserved-bit violation and also reset the queue. It is not the raw memory
  copy format required by this application-facing API.

These failures explain the previous SDL symptom: `SDL_RenderClear` completed,
then `SDL_RenderPresent` timed out in the scanout copy and left the title
stopped until cleanup.

## Passing evidence

With the seven-dword raw packet, the same consumer run produced all of the
following:

- renderer creation and clear submission succeeded;
- the full 8.29 MiB copy fence completed without the prior multi-second wait;
- VideoOut presentation returned;
- two later fill submissions, 120 point calls, six line calls, and
  `SDL_RenderReadPixels` all returned success;
- the title destroyed its renderer/window and exited without intervention;
- the target-only kernel log contained no bad packet, protection fault,
  graphics-queue reset, GPU-reset sequence, reboot, or shutdown event.

The SDL image comparison still reported 3,647 color mismatches out of 4,800
pixels. That is tracked as renderer shader/color/readback correctness and is
not treated here as an exact payload oracle for the copy API. A future
standalone pattern-copy test should add byte-for-byte source/destination
comparison independent of rendering.

## Host and cross-build checks

- Generic host suite: 4,435 assertions passed, 0 failed.
- Prospero static library: built with `-Wall -Wextra -Wpedantic` and no new
  warnings.
- SDL Prospero consumer: relinked against the staged OpenAGC archive before
  every WebSrv launch.
