# FW 5.50 public graphics baseline qualification

Date: 2026-07-27

## Artifact and deployment

- Firmware: `0x05500008` (`5.500.008`)
- GPU profile: standard PS5 gfx1013
- Artifact: `samples/hw_test/agc_graphics.elf`
- Size: 508,248 bytes
- SHA-256: `673bba3e560315e0f1b96ed13a5f9d6db75a022869feb3a7945b41cefdf83d95`
- Deployment: FTP upload plus foreground curl/websrv `/hbldr`
- Runner: `run_fw550_conformance.sh --sample agc_graphics`
- Retained log: `samples/hw_test/conformance-logs/20260727T112112Z-99157/`

The runner completed normally in 31 seconds and reported `1/1 samples passed`.

## Public API path

The sample used the reusable gfx1013 frame and render-target state, Wave32
NGG/PS binding, resource-table binding, baseline index-auto draw, completion
fence, and cache/resource transition APIs. It did not introduce a new
sample-local PM4 path for this qualification.

## Machine oracles

- FW profile selection and all internal AGC allocations succeeded.
- Wave32 NGG and pixel-shader metadata audit passed.
- The reusable baseline bind/index/instance/auto draw returned `AGC_OK`.
- The 2,470-dword DCB submitted with `AGC_OK`.
- The GPU completion fence arrived after 1,000 microseconds.
- The ordered marker matched `0xdeadcafe`.
- FP16 coverage changed 255,744 pixels, within the 1,536-pixel tolerance of
  the 255,456-pixel analytic expectation.
- Every changed pixel stored all four FP16 components: 255,744/255,744.
- Eight distinct packed colors were sampled and no component was outside
  `[0,1]`.
- Packed FP16 FNV64 was `0x4a40c2eb4f12bc26`.
- VideoOut accepted and completed 1,800/1,800 preview flips.

## Diagnostic-oracle correction

The first launch exposed a stale software predicate: historical output treated
112,198 alpha-one pixels as an informative opacity count, while a later edit
renamed it to complete samples and incorrectly required every interpolated
alpha value to equal one. The GPU command stream, fence, marker, coverage, and
packed output were already correct.

The replacement all-lanes-written check initially used finite half value
`0x3555` and collided with 94 valid bilinear results. The final fixture clears
each FP16 lane to quiet NaN `0x7e00`, which valid `[0,1]` shader output cannot
produce, and requires every changed pixel to overwrite all four lanes. The
conformance runner was updated to recognize the current typed FP16 target
label. The final foreground run passed both the sample and runner gates.

## Result

The homebrew-facing FW 5.50 baseline is hardware-qualified at this revision:
typed render-target setup, viewport/scissor/default state, Wave32 shader
binding, draw submission, synchronization, CPU readback, and presentation all
completed with deterministic machine oracles. Physical appearance remains a
separate user-visible observation and is not inferred from the software log.
