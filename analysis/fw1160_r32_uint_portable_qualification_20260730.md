# FW 11.60 R32_UINT portable qualification — 2026-07-30

## Artifact

- Target: `R32_UINT`
- SHA-256: `d2eb57d8e6f5f664a72b1ddfa7452ceae2b72328e0bd49445e18fc9bb233ff8f`
- Runtime: standard PS5, system software `0x11600005`, normalized ABI key
  `0x1160`
- gfx10.3 tuple: `COLOR_32` (`0x04`), UINT, standard swap, four bytes per
  element
- pixel-shader export: `32_R` (`1`)
- firmware expectation marker and AGC SPRX dependencies: absent

The final bytes were hash-named and preserved before execution. Both
qualification runs used the same pinned ELF through the cleanup-first guarded
target.

## Reproduced result

Both guarded runs reproduced:

- changed and complete samples: 255,744 of 2,359,296
- coverage bounds: x=384..1151, y=436..1100 (768 by 665)
- unsigned range: `0x00000000..0xffffffff`
- distinct values: 8
- exact expected-value mismatches: zero
- lane 0 FNV64: `0xac93e4f1b2bde483`
- packed FNV64: `0xc861450bfbf7bf83`
- channel-independence predicate: PASS
- GPU fence: reached after 0 microseconds
- driver shutdown and all four memory teardown results: zero

The first pre-qualification launch also produced the same passing GPU oracle,
but exposed a missing `R32_UINT` case in the guarded runner and was rejected by
the wrapper. Commit `2a7d9b9` added the exact UINT32 checks before the two
qualification runs; the pinned ELF itself did not change.

Final cleanup completed after run two. Ports 8080, 2121, 744, and 3232
remained reachable.

## Qualification state

- Host-tested: yes
- Hardware-qualified: FW 11.60 (`0x11600005`), twice with identical bytes
- FW 5.50: exact-byte replay pending; do not rebuild the pinned ELF

The next 32-bit integer tuple is `RG32_UINT`.
