# FW 11.60 RG16_UINT portable qualification — 2026-07-30

## Contract and artifact

`AGC_GFX1013_RT_FORMAT_RG16_UINT` is append-only public enum value 21 and maps
to gfx1013 `(format=0x05, number=UINT, swap=standard)`, four bytes per pixel,
and UINT16_ABGR pixel export 7. Its immutable local artifact is:

```text
samples/hw_test/pinned/agc_graphics_rg16_uint-4195cc77045496d589aa846ec256116477fefb0d7b4cc5cd890155951cca596b.elf
SHA-256 4195cc77045496d589aa846ec256116477fefb0d7b4cc5cd890155951cca596b
```

The ELF was copied to its hash-named path before execution. The neutral
verifier found no expected-firmware marker and no dependency on
`libSceAgc.sprx` or `libSceAgcDriver.sprx`. FW 11.60 and FW 5.50 deploy targets
have no build prerequisite and authenticate local and uploaded bytes.

## Host evidence

The host suite locks exact `CB_COLOR0_INFO=0x00070414`, UINT16_ABGR export 7,
the complete 28-dword PM4 stream, all 39 active profiles, every insufficient
capacity from zero through 27 dwords, exact-capacity success, invalid-enum
behavior, and maximum 64-bit layout arithmetic. The generic suite passes
`8252 passed, 0 failed`; the clean Prospero cross-build passes.

## FW 11.60 results

The identical pinned ELF ran twice on standard FW `0x11600005`, with the
process-cleanup ELF immediately before each launch. Both runs reproduced:

| Oracle | Result |
| --- | --- |
| Runtime profile | standard PS5, ABI `0x1160`, PASS |
| DCB/fence | 2,461 dwords, fence at 0 us |
| Coverage | 255,744 pixels, exact `768x665` bounds |
| Lane 0 | `0x0000..0xffff`, 0 mismatches, FNV64 `0x95703f620261e483` |
| Lane 1 | `0x0000..0xffff`, 0 mismatches, FNV64 `0x0c5ac912c637c3c3` |
| Independence | distinct lane hashes, PASS |
| Packed FNV64 | `0xb4bccb0f2909e483` |
| Teardown | driver and every memory cleanup field zero, PASS |

A final cleanup ran after the second result. TCP ports 8080, 744, and 3232
remained reachable, and no fault record was emitted during the bounded check.

## Qualification boundary

RG16_UINT is hardware-qualified only on exact FW 11.60. The identical bytes
are preserved for later FW 5.50 replay; FW 5.50 and all other active profiles
remain hardware-unverified. The next ordered milestone is RGBA16_UINT using
the same exact-coordinate shader and four independently checked lanes.
