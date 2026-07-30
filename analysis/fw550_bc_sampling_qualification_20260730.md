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
