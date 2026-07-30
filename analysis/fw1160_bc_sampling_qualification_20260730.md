# FW 11.60 BC sampling qualification — 2026-07-30

## BC1 UNORM first attempt

- Artifact SHA-256:
  `22834911ea4c75eb0dfdf445cc38c7d5a79dd4ec273a574d5dc1ab6a462e5a62`
- The cleanup-first runner uploaded and re-read the exact pinned bytes.
- No fresh file-backed `Graphics result` appeared during the bounded 30-second
  poll, so the runner failed closed.
- Ports 8080, 2121, and 744 remained reachable, and ps5debug-NG found no
  residual `eboot.bin`.

This first attempt was inconclusive rather than a BC1 sampling failure and
matched the known websrv loader-stale symptom previously isolated with
RG32_UINT.

## BC1 UNORM clean-reboot diagnosis

After reboot and ps5debug-NG reinjection, the same old artifact executed to a
bounded fatal verdict:

- Runtime profile `0x1160`, submission, completion fence, marker, driver
  shutdown, memory cleanup, and self-termination all succeeded.
- All three mip/layer regions executed, with `224640` changed pixels.
- The GPU returned the expected eight-color family, including intermediate
  endpoint blends `171/84`.
- The oracle reported `121659` exact mismatches because it computed ideal
  integer thirds `170/85`.

The failure is in the CPU oracle, not BC1 sampling. GFX10.3 follows the
standard six-bit BC interpolation weights `43/21`. The corrected oracle applies
those weights to BC1, BC2, and BC3 and maps the resulting `84/171` values in
the SRGB expectation. The runner now treats `FATAL` plus completed memory
cleanup as a ready verdict, avoiding the misleading timeout report.

Corrected firmware-neutral artifacts were preserved before execution:

- BC1 UNORM:
  `db3965f2c8da26273b9683794595612c5b2c216b06a6b05ab05bb579a4842aa5`
- BC1 SRGB:
  `1206fa93091cc0f12043617d9e3f83b4951ef5f727a3aca9a94af73c61d7353f`
- BC2 UNORM:
  `497c8c79b43c49e9079d54167a50fce71b26b5235b4ecf2d2c1034a848513c7a`
- BC2 SRGB:
  `ce34b39c3fdf32034e5755ce990b8bc1fc20e01faa6524b32142244cdc40f83c`
- BC3 UNORM:
  `7a9e27cf713c3d333f7174183109df9ea5ef33b551743d210ca17cf3ec4470fb`
- BC3 SRGB:
  `86f94112a0764d37038b59f0f264a4973cfb0e863c4bf90fe3999ef5494acf6f`

## BC1 UNORM valid-mip diagnosis

The corrected-weight artifact
`b8ec09a3e15b33b20897e0526e93c6a0828931e358563cf3539d5b964e3a991c`
then reduced the mismatch count from `121659` to `46779`. That residual count
corresponded to the undefined mip-1 fetches: a 5x7 base mip has a 2x3 mip 1,
but the shader requested `x=2` and `y=3`. The GPU was not required to match the
CPU decoder for those out-of-range `texelFetch` coordinates.

All shared and dedicated BC sampling shaders now clamp mip 1 to `x=0..1` and
`y=0..2`, and every CPU oracle applies the identical valid coordinate mapping.
All 14 affected firmware-neutral ELFs were rebuilt, dependency-checked,
hash-named, and preserved before any further execution. Retry the newly pinned
BC1 UNORM artifact twice before advancing.

## BC1 UNORM corrected pass 1

The newly pinned artifact
`db3965f2c8da26273b9683794595612c5b2c216b06a6b05ab05bb579a4842aa5`
passed its first guarded FW 11.60 run:

- changed pixels: `224640`
- mip/layer regions: `{74880,74880,74880}`
- exact mismatches: `0`
- packed FNV64: `0x611e681989bb483d`
- completion fence, marker, driver shutdown, memory cleanup, final verdict,
  and self-termination: PASS

## BC1 UNORM corrected pass 2

The identical artifact reproduced every pass-1 value:

- changed pixels: `224640`
- mip/layer regions: `{74880,74880,74880}`
- exact mismatches: `0`
- packed FNV64: `0x611e681989bb483d`
- completion fence, marker, driver shutdown, memory cleanup, final verdict,
  and self-termination: PASS

BC1 UNORM is hardware-qualified on exact FW 11.60. BC1 SRGB is next; FW 5.50
identical-byte replay remains a separate endpoint qualification.

## BC1 SRGB corrected pass 1

Artifact
`1206fa93091cc0f12043617d9e3f83b4951ef5f727a3aca9a94af73c61d7353f`
passed its first guarded FW 11.60 run:

