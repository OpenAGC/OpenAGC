# FW 11.60 graphics and compute gate audit

## Purpose

The FW 11.60 workload investigation is independent of ordinary graphics and
compute execution. Neither `agc_graphics.c` nor `agc_compute.c` emits a
workload packet or calls a workload API. The existing headless artifacts can
therefore qualify major FW 5.50 capabilities without waiting for stage 16.

## Static audit

Both FW 11.60 artifacts:

- require raw firmware normalized to exact ABI key `0x1160`;
- require the standard PS5 family and reject Trinity;
- use the public runtime selector and direct `/dev/gc` backend;
- initialize all internal memory, notify version-12 defaults, and run async
  setup before GPU work;
- avoid VideoOut presentation while retaining GPU-visible buffers and exact
  readback oracles;
- use bounded completion fences and require clean `agcDriverShutdown()`;
- force process termination after flushing the final result;
- link only VideoOut, libkernel, libc, and libnet, with no
  `libSceAgcDriver.sprx` dependency.

`agc_graphics_fw1160.elf` submits the FW 5.50-qualified Gfx1013 Wave32 NGG/PS
path to a 1536x1536 RGBA16F offscreen target. The runner requires the GPU
completion fence, exact FP16 target oracle, driver shutdown, and final PASS
line.

`agc_compute_fw1160.elf` submits the FW 5.50-qualified Wave32 compute shader
and requires all 2,073,600 output pixels to equal `0xff00ff00`, plus completion,
shutdown, and final PASS lines.

Current artifact SHA-256 values:

- graphics: `01f9f627f8ba9116090c307921115ec6fc92e8ceb140448c0a0796714dcfe915`
- compute: `8411515c2ab11e9b61c95150d57820513d21c0a3c275ca1e5782d8c9d50051b6`

Both compile with `-Wall -Wextra` without warnings.

## Hardware order

After a clean FW 11.60 reboot, inject ps5debug-NG and run the guarded gates in
this order:

1. `deploy_agc_graphics_fw1160`
2. `deploy_agc_compute_fw1160`
3. `deploy_agc_fw1160_stage16`

Each runner launches the cleanup ELF immediately before its test. Graphics and
compute go first because their packet paths are already proven on FW 5.50 and
do not depend on workload state. Stage 16 goes last because every prior FW
11.60 `SET_WORKLOAD` candidate stalled the UI. If any gate stalls or fails to
shut down, use only the cleanup ELF, then reboot before the next GPU payload.

One pass is discovery evidence. Require two clean identical passes for each
capability before promotion, followed by the corresponding FW 5.50 regression.
