# FW 11.60 headless graphics qualification

Date: 2026-07-30  
Console: standard PS5, raw firmware `0x11600005`  
Artifact SHA-256:
`01f9f627f8ba9116090c307921115ec6fc92e8ceb140448c0a0796714dcfe915`

## Result

`agc_graphics_fw1160.elf` passed twice through the guarded websrv runner. Each
run was immediately preceded by the process-cleanup ELF, which reported no
stale `eboot.elf`.

Both runs independently passed:

- exact runtime ABI key `0x1160`, standard-family selection, and Trinity
  rejection;
- corrected direct `/dev/gc` initialization and all nine internal mappings;
- version-12 register defaults and async-graphics setup;
- Wave32 NGG front/back and Wave32 PS record audit;
- fused-shader resource state and indexed Gfx1013 draw submission;
- bounded GPU completion fence;
- the offscreen 1536x1536 RGBA16F coverage/value oracle;
- `agcDriverShutdown()` and the final `Graphics result: PASS` verdict.

Presentation was deliberately skipped. The gate used flexible GPU-visible
memory and exact readback, so its result does not depend on VideoOut or a black
screen. ps5debug-NG reported no `eboot.elf` after the second run; websrv and
TCP 744 remained responsive.

## Qualification scope

This hardware-qualifies the reusable FW 5.50 Wave32 NGG/PS baseline graphics
path on standard FW 11.60: direct submission, version-12 defaults, shader
binding/fusion, indexed draw, RGBA16F render-target writes, completion fence,
CPU readback, and clean teardown.

It does not yet qualify every FW 5.50 graphics extension on FW 11.60. Indexed
indirect variants, tessellation/geometry variants, additional color formats,
depth/stencil, HTILE, and MSAA retain their existing FW 5.50 qualification
until separate FW 11.60 evidence is collected. Workload capability is also
independent and remains disabled.
