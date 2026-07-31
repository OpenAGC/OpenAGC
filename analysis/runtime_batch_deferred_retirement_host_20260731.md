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
9. Recycles both commands atomically from the completed fence, collects both
   resources, and resets the fence.
10. Verifies deferred count is zero and allocation count/live bytes exactly
    equal the pre-loop baseline.

The present-chain fixture additionally retires an image while the chain retains
it, verifies new presentation is rejected, destroys the chain, and then
collects the image.

The complete host binary passes 16,902 assertions with zero failures. Generic
and Prospero builds complete without warnings.

## Prospero artifact

`samples/hw_test/agc_runtime_retirement_stress.elf` carries the same 32-cycle
sequence and bounded waits.

- SHA-256: `837183c7d4ad463a50baa993755b55d071845ba479454ede0441901efc66b17d`
- Expected verdict: `BATCH_RETIREMENT_STRESS PASS`
- Expected teardown: both labels, fence, both command buffers, queue, and
  device return `AGC_OK`.

Early preflights found the FW 5.50 console services unreachable and uploaded
nothing. After websrv recovery, the guarded target passed all 32 cycles on raw
system version `0x05500008`. Every cycle completed its batch fence, rejected
premature collection, atomically recycled both command buffers, collected the
retired buffer and image, and returned live/deferred statistics to baseline.
Both labels, the fence, both command buffers, the queue, and the device then
destroyed with `AGC_OK`. The same self-terminating bytes subsequently passed
on exact FW 5.50 and FW 11.60. This hardware-qualifies the stress contract and
API v22 command recycling on both endpoint profiles.

Regression reruns must use the cleanup-first, hash-pinned target:

```sh
make -C samples/hw_test deploy_agc_runtime_retirement_stress \
  PS5_HOST=10.0.1.41
```
