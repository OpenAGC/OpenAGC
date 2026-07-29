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
and gfx1013 image plus bilinear-sampler checks. They also emit and require a
native packed FNV64 readback hash.

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
| 11.60 | `RGBA8_UNORM` | `fc826cc3eafafb1fa890bfbd25223e393bce5b3737d0835edc646fe54b10c487` |
| 11.60 | `BGRA8_UNORM` | `bf9a83c16b31ad37fcc35b86d936fe80c07f22df8da93136bb6dc1f97333aed8` |
| 11.60 | `RGBA8_SRGB` | `de4a621acc733359783cea0d30baa17552a3c0afc42213c8325318136b046baf` |
| 11.60 | `BGRA8_SRGB` | `5c2bc1e83d3b89d19a17ab310e7656d59f1d7a5c28d2475473169fa735337bd7` |
| 5.50 | `RGBA8_UNORM` | `80fd88e8b8b612ecfadbd98ae9014e60aa3aab3f8c29d0bb901676a7afc7011e` |
| 5.50 | `BGRA8_UNORM` | `65709b46f52bfbe6db56d026428b50c088f78c024d7fe391b67714a3ef83c6b0` |
| 5.50 | `RGBA8_SRGB` | `9f4556eeddd622ca12d188728f66f68a72ffb9813f988793548fb02215476caf` |
| 5.50 | `BGRA8_SRGB` | `fdabd12d284515a426a38a0778c4f28ffb0eb617e7ef29f7d324500a00eebb77` |

The 11.60 artifacts force ABI key `0x1160`; the regression mirrors force
`0x0550`. All reject Trinity hardware, self-terminate after flushing their
verdict, and compile without warnings.

Because FW 11.60 websrv stopped returning stdout pipes after repeated launches,
the following otherwise-identical variants write their complete line-buffered
verdict to a fresh FTP-readable file. The foreground launcher subsequently
stopped entering `main`, while an RG32 daemon-loader probe passed every GPU and
shutdown oracle. The runner therefore uses daemon mode only for these headless,
self-terminating logged artifacts, deletes stale evidence, and requires the
fresh final verdict before applying every normal oracle:

| File-backed FW 11.60 target | SHA-256 |
| --- | --- |
| `RGBA8_UNORM` | `da1c8cab69f099b1ab58f1ee506893e2070e95ad139198a7009b9147a9d83fbe` |
| `BGRA8_UNORM` | `997900d3f1419713109006cc0bc2ac64495e834bd57c0e7c0d5958945e5e8375` |
| `RGBA8_SRGB` | `b31f04e8bbf9a87ed1120548c9ea712d54c420ed727e0337b2d50b9e34fb3a45` |
| `BGRA8_SRGB` | `fb69aac11b7bce9f189a0362fe23db070780a4ea9b71d350278d1f29f325a02f` |

Use one file-backed artifact for both qualifying passes of each target. The
logging mode does not alter the render-target allocation, shader, PM4 stream,
fence, native readback, or shutdown path.

The first FW 11.60 RGBA8_UNORM attempt exposed a retained-oracle bug rather
than a GPU failure. The current public viewport covers the full 1920x1080
rectangle and rendered 224,640 pixels, but the display-era validator still
computed its expectation from `min(width,height)^2` (about 126,293 pixels).
The DCB fenced immediately, marker and vertex fetch passed, and shutdown was
clean; only the obsolete coverage-derived index and texture verdicts failed.
Headless RGBA8 now derives expected coverage from `width*height`, while
display-backed fixtures retain the centered-square formula. This is the same
full-rectangle distinction already established by the FW 11.60 depth gates.

## Hardware order

Finish the interrupted `RG32_FLOAT` second pass and both `RGBA32_FLOAT` passes
on a clean FW 11.60 boot first. Then run `RGBA8_UNORM`, `BGRA8_UNORM`,
`RGBA8_SRGB`, and `BGRA8_SRGB` twice each, with the established cleanup ELF
immediately before every launch. The runner uses detached cleanup because that
helper intentionally self-terminates with `SIGKILL`; it waits and requires a
responsive websrv before uploading the graphics ELF. Confirm no residual
`eboot.elf` and inspect
the live ps5debug-NG fault log after each verdict. Stop on the first stall,
mismatch, panic, page fault, bad packet, or GPU reset.

Matching exact FW 5.50 headless artifacts must pass before these formats are
promoted as cross-firmware parity. The FW 11.60 verdicts are recorded below;
the FW 5.50 console remains unavailable, so the mirror regression is pending.

## FW 11.60 hardware result

Standard PS5 firmware `0x11600005` passed every logged headless gate twice
after the full-rectangle coverage correction:

| Target | Reproduced native oracle | State |
| --- | --- | --- |
| `RGBA8_UNORM` | changed `224640`, distinct `8`, FNV64 `0xdc0fe459fbd8c58c` | passed twice |
| `BGRA8_UNORM` | changed `224640`, distinct `8`, FNV64 `0xbcec6133bc4e8583` | passed twice |
| `RGBA8_SRGB` | changed `224640`, mismatches `0/0/0`, converted `279859`, hashes `0xdc0fe459fbd8c58c` / `0x5024a48443f3e7a8` | passed twice |
| `BGRA8_SRGB` | changed `224640`, mismatches `0/0/0`, converted `279859`, hashes `0xbcec6133bc4e8583` / `0xd5c40dfcc7c28283` | passed twice |

Every UNORM run passed vertex fetch, bound u16 indexing, bilinear texture
sampling, ordered marker, and immediate completion-fence checks. Every sRGB
run completed both the UNORM control and sRGB submissions with immediate
fences. All eight qualifying runs selected exact ABI key `0x1160`, shut the
driver down cleanly, produced final PASS, left no `eboot.elf` according to
ps5debug-NG, and kept websrv responsive.

The FW 5.50 console at `10.0.1.41` still refused both ports 8080 and 744 after
these runs. The exact FW 5.50 mirrors remain built but unexecuted, so promotion
to cross-firmware parity is still pending that regression.
