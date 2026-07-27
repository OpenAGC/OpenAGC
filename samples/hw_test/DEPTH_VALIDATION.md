# FW 0x0550 D32 depth validation

`agc_depth.elf` is the first hardware qualification sample for OpenAGC's typed
gfx1013 depth-surface and depth/stencil-control builders. It is prepared for FW
`0x0550`, but has not yet been run on a real PS5.

## Test contract

- D32 float depth-only surface, `ADDR_SW_64KB_Z_X` swizzle mode 24.
- 16 MiB, 64 KiB-aligned flexible-memory allocation.
- HTILE, expclear, stencil, compression, and MSAA are disabled.
- An always-pass full-screen draw initializes depth to `1.0` with color writes
  disabled.
- A near green triangle at clip-space Z `0.25` must pass and update depth.
- An overlapping red triangle at Z `0.75` must fail and leave the green image.
- A separate red triangle at Z `0.75` must pass over depth `1.0`.
- Four `WRITE_DATA` stage markers prove that the command processor progressed
  through every draw; the final EOP marker proves completion and cache flush.
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
Do not add this sample to the passing FW `0x0550` conformance matrix until a
real-console run records the expected display, marker values, and readback.
