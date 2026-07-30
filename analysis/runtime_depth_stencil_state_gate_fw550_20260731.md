# Native depth/stencil state gate — FW 5.50

## Scope

This record qualifies the public native-runtime writable depth-target state
gate on a standard PS5 reporting raw system software `0x05500008` (FW ABI key
`0x0550`). The runtime selected `prospero-gc-submit16-standard-standard` and
used the direct DCB graphics carrier.

Artifact SHA-256:

```
39605323b4a2b5e15596fd3dd034680b97782e683df6b717e88d409edcfa8cc9
```

`agcCmdBindDepthStencilTarget` derives required usage from the bound graphics
pipeline: `depth-stencil-write` for depth/stencil writes (including reflected
pixel-shader writes), otherwise `depth-stencil-read`. The target must already
be in that state and owned by graphics in the recording command buffer. The
state check follows all image, format, sample, layout, and extent validation.

## Result

The probe created two RGBA8 color targets and a D16 depth image, then recorded
one three-image transition batch: both colors from `undefined`/host to
`color-target`/graphics and depth from `undefined`/host to
`depth-stencil-write`/graphics. Its pipeline enabled depth test and depth
writes before binding and drawing.

Every transition, bind, draw, submission, bounded fence wait, MRT readback,
reset, and destruction call returned `AGC_OK`. Each color target changed 1,152
sentinel pixels and all 1,152 MRT output pairs differed; the probe printed
`MRT readback: PASS` and `Native runtime graphics result: PASS`.

## Qualification boundary

This qualifies the exact FW 5.50 public D16 1x linear depth-write state gate
alongside the two-target RGBA8 graphics row. It does not qualify depth/stencil
reads, S8 or packed depth/stencil formats, HTILE, reflected shader depth/stencil
writes, partial images, cross-queue ownership, or other firmware profiles.
