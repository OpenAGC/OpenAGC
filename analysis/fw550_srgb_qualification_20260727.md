# FW 5.50 sRGB Render-Target Qualification

Date: 2026-07-27  
Hardware: standard PS5 gfx1013  
Firmware: raw `0x05500008`, ABI profile `0x0550`  
Deployment: foreground curl/websrv only

## API and host evidence

`AGC_GFX1013_RT_FORMAT_RGBA8_SRGB` and
`AGC_GFX1013_RT_FORMAT_BGRA8_SRGB` are appended as public enum values 12 and
13. Existing values, including R11G11B10 value 11, remain unchanged. Both use
gfx1013 CB format `0x0a`, CB number type `6`, four bytes per pixel, and
FP16_ABGR shader export. RGBA uses standard component swap and produces
`CB_COLOR0_INFO = 0x00010628`; BGRA uses alternate swap and produces
`0x00010e28`. Exact 28-dword host fixtures lock both complete target streams.

The CB number type is intentionally distinct from texture descriptor sRGB
encodings. Local Mesa gfx10.3 reference data independently confirms
`NUMBER_SRGB = 6`, normal 8_8_8_8 format selection, component-swap handling,
and FP16_ABGR export behavior.

## Native packed-memory oracle

Each sample executes two otherwise identical fenced Wave32 draws. The first
writes a native UNORM control target; the second writes a native sRGB target.
The CPU then compares the two mapped GPU allocations before any preview
conversion or copy:

- Changed-pixel masks must match exactly.
- Alpha bytes must remain identical because sRGB conversion applies only to
  RGB.
- Every packed RGB byte must fall inside the IEC 61966-2-1 output interval for
  the complete linear interval capable of quantizing to its paired UNORM8
  control byte.
- More than 1,000 channels must differ from the linear control, preventing an
  identity/no-conversion implementation from passing.
- FNV64 hashes over changed native packed pixels must repeat for identical
  ELFs.

This checks actual render-target memory rather than relying on VideoOut color
management or visual appearance.

## Passing matrix

| Format | Runs | Pixels per target | Coverage mismatch | Alpha mismatch | Transfer mismatch | Converted channels | Fences | Flips |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| RGBA8 SRGB | 2 | 126,360 | 0 | 0 | 0 | 157,421 | 2x PASS/run | 1,800/1,800 |
| BGRA8 SRGB | 2 | 126,360 | 0 | 0 | 0 | 157,421 | 2x PASS/run | 1,800/1,800 |

Every draw also passed the Wave32 NGG/PS register audit, reusable color-target
builder, interleaved vertex fetch, bound u16 indexed draw, bilinear texture
sampling, expected coverage window, and marker `0xdeadcafe`.

The two RGBA runs reproduced linear FNV64 `0xc5d04c1ca97fe68f` and sRGB FNV64
`0x93794babd11efda7`. The two BGRA runs reproduced linear FNV64
`0xe9fbb8e3a93537a3` and sRGB FNV64 `0x9f05255a918c5ba3`. The distinct hash
pairs and user captures confirm standard versus alternate packed color order.
No timeout, GPU reset, kernel panic, or UI crash occurred.

## Artifacts

ELF SHA-256:

```text
006fc6e37f972338599e7bfb5ea09063179de80b0a027e8288053301673d3484  agc_graphics_rgba8_srgb.elf
01b7ad16afa9964d0f8fad7e7801bb47d97c7e639b335e73bdca23e09ff2dbe0  agc_graphics_bgra8_srgb.elf
```

Log SHA-256:

```text
03237dd3b6d06d8ff8e0b5d547f56492ce11a78faa24fbb1996cea54708bb787  rgba8-srgb-run1.log
d74d942a1e1166d4f517e7a9b4dd02d79317dfc71dd570734a8b304be1b1f781  rgba8-srgb-run2.log
7f04c34d11b3766ab867a61d8809056b724fe617d32251649ef55bd8ba360f87  bgra8-srgb-run1.log
4370e92d30762168165ec3b97841e8d71e2392cc2483c753aa9620b6d4b2c89b  bgra8-srgb-run2.log
```

Raw logs remain local under
`samples/hw_test/conformance-logs/srgb-20260727/` and are not committed.

## Next format work

Qualify further 16-bit color tuples in increasing hardware-risk order. Begin
depth/stencil expansion only afterward with isolated D16 and S8-only targets,
then D16+S8, before enabling any compressed combination.
