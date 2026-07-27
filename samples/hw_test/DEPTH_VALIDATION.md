# FW 0x0550 D32 depth validation

The sample obtains its D32 `64KB_Z_X` pitch, padded height, 64 KiB alignment,
slice size, and allocation size from `agcGfx1013GetDepthSurfaceLayout`. It no
longer reserves a sample-local fixed 16 MiB image. FW `0x05500008` hardware
execution passed through curl/websrv on 2026-07-27.

The sample also reserves and zeroes a separately aligned HTILE allocation from
`agcGfx1013GetHtileLayout`, but it deliberately leaves `htile_enable` clear.
The provisional eight-address-pipe input must be checked against the PS5's
`GB_ADDR_CONFIG` before the metadata gate is enabled.

## Ordered follow-up gates

1. **Stencil gate (passed):** `agc_depth_stencil.elf` keeps 1x
   sampling and HTILE disabled, binds separate D32 and S8 `64KB_Z_X` planes,
   uses `ALWAYS` comparison with `0xff` compare/write masks and `REPLACE 0x5a`,
   and requires deterministic color, raw depth, and raw stencil readback.
   FW `0x05500008` produced 256,608 `0x5a` bytes, 2,364,832 zero bytes,
   no other stencil values, and 1,800/1,800 completed flips.
2. **MSAA gate (passed):** `agc_depth_msaa.elf` keeps HTILE and stencil
   disabled, uses 4x D32 and matching RGBA8 state, shader-resolves to a 1x
   target, and requires edge/sample and raw-depth checks. FW `0x05500008`
   passed all marker, color/depth, visual, and responsiveness requirements.
3. **HTILE gate:** return to 1x D32, confirm the address-pipe count from
   `GB_ADDR_CONFIG`, initialize the typed metadata allocation, and enable HTILE
   first without expclear. Require the EOP fence, unchanged PS5 responsiveness,
   deterministic color/depth readback, and metadata changes before testing
   expclear or combined stencil.

Each gate is promoted independently. A failure or kernel panic stops the
sequence and does not invalidate the earlier gate.

`agc_depth.elf` is the first hardware-qualified sample for OpenAGC's typed
gfx1013 depth-surface and depth/stencil-control builders. It passed on a real
standard PS5 running FW `0x05500008`.

## Test contract

- D32 float depth-only surface, `ADDR_SW_64KB_Z_X` swizzle mode 24.
- Typed, 64 KiB-aligned flexible-memory allocation sized for the active plane.
- HTILE, expclear, stencil, compression, and MSAA are disabled.
- An always-pass full-screen draw initializes depth to `1.0` with color writes
  disabled.
- A near green triangle at clip-space Z `0.25` must pass and update depth.
- An overlapping red triangle at Z `0.75` must fail and leave the green image.
- A separate red triangle at Z `0.75` must pass over depth `1.0`.
- Four `WRITE_DATA` stage markers prove that the command processor progressed
  through every draw; the final EOP marker proves completion and cache flush.
- The final typed `DEPTH_STENCIL_WRITE` to `HOST_READ` transition emits the
  gfx1013 DB metadata event (`0x2c`) and DB data timestamp event (`0x2b`). A
  separate color-to-host transition preserves RGBA8 readback coherence.
- CPU readback requires nonzero green and red coverage, a green sample in the
  overlap region, a red sample in the independent region, and raw D32 words for
  `1.0`, `0.625`, and `0.875`. The latter values are the `0.25` and `0.75`
  clip-space inputs after the reusable viewport's `0.5/0.5` Z transform.

Expected display: a dark-gray background with a green triangle on the left and
a red triangle on the right. No red should replace the green overlap triangle.
The two fixtures are deliberately tall and narrow to separate the overlap and
independent-pass regions, so their bases are visibly shorter than their sides.

Expected terminal result:

```text
[Depth Marker] stage[0..3] ... expected ...
[Depth Readback] green=<nonzero> red=<nonzero> left=ff00ff00 right=ffff0000
[Depth Readback] raw D32: one=<nonzero> near=<nonzero> far=<nonzero>
[Depth Result] markers=PASS color=PASS raw-depth=PASS
```

