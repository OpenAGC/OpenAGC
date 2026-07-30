# FW 5.50 native runtime compute-workload batch qualification

Date: 2026-07-31
Console: standard PS5, system software `5.500.008` (runtime ABI key `0x0550`)
Artifact: `b4d21c6673d74af2b997e695605018ff4499df4998782fc243f18523e7c7576e` (`samples/hw_test/agc_runtime_compute_batch.elf`)

## Scope

The artifact is the public native-runtime reflected-compute sample compiled
with `AGC_COMPUTE_BATCH=1`. After a separate producer label submission, the
first of two nonempty compute DCBs waits on that label, transitions a readback
storage buffer from `undefined` to `shader-write`, binds the reflected
`fill_color_native` pipeline/descriptors/push constants, dispatches one
64-thread group, and transitions the buffer to `host-read`. The second DCB
writes a distinct EOP GPU label. Both DCBs are submitted in one
`AgcSubmitInfo` batch with one runtime fence.

## Result

The standard FW 5.50 profile returned `AGC_OK` for setup, both DCB recordings,
the producer submit, the two-DCB batch submit, bounded 200 ms waits, GPU-label
diagnostics, 64-word readback, resets, and teardown. The second DCB label was
observed at value `1`, and every output word read back as `0xff00ff00`.

```
Output verification: PASS
Native runtime compute batch result: PASS
```

This hardware-qualifies the exact reflected-compute workload batch on a
standard PS5 FW 5.50: first-DCB dispatch/transition, second-DCB label tail,
one direct multi-DCB submission, batch-fence completion, and host readback. It
does not qualify cross-command-buffer resource state dependencies, graphics
workloads, larger batches, V2 submit-label-list injection beyond its separate
label-only qualification, cross-queue execution, or other firmware profiles.
