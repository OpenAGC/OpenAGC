# FW 11.60 RGBA16_UINT portable qualification — 2026-07-30

## Artifact

- Target: `RGBA16_UINT`
- SHA-256: `22bd65d4b3f0aec4659685d01b100b4e83617710c6fb01c82f04cd13f0a89a84`
- Runtime: standard PS5, system software `0x11600005`, normalized ABI key
  `0x1160`
- Firmware expectation marker: absent
- `libSceAgc.sprx` dependency: absent
- `libSceAgcDriver.sprx` dependency: absent

The ELF was hashed and copied to its hash-named local pinned path before its
first execution. Both hardware attempts used that exact pinned file through
`deploy_agc_graphics_rgba16_uint_portable_fw1160`, which runs the process
cleanup ELF before each launch.

## Results

Both runs passed and reproduced the same bounded verdict:

- gfx10.3 tuple: `COLOR_16_16_16_16` (`0x0c`), UINT, standard swap
- target: 1536 by 1536, eight bytes per element
- changed samples: 255,744 of 2,359,296
- exact coverage bounds: x=384..1151, y=436..1100 (768 by 665)
- complete four-component samples: 255,744
- every lane range: `0x0000..0xffff`
- every lane exact mismatches: zero
- lane 0 FNV64: `0x95703f620261e483`
- lane 1 FNV64: `0x0c5ac912c637c3c3`
- lane 2 FNV64: `0x5abb99f29418e26f`
- lane 3 FNV64: `0x8ebc907143aab923`
- all pairwise lane hashes differ
- packed FNV64: `0x2aab55d32909e483`
- GPU fence: reached after 0 microseconds
- driver shutdown: `0x00000000`
- pool, command-buffer, unmap, and direct-memory teardown: all
  `0x00000000`

A final process-cleanup launch completed after the second attempt. Websrv,
ps5debug-NG, and the auxiliary listener remained reachable on ports 8080,
744, and 3232.

## Qualification state

- Host-tested: yes
- SPRX/profile-qualified, hardware-unverified profiles: retained separately
- Hardware-qualified: FW 11.60 (`0x11600005`), twice with identical bytes
- FW 5.50: exact-byte replay pending; do not rebuild the ELF before replay

