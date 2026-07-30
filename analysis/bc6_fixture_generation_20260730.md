# BC6 UFLOAT/SFLOAT fixture generation (2026-07-30)

The portable BC6 sampling gates contain no runtime Mesa dependency. Their
legal 16-byte blocks and expected decoded values were generated offline from
the local Mesa tree at `../mesa` using the independent BPTC reference routines
in `src/util/format/texcompress_bptc_tmp.h`:

- `compress_rgb_float_block(..., is_signed)` produced mode-3 BC6 blocks.
- `decompress_rgb_float_block(..., is_signed)` independently decoded them.
- `src/util/half_float.c` and `src/util/softfloat.c` supplied Mesa's half-float
  conversion helpers.

The generator used four deterministic 4x4 RGB fields: an XY gradient, a
transposed/complemented gradient, channel-independent checkerboards, and a
channel-independent threshold pattern. UFLOAT used `[0,1]`; SFLOAT remapped
the same sources to `[-1,1]`. Expected RGBA8 bytes apply the exact gate shader
transform: UFLOAT clamps directly to `[0,1]`, while SFLOAT applies
`value * 0.5 + 0.5` before clamping. Alpha is fixed to one.

The embedded oracle checks every sampled texel from mip 0, mip 1, the second
array layer, and a partial edge block. It permits at most two stored-byte units
per RGB channel to account for GPU half-float interpolation and render-target
conversion rounding, while requiring all four fixture classes, broad range,
channel independence, a bounded fence, and a native render-target hash.

These artifacts qualify the native BC6 resource/decoder path only after
hardware execution. Offline generation and successful cross-build do not
constitute hardware qualification on any firmware.
