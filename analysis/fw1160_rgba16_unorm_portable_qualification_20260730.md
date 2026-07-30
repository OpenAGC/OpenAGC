# FW 11.60 RGBA16_UNORM portable qualification — 2026-07-30

## Scope and artifact

`AGC_GFX1013_RT_FORMAT_RGBA16_UNORM` is an appended public enum value. It
selects the gfx1013 color-buffer tuple `(format=0x0c, number=UNORM,
swap=standard)`, eight bytes per pixel, and the `FP16_ABGR` pixel-shader
export. The firmware-neutral hardware artifact is pinned locally as:

```text
samples/hw_test/pinned/agc_graphics_rgba16_unorm-13ca0dfaa743438301ecbe5d5255c0168bb89a80bf3ff0e68cdeae8a34908c88.elf
SHA-256 13ca0dfaa743438301ecbe5d5255c0168bb89a80bf3ff0e68cdeae8a34908c88
```

The verifier found no `AGC_EXPECT_FIRMWARE_ABI_KEY` and no dependency on
`libSceAgc.sprx` or `libSceAgcDriver.sprx`. The FW 11.60 and FW 5.50 deploy
targets have no build prerequisite and authenticate both the local and
uploaded bytes. The exact pinned file must be replayed on FW 5.50; rebuilding
does not qualify the endpoint.

## Retired first stimulus

The first pinned experiment (`a27e0243663a87a42619f12a019cba4671216f770fd9cdcae8101fb153273588`)
completed submission and its fence immediately, stored a four-component
64-bit target, and shut the driver down. It failed the bounded readback oracle:
the reused sampled-texture stimulus generated only an R/G gradient on this
path, so B remained zero and A duplicated G. The file-backed log therefore
never reached the required final `Graphics result:` line and the guarded
runner stopped. This was a test-stimulus defect, not evidence against the
color-target tuple. Do not rerun that artifact.

The replacement fragment fixture emits four distinct full-range float
functions of one interpolant. It retains the normal FP16 export path while
making every native UNORM16 destination lane independently observable.

## Host qualification

Host fixtures lock:

- exact 28-dword PM4 state, including `CB_COLOR0_INFO=0x00028030`;
- enum append value `16`, table selection, eight bytes per pixel, and
  `FP16_ABGR` export;
- atomic rejection for every short capacity from 0 through 27 dwords;
- 64x32 block padding, pitch, slice size, allocation size, and large valid
  64-bit layout arithmetic;
- overflow/limit rejection without mutating the output;
- selection and initialization under all 39 active firmware profiles.

The clean generic build passed all four CTest gates and `6454 passed, 0
failed`. The clean Prospero cross-build and firmware-neutral ELF verifier also
passed without compiler warnings.

## FW 11.60 results

The identical pinned bytes ran twice on the standard PS5 at runtime version
`0x11600005`. Each run was immediately preceded by the process-cleanup ELF.
Both runs reproduced:

| Oracle | Result |
| --- | --- |
| Runtime ABI/profile | `0x1160`, standard PS5, PASS |
| DCB/fence | 2,464 dwords, fence at 0 us |
| Coverage | 255,744 pixels, bounds `768x665` |
| Packed native FNV64 | `0xbad47fbdb2e3991e` |
| Lane 0 | 255,681 values, `0x0000..0xffbf`, `0xca74ff0f14f4450a` |
| Lane 1 | 255,682 values, `0x002d..0xffdf`, `0xe1316e6cb931624d` |
| Lane 2 | 255,682 values, `0x0000..0xffdf`, `0xe3b98653a2748a50` |
| Lane 3 | 255,682 values, `0x0000..0xffdf`, `0x6c08e86a07f48197` |
| Independence | all lane hashes pairwise distinct, PASS |
| Teardown | driver and all memory cleanup fields zero, PASS |

Every lane had at least eight distinct native values and approached both UNORM
endpoints. The apparent difference between total coverage and per-lane changed
counts is expected: `0x3555` is both the initialization sentinel and a legal
UNORM value. The oracle therefore combines bounded coverage, per-lane ranges,
diversity, hashes, and channel independence instead of rejecting legal
sentinel collisions.

After the second run, the cleanup ELF was launched again. A direct ps5debug-NG
process-list query enumerated 157 processes and found no `eboot.elf` or
`eboot.bin`. Websrv TCP 8080, the debugger on TCP 744, and its kernel-log
forwarder on TCP 3232 remained reachable; the bounded kernel-log check emitted
no fault record.

## Qualification boundary

The tuple is host-tested for all active profiles and hardware-qualified only
on exact FW 11.60. FW 5.50 and the other profiles remain
SPRX/profile-qualified but hardware-unverified. The next format work without
FW 5.50 hardware is `R16_SNORM`; preserve this RGBA16_UNORM ELF unchanged for
the later FW 5.50 endpoint replay.
