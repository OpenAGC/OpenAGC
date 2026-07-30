# Format, compressed-depth, and MSAA completion audit — 2026-07-30

## Scope and result

The active milestone is complete for its explicitly developed paths. This
audit uses current source, host tests, hash-pinned artifacts, guarded hardware
verdicts, and residual-process checks rather than inferring completion from
the roadmap text alone.

## Requirement evidence

1. **R16/RG16/RGBA16 SNORM**

   The append-only public tuples, gfx10.3 format/number/swap/export mappings,
   exact PM4 fixtures, all-profile selection, layout overflow rejection,
   invalid enums, and every short-buffer boundary are host-covered. Each
   firmware-neutral artifact passed twice on exact FW 11.60 with signed
   endpoint, diversity, per-lane independence, native hash, fence, and clean
   teardown oracles.

2. **Sixteen-bit UINT/SINT with dedicated integer shaders**

   R/RG/RGBA UINT16 and SINT16 are append-only and host-qualified through the
   same exact ABI and failure boundaries. All six dedicated integer-output
   shader artifacts passed twice on exact FW 11.60 with exact native integer
   values, independent lane hashes, bounded fences, and zero-valued teardown.

3. **Thirty-two-bit integer formats**

   R/RG/RGBA UINT32 and SINT32 cover the regular 32-, 64-, and 128-bit element
   widths with checked 64-bit layout arithmetic. All six dedicated integer
   artifacts passed twice on exact FW 11.60 with exact per-pixel values and
   independent stored lanes.

4. **BC1-BC7 layout and sampling**

   Host coverage locks all 14 native UNORM/SNORM/SRGB/UFLOAT/SFLOAT resource
   encodings and the linear gfx10 AddrLib contract: 4x4 ceiling-divided blocks,
   8/16-byte block sizes, 256-byte pitch/base alignment, smallest-mip-first
   offsets, arrays/cube faces, odd dimensions, partial blocks, mip tails, and
   checked maximum sizes. Every hash-pinned firmware-neutral direct-upload
   sampling ELF passed twice on exact FW 11.60 and then the identical bytes
   passed twice on exact FW 5.50. Format-specific CPU decoders validate mip and
   layer selection, exact/tolerant texels as appropriate, alpha/range/channel
   behavior, and reproducible native hashes.

5. **Compressed depth and HTILE progression**

   The endpoint sequence covers the uncompressed D32/D16/S8/D16+S8 baseline;
   ordinary D16 and D32 HTILE with decompress/resummarize; D16 and D32
   expclear; combined D32+S8 ordinary plus depth-only, stencil-only, and
   both-aspect expclear; and mip-1/array-layer-1 selected-versus-outside HTILE
   isolation. All are hardware-qualified on exact FW 5.50 and FW 11.60 with
   exact depth/stencil distributions, frozen metadata counts, bounded fences,
   shutdown, cleanup, and no residual process. The final identical FW 5.50 D16
   expclear replay produced `49152` changed HTILE words and closes the prior
   one-versus-two-pass asymmetry.

6. **MSAA parity**

   D32+4x RGBA8 shader resolve passed twice on both endpoints with exact
   resolved-color and native D32 class counts. Dedicated full-4x and
   partial-2x sample-rate gates also passed twice on each endpoint, including
   exact per-sample/total invocation counters and guard regions.

## Final verification

- Clean generic build completed without warnings before endpoint replay.
- `openagc_tests`: `12240 passed, 0 failed`.
- CTest: 6/6 suites passed, including runner fail-closed fixtures, combined
  depth oracle, cleanup stress paths, and firmware-neutral ELF verification.
- The final hardware process query found no residual `eboot.bin`.
- Worktree changes are limited to documented commits; runtime cache/log
  directories remain untracked and untouched.

## Qualification boundaries

The result proves the exact host contracts and the tested hardware endpoints.
It does not promote the other 37 active firmware profiles from
SPRX/profile-qualified to hardware-qualified. It also does not include tiled
BC layout, BC image-copy/mip-copy, or unrelated pending graphics features;
those remain independent future gates.
