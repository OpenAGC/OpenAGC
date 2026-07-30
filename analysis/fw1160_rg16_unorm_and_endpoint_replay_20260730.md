# Firmware-neutral RG16 UNORM and endpoint replay set

Date: 2026-07-30  
Hardware: standard PS5, raw firmware `0x11600005`  
Backend: direct `/dev/gc`, no AGC SPRX dependency

## RG16 UNORM contract

`AGC_GFX1013_RT_FORMAT_RG16_UNORM` is appended after R16_UNORM so every
existing public enum value remains stable. It maps to gfx1013 CB format
`0x05`, UNORM number type `0`, standard component swap, four bytes per pixel,
and FP16_ABGR shader export. The exact host packet fixture locks
`CB_COLOR0_INFO=0x00028014`; atomic one-dword-short behavior and all 39 active
runtime-profile selections are covered.

The native oracle treats each 16-bit lane independently. Because every 16-bit
pattern is a legal UNORM value, the initialization sentinel `0x7e00` cannot be
used as a strict per-pixel completeness test. Instead, each lane must
independently meet the expected triangle coverage window, span low and high
UNORM values, expose at least eight distinct values, and produce a hash
different from the other lane. The packed target retains its separate
coverage, diversity, bounds, and FNV64 checks.

The discovery artifact rendered both lanes correctly but failed the inherited
strict completeness test when 789 pixels had exactly one lane quantize to the
legal sentinel value. It reached the fence, passed both full-range lane
checks, shut the driver down, and left websrv and ps5debug-NG responsive. It
does not count toward qualification. The corrected artifact SHA-256 is:

```text
d004a33d1d1245964b08ee22b577948d36537c68d9d8c5241ba9e78e4a39f2fd  agc_graphics_rg16_unorm_portable.elf
```

It contains no firmware expectation and no dynamic dependency on
`libSceAgc.sprx` or `libSceAgcDriver.sprx`.

## FW 11.60 hardware result

The corrected bytes passed twice through the hash-verifying cleanup-first
runner. Both runs reproduced:

| Oracle | Result |
| --- | --- |
| Runtime profile | `0x1160`, standard PS5, PASS |
| CB tuple | format `0x05`, number `0`, swap `0` |
| DCB and submission | 2,470 dwords, `AGC_OK` |
| Completion fence | immediate (`0 us`) |
| Changed packed pixels | `255742` |
| Bounds | `x=384..1151`, `y=436..1100` (`768x665`) |
| Lane 0 | changed `255217`, range `0x0000..0xffff`, FNV64 `0x4f17d5e6b1c0d45b` |
| Lane 1 | changed `255478`, range `0x0000..0xffff`, FNV64 `0x7cb38a81e7d27391` |
| Packed FNV64 | `0xf0866450a3c42b45` |
| Channel independence | PASS |
| Driver and memory teardown | all zero / PASS |
| Final verdict | PASS |

## No-rebuild endpoint replay set

The existing FW 11.60-only indirect artifacts embedded a firmware-specific
result path, so they were not suitable as one-binary endpoint evidence. Three
new artifacts use runtime profile selection and the neutral result path. Each
passed twice on FW 11.60 through the same cleanup-first runner before being
copied to its hash-named local pinned path:

| Gate | SHA-256 | Reproduced oracle |
| --- | --- | --- |
| non-indexed `draw_count=2` | `230809ee6ba03cc10cd136fc269d4c9e08f9ccfcec356f23d6d2b31b98df826c` | exact 10-dword packet, two geometries |
| indexed `draw_count=2` | `afea810a97e36dfae517866acc21889202dbb9c63da04746801382442874f5a6` | exact indexed 10-dword packet, two geometries |
| GPU count buffer (`2`) | `78d4a3d3377cc6458e257f9e213b973f7e1c57954d2c4be254af3f0fe19d4227` | exact count-address packet, GPU-selected two records |

All three reproduced 463,430 changed and complete FP16 pixels in bounds
`x=384..1535`, `y=436..1100`, with packed FNV64
`0x4352dc6d19dc690f`, clean shutdown, and final PASS.

The complete future FW 5.50 sequence now consumes only pinned files:

1. `portability_fw550`
2. `deploy_agc_graphics_multi_indirect_portable_fw550`
3. `deploy_agc_graphics_multi_indexed_indirect_portable_fw550`
4. `deploy_agc_graphics_count_indirect_portable_fw550`
5. `deploy_agc_graphics_r16_unorm_portable_fw550`
6. `deploy_agc_graphics_rg16_unorm_portable_fw550`

A Make dry-run proves those targets issue runner commands only and invoke no
compiler. The runners validate local and uploaded SHA-256 values and execute
the cleanup ELF immediately before each payload. Firmware blobs and pinned
ELFs remain host-local and ignored by git, consistent with repository policy.

FW 5.50 hardware is still required. Until the exact pinned bytes pass there,
FW 11.60 is the only hardware-qualified profile for RG16_UNORM and the neutral
replay set is not evidence of cross-firmware hardware qualification.
