# FW 5.50 compute-batch submit-label-list qualification

Date: 2026-07-31
Console: standard PS5, system software `5.500.008` (runtime ABI key `0x0550`)
Artifact: `95caaab9277368f06db8907147604e8e8dbc3296189fd80e5e15a37f0d46f9a2` (`samples/hw_test/agc_runtime_compute_batch_submit_lists.elf`)

## Scope

The artifact builds `agc_runtime_batch_submit_lists.c` with its queue type set
to `kAgcQueueCompute`. It uses one public compute queue and four 32-dword
command buffers. A producer records an EOP GPU-label signal. The tested
version-2 `AgcSubmitInfo` batch contains two distinct, nonempty compute DCBs:
the first carries an observable label EOP write and receives the runtime's
wait-list prefix; the last carries a second observable EOP write and receives
the runtime's signal-list tail and completion fence. A final consumer
submission waits on the list-produced label. No CPU waits occur between the
three submissions, and the sample emits no shader, dispatch, draw, VideoOut,
or resource-transition commands.

## Result

The runtime selected the standard FW 5.50 profile and returned `AGC_OK` for
creation, recording, producer submit, the two-DCB compute-batch submit with
one wait and one signal, consumer submit, all three bounded 200 ms fence
waits, resets, and teardown. The final verdict was:

```
Native runtime compute batch submit-list result: PASS
```

This hardware-qualifies exactly the native compute-queue batch placement and
ordering path: wait list before the first DCB body, signal list after the last
DCB body, and consumer visibility of that signal on the standard PS5 FW 5.50
profile. It does not qualify dispatched compute workloads in a batch, empty or
larger batches, shader state, resource transitions, cross-queue execution, or
any other firmware profile.
