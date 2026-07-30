# Native graphics-input state gate — FW 5.50

## Scope

This record qualifies the public native-runtime vertex/index input state gate
on a standard PS5 reporting raw system software `0x05500008` (FW ABI key
`0x0550`). The runtime selected `prospero-gc-submit16-standard-standard` and
used the direct DCB graphics carrier.

Artifact SHA-256:

```
d99d0ca7a37f4020e27c8ef1117e9d94643b736136c895ae08660f7982d6f9a4
```

`agcCmdBindVertexBuffers` and `agcCmdBindIndexBuffer` now require every input
buffer to be in the explicit `shader-read` state and owned by graphics before
recording their bindings. `shader-read` accepts uniform, storage, vertex, and
index buffer usage so the one state models every GPU-readable buffer input.
Argument, range, and pipeline-layout validation remains ahead of the state
check.

## Result

The public graphics probe recorded one five-resource transition batch: two
RGBA8 images to `color-target`/graphics, D16 depth to
`depth-stencil-write`/graphics, and its upload vertex/index buffers to
`shader-read`/graphics. Every bind, draw, submission, bounded fence wait,
readback, reset, and destruction returned `AGC_OK`.

Both MRT targets changed 1,152 sentinel pixels and all 1,152 output pairs
differed. The probe printed `MRT readback: PASS` and
`Native runtime graphics result: PASS`.

## Qualification boundary

This qualifies the exact FW 5.50 public upload vertex/index graphics-input row
in the reflected NGG/MRT/D16 runtime probe. It does not qualify other buffer
usages, indirect inputs, cross-queue ownership, partial ranges, or other
firmware profiles.
