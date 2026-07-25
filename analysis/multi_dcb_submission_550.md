# FW 5.50 Multi-DCB Submission

## Result

Multiple graphics DCBs that belong to one frame must be passed as one CB
descriptor array to `sceAgcDriverSubmitMultiCommandBuffersDirect`. OpenAGC's
`sceAgcDriverSubmitMultiDcbs` and `sceAgcDriverSubmitMultiCommandBuffers`
wrappers preserve that array and issue one `SUBMIT_16` ioctl.

Do not implement these wrappers by looping over `sceAgcDriverSubmitDcb`.

## Kernel contract

FW 5.50 `gc_frame_submit_internal` at kernel VA `0xb7da90`:

1. Validates the descriptor count is between 1 and `0xfff`.
2. Allocates `count * 16` bytes of graphics-ring space.
3. Copies the complete user descriptor array into that allocation.
4. Validates each descriptor as `IT_INDIRECT_BUFFER` (`0x3f`) or
   `IT_INDIRECT_BUFFER_CONST` (`0x33`).
5. Inserts the process VMID and commits the complete range as one frame.

This matches the existing 16-byte `AgcGcCommandBuffer` layout and the
descriptor-array implementation in `driver_prospero.c`.

## Hardware evidence

The `agc_init.elf` test allocates two DCBs and two completion markers in one
GPU-visible flexible-memory region. Each DCB contains a distinct `WRITE_DATA`
packet followed by a trailer NOP.

Observed on a real PS5 running FW 5.50:

- Two separate `sceAgcDriverSubmitDcb` ioctls both returned `AGC_OK`, but only
  the first marker changed: `D001CAFE`, `00000000`.
- One `sceAgcDriverSubmitMultiCommandBuffersDirect` call with both descriptors
  returned `AGC_OK`, and both markers changed in order: `D001CAFE`,
  `D002CAFE`.

The second result proves that the GPU can read both DCB addresses and that the
descriptor array is consumed in order.

## SUBMITDONE warning

Issuing `AGC_GC_IOCTL_SUBMITDONE` automatically after every standalone DCB made
the two marker submissions execute, but a later workload-complete submission
stalled and the console display froze. That experiment was removed.

`SUBMITDONE` must not be used as a generic per-submit fence until its complete
FW 5.50 state machine and call-site contract are independently recovered.
Descriptor-array batching is the hardware-proven path for multiple DCBs in one
frame.
