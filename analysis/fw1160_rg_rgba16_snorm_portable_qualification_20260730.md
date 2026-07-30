# FW 11.60 RG16/RGBA16_SNORM portable qualification — 2026-07-30

## Contracts and artifacts

`AGC_GFX1013_RT_FORMAT_RG16_SNORM` is appended at public enum value 18 and
maps to gfx1013 `(format=0x05, number=SNORM, swap=standard)`, four bytes per
pixel, and the FP16_ABGR pixel-shader export. Its immutable local artifact is:

```text
samples/hw_test/pinned/agc_graphics_rg16_snorm-cc545a61e7b6689f63a905651acbee900acb52306ea0e732a664f8fe5e662352.elf
SHA-256 cc545a61e7b6689f63a905651acbee900acb52306ea0e732a664f8fe5e662352
```

`AGC_GFX1013_RT_FORMAT_RGBA16_SNORM` is appended at public enum value 19 and
maps to gfx1013 `(format=0x0c, number=SNORM, swap=standard)`, eight bytes per
pixel, and the FP16_ABGR pixel-shader export. Its immutable local artifact is:

```text
samples/hw_test/pinned/agc_graphics_rgba16_snorm-85ce21feba113b77b757dcf21f3292b9fb673e27707d72473bde258ca894748d.elf
SHA-256 85ce21feba113b77b757dcf21f3292b9fb673e27707d72473bde258ca894748d
```

Both ELFs were copied to their hash-named paths before execution. The
firmware-neutral verifier found no expected-firmware marker and no dependency
on `libSceAgc.sprx` or `libSceAgcDriver.sprx`. Their FW 11.60 and FW 5.50
deploy targets have no build prerequisite and authenticate both local and
uploaded bytes.

## Host evidence

The host suite locks both append-only enum values, tuple properties, element
sizes, FP16_ABGR exports, exact 28-dword PM4 streams, all 39 active profile
selections, every insufficient command capacity from zero through 27 dwords,
success at exactly 28 dwords, invalid-enum rejection, and maximum 64-bit
surface-layout arithmetic. The exact `CB_COLOR0_INFO` values are
`0x00028114` for RG16_SNORM and `0x00028130` for RGBA16_SNORM.

The final from-scratch generic build passed all four CTest gates and `7486
passed, 0 failed`. The clean Prospero cross-build passed without compiler
warnings.

## Signed native oracle

The reusable signed fragment fixture emits four distinct signed functions.
Native validation interprets stored components as `int16_t` while hashing
their exact raw 16-bit encodings. Each lane must independently prove bounded
triangle coverage, at least eight values, a minimum at or below `-0x7000`, a
maximum at or above `+0x7000`, and a reproducible lane hash. Multi-component
targets additionally require every pair of lane hashes to differ. The packed
surface hash must also reproduce. Sentinel collisions remain legal because
every 16-bit pattern represents a valid SNORM sample.

## FW 11.60 results

Each pinned ELF ran twice on standard FW `0x11600005`, with the process-cleanup
ELF immediately before every launch. All four runs used a 2,464-dword DCB,
completed their fence at 0 us, covered 255,744 pixels in exact `768x665`
bounds, and reported zero-valued driver and memory teardown fields.

| Artifact | Lane | Signed range | Native FNV64 |
| --- | ---: | --- | --- |
| RG16_SNORM | 0 | `-32751..32719` | `0x3908f13005165ed7` |
| RG16_SNORM | 1 | `-32719..32751` | `0x33cc2d1e90919107` |
| RG16_SNORM | packed | — | `0x4600d1f630de5ed7` |
| RGBA16_SNORM | 0 | `-32751..32719` | `0x3908f13005165ed7` |
| RGBA16_SNORM | 1 | `-32719..32751` | `0x33cc2d1e90919107` |
| RGBA16_SNORM | 2 | `-32751..32751` | `0x8dec7de18410e5c5` |
| RGBA16_SNORM | 3 | `-32751..32751` | `0xa9906913889ca15d` |
| RGBA16_SNORM | packed | — | `0xd3b5d7c030de5ed7` |

Every lane passed the range and diversity checks, and all pairwise channel
independence checks passed. After the final runs, cleanup and ps5debug-NG
process audits found no residual `eboot.elf` or `eboot.bin` among 163
processes after RG16_SNORM and 166 processes after RGBA16_SNORM. The TCP 3232
kernel-log forwarder remained reachable and emitted no fault record during the
bounded checks.

## Qualification boundary

RG16_SNORM and RGBA16_SNORM are hardware-qualified only on exact FW 11.60.
Their exact bytes are preserved for later FW 5.50 replay; FW 5.50 and every
other active profile remain hardware-unverified. This completes the ordered
16-bit normalized group on FW 11.60. The next milestone is R16_UINT, beginning
with evidence for the integer pixel-shader export contract and a dedicated
unsigned-integer output fixture.
