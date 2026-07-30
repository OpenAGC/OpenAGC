# Gfx1013 integer color-export contract — 2026-07-30

## Evidence

The local Mesa gfx10.3 register database defines `SPI_SHADER_UINT16_ABGR=7`
and `SPI_SHADER_SINT16_ABGR=8`. Mesa's color-format selection maps gfx1013
`COLOR_16`, `COLOR_16_16`, and `COLOR_16_16_16_16` UINT/SINT attachments to
those exports. ACO's pixel epilog implements them with packed unsigned or
signed 16-bit integer conversion, respectively. This is distinct from the
FP16_ABGR export used by OpenAGC's qualified floating-point and normalized
fixtures.

For R16_UINT, the resulting OpenAGC tuple is:

| Property | Value |
| --- | --- |
| Public enum | append-only value 20 |
| CB format | `COLOR_16 = 0x02` |
| Number type | `UINT = 4` |
| Component swap | standard |
| Element size | 2 bytes |
| Pixel export | `UINT16_ABGR = 7` |
| Exact `CB_COLOR0_INFO` | `0x00070408` |

The INFO value includes integer blend bypass and round mode in addition to the
format and number-type fields. Integer attachments are not blendable.

## Host qualification

Host coverage locks the tuple and exact 28-dword PM4 stream, append-only enum
value, all 39 active profile selections, every insufficient capacity from zero
through 27 dwords, exact-capacity success, invalid-enum rejection through the
table count, and 64-bit layout/overflow boundaries. The generic suite passes
`7869 passed, 0 failed`, and the clean Prospero cross-build passes without
warnings.

## Hardware-gate prerequisite

Sibling `openagc-psbc` commit `7706efb` adds API-v13 per-attachment export
selection and the CLI option `--color-export uint16_abgr`. Its library test
locks `SPI_SHADER_COL_FORMAT=7` in the emitted shader record and rejects an
invalid export value. The previous command-line FP16_ABGR default remains
unchanged for existing callers.

OpenAGC now has a dedicated unsigned-integer fragment fixture. Each lane is an
exact function of integer `gl_FragCoord` and cycles through values `n*257`, so
the native oracle can compute the expected `uint16_t` for every covered pixel,
prove full-range diversity, and detect duplicated channels. The portable
R16_UINT ELF builds successfully, passes the firmware-neutral verifier, and
contains no AGC SPRX dependency. It remains mutable and unexecuted: pin and
hash one final artifact before its first hardware run.
