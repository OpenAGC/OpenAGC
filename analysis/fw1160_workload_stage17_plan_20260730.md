# FW 11.60 workload stage-17 plan

## Newly recovered constructor lifecycle

The earlier workload analysis stopped after the userspace initializer at FW
11.60 vaddr `0xa50`. That helper configures registration metadata, but it is
not the last operation applied to the GPU-visible slot table.

The standard-console module constructor at vaddr `0x74f0` later performs this
sequence at `0x78d8..0x7919`:

1. resolve internal region 2 with helpers `0x650` and `0x670`;
2. fill its complete `0x200` bytes with `0xff`;
3. zero a caller-owned 16-byte result object;
4. call helper `0x86a0`, which initializes a separate 16-byte ioctl argument
   to all ones and issues `0xc010813b`;
5. on ioctl success, copy the returned object to the caller; on failure, leave
   the caller object zero;
6. copy those 16 bytes into the beginning of region 2.

The standard region table at vaddr `0x100e0` identifies region 2 as offset
`0x3a000`, size `0x200`: the same `SceGnmGpuInfo` workload table used by the
active and complete builders. FW 5.50 performs the same sequence at
`0x7c26..0x7c67`, using helper `0x8890`. Its request word and success/failure
semantics are identical.

This corrects the earlier assumption that the 32 GPU-visible workload slots
were zero-initialized. OpenAGC did zero the complete table in stages 11-15.
Sony instead initializes slots 0 and 1 from the 16-byte kernel response (or
zero on query failure) and leaves slots 2-31 as all ones. Because the final
nine-dword packet carries a slot address for GPU consumption, the difference
is relevant even though userspace never dereferences the slot.

The constructor's `INFO_34(0)` call is also shared by FW 5.50 and FW 11.60 and
is not a newly introduced 11.60 prerequisite. Stage 17 isolates the slot-table
lifecycle, not that unrelated probe.

## Private implementation and gate

`agcSonyWorkloadInitializeGpuSlots()` reproduces the deterministic table
transformation and has host coverage for successful-query, failed-query,
alignment, size, and null-input cases. The Prospero-only private helper
`agcProsperoInitializeSonyWorkloadSlots()` performs the exact all-ones
`0xc010813b` request, applies Sony's failure fallback, flushes all `0x200`
bytes, and prints the four returned dwords. It does not enable a public
capability.

Stage 17 otherwise preserves stage 15's exact sequence: version-12 defaults,
async setup, GPU-info property, Gn2/Gn3/Gn4 shadow publications, stream-1
registration, ordinary preflight, exact 40-dword inline workload DCB, complete
cache flush, bounded markers, shutdown, and forced termination.

Artifact SHA-256:
`1af020301ad1656bd952edbaedd860bca012d63bace2cf12dfe0f8b517f133fd`.
It has no `libSceAgcDriver.sprx` dependency.

Run only after a clean reboot and with the cleanup ELF immediately preceding
the gate:

```sh
make -C samples/hw_test deploy_agc_fw1160_stage17 PS5_HOST=10.0.1.39
```

Require two clean passes, including identical seed semantics and both ordered
markers, before promoting FW 11.60 workload support. Then regress the matching
path on FW 5.50. On a stall, use the cleanup ELF and do not repeat the candidate
unchanged.