## Build

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config
cmake --build ../../build-prospero
make agc_depth.elf
make agc_depth_stencil.elf
```

## Deploy through websrv

Do not use `prospero-deploy`. Upload and foreground-launch with curl:

```sh
PS5_HOST=10.0.1.41
curl -s "ftp://$PS5_HOST:2121/" --quote "MKD /data/homebrew/agc_depth"
curl -s "ftp://$PS5_HOST:2121/" --quote "MKD /data/homebrew/agc_depth/sce_sys"
curl -T agc_depth.elf "ftp://$PS5_HOST:2121/data/homebrew/agc_depth/eboot.elf"
curl -T sce_sys/icon0.png "ftp://$PS5_HOST:2121/data/homebrew/agc_depth/sce_sys/icon0.png"
curl -s "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_depth/eboot.elf"
```

The equivalent Make target is `make deploy_agc_depth PS5_HOST=<address>`.
The stencil equivalent is
`make deploy_agc_depth_stencil PS5_HOST=<address>` and uses the same curl/websrv
foreground-launch path.
The real-console run recorded all four stage markers, the completion fence,
128,304 green pixels, 128,304 red pixels, raw initialization/near/far D32
values, and 1,800/1,800 completed flips without a hang or kernel panic.

## Stencil gate contract

- D32 and S8 are separate typed `64KB_Z_X` allocations.
- The initialization draw runs with stencil disabled and establishes D32 1.0.
- The three visible validation draws enable front-face stencil testing with
  `ALWAYS`, compare mask `0xff`, write mask `0xff`, and `REPLACE 0x5a` on
  depth pass. The overlapping depth-failed triangle keeps the old stencil.
- MSAA is fixed at 1x. HTILE and expclear remain disabled.
- Raw S8 readback must contain both `0x00` and more than 1000 `0x5a` bytes,
  with no other values. The existing marker, color, and D32 checks must also
  pass.

Expected additional terminal result:

```text
[Stencil Readback] zero=<nonzero> replace-5a=<more than 1000> other=0
[Depth+Stencil Result] markers=PASS color=PASS raw-depth=PASS stencil=PASS
```

The FW `0x05500008` curl/websrv run passed this contract on 2026-07-27. The
screen showed the expected green and red triangles, and the console remained
responsive without a hang or kernel panic.

## 4x MSAA gate contract

`agc_depth_msaa.elf` is the isolated gfx1013 4x gate. It is host-tested,
Prospero-cross-built, and hardware-validated on FW `0x05500008`.

- The color image is a separately allocated, 64 KiB-aligned `64KB_R_X`
  RGBA8 surface. At 1920x1080 its typed layout is 1920x1088 pixels and
  33,423,360 bytes for four stored fragments.
- D32 uses the existing typed `64KB_Z_X` layout with `sample_count=4`.
- `PA_SC_AA_CONFIG=0x2020c002`, `DB_EQAA=0x00002202`, and
  `PA_SC_MODE_CNTL_0=0x3` select 4x rasterization with one pixel-shader
  iteration per pixel.
- Standard DX sample positions `(-2,-6), (2,6), (-6,2), (6,-2)` are packed
  as `0xe62a62ae` in all four pixel-quadrant registers. Centroid priority is
  `0x3210321032103210`; both coverage registers are `0x000f000f`.
- `CB_COLOR0_ATTRIB.NUM_SAMPLES=2` and `NUM_FRAGMENTS=2` encode log2(4), and
  `CB_COLOR0_ATTRIB3.COLOR_SW_MODE=27` selects `64KB_R_X`.
- A `sampler2DMS` fragment shader fetches samples 0 through 3, averages them,
  composites fixture coverage over dark gray, and draws a fullscreen triangle
  into the registered 1x VideoOut buffer. Its descriptor selects `Z,Y,X,W` to
  undo the source `ALT` red/blue storage. Gfx10.3 has no supported legacy
  fixed-function `CB_RESOLVE` path.
- Stencil addresses remain zero, stencil testing stays disabled, and
  `htile_enable`, expclear, compression, CMASK, FMASK, and DCC remain clear.
- A render-target-to-shader-read transition precedes the resolve. The resolve
  frame prologue binds the 1x destination, then restores 1x AA state after
  defaults and before the fullscreen draw.

Expected display: the existing dark background with green depth-pass and red
independent-pass triangles, with antialiased edges. Interior readback samples
must remain exact green/red, raw 4x D32 must contain initialization, near, and
far values, all stage markers must match, and the console must remain
responsive.

Expected terminal result:

```text
[MSAA] shader-resolved 4x RGBA8 to 1x VideoOut target
[Depth+4xMSAA Result] markers=PASS color=PASS raw-depth=PASS stencil=PASS
```

Repeated FW `0x05500008` websrv runs passed this contract on 2026-07-27. The
5,131-dword DCB reached its fence in 1-4 ms, all four stage markers matched,
readback found 127,818 exact green and 127,818 exact red pixels plus all three
raw D32 classes, and VideoOut completed 1,800/1,800 flips. The capture showed
green and red triangles with resolved edges on the dark-gray framebuffer;
black side pillars were outside the registered 1920x1080 framebuffer.

Build and deploy for a repeat qualification run:

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config
cmake --build ../../build-prospero
make agc_depth_msaa.elf
make deploy_agc_depth_msaa PS5_HOST=<address>
```

The deployment target uses only curl with websrv FTP/HTTP. Do not use
`prospero-deploy`. This gate may now be included in the passing FW `0x0550`
conformance matrix; HTILE remains excluded until separately qualified.
