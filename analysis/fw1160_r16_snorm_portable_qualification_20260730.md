# FW 11.60 R16_SNORM portable qualification — 2026-07-30

## Contract and artifact

`AGC_GFX1013_RT_FORMAT_R16_SNORM` is appended at public enum value 17 and
maps to gfx1013 `(format=0x02, number=SNORM, swap=standard)`, two bytes per
pixel, and the FP16_ABGR pixel-shader export. Its immutable local artifact is:

```text
samples/hw_test/pinned/agc_graphics_r16_snorm-e6aea5164b215d401244ebec13ace8e8ab95fe9e15a8e82d9a59310cfc09e1ef.elf
SHA-256 e6aea5164b215d401244ebec13ace8e8ab95fe9e15a8e82d9a59310cfc09e1ef
```

The ELF was copied to its hash-named path before its first execution. The
firmware-neutral verifier found no expected-firmware marker and no dependency
on `libSceAgc.sprx` or `libSceAgcDriver.sprx`. FW 11.60 and FW 5.50 deploy
targets have no build prerequisite and authenticate both local and uploaded
bytes.

## Host evidence

The host suite locks:

- exact `CB_COLOR0_INFO=0x00028108` in the 28-dword PM4 stream;
- append-only enum value 17, two-byte element size, and FP16_ABGR export;
- tuple selection and target initialization across all 39 active profiles;
- every insufficient capacity from zero through 27 dwords, with cursor and
  command memory preserved, and success at exactly 28;
- 4x layout pitch 1920, padded height 1088, block `128x64`, and exact
  16,711,680-byte slice/allocation;
- maximum valid 64-bit slice `2,147,483,648` and allocation
  `17,592,186,044,416`, plus oversized-dimension rejection without output
  mutation;
- invalid public enum rejection through the table-backed count boundary.

The from-scratch generic build passed all four CTest gates and `6798 passed,
0 failed`. The clean Prospero cross-build passed without compiler warnings.

## Signed native oracle

The reusable SNORM fragment fixture emits four distinct signed functions, so
the same shader can qualify R, RG, and RGBA in order. Native validation treats
each stored component as `int16_t`, but hashes its exact raw 16-bit encoding.
It requires bounded triangle coverage, at least eight values, a minimum at or
below `-0x7000`, a maximum at or above `+0x7000`, distinct lane hashes for
multi-component targets, and deterministic packed hashes. Sentinel collisions
remain legal because every 16-bit pattern can represent a SNORM sample.

## FW 11.60 results

The identical pinned ELF ran twice on standard FW `0x11600005`, with the
process-cleanup ELF immediately before each launch. Both runs reproduced:

| Oracle | Result |
| --- | --- |
| Runtime profile | standard PS5, ABI `0x1160`, PASS |
| DCB/fence | 2,464 dwords, fence at 0 us |
| Coverage | 255,744 pixels, exact `768x665` bounds |
| Signed range | `-32751..32719` |
| Distinct values | at least 8 |
| Native FNV64 | `0x3908f13005165ed7` |
| Teardown | driver and every memory cleanup field zero, PASS |

After run two, cleanup was launched again. ps5debug-NG enumerated 160
processes and found no `eboot.elf` or `eboot.bin`. Its TCP 3232 kernel-log
forwarder remained reachable and emitted no fault record during the bounded
check.

## Qualification boundary

R16_SNORM is hardware-qualified only on exact FW 11.60. The exact bytes are
preserved for later FW 5.50 replay; FW 5.50 and every other active profile
remain hardware-unverified. The next ordered milestone is RG16_SNORM using the
same signed shader and per-lane oracle.
