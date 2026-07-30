# Runtime render-to-shader ownership handoff — FW 5.50

Date: 2026-07-31

Endpoint: standard PS5, exact system software `0x05500008` (ABI key `0x0550`)

Artifact SHA-256:

```text
48a6bd30c5fdcf417be79859e9e3549ec3f3d495b2ec78b97ea192f487e96ea1
```

The local artifact and the bytes read back from websrv FTP had the same digest.
Those exact bytes passed twice after complete runtime teardown and relaunch.

The public runtime probe performs this vertical slice:

1. Draw an indexed triangle through the reflected NGG vertex and two-export
   fragment pipelines into two 64x64 RGBA8 targets plus D16 depth.
2. Release the first color target from Graphics `color-target` ownership to
   Compute `shader-read` ownership with a monotonic GPU label.
3. Submit the destination acquire and reflected compute consumer without a CPU
   wait between the graphics and compute submissions.
4. Bind the acquired target as a combined image/sampler descriptor and a
   readback buffer as a storage descriptor.
5. Dispatch the compiler-produced shader over all 4,096 pixels, transition the
   output and image to host read, and compare every packed RGBA8 word.

Both runs reported:

```text
MRT readback: target0=1152 target1=1152 distinct=1152
MRT readback: PASS
Render-to-shader readback: mismatches=0
Render-to-shader readback: PASS
Native runtime render-to-shader result: PASS
```

All three bounded fence waits, command resets, label/view/sampler/image/buffer
destruction, pipeline/shader destruction, queue destruction, and device teardown
returned `AGC_OK` on the full first run.

This hardware-qualifies the whole-image render-to-shader graphics-to-compute
ownership row on exact FW 5.50. FW 11.60 remains unqualified for this artifact.
