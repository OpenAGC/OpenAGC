# FW 5.50 Render-Target Format Qualification

Date: 2026-07-27  
Hardware: standard PS5 gfx1013  
Firmware: raw `0x05500008`, ABI profile `0x0550`  
Deployment: foreground curl/websrv only

## Scope

This checkpoint expands the reusable gfx1013 color-target path beyond the
previously proven alternate-swap BGRA8 and `R16G16B16A16_FLOAT` targets. The
typed table and exact host fixtures cover standard-swap `RGBA8_UNORM`,
`RGB10A2_UNORM`, and `R11G11B10_FLOAT`. The dedicated samples retain the same
Wave32 NGG/PS shaders, indexed draw, texture/sampler input, synchronization,
and VideoOut lifecycle while changing only the target tuple and the
format-specific readback oracle.

## Passing matrix

| Case | Target | CB tuple `(format, number, swap)` | Changed pixels | Format oracle | Fence | Flips |
| --- | --- | --- | ---: | --- | --- | --- |
| RGBA8 standard swap | 1920x1080 display | `(0x0a, 0, 0)` | 126,360 | coverage and 8 colors | PASS | 1,800/1,800 |
| RGB10A2 UNORM | 1536x1536 offscreen | `(0x08, 0, 0)` | 255,744 | top-two-bit histogram `{35857,27914,36523,155450}` | PASS | 1,800/1,800 |
| R11G11B10 FLOAT, discovery | 1536x1536 offscreen | `(0x06, 7, 0)` | 255,744 | FNV64 `0x4b75c00e8a6bb04d` | PASS | 1,800/1,800 |
| R11G11B10 FLOAT, locked | 1536x1536 offscreen | `(0x06, 7, 0)` | 255,744 | exact FNV64 match | PASS | 1,800/1,800 |

Every case passed the Wave32 PM4 audit, reusable color-target builder,
interleaved vertex fetch, bound u16 indexed draw, bilinear texture sampling,
completion marker, and expected coverage window. The two offscreen formats
were converted by CPU only for VideoOut preview; validation used their native
packed render-target memory. No timeout, GPU reset, kernel panic, metadata
fault, or UI crash occurred.

The early RGB10A2 diagnostic iterations refined the oracle after showing that
an assumed uniform two-bit alpha value did not describe the actual packed
shader output. They were not GPU failures. The final exact histogram covers
all 255,744 changed pixels and is enforced by the sample.

## Host evidence

`agcGfx1013GetColorTargetFormatInfo` maps each public typed enum to the exact
gfx1013 CB format, number type, component swap, byte size, and compatible SPI
shader export. The golden fixed-function fixture locks
`R11G11B10_FLOAT` to `CB_COLOR0_INFO = 0x00010718`; existing fixture rows lock
the RGBA8 and RGB10A2 tuples. Invalid enum values and atomic short-buffer
behavior remain covered by the generic test suite.

## Artifacts

ELF SHA-256:

```text
ae6a707a67507ee1fce2c7995cee6aabf25cb24a93c23f70e05d65ce7a7f781c  agc_graphics_rgba8_std.elf
0b5b51e5659ae3bc422c96f20eb3365b729a3e43cf6bd8940f7f6592956ae1ce  agc_graphics_rgb10a2.elf
f2302d2d8d0cccad7f75e7c47e2ecb9592b3ce53a908880868e94fefe4e690e8  agc_graphics_r11g11b10.elf
```

Log SHA-256:

```text
69b58c2fb0ac8e1ee85f9339860d917a37baf46de4a8df77021ae18c5053ffb3  rgba8-std-run1.log
9207f5337f8d84c028df8a75721cfd7f18c8835609a5bb198428bcb22f079d13  rgb10a2-run5-golden.log
e70a75cb86217ed112f9986117530098dc4c80fc6f1e1766329edb61a4aaf96a  r11g11b10-run1-hash.log
0e6719db950a4747e4ad1f80163b19e14dbcbdf77034d1e210b6d6822e0696a2  r11g11b10-run2-golden.log
```

Raw logs remain local under
`samples/hw_test/conformance-logs/formats-20260727/` and are not committed.

## Remaining format work

sRGB encode behavior is next because it can reuse the validated 8_8_8_8
storage path while adding a semantic conversion oracle. Further 16-bit color
tuples follow. Depth/stencil expansion begins afterward with isolated D16 and
S8-only targets, then D16+S8, before enabling any compressed combination.
