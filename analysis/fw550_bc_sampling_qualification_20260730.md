# FW 5.50 BC sampling qualification — 2026-07-30

This endpoint qualification replays the exact hash-pinned artifacts that each
passed twice on FW 11.60. Every launch uses the cleanup-first guarded runner,
checks the full runtime version normalized to `0x0550`, and requires a bounded
fence, format-specific readback oracle, driver shutdown, memory cleanup, final
file-backed verdict, self-termination, and no residual `eboot.bin`.

## BC1 UNORM pass 1

Artifact SHA-256:
`db3965f2c8da26273b9683794595612c5b2c216b06a6b05ab05bb579a4842aa5`

- runtime version: `0x05500008`, normalized profile `0x0550`
- changed pixels: `224640`
- mip/layer regions: `{74880,74880,74880}`
- exact mismatches: `0`
- packed FNV64: `0x611e681989bb483d`
- completion fence, marker, driver shutdown, memory cleanup, final verdict,
  self-termination, and residual-process check: PASS

## BC1 UNORM pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, zero exact mismatches, and FNV64
`0x611e681989bb483d`. All lifecycle gates and the post-run residual-process
check passed again.

BC1 UNORM is hardware-qualified on exact FW 5.50 and FW 11.60. BC1 SRGB is
next.

## BC1 SRGB pass 1

Artifact SHA-256:
`1206fa93091cc0f12043617d9e3f83b4951ef5f727a3aca9a94af73c61d7353f`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, zero exact mismatches, and FNV64
`0x7ed831bc232c8da1`. The completion fence, marker, driver shutdown, memory
cleanup, final verdict, self-termination, and residual-process check passed.
Repeat the identical artifact once.

## BC5 UNORM pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, full R/G ranges, zero decode mismatches/error, channel
independence, and FNV64 `0x3bc37aa96460e455`. Every lifecycle gate and the
post-run residual-process check passed again.

BC5 UNORM is hardware-qualified on exact FW 5.50 and FW 11.60. BC5 SNORM is
next.

## BC5 SNORM pass 1

Artifact SHA-256:
`cf6fcaa788fe65fd7b0bb352888dce09be674ddfbbfde2d37faf0ab9cb6a3fe0`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, full remapped R/G ranges, zero decode mismatches/error,
channel independence, and FNV64 `0xf1464077ada8ce8d`. All format, lifecycle,
and residual-process gates passed. Repeat the identical artifact once.

## BC7 SRGB pass 2

The identical artifact reproduced `224640` changed pixels, mode counts
`{4:205880,6:18760}`, alpha range `0..255`, zero exact mismatches, channel
independence, and FNV64 `0x74a3526f9a3eef65`. Every lifecycle gate and the
post-run residual-process check passed again.

BC7 UNORM and SRGB are hardware-qualified on exact FW 5.50 and FW 11.60.
Proceed to BC6 UFLOAT/SFLOAT.

## BC7 UNORM pass 2

The identical artifact reproduced `224640` changed pixels, mode counts
`{4:205880,6:18760}`, alpha range `0..255`, zero exact mismatches, channel
independence, and FNV64 `0xf46729633292d01b`. Every lifecycle gate and the
post-run residual-process check passed again.

BC7 UNORM is hardware-qualified on exact FW 5.50 and FW 11.60. BC7 SRGB is
next.

## BC7 SRGB pass 1

Artifact SHA-256:
`f6a97569f854ddad7080247c44bec6dd1edccc0b7c248a8b1f8701b276c8eb27`

The guarded run produced `224640` changed pixels, mode counts
`{4:205880,6:18760}`, alpha range `0..255`, zero exact mismatches, channel
independence, and FNV64 `0x74a3526f9a3eef65`. All mode, format, lifecycle,
and residual-process gates passed. Repeat the identical artifact once.

## BC5 SNORM pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, full remapped R/G ranges, zero decode mismatches/error,
channel independence, and FNV64 `0xf1464077ada8ce8d`. Every lifecycle gate and
the post-run residual-process check passed again.

BC5 UNORM and SNORM are hardware-qualified on exact FW 5.50 and FW 11.60.
Proceed to BC7 UNORM/SRGB.

## BC7 UNORM pass 1

Artifact SHA-256:
`a98adaa1125c6ab1590d5c3cb1d65e19b0573ccf4f5e5a1ec0b40ad93bb17db6`

The guarded run produced `224640` changed pixels, mode counts
`{4:205880,6:18760}`, alpha range `0..255`, zero exact mismatches, channel
independence, and FNV64 `0xf46729633292d01b`. All mode, format, lifecycle,
and residual-process gates passed. Repeat the identical artifact once.

