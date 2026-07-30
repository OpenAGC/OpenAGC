# Runtime sampled-image handoff consumer — host qualification

Date: 2026-07-31

The generic runtime fixture now carries a whole RGBA8 image from graphics
`color-target` ownership to compute `shader-read` ownership and binds it to a
reflected combined image/sampler descriptor.

The fixture verifies that:

- the released image cannot bind before destination acquire;
- acquire emits the exact label wait and qualified cache invalidation;
- the combined image/sampler descriptor binds only after acquire;
- a storage-buffer output in `shader-write` state binds in the same reflected
  descriptor set;
- `DISPATCH_DIRECT` follows the wait/acquire stream; and
- command reset releases the image view, sampler, image, buffer, and label.

The endpoint shader source is
`samples/hw_test/shaders/render_consume_native.comp`. `openagc-psbc` compiles
it with a combined image/sampler at set 0 binding 0 and a storage buffer at set
0 binding 1. It copies the complete 64x64 RGBA8 image into the buffer using
`texelFetch` and `packUnorm4x8`.

Verification:

```text
openagc_tests: 15010 passed, 0 failed
```

This host contract was promoted with a real graphics producer, no-CPU-wait
queue handoff, shader dispatch, and exact readback match on FW 5.50; see
`runtime_render_to_shader_fw550_20260731.md`.
