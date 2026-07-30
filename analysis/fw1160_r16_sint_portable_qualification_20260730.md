# FW 11.60 R16_SINT portable qualification — 2026-07-30

## Artifact and contract

- Target: `R16_SINT`
- SHA-256: `3083e2f6ce3ff30d22508c93e51b58bb73f7a36e9ba5e2d255c3c32be7e79652`
- Runtime: standard PS5, system software `0x11600005`, normalized ABI key
  `0x1160`
- gfx10.3 tuple: `COLOR_16` (`0x02`), SINT, standard swap, two bytes per
  element
- pixel-shader export: SINT16_ABGR (`8`)
- firmware expectation marker: absent
- `libSceAgc.sprx` dependency: absent
- `libSceAgcDriver.sprx` dependency: absent

The dedicated `ivec4` fragment shader emits
`((coordinate & 255) * 257) - 32768`. This spans exactly `-32768..32767`,
avoids the `0xdead` untouched-memory sentinel, and gives the CPU oracle an
exact expected `int16_t` value for every covered sample.

The ELF was hashed and copied to its hash-named local pinned path before its
first execution. Both attempts used that exact pinned file through
`deploy_agc_graphics_r16_sint_portable_fw1160`, with process cleanup
immediately before every launch.

## Reproduced result

Both runs produced the same verdict:

- changed and complete samples: 255,744 of 2,359,296
- coverage bounds: x=384..1151, y=436..1100 (768 by 665)
- signed-range gate: PASS (`int16_t` minimum at most `-0x7000`, maximum at
  least `+0x7000`; the exact coordinate function includes both endpoints)
- exact mismatches: zero
- distinct sampled values: at least eight
- lane and packed FNV64: `0x055e15e74e22e483`
- GPU fence: reached after 0 microseconds
- driver shutdown: `0x00000000`
- pool, command-buffer, unmap, and direct-memory teardown: all
  `0x00000000`

The log's generic hexadecimal summary orders the same signed samples as
`uint16_t` (`0x0080..0xff7f`); it is diagnostic text only. The qualification
predicate uses separately accumulated `int16_t` minima/maxima and exact
signed expected values.

A final cleanup ran after the second result. Ports 8080, 744, and 3232
remained reachable.

## Qualification state

- Host-tested: yes
- Hardware-qualified: FW 11.60 (`0x11600005`), twice with identical bytes
- FW 5.50: exact-byte replay pending; do not rebuild the pinned ELF

