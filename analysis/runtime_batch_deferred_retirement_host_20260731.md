# Runtime batch/deferred-retirement stress — host — 2026-07-31

## Contract gap

The earlier deferred-free API rejected any buffer or image with
`recorded_refs`. That made it impossible to request fence-keyed retirement for
a resource actually referenced by a submitted command buffer: command reset
is forbidden while pending, but after completion and reset the signaled-fence
fast path simply destroyed the object synchronously.

Runtime API v14 separates the retirement request from physical collection.
`agcDestroyBufferDeferred` and `agcDestroyImageDeferred` may mark an object
deferred while command or dependent-object references remain. New API use is
rejected immediately. Collection requires the named fence to complete and the
references to reach zero; transfer-pending resources still reject retirement
because deferring them would prevent the required acquire.

## Generic stress evidence

The exact fixture repeats 32 cycles. Every cycle:

1. Creates one 4 KiB buffer and one 64x64 RGBA8 image.
2. Records `Undefined -> CopyDestination` in the first compute command.
3. Records v2 batch-dependent `CopyDestination -> HostRead` in the second.
4. Adds observable monotonic label signals so both batch members are nonempty.
5. Submits both commands under one fence.
6. Queues both still-command-referenced resources for deferred destruction.
7. Waits at most 200 ms for the batch fence.
8. Proves collection returns `AGC_ERROR_BUSY` while command references remain.
9. Resets both commands, collects both resources, and resets the fence.
10. Verifies deferred count is zero and allocation count/live bytes exactly
    equal the pre-loop baseline.

The present-chain fixture additionally retires an image while the chain retains
it, verifies new presentation is rejected, destroys the chain, and then
collects the image.

The complete host binary passes 16,402 assertions with zero failures. Generic
and Prospero builds complete without warnings.

## Prospero artifact

`samples/hw_test/agc_runtime_retirement_stress.elf` carries the same 32-cycle
sequence and bounded waits.

- SHA-256: `030fc66604db48d217eb7c4b140c16880516419ae82cd76ac829b9168fa47f1f`
- Expected verdict: `BATCH_RETIREMENT_STRESS PASS`
- Expected teardown: both labels, fence, both command buffers, queue, and
  device return `AGC_OK`.

It has not been deployed because the FW 5.50 console services remained
unreachable after the presentation incident. Hardware qualification is still
open.
