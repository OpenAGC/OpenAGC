# Native runtime GPU-label qualification — FW 5.50

## Scope

This record covers runtime API v6 `AgcGpuLabel` on a standard PS5 reporting
raw system software `0x05500008` (FW ABI key `0x0550`). The exact
`agc_runtime_compute.elf` artifact SHA-256 was:

```
acca740f817c032081012ec95c09dd8f66b81288f0d3a68b5eaab8d757f55991
```

The first public command buffer only records `agcCmdSignalGpuLabel`, which
emits an EOP release write to a runtime-owned flexible-memory word. The second
command buffer records `agcCmdWaitGpuLabel` for that exact value before the
existing reflected compute dispatch and `shader-write -> host-read` transition.
The producer and consumer submit back-to-back through the direct DCB carrier;
the process does not wait on the producer fence before submitting the consumer.

## Result

The producer signal, consumer wait, both submissions, and both bounded 200 ms
fence waits returned `AGC_OK`. The consumer's 64-word GPU output read back as
the expected solid `0xff00ff00` value. Both command buffers reset, both fences
destroyed, the label destroyed, and all remaining objects/device tore down with
`AGC_OK`. The websrv HTTP and FTP ports remained reachable afterward.

## Qualification boundary

This hardware-qualifies same-compute-queue exact-value GPU labels on FW 5.50:
the EOP release signal, `WAIT_REG_MEM` consumer dependency, lifetime retention,
and post-completion teardown. It does not qualify submit wait/signal lists,
timeline counters, cross-queue labels/ownership, graphics labels, FW 11.60,
or an application wait whose producer has not first submitted. Those paths
remain fail-closed or unqualified.
