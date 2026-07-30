# FW 11.60 RGBA16_SINT portable qualification — 2026-07-30

## Artifact

- Target: `RGBA16_SINT`
- SHA-256: `b9bdb9641c22bfedfb9367fe60ba97baaef84af191d0326001c2e8285afbef34`
- Runtime: standard PS5, system software `0x11600005`, normalized ABI key
  `0x1160`
- gfx10.3 tuple: `COLOR_16_16_16_16` (`0x0c`), SINT, standard swap, eight
  bytes per element
- pixel-shader export: SINT16_ABGR (`8`)
- firmware expectation marker and AGC SPRX dependencies: absent

The final bytes were hash-named and preserved before execution. Both attempts
used the same pinned ELF through the cleanup-first guarded target.

## Reproduced result

Both runs reproduced:

- changed and complete samples: 255,744 of 2,359,296
- coverage bounds: x=384..1151, y=436..1100 (768 by 665)
- every lane signed range: `-32768..32767`
- exact expected-value mismatches: zero in all four lanes (required by each
  lane's PASS predicate)
- lane 0 FNV64: `0x055e15e74e22e483`
- lane 1 FNV64: `0xc23dc31b5413c3c3`
- lane 2 FNV64: `0xfcfd21723cf6e26f`
- lane 3 FNV64: `0xea40b2156dafb923`
- all six pairwise channel-independence comparisons: PASS
- packed FNV64: `0x0a12ca15c78ce483`
- GPU fence: reached after 0 microseconds
- driver shutdown and all four memory teardown results: zero

Final cleanup completed after run two. Ports 8080, 744, and 3232 remained
reachable.

## Qualification state

- Host-tested: yes
- Hardware-qualified: FW 11.60 (`0x11600005`), twice with identical bytes
- FW 5.50: exact-byte replay pending; do not rebuild the pinned ELF

This completes the planned 16-bit UINT/SINT render-target matrix on FW 11.60.

