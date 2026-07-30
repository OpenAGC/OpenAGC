# FW 5.50 graphics-batch submit-label-list qualification

Date: 2026-07-31
Console: standard PS5, system software `5.500.008` (runtime ABI key `0x0550`)
Artifact: `32112756c2446146758409b1605fa8c55a6385d270f454af2cadcfb4262d054b` (`samples/hw_test/agc_runtime_batch_submit_lists.elf`)

## Scope

`agc_runtime_batch_submit_lists.elf` uses one public graphics queue and four
32-dword command buffers. A producer records an EOP GPU-label signal. The
tested `AgcSubmitInfo` v2 batch contains two distinct, nonempty graphics DCBs:
the first carries an observable label EOP write and receives the runtime's
wait-list prefix; the last carries a second observable EOP write and receives
the runtime's signal-list tail plus its completion fence. A final consumer
submission waits on the list-produced label. No CPU waits occur between the
three submissions, and the sample emits no shader, draw, dispatch, VideoOut,
or resource-transition commands.

## Result

The runtime reported the standard FW 5.50 profile and returned `AGC_OK` for
device/queue/object creation, all command recording, producer submit, the
two-DCB graphics batch submit with one wait and one signal, consumer submit,
all three bounded 200 ms fence waits, resets, and teardown. The final verdict
was:

```
Native runtime graphics batch submit-list result: PASS
```

This hardware-qualifies exactly the native graphics-batch placement and
ordering path: wait list before the first DCB body, signal list after the last
DCB body, and consumer visibility of that signal on the standard PS5 FW 5.50
profile. It does not qualify graphics/compute batches, empty batches, shader or
render state, resource transitions, cross-queue execution, broader list sizes,
or any other firmware profile.
