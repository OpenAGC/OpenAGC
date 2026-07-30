# Native runtime compute FW 5.50 attempts — 2026-07-31

## Scope

This record covers the public-only `agc_runtime_compute.elf` oracle on a
standard PS5 with raw system-software value `0x05500008` (FW ABI key `0x0550`).
The console selected `prospero-gc-submit16-standard-standard`; device setup,
default states, async setup, shader/pipeline/resource creation, command
recording, and `agcQueueSubmit` all returned `AGC_OK` in both attempts.

## Attempts

| Artifact | Submission policy | Result |
| --- | --- | --- |
| pre-`4938c8b` | user-special queue plus ACB | `agcWaitFence` timed out after 200 ms |
| `4938c8b` (`64304148af2ff77c7155eb2f3e705a8d993cf856bb95318efb7f3f9fe2a071dd`) | `SetupAsyncGraphics(1)` plus direct DCB | `agcWaitFence` timed out after 200 ms |
| `dc9ab11` (`27b2018483ad520a4ab18b1bb642e78250124dfc17e154e7bdb25fd7f634681a`) | direct DCB, no application commands plus runtime EOP | submit, 200 ms wait, reset, and teardown passed |

Both failing artifacts then correctly refused to destroy the pending fence,
command buffer, resources, queue, and device with `AGC_ERROR_BUSY`. After the
second attempt, the web launcher and ps5debug-NG command ports remained
reachable, so this did not leave the console unresponsive.

## Consequence

The EOP-only pass proves the public runtime's command allocation, direct DCB
carrier, EOP completion packet, fence allocation, and CPU visibility path on
exact FW 5.50. The failing workload artifacts therefore point to the emitted
compute state, rather than submission or completion. This does not promote the
broader reflected compute-pipeline capability.

Do not rerun either workload artifact unchanged. The next diagnostic must
retain this proven completion path while isolating the compute state emitted
before `DISPATCH_DIRECT`.
