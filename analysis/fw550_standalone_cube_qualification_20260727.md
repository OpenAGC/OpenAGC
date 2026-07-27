# FW 5.50 standalone cube qualification

Date: 2026-07-27  
Firmware: `0x05500008` (`5.500.008`)  
Deployment: foreground curl/etaHEN websrv  
ELF: `examples/cube/build/openagc_cube.elf`  
Size: 463,840 bytes  
SHA-256: `228570336de5ba5e87012ea477166057829cfbd7490df59b68c525bcb12dd442`

## Application boundary

`examples/cube` configures as a separate CMake project through the staged
installed `OpenAGCConfig.cmake`. Its compile command receives only the installed
include directory, and its link uses the installed `libopenagc.a`. The
application does not include `samples/hw_test` files or link an in-tree target.

The application owns:

- One 25 MiB flexible mapping containing uploaded shaders and three frame slots.
- Three independent linear BGRA8 render targets and DCBs.
- Three continuously updated 24-vertex/36-index cube resource sets.
- Three gfx1013 buffer descriptors and EOP completion fences.
- Three 1920x1080 garlic buffers registered with VideoOut.
- Shader-record parsing, Wave32 NGG front/back fusion, submission, presentation,
  launch-context cleanup, VideoOut close, direct-memory release, and flexible
  memory unmapping.

All command construction uses public OpenAGC gfx1013 builders. GPU completion
and frame-slot reuse waits have a two-second bound. Accepted VideoOut flips are
paced at 17 ms because the websrv launcher context terminated inside the kernel
event-queue wait even though the flip itself had been accepted.

## Build gates

- Clean generic CMake build with `OPENAGC_BUILD_TESTS=ON` and
  `OPENAGC_BUILD_EXAMPLES=ON`: pass without warnings.
- CTest: 1/1 suite passed.
- Prospero OpenAGC configure/build/install: pass without warnings.
- Separate Prospero cube configure/build through `OpenAGC_DIR`: pass without
  repository include or library paths.
- GLSL to SPIR-V to psbc: Wave32 GsFront/GsBack records and descriptor-free
  pixel record generated successfully.

## Hardware results

Two consecutive executions of the final ELF completed:

- FW profile selected the standard PS5 `0x05500008` submit/queue layout.
- NGG front record: type 4, four SH registers, 172 bytes of code.
- NGG back record: type 6, sixteen SH and eleven CX registers, 1,116 bytes.
- Pixel record: type 1, five SH and eight CX registers, 72 bytes.
- First indexed-draw DCB: 2,467 dwords.
- The DCB was accepted and its EOP fence completed.
- Frames 0, 600, 1200, 1800, 2400, and 3000 reported progress.
- All 3,600 frames completed on both runs.
- VideoOut, direct memory, flexible memory, and local launch state shut down
  cleanly.
- No hang, GPU reset, kernel panic, UI crash, or stuck process occurred.

The physical display and Chiaki capture showed a continuously rotating,
multi-colored cube on dark gray. The capture was 1072x603, preserving 16:9.
Projection constants map to `1.15 * 960 = 1104` horizontal pixels per camera
unit and `2.05 * 540 = 1107` vertical pixels per camera unit at 1920x1080, a
0.27% difference. Apparent tall or wide silhouettes during rotation are normal
perspective foreshortening; there is no framebuffer-aspect stretch.

## Result

The representative standalone-homebrew gate passes on FW 5.50. OpenAGC now has
hardware evidence that an external installed-package consumer can own resources,
update them every frame, record public indexed graphics work, synchronize,
present continuously, and exit cleanly.

