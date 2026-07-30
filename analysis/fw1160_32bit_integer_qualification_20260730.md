# FW 11.60 32-bit integer qualification — 2026-07-30

All executions use the committed, firmware-neutral, hash-pinned ELFs through
the cleanup-first file-backed graphics runner. Every pass requires the exact
runtime ABI, bounded fence, per-lane exact-value oracle, clean driver and memory
teardown, self-termination, and a ps5debug-NG residual-process check.

## RGBA32_UINT pass 1

- Artifact SHA-256:
  `81eaf3d07304cd4d1be7ccca8a332f3f40dac8e605be14fcc6ae87c0bcdd1de8`
- 255,744 complete four-lane samples in exact bounds.
- Every lane spans `0x00000000..0xffffffff`, has eight distinct values, and
  has zero exact mismatches.
- Lane hashes: `0xac93e4f1b2bde483`, `0x998a600f39b5c3c3`,
  `0x88a10f9d0dace26f`, `0x964d056ca248b923`.
- Packed FNV64: `0x4d36e6ccd1b3e617`; channel independence passed.
- Fence, teardown, self-termination, and debugger absence passed.

One identical-byte replay remains before promotion.

## RGBA32_UINT pass 2

The identical pinned bytes reproduced every sample count, full-range lane,
zero mismatch count, lane hash, packed hash, fence, teardown,
self-termination, and debugger absence. RGBA32_UINT is hardware-qualified on
FW 11.60.

## R32_SINT pass 1

- Artifact SHA-256:
  `048b903713ce1a0b82e0d3dc5c01b37f2ea068da5e2ce6164bc5ae938c02f32f`
- 255,744 exact samples span `-2147483648..2147483647`, contain eight
  distinct values, and have zero mismatches.
- Lane FNV64: `0x74b09081b2bde483`; packed FNV64:
  `0x7e0438a1fbf7bf83`.
- Fence, teardown, self-termination, and debugger absence passed.

One identical-byte replay remains.
