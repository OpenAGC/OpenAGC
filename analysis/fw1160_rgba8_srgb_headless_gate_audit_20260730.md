# FW 11.60 headless RGBA8 and sRGB gate audit

## Scope

The retained FW 5.50 RGBA8 and sRGB fixtures originally used VideoOut-owned
garlic buffers as one or both native render targets. That prevented the exact
format gates from running headlessly on FW 11.60. The shared graphics fixture
now gives these variants real system-flexible-memory targets while preserving
the qualified PM4 stream and native readback rules.

The UNORM gates use the normal headless render target. The sRGB gates reserve
two full aligned 1920x1080 RGBA8 spans in the graphics pool: the first is the
UNORM control and the second, at `buffer_stride`, is the sRGB result. The
stride is rounded to the existing 2 MiB direct-memory alignment, so the two
targets cannot overlap and retain identical low address alignment.

## Oracles

`RGBA8_UNORM` and `BGRA8_UNORM` retain the complete baseline graphics audit:
the expected render-target name, ordered GPU markers and bounded EOP fence,
nonzero triangle coverage, interleaved vertex fetch, bound u16 indexed draw,
and gfx1013 image plus bilinear-sampler checks.

`RGBA8_SRGB` and `BGRA8_SRGB` draw the same workload first into the UNORM
control and then into the native sRGB target. Their CPU readback requires:

- identical changed-pixel coverage;
- unchanged alpha for every covered pixel;
- every sRGB RGB byte inside the inclusive IEC 61966-2-1 quantization bounds
  for the corresponding UNORM control byte;
- zero transfer mismatches and more than 1,000 actually converted channels;
- native packed FNV64 hashes for both surfaces in the captured verdict.

The bounded runner also rejects fatal, failure, mismatch, timeout, and firmware
profile errors. No display preview or VideoOut allocation is part of these
headless gates.

## Exact artifacts

| Firmware | Target | SHA-256 |
| --- | --- | --- |
| 11.60 | `RGBA8_UNORM` | `09c509f3dac6f6864ed53caf969a0046a33e4f7ad9f5cafe872b8a36b2bef406` |
| 11.60 | `BGRA8_UNORM` | `37ed666df195750e32308819a372f5256b4f58caace8a01cb6f7daa0a5e0a840` |
| 11.60 | `RGBA8_SRGB` | `92dcd0cb29926a2c1d9aaf24efe3eda6c1c2548225b739a98092d05fa80a1a94` |
| 11.60 | `BGRA8_SRGB` | `f73f67b5ae326a4af2bb1ad9dfec4b056f7ee8535e2cf69c7493b3354b40f2bb` |
| 5.50 | `RGBA8_UNORM` | `14ef47de0c9f7a0784ceee60cbc048a9caa87865910e8be7f0e7b3321fe27c94` |
| 5.50 | `BGRA8_UNORM` | `3a3f873941de40fd9533adb76843dae2c00aa33f3d8631b3273896b1d99f68eb` |
| 5.50 | `RGBA8_SRGB` | `fbac8690a796d92eba253e195b8e97377f4fdfefd193744410a0ccc789bdabc9` |
| 5.50 | `BGRA8_SRGB` | `b6d12b1f42f36da9954a74728265c0b56312df6fe03f3141e131994ede64334c` |

The 11.60 artifacts force ABI key `0x1160`; the regression mirrors force
`0x0550`. All reject Trinity hardware, self-terminate after flushing their
verdict, and compile without warnings.

## Hardware order

Finish the interrupted `RG32_FLOAT` second pass and both `RGBA32_FLOAT` passes
on a clean FW 11.60 boot first. Then run `RGBA8_UNORM`, `BGRA8_UNORM`,
`RGBA8_SRGB`, and `BGRA8_SRGB` twice each, with the established cleanup ELF
immediately before every launch. Confirm no residual `eboot.elf` and inspect
the live ps5debug-NG fault log after each verdict. Stop on the first stall,
mismatch, panic, page fault, bad packet, or GPU reset.

Matching exact FW 5.50 headless artifacts must pass before these formats are
promoted as cross-firmware parity. At build time the FW 5.50 console remains
unavailable, so neither firmware has a hardware verdict for this new headless
allocation path yet.
