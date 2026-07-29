# FW 11.60 headless compute qualification

Date: 2026-07-30  
Console: standard PS5, raw firmware `0x11600005`  
Artifact SHA-256:
`8411515c2ab11e9b61c95150d57820513d21c0a3c275ca1e5782d8c9d50051b6`

## Result

`agc_compute_fw1160.elf` passed twice through the guarded websrv runner. Each
run was immediately preceded by the process-cleanup ELF, which reported no
stale `eboot.elf`.

Both runs independently passed:

- exact runtime ABI key `0x1160`, standard-family selection, and Trinity
  rejection;
- corrected direct `/dev/gc` initialization and all nine internal mappings;
- version-12 register defaults and async-graphics setup;
- parsing and upload of the 76-byte Wave32 compute shader;
- 174 SH default writes in 44 PM4 packets;
- dispatch of 32,400 workgroups over 2,073,600 output pixels;
- pre-dispatch and post-dispatch marker execution;
- exact `2,073,600 / 2,073,600` output match to `0xff00ff00`;
- `agcDriverShutdown()` and the final `Compute result: PASS` verdict.

The completion fence arrived after 2 ms on run 1 and 1 ms on run 2.
Presentation was deliberately skipped; validation used exact CPU readback from
the GPU-written flexible-memory target. ps5debug-NG reported no `eboot.elf`
after the second run, and both websrv and TCP 744 remained responsive.

## Qualification scope

This hardware-qualifies the reusable FW 5.50 Wave32 compute path on standard
FW 11.60: defaults, shader record and register binding, user SGPR layout,
direct dispatch, resource transition, completion fence, exact shader writes,
CPU readback, and clean teardown.

It does not qualify Trinity, other untested firmware, every shader/resource
combination, or the independent workload-tracking operation. Those remain
separately gated.
