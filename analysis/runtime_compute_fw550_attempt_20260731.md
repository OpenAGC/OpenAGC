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

Both failing artifacts then correctly refused to destroy the pending fence,
command buffer, resources, queue, and device with `AGC_ERROR_BUSY`. After the
second attempt, the web launcher and ps5debug-NG command ports remained
reachable, so this did not leave the console unresponsive.

## Consequence

Successful submission is not evidence that the public runtime stream executes
or that its completion write becomes CPU-visible. Direct DCB routing alone did
not resolve the timeout and does not promote any native runtime capability.

Do not rerun either artifact unchanged. The next diagnostic must be a changed
public-runtime EOP-only command submission using the same command-buffer and
fence allocation path. Its result will distinguish completion visibility from
the reflected shader/descriptor/dispatch stream before another workload test.
