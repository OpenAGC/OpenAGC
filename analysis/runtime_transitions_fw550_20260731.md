# Native runtime transition compute qualification — FW 5.50

## Scope

This record covers the changed public-only `agc_runtime_compute.elf` artifact
on a standard PS5 reporting raw system software `0x05500008` (FW ABI key
`0x0550`). The runtime selected the
`prospero-gc-submit16-standard-standard` profile and used its direct DCB
compute carrier.

Artifact SHA-256:

```
ab8852e9161c0f6ed1c373bc6de047bb9831df0d7cc7bc3df6d247baf549af31
```

The sample uses only native runtime objects and reflected shader metadata. Its
single compute command buffer explicitly records these transitions around a
real 64-word storage-buffer dispatch:

1. `undefined` / host to `shader-write` / compute.
2. `shader-write` / compute to `host-read` / host.

The second request derives the qualified release/flush sequence before the
runtime-owned EOP fence. No application code supplies PM4 or cache-control
words.

## Result

The artifact was uploaded once through websrv to
`/data/homebrew/agc_runtime_compute/eboot.elf` and launched in the foreground.
Both `agcCmdTransitionResources` calls returned `AGC_OK`; device/queue/shader/
pipeline/buffer/command/fence creation, submit, the 200 ms bounded fence wait,
and readback all returned `AGC_OK`. Every output word equalled `0xff00ff00`.
Command reset and every object/device destruction call also returned `AGC_OK`.
The HTTP launcher and FTP service remained reachable after completion.

## Qualification boundary

This hardware-qualifies the explicit whole-buffer compute row
`undefined -> shader-write -> host-read` on exact FW 5.50, including the
derived compute-write flush before CPU readback. It does not qualify image,
color/depth, copy, scanout, partial-range, cross-queue, multi-command-buffer,
wait/signal-list, or timeline transitions, and it does not promote FW 11.60.
