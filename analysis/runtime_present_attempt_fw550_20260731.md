# Native runtime presentation attempt — FW 5.50 — 2026-07-31

## Scope

This investigation introduced runtime API v13's opaque presentation boundary.
`AgcPresentChain` takes two to 16 distinct native scanout images, validates the
default 1920x1080 linear RGBA8 layout and common pitch, registers their
dedicated direct-memory mappings through the existing VideoOut backend, and
retains them. `agcPresent` accepts only committed Graphics-owned
`VideoOutScanout` state and a finite readiness fence before its bounded VSYNC
flip. Raw addresses, VideoOut patch selection, buffer attributes, and user IDs
remain internal.

The generic fixture passes image retention, rejection before scanout state,
infinite/zero timeout rejection, initial presentation, and the typed
`VideoOutScanout -> ColorTarget -> VideoOutScanout -> present` round trip.
Generic and Prospero compilation completed without warnings.

## Hardware attempts

The standard PS5 reported raw system software `0x05500008` and ABI key
`0x0550`.

The first artifact used one registered image:

- SHA-256: `3d19ad2f8dbee77ec8bd3f72d97cabfa610965c5fc5d0ccf90023c69a240bc29`
- Device, queue, dedicated scanout image, and image upload succeeded.
- `agcCreatePresentChain` returned `AGC_ERROR_NOT_SUPPORTED` during VideoOut
  registration.
- Image, queue, and device teardown all returned `AGC_OK`.

The next artifact used the two buffers required by the proven VideoOut sample
shape:

- SHA-256: `075028e327423d6285c40a58425bb2b564f93240d6b782c773fd96a716fb8bcf`
- The foreground launcher did not return within 30 seconds.
- Because stdout was still buffered, no boundary verdict was recovered.
- HTTP 8080, FTP 2121, and debugger 744 were unreachable afterward.

This is a hardware safety failure, not qualification evidence. Do not rerun
either artifact unchanged and do not infer whether the failure occurred in the
initial typed transition, first flip, round-trip transition, repeated flips,
or process teardown.

## Replacement ladder

`agc_runtime_present.c` now makes stdout unbuffered, uses two buffers, performs
at most one final flip, and compiles five guarded stages:

1. Stage 0: registration and teardown only.
2. Stage 1: initial host-write-to-scanout transition and bounded fence only.
3. Stage 2: stage 1 plus one initial bounded flip.
4. Stage 3: stage 2 plus scanout-to-color-target-to-scanout and bounded fence.
5. Stage 4: stage 3 plus one final bounded flip.

The final API-v22 staged artifact hashes are:

- Stage 0: `70c9f44db94154a8105b4379ccb86f213736047001f6d09ba72a97688542c714`
- Stage 1: `84e709cb15a18b3262e81b732d4258ae52e42913ad063fd9dfe332745c9e6643`
- Stage 2: `d67bbade7d48a918cb2636ac1e42f0c176dca99848a3a00f244019f5686ece11`
- Stage 3: `d5ee2434129f16d068110208671bba72d95637681db22d2226919d487a4452f6`
- Stage 4: `108416ea85233ff5768062a8bc5b062376a2ee2ced909b5358e2a7e050e658bf`

Use only the hash-pinned cleanup-first Make targets:

```sh
make -C samples/hw_test deploy_agc_runtime_present_stage0 PS5_HOST=10.0.1.41
make -C samples/hw_test deploy_agc_runtime_present_stage1 PS5_HOST=10.0.1.41
make -C samples/hw_test deploy_agc_runtime_present_stage2 PS5_HOST=10.0.1.41
make -C samples/hw_test deploy_agc_runtime_present_stage3 PS5_HOST=10.0.1.41
make -C samples/hw_test deploy_agc_runtime_present_stage4 PS5_HOST=10.0.1.41
```

Each target verifies its exact SHA-256 before contacting the console, uploads
and launches the process-cleanup ELF first, checks service recovery, requires
the exact firmware line and stage verdict, rejects any AGC error/failure text,
requires clean device teardown, and checks websrv again afterward.

## Final staged result

All five stages passed once in order on the exact standard-PS5 FW 5.50
profile. Stage 0 registered two images and tore down. Stage 1 submitted the
initial `Undefined -> HostWrite -> VideoOutScanout` lifecycle and completed its
bounded fence. Stage 2 added the first bounded flip. Stage 3 completed
`VideoOutScanout -> ColorTarget -> VideoOutScanout` and its bounded fence.
Stage 4 performed the final bounded flip. Every public destroy operation
returned `AGC_OK`, and the guarded runner verified HTTP/FTP recovery after
each process.

The staged carrier therefore hardware-qualifies the opaque presentation
boundary on exact FW 5.50. The earlier combined artifact remains invalid and
must not be rerun.
