# FW 11.60 RG16_SINT portable qualification — 2026-07-30

## Artifact

- Target: `RG16_SINT`
- SHA-256: `6b91d8e21d8a47e4fd3a529c4f5637c46fab42ba22a62d8bbaed6c74befeaac0`
- Runtime: standard PS5, system software `0x11600005`, normalized ABI key
  `0x1160`
- gfx10.3 tuple: `COLOR_16_16` (`0x05`), SINT, standard swap, four bytes per
  element
- pixel-shader export: SINT16_ABGR (`8`)
- firmware expectation marker and AGC SPRX dependencies: absent

The final bytes were hash-named and preserved before execution. Both attempts
used the same pinned ELF through the cleanup-first guarded target.

## Reproduced result

Both runs reproduced:

- changed and complete samples: 255,744 of 2,359,296
- coverage bounds: x=384..1151, y=436..1100 (768 by 665)
- lane 0 signed range: `-32768..32767`
- lane 1 signed range: `-32768..32767`
- exact expected-value mismatches: zero in both lanes (required internally by
  each lane's PASS predicate)
- lane 0 FNV64: `0x055e15e74e22e483`
- lane 1 FNV64: `0xc23dc31b5413c3c3`
- channel independence: PASS
- packed FNV64: `0xd283b845c78ce483`
- GPU fence: reached after 0 microseconds
- driver shutdown and all four memory teardown results: zero

Final cleanup completed after run two. Ports 8080, 744, and 3232 remained
reachable.

## Qualification state

- Host-tested: yes
- Hardware-qualified: FW 11.60 (`0x11600005`), twice with identical bytes
- FW 5.50: exact-byte replay pending; do not rebuild the pinned ELF

