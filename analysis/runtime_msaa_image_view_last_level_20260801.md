# Native 4x sampled-image view descriptor fix — 2026-08-01

The Vulkan 4x color-resolve gate exposed a native image-view encoding bug.
`agcGfx1013Image2DDescriptorEncode` correctly stores `log2(sample_count)` in
the gfx1013 2D-MSAA descriptor's `LAST_LEVEL` field. The runtime then applied
its ordinary mip-view patch and replaced that value with mip level zero. As a
result, `sampler2DMS` could read sample zero while samples one through three
returned zero.

`agcRuntimeEncodeImageView` now applies `BASE_LEVEL`/`LAST_LEVEL` view slicing
only to single-sample images. The four-sample path preserves the encoder's
`LAST_LEVEL=2` value. Generic regression coverage creates a 32x32 optimal 4x
RGBA8 color/sampled image and compares the complete runtime view allocation
against the typed gfx1013 descriptor.

Verification evidence:

- generic OpenAGC tests: 17,932 passed, 0 failed;
- Prospero OpenAGC and Vulkan resolve probe cross-build: clean;
- FW 11.600.005 Vulkan probe: all 1,024 pixels resolved to exact
  `0xff00ff80`, with bounded completion, clean system exit, and only the known
  `amount=0x4000` baseline VM warning.

The FW 5.50 replay of the final identical Vulkan artifact remains required
before the Vulkan resolve slice is cross-firmware qualified.
