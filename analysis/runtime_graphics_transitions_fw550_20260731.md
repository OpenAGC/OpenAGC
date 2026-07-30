# Native runtime graphics transition qualification — FW 5.50

## Scope

This record covers the public-only `agc_runtime_graphics.elf` artifact on a
standard PS5 reporting raw system software `0x05500008` (FW ABI key `0x0550`).
The runtime selected `prospero-gc-submit16-standard-standard` and used the
direct DCB graphics carrier.

Artifact SHA-256:

```
8cd97b0b26d568c92870047d65698bd71fe31b72c162c7ca1a62c59d159bf643
```

The reflected NGG/MRT probe creates two linear RGBA8 targets, writes a
triangle to both, and reads both images back. It uses two distinct-image
transition batches in one command buffer:

1. `undefined` / host to `color-target` / graphics for both MRT images.
2. `color-target` / graphics to `host-read` / host for both images after draw.

The native runtime derives the required color-write release/flush packets; no
application code assembles PM4 or supplies cache controls.

## Result

The artifact was uploaded once through websrv to
`/data/homebrew/agc_runtime_graphics_oracle/eboot.elf` and launched in the
foreground. Both transition calls, all object creation/binding calls, submit,
and the 200 ms bounded fence wait returned `AGC_OK`. Both image reads returned
`AGC_OK`; each image changed 1,152 sentinel pixels and all 1,152 changed pairs
were distinct. The command buffer reset and every object/device destroy call
returned `AGC_OK`. HTTP and FTP remained reachable afterward.

## Qualification boundary

This hardware-qualifies the explicit whole-image 1x linear RGBA8 MRT row
`undefined -> color-target -> host-read` on exact FW 5.50, including two
distinct resources in a batch and the derived color-write flush before CPU
readback. It does not qualify depth/stencil, HTILE, copy, shader-read,
VideoOut scanout, partial images, cross-queue, multi-command-buffer,
wait/signal-list, or timeline synchronization; it does not promote FW 11.60.