- changed pixels: `224640`
- mip/layer regions: `{74880,74880,74880}`
- exact mismatches: `0`
- packed FNV64: `0x7ed831bc232c8da1`
- completion fence, marker, shutdown, cleanup, verdict, and self-termination:
  PASS

## BC1 SRGB corrected pass 2

The identical artifact reproduced pass 1 exactly, including `224640` changed
pixels, `{74880,74880,74880}` regions, zero mismatches, and FNV64
`0x7ed831bc232c8da1`. Fence, marker, shutdown, cleanup, verdict, and
self-termination passed again.

BC1 SRGB and BC1 UNORM are hardware-qualified on exact FW 11.60. Proceed to
BC4 UNORM and SNORM; FW 5.50 replay remains separate.

## BC4 UNORM first attempt

The valid-mip artifact reached the fence and its independent decoder passed:

- changed pixels: `224640`
- mip/layer regions: `{74880,74880,74880}`
- decode mismatches: `0`
- maximum error: `0`
- packed FNV64: `0x5327e8ad53b3a455`
- shutdown, cleanup, and self-termination: PASS

The final verdict failed only because the generic texture smoke check required
eight distinct colors, while this scalar BC4 fixture intentionally emits six.
Compressed formats now use a three-color baseline diversity floor and retain
their stronger format-specific exact decode, range, channel, and region gates.
Corrected BC4 artifacts were preserved before execution:

- UNORM: `f74c393112fc465eace431a3fe288095ae4b3bf5ee993e8147ed0e9a2f22f2a4`
- SNORM: `022f159f0186aab25222bfd882f9b59b8ab40bdfcf6d9c59da389d057454b28d`

## BC4 UNORM corrected pass 1

The corrected UNORM artifact passed with `224640` changed pixels,
`{74880,74880,74880}` regions, zero decode mismatches, zero maximum error, and
FNV64 `0x5327e8ad53b3a455`. Fence, marker, shutdown, cleanup, verdict, and
self-termination passed.

## BC4 UNORM corrected pass 2

The identical artifact reproduced `224640` changed pixels,
`{74880,74880,74880}` regions, zero decode mismatches/error, and FNV64
`0x5327e8ad53b3a455`. All lifecycle gates passed again. BC4 UNORM is
hardware-qualified on exact FW 11.60; BC4 SNORM is next.

## BC4 SNORM corrected pass 1

Artifact
`022f159f0186aab25222bfd882f9b59b8ab40bdfcf6d9c59da389d057454b28d`
passed with `224640` changed pixels, `{74880,74880,74880}` regions, zero
decode mismatches, zero maximum error, and FNV64 `0x16b22a8b52c7ce8d`.
Fence, marker, shutdown, cleanup, verdict, and self-termination passed. Repeat
the identical bytes once.

## BC4 SNORM corrected pass 2

The identical artifact reproduced `224640` changed pixels,
`{74880,74880,74880}` regions, zero decode mismatches/error, and FNV64
`0x16b22a8b52c7ce8d`. All lifecycle gates passed again. BC4 UNORM and SNORM
are hardware-qualified on exact FW 11.60.

## BC2 corrected artifacts

After the shared valid-mip and compressed-diversity corrections, both BC2
variants were rebuilt, passed firmware-neutral dependency verification, and
were preserved before execution:

- UNORM: `e86a53fdb7b3c65cf13dcf66ca0588867d1cb37fab6bbf5b446c654948847b5b`
- SRGB: `d182824d912f7473f25beeba80ffc58e3ceb31d8a5b7bff678d2237b24c9c5b8`

## BC2 UNORM pass 1

The pinned UNORM artifact passed with `224640` changed pixels,
`{74880,74880,74880}` regions, alpha range `0..255`, zero exact mismatches,
and FNV64 `0xf3b07b5935bb483d`. All lifecycle gates passed. Repeat the
identical bytes once.

## BC2 UNORM pass 2

The identical artifact reproduced `224640` changed pixels,
`{74880,74880,74880}` regions, alpha range `0..255`, zero exact mismatches,
and FNV64 `0xf3b07b5935bb483d`. All lifecycle gates passed again. BC2 UNORM
is hardware-qualified on exact FW 11.60; BC2 SRGB is next.

## BC2 SRGB pass 1

The pinned SRGB artifact passed with `224640` changed pixels,
`{74880,74880,74880}` regions, alpha range `0..255`, zero exact mismatches,
and FNV64 `0x0a8a977e6f2c8da1`. All lifecycle gates passed. Repeat the
identical bytes once.
