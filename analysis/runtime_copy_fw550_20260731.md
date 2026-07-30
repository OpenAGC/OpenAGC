# FW 5.50 native-runtime typed buffer-copy qualification

Date: 2026-07-31
Console: standard PS5, system software `5.500.008` (runtime ABI key `0x0550`)
Artifact: `6724c1371af5cec112abbd2f60cca34dbd61d4631f8b9dd3f79ceb9e6f9a8822` (`samples/hw_test/agc_runtime_copy.elf`)

## Scope

The artifact exercises only the public runtime API on one compute queue. It
creates an upload source buffer and a readback destination buffer, writes 256
deterministic source words with `agcWriteBuffer`, and records the exact state
chain `undefined -> host-write -> copy-source` for the source and
`undefined -> copy-destination -> host-read` for the destination. It records
one `agcCmdCopyBuffer` of 1,024 bytes, submits one command buffer, waits on one
runtime-owned fence for at most 200 ms, then reads the destination through
`agcReadBuffer`.

## Result

All device, queue, resource, command, transition, typed-copy, submit, fence,
readback, reset, and teardown operations returned `AGC_OK`. The fence reached
submission/completion value `1`, and all 256 destination words matched the
source. Both FNV-1a hashes were `0x588c119c73f54d83`.

```
Copy verification: words=256 source-fnv64=0x588c119c73f54d83 destination-fnv64=0x588c119c73f54d83 PASS
Native runtime typed buffer copy result: PASS
```

This hardware-qualifies the exact standard-PS5 FW 5.50 public runtime row for
same-queue upload-to-buffer-copy-to-readback and its internally emitted
`DMA_DATA` carrier. It does not qualify large or multi-packet copies,
same-buffer disjoint copies, images, graphics-queue copies, compute-produced
copy sources, copied shader inputs, cross-queue ownership transfer, or any
other firmware profile.
