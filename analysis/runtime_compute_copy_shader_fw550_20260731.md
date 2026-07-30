# FW 5.50 native-runtime compute-to-copy-to-shader qualification

Date: 2026-07-31
Console: standard PS5, system software `5.500.008` (runtime ABI key `0x0550`)
Artifact: `83342910f2fd15210f9219796eaccacead441f3bd02b8866d966b54c8a44675d` (`samples/hw_test/agc_runtime_compute_copy_chain.elf`)

## Scope

The artifact uses three ordered public-runtime compute command buffers under
one batch fence. The first reflected shader writes 64 words of `0xff40b0ff`
to a storage/transfer-source buffer. The second command buffer consumes that
state through `AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT`, transitions the
producer from `shader-write` to `copy-source`, transitions a second buffer
from `undefined` to `copy-destination`, records `agcCmdCopyBuffer`, then
transitions that destination to `shader-read`. The third command buffer
records an explicit same-queue `shader-read` batch dependency, binds a second
reflected shader with that copied buffer and a fresh storage output buffer,
dispatches, then transitions the output from `shader-write` to `host-read`.

The consumer shader copies every source word to the output. The host observes
only that final readback after the one runtime-owned fence has completed.

## Result

All artifact setup, shader/pipeline reflection validation, resource
transitions, typed copy, three-command batch submission, bounded 200 ms fence
wait, final readback, resets, and teardown returned `AGC_OK`. All 64 final
words were `0xff40b0ff`.

```
agcQueueSubmit(compute-copy-shader batch): 0x00000000 (AGC_OK)
Compute-copy-shader verification: words=64 color=0xff40b0ff PASS
Native runtime compute-copy-shader result: PASS
```

This hardware-qualifies the exact standard-PS5 FW 5.50 same-queue native
compute-to-buffer-copy-to-shader-read transition chain and reflected consumer
dispatch. It does not qualify images, graphics queue consumers, cross-queue
ownership transfer, partial ranges, multi-packet copies, other resource
usages, or any other firmware profile.