## BC3 SRGB pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0x4cef62aedf2c8da1`. Every lifecycle gate and the post-run residual-process
check passed again.

BC3 UNORM and SRGB are hardware-qualified on exact FW 5.50 and FW 11.60.
Proceed to BC5 UNORM/SNORM.

## BC5 UNORM pass 1

Artifact SHA-256:
`2ecb276612e06b42e2408e5b5352272493cd2f167eb47bbee79ff4dc6ffebeb7`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, full R/G ranges, zero decode mismatches/error, channel
independence, and FNV64 `0x3bc37aa96460e455`. All format, lifecycle, and
residual-process gates passed. Repeat the identical artifact once.

## BC3 UNORM pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0xae513a67c9bb483d`. Every lifecycle gate and the post-run residual-process
check passed again.

BC3 UNORM is hardware-qualified on exact FW 5.50 and FW 11.60. BC3 SRGB is
next.

## BC3 SRGB pass 1

Artifact SHA-256:
`7a5587e843d43217389b39b86129fa28de83da91d81648b37f4fa13b3fdb2b61`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0x4cef62aedf2c8da1`. All format, lifecycle, and residual-process gates passed.
Repeat the identical artifact once.

## BC1 SRGB pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, zero exact mismatches, and FNV64
`0x7ed831bc232c8da1`. Every lifecycle gate and the post-run residual-process
check passed again.

BC1 UNORM and SRGB are hardware-qualified on exact FW 5.50 and FW 11.60.
Proceed to BC4 UNORM/SNORM in the established risk order.

## BC4 UNORM pass 1

Artifact SHA-256:
`f74c393112fc465eace431a3fe288095ae4b3bf5ee993e8147ed0e9a2f22f2a4`

The guarded run reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, zero decode mismatches, zero maximum error, and FNV64
`0x5327e8ad53b3a455`. The fence, marker, shutdown, memory cleanup, verdict,
self-termination, and residual-process check all passed. Repeat the identical
artifact once.

## BC4 UNORM pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, zero decode mismatches/error, and FNV64
`0x5327e8ad53b3a455`. Every lifecycle gate and the post-run residual-process
check passed again.

BC4 UNORM is hardware-qualified on exact FW 5.50 and FW 11.60. BC4 SNORM is
next.

## BC4 SNORM pass 1

Artifact SHA-256:
`022f159f0186aab25222bfd882f9b59b8ab40bdfcf6d9c59da389d057454b28d`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, zero decode mismatches/error, and FNV64
`0x16b22a8b52c7ce8d`. The signed-range, fence, marker, shutdown, memory
cleanup, verdict, self-termination, and residual-process checks passed. Repeat
the identical artifact once.

## BC4 SNORM pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, zero decode mismatches/error, and FNV64
`0x16b22a8b52c7ce8d`. Every lifecycle gate and the post-run residual-process
check passed again.

BC4 UNORM and SNORM are hardware-qualified on exact FW 5.50 and FW 11.60.
Proceed to BC2 UNORM/SRGB.

## BC2 UNORM pass 1

Artifact SHA-256:
`e86a53fdb7b3c65cf13dcf66ca0588867d1cb37fab6bbf5b446c654948847b5b`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0xf3b07b5935bb483d`. The format oracle, fence, marker, shutdown, cleanup,
verdict, self-termination, and residual-process checks passed. Repeat the
identical artifact once.

## BC2 UNORM pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0xf3b07b5935bb483d`. Every lifecycle gate and the post-run residual-process
check passed again.

BC2 UNORM is hardware-qualified on exact FW 5.50 and FW 11.60. BC2 SRGB is
next.

## BC2 SRGB pass 1

Artifact SHA-256:
`d182824d912f7473f25beeba80ffc58e3ceb31d8a5b7bff678d2237b24c9c5b8`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0x0a8a977e6f2c8da1`. All format and lifecycle gates passed, including the
post-run residual-process check. Repeat the identical artifact once.

## BC2 SRGB pass 2

The identical artifact reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0x0a8a977e6f2c8da1`. Every lifecycle gate and the post-run residual-process
check passed again.

BC2 UNORM and SRGB are hardware-qualified on exact FW 5.50 and FW 11.60.
Proceed to BC3 UNORM/SRGB.

## BC3 UNORM pass 1

Artifact SHA-256:
`54807cec76c1b1e0d6669d4e72110e1dec55933b76a4f40a5da79098bea0b1af`

The guarded run produced `224640` changed pixels, regions
`{74880,74880,74880}`, alpha range `0..255`, zero exact mismatches, and FNV64
`0xae513a67c9bb483d`. All format, lifecycle, and residual-process gates passed.
Repeat the identical artifact once.
