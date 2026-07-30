# FW 5.50 descriptor-state gate regression

Date: 2026-07-31
Console: standard PS5, system software `5.500.008` (runtime ABI key `0x0550`)
Artifact: `3e04730568e5a2695b7ea8e888e097e04b922817de031498d0269ba5d09e819b` (`samples/hw_test/agc_runtime_compute_copy_chain.elf`)

## Scope and result

The runtime now rejects descriptor binds unless the resource has an explicit
compatible state on the command queue: sampled/uniform/input descriptors
require `shader-read`; storage descriptors require either `shader-read` or
`shader-write` because current reflection does not retain per-binding access
qualifiers. Generic tests cover the untransitioned rejection and accepted
storage states.

The exact FW 5.50 three-DCB compute-to-copy-to-shader artifact passed through
this gate: both reflected producer/consumer descriptor binds returned
`AGC_OK`, the one bounded batch fence completed, and all 64 final words were
`0xff40b0ff`. This preserves the existing workload qualification while proving
that its explicit typed states satisfy the new gate. It does not qualify
per-binding read/write access reflection, graphics/image descriptor workloads,
or other firmware profiles.
