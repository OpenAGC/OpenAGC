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

- Stage 0: `f83de2ab8251375db4dd040e34c6abc3d7557eb8b6d503c1cef6a256e4b89de4`
- Stage 1: `adceba2bb3da402fbb29660d1a9a30597585366f80082581ebd925b21ae1ece0`
- Stage 2: `f384e65fe0a7d3cd3cdb26980cd07b003dabeceb1a4489441a906b65943691fb`
- Stage 3: `30a08d185ddef730ebdebccf672c80bbd1781e555747020a2d299cbf22d4d9c8`
- Stage 4: `a24956e489cc9abff3b562199a65ceeded8a8b38a138b453210de04e86e7b6fa`

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
boundary on exact FW 5.50. The self-terminating current bytes were replayed on
FW 5.50 and passed unchanged on exact FW 11.60. The earlier combined artifact
remains invalid and must not be rerun.
