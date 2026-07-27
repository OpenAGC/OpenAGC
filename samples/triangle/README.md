# Minimal gfx1013 triangle pass

This example shows the command-recording core of a homebrew graphics loop. It
uses only public OpenAGC APIs and contains no sample-private PM4 construction.
The larger `samples/hw_test/agc_graphics.c` program remains the FW 5.50
qualification harness; it is not the recommended application template.

`openagcTriangleRecord()` composes three reusable operations:

1. `agcGfx1013BuildFramePrologue()` binds the color target, viewport, scissor,
   graphics defaults, and target mask.
2. `agcGfx1013DrawBaselineIndexAuto()` binds the gfx1013 Wave32 primitive and
   pixel shaders and emits the non-indexed triangle draw.
3. `agcGfx1013TransitionResource()` completes render-target writes and moves
   the image to presentation usage.

The composition is atomic with respect to the command-buffer cursor. If any
public builder rejects the state or runs out of space, the cursor is restored
to its entry position.

## Application-owned setup

A PS5 homebrew application supplies:

- A GPU-visible command buffer initialized with `agcCbInit()`.
- Uploaded fused GsFront/GsBack and pixel `AgcShaderRecord` data produced by
  `openagc-psbc`.
- A VideoOut-compatible render target and a GPU-visible completion word.
- Platform initialization, allocation, submission, bounded completion waits,
  and VideoOut flips.

Select the typed render-target format without packing registers:

```c
OpenAgcTrianglePass pass = {0};

agcGfx1013InitColorTarget(&pass.frame.color_target,
    render_target_address, width, height,
    AGC_GFX1013_RT_FORMAT_RGBA8_SRGB);
pass.frame.viewport = (AgcGfx1013ViewportState){width, height};
pass.frame.scissor = (AgcGfx1013ScissorState){0u, 0u, width, height};
pass.frame.target_mask = AGC_GFX1013_TARGET_MASK_RGBA0;

/* Fill pass.frame's documented gfx1013 controls and pass.draw's uploaded
 * shader bindings, resource tables, counts, and draw parameters here. */
pass.completion_address = completion_address;
pass.completion_value = frame_number;

if (openagcTriangleRecord(&cb, &pass) != AGC_OK)
    agcCbReset(&cb, command_buffer, command_buffer_size);
```

Use `AGC_GFX1013_RT_FORMAT_RGBA8_UNORM` when the render target must remain
linear instead of applying sRGB encoding. Do not combine an sRGB target with a
shader that already gamma-encodes its output.

## Build

The example recording layer is included in a normal host build when examples
are enabled:

```sh
cmake -B build -DOPENAGC_PLATFORM=generic \
    -DOPENAGC_BUILD_TESTS=ON -DOPENAGC_BUILD_EXAMPLES=ON
cmake --build build
```

For PS5, build OpenAGC with the Prospero toolchain and compile `triangle.c`
into the homebrew application. Deploy the complete application through
curl/websrv; do not use `prospero-deploy`.

The FW 5.50 qualification counterpart is `samples/hw_test/agc_graphics.elf`.
Build and launch it with the repository runner:

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config
make -C samples/hw_test -B agc_graphics.elf
PS5_HOST=10.0.1.41 \
    samples/hw_test/run_fw550_conformance.sh --sample agc_graphics
```

The runner uses curl with FTP port 2121 and the websrv HTTP launcher on port
8080. Run it only after confirming the console is ready; a disconnected or
timed-out foreground request is a hard stop.

The FW `0x05500008` baseline passed this exact path on 2026-07-27. The retained
qualification evidence is in
`analysis/fw550_public_triangle_qualification_20260727.md`.
