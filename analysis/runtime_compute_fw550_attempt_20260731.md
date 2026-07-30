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
| `0754117` (`52a1e82a75cafe5b7541f130e862ae6cf4813ecedd460dd7017408ef2a254775`) | direct DCB plus 174 V8 compute SH defaults | reflected dispatch, 200 ms wait, readback, reset, and teardown passed |

Both failing artifacts then correctly refused to destroy the pending fence,
command buffer, resources, queue, and device with `AGC_ERROR_BUSY`. After the
second attempt, the web launcher and ps5debug-NG command ports remained
reachable, so this did not leave the console unresponsive.

## Consequence

The EOP-only pass proved the public runtime's command allocation, direct DCB
carrier, EOP completion packet, fence allocation, and CPU visibility path on
exact FW 5.50. The failing workload artifacts therefore pointed to emitted
compute state, rather than submission or completion.

Do not rerun either failed workload artifact unchanged. The corrected artifact
retains the proven completion path and isolates the missing state emitted before
`DISPATCH_DIRECT`.

The first corrected difference was the absent V8 compute-default prefix:
`agcCmdDispatch` emits all 174 SH defaults before the existing compute state
and dispatch packets, matching the manually qualified sample's order. The
rebuilt artifact was deployed once through websrv and passed `agcQueueSubmit`,
the 200 ms `agcWaitFence`, buffer readback verification, command reset, and
every destroy call with `AGC_OK`. The launcher completed normally, so the
native reflected compute slice is hardware-qualified for raw `0x05500008`.
This evidence does not extend to the separate native graphics sample or other
firmware profiles.
