# Native runtime cross-queue label qualification — FW 5.50

The exact `agc_runtime_eop.elf` artifact was:

```
d359230d1f35cde813581418407e7d94cb4b3aa3adf4b244a6c027cc16c0acd7
```

On a standard PS5 reporting raw system software `0x05500008` (FW ABI
`0x0550`), the compute queue submitted an EOP GPU-label signal without a CPU
wait. The graphics queue then submitted a matching `WAIT_REG_MEM` label wait.
Both bounded 200 ms fences completed with `AGC_OK`; both command buffers,
fences, label, queues, and device tore down with `AGC_OK`.

This qualifies a minimal compute-producer/graphics-consumer label dependency
on exact FW 5.50. Resource ownership transfers, cross-queue image/buffer
transitions, submit wait/signal lists, FW 11.60, and graphics-to-compute
ordering remain unqualified.
