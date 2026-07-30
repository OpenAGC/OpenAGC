# Native runtime graphics FW 5.50 qualification — 2026-07-31

## Artifact and environment

The public-only graphics oracle was rebuilt from commit `8d4e25d` and deployed
once through websrv to a standard PS5. The console reported raw system
software `0x05500008` and selected
`prospero-gc-submit16-standard-standard` (FW ABI key `0x0550`).

SHA-256:

```
e7c3cb908910e28ea1ee1d9c3db0a887d45bd1d9e84e356cf6c8a159167d2941
```

## Public runtime path

`agc_runtime_graphics.elf` used only native runtime objects: device, graphics
queue, reflected NGG vertex and fragment shaders, graphics pipeline, upload
vertex/index buffers, two RGBA8 color-target images, one D16 depth image,
command buffer, and fence. It did not assemble PM4 or select firmware policy.

The command bind emitted the exact 2,184-dword V8 graphics-default prefix,
then recorded vertex/index binding, two typed color targets, typed depth
target, viewport, scissor, and indexed draw. Both color allocations were
prefilled with `0xa5a5a5a5` using `agcWriteImage` and read after the bounded
fence using `agcReadImage`.

## Result

Every creation, recording, submission, bounded wait, readback, reset, and
destroy operation returned `AGC_OK`. Each MRT target changed exactly 1,152
sentinel pixels and all 1,152 changed target pairs differed, so the probe
printed `MRT readback: PASS` and `Native runtime graphics result: PASS`.

The web launcher (8080) and ps5debug-NG (744) remained reachable afterward.
This qualifies the native baseline reflected graphics slice on exact FW 5.50.
It does not qualify unrun optional pipeline features or other firmware
profiles.
