# FW 0x0550 D32 depth validation

The sample obtains its D32 `64KB_Z_X` pitch, padded height, 64 KiB alignment,
slice size, and allocation size from `agcGfx1013GetDepthSurfaceLayout`. It no
longer reserves a sample-local fixed 16 MiB image. This changes allocation
only; PS5 execution remains pending while hardware is unavailable.

The sample also reserves and zeroes a separately aligned HTILE allocation from
`agcGfx1013GetHtileLayout`, but it deliberately leaves `htile_enable` clear.
The provisional eight-address-pipe input must be checked against the PS5's
`GB_ADDR_CONFIG` before the metadata gate is enabled.

## Ordered follow-up gates

1. **Stencil gate:** `agc_depth_stencil.elf` is cross-build ready. It keeps 1x
   sampling and HTILE disabled, binds separate D32 and S8 `64KB_Z_X` planes,
   uses `ALWAYS` comparison with `0xff` compare/write masks and `REPLACE 0x5a`,
   and requires deterministic color, raw depth, and raw stencil readback.
2. **MSAA gate:** keep HTILE disabled, use a 4x D32 surface and matching color
   sample state, resolve to a 1x target, and require edge/sample and raw-depth
   checks. Do not combine this first MSAA run with stencil.
3. **HTILE gate:** return to 1x D32, confirm the address-pipe count from
   `GB_ADDR_CONFIG`, initialize the typed metadata allocation, and enable HTILE
   first without expclear. Require the EOP fence, unchanged PS5 responsiveness,
   deterministic color/depth readback, and metadata changes before testing
   expclear or combined stencil.

Each gate is promoted independently. A failure or kernel panic stops the
sequence and does not invalidate the earlier gate.

`agc_depth.elf` is the first hardware qualification sample for OpenAGC's typed
gfx1013 depth-surface and depth/stencil-control builders. It is prepared for FW
`0x0550`, but has not yet been run on a real PS5.

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

Expected terminal result:

```text
[Depth Marker] stage[0..3] ... expected ...
[Depth Readback] green=<nonzero> red=<nonzero> left=ff00ff00 right=ff0000ff
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
Do not add this sample to the passing FW `0x0550` conformance matrix until a
real-console run records the expected display, marker values, and readback.

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
