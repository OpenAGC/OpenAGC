# FW 11.60 R16_UINT portable qualification — 2026-07-30

## Contract and artifact

`AGC_GFX1013_RT_FORMAT_R16_UINT` is append-only public enum value 20 and maps
to gfx1013 `(format=0x02, number=UINT, swap=standard)`, two bytes per pixel,
and UINT16_ABGR pixel export 7. The corrected immutable artifact is:

```text
samples/hw_test/pinned/agc_graphics_r16_uint-aabefd4d05f8d7ea7f56f917ae79c23f60eccf801627f06a053451e74ae8bf18.elf
SHA-256 aabefd4d05f8d7ea7f56f917ae79c23f60eccf801627f06a053451e74ae8bf18
```

It was copied to the hash-named local path before execution. The neutral
verifier found no expected-firmware marker and no `libSceAgc.sprx` or
`libSceAgcDriver.sprx` dependency. FW 11.60 and FW 5.50 deploy targets have no
build prerequisite and authenticate both local and uploaded bytes.

## Host and compiler evidence

The host suite locks exact `CB_COLOR0_INFO=0x00070408`, UINT16_ABGR export 7,
the full 28-dword PM4 stream, all 39 active profiles, every short capacity from
zero through 27 dwords, exact-capacity success, invalid-enum behavior, and
64-bit layout limits. The generic suite passes `7869 passed, 0 failed`; the
clean Prospero cross-build passes.

Sibling psbc API v13 selects the UINT16_ABGR epilog explicitly. Commit
`c624c5c` also clears Mesa/ACO's 8-bit integer clamp for UINT16/SINT16 exports.
The dedicated integer fragment shader derives each result from integer pixel
coordinates, allowing the CPU oracle to compute the exact expected native
`uint16_t` value rather than accepting approximate conversion.

## FW 11.60 results

The identical pinned ELF ran twice on standard FW `0x11600005`, with the
process-cleanup ELF immediately before every launch. Both corrected runs
reproduced:

| Oracle | Result |
| --- | --- |
| Runtime profile | standard PS5, ABI `0x1160`, PASS |
| DCB/fence | 2,461 dwords, fence at 0 us |
| Coverage | 255,744 pixels, exact `768x665` bounds |
| Native range | `0x0000..0xffff` |
| Distinct values | at least 8 |
| Exact mismatches | 0 |
| Native FNV64 | `0x95703f620261e483` |
| Teardown | driver and every memory cleanup field zero, PASS |

A final cleanup ran after the second result. TCP ports 8080, 744, and 3232
remained reachable, and no fault record was emitted during the bounded check.

## Qualification boundary

R16_UINT is hardware-qualified only on exact FW 11.60. The exact corrected
bytes are preserved for later FW 5.50 replay; FW 5.50 and every other active
profile remain hardware-unverified. The retired SHA from the diagnostic first
attempt must not be rerun; see
`analysis/fw1160_r16_uint_first_attempt_20260730.md`. The next ordered
milestone is RG16_UINT using the same exact-coordinate integer fixture with
independent two-lane validation.
