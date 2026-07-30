# Native color-target state gate — FW 5.50

## Scope

This record qualifies the public native-runtime color-target state gate on a
standard PS5 reporting raw system software `0x05500008` (FW ABI key `0x0550`).
The runtime selected `prospero-gc-submit16-standard-standard` and used the
direct DCB graphics carrier.

Artifact SHA-256:

```
7f86dc3346e70212ce3469380639b0a4e49b8372a08dbd4a5624b9448a103429
```

`agcCmdBindColorTargets` now requires every target image to have an explicit
whole-image `color-target` usage owned by graphics in the recording command
buffer. It performs that state check only after validating every binding's
format, sample count, subresource layout, extent, and MRT uniqueness; invalid
MRT definitions therefore still fail atomically before a state mismatch can
mask their validation result.

## Result

The probe created two linear RGBA8 targets, recorded a two-image
`undefined`/host to `color-target`/graphics transition, bound both targets,
completed the reflected MRT draw, and recorded a two-image `color-target`/
graphics to `host-read`/host transition. Every creation, command, submission,
bounded fence wait, readback, reset, and destruction call returned `AGC_OK`.

Both reads found 1,152 overwritten sentinel pixels and all 1,152 MRT output
pairs differed. The probe printed `MRT readback: PASS` and
`Native runtime graphics result: PASS`; HTTP and FTP remained reachable after
the foreground websrv launch.

## Qualification boundary

This qualifies the explicit color-target state requirement for the exact FW
5.50 public 1x linear RGBA8 two-target graphics row. It does not qualify the
depth/stencil, vertex/index, sampling, VideoOut, partial-image, cross-queue,
or FW 11.60 typed-state rows.
