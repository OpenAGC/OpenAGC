# FW 5.50 Multi-DCB Submission

## Result

Multiple graphics DCBs that belong to one frame must be passed as one CB
descriptor array to `sceAgcDriverSubmitMultiCommandBuffersDirect`. OpenAGC's
`sceAgcDriverSubmitMultiDcbs` and `sceAgcDriverSubmitMultiCommandBuffers`
wrappers preserve that array and issue one `SUBMIT_16` ioctl.

Before that submit, the FW 5.50 SPRX callback issues ioctl nr=1 with the
8-byte argument `{3, 0}`. OpenAGC mirrors this frame-state transition.

Do not implement these wrappers by looping over `sceAgcDriverSubmitDcb`.

## Kernel contract

FW 5.50 common graphics-ring submit at kernel VA `0xc7da90`:

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
GPU-visible flexible-memory region. Each DCB contains one distinct 5-dword
`WRITE_DATA` packet. The marker payload changes on every iteration so delayed
work cannot be mistaken for current completion.

Observed on a real PS5 running FW 5.50:

- Two separate `sceAgcDriverSubmitDcb` ioctls both returned `AGC_OK`, but only
  the first marker changed: `D001CAFE`, `00000000`.
- Two-descriptor tests returned `AGC_OK` and executed descriptor zero, while
  descriptor one remained pending for more than 15 seconds. The next submit
  caused descriptor one to write the previous iteration's unique marker.
- Cache flush/invalidation, correct `WRITE_DATA` encoding, `WRITE_CONFIRM`
  removal, NOP-only descriptor zero, default-state removal, async setup,
  8/16-dword IB padding, descriptor-array alignment, a post-submit nr=1 close,
  and one nr=0x25 `SUBMITDONE` did not change that one-submit delay.
- Adding a third NOP-only IB descriptor made both marker DCBs execute
  immediately in all three iterations. This established that the payload
  context defers the final ring descriptor, regardless of its contents.
- OpenAGC now allocates a dedicated GPU-visible 16-dword NOP IB during internal
  memory initialization and appends its descriptor after every caller DCB/ACB.
  Two immediate deployments each passed three unique-marker iterations with
  zero polling delay.
- The standalone `sceAgcDriverSubmitDcb` backend path uses the same nr=1
  frame-state operation and appends the same trailer. This prevents a public
  single-DCB submission from returning success while leaving the caller's work
  deferred until another submission.

The repeated result proves that the kernel copies and queues the complete
descriptor array in order, but the exploited-payload graphics-ring context
does not advance its final descriptor until a later submit. The backend trailer
preserves all caller ordering and semantics while ensuring only a harmless NOP
IB remains deferred.

## SUBMITDONE warning

Issuing `AGC_GC_IOCTL_SUBMITDONE` automatically after every standalone DCB made
the two marker submissions execute, but a later workload-complete submission
stalled and the console display froze. That experiment was removed.

`SUBMITDONE` must not be used as a generic per-submit fence until its complete
FW 5.50 state machine and call-site contract are independently recovered.
Descriptor-array batching is the hardware-proven path for multiple DCBs in one
frame.
