# FW 5.50 cross-DCB transition-chain qualification

Date: 2026-07-31
Console: standard PS5, system software `5.500.008` (runtime ABI key `0x0550`)
Artifact: `0748f67a4eabb156bbf66f2ee18e0a20d268309ba1e3645a15997a04f09df5f3` (`samples/hw_test/agc_runtime_compute_batch.elf`)

## Scope

The artifact is the public reflected-compute sample compiled with
`AGC_COMPUTE_BATCH=1` against runtime API v10. Its first compute DCB waits on
the established producer label, transitions the output buffer from `undefined`
to `shader-write`, binds the reflected pipeline, and dispatches 64 threads.
The second DCB records the output's `shader-write -> host-read` transition with
`AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT`, then writes an observable EOP
label. Both DCBs submit as one ordered compute batch and share one fence.

## Result

All setup, recording, producer submit, batch submit, bounded 200 ms fence
waits, label diagnostics, readback, resets, and teardown returned `AGC_OK` on
the standard FW 5.50 profile. The second-DCB label was observed at `1`, and all
64 readback words were `0xff00ff00`.

```
Output verification: PASS
Native runtime compute batch result: PASS
```

This hardware-qualifies the exact same-queue cross-DCB state chain on standard
PS5 FW 5.50. It does not qualify cross-queue dependencies, graphics chains,
partial subresources, larger batches, ownership handoffs, or other firmware
profiles.
