# FW 5.50 HTILE Subresource Qualification

Date: 2026-07-27

## Scope

This qualification isolates ordinary compressed HTILE writes for a nonzero
mip and a nonzero array layer. Decompression/resummarization remains covered by
the separately qualified single-level `agc_depth_htile_ops` fixture; combining
that full-surface diagnostic with subresource isolation would obscure which
operation changed adjacent metadata.

Target environment:

- Raw system software: `0x05500008` (`5.500.008`)
- GPU: standard PS5 gfx1013
- Deployment: sequential FTP upload and foreground curl/websrv `/hbldr`
- Host suite: clean generic build and CTest pass
- Cross-build: Prospero library and both fixture ELFs pass without warnings

## Mip-level result

`agc_depth_htile_mip.elf` binds mip 1 of a two-level 1920x1080 D32 image. The
base image remains 1920x1080 in `DB_DEPTH_SIZE_XY`; viewport and scissor are
restricted to the selected mip's 960x540 extent.

- HTILE subresource offset: `0x0`
- HTILE subresource size: `0x10000`
- Selected words changed: 4,385
- Words changed outside the selected range: 0
- Four stage markers: PASS
- Color outcome: PASS, 31,968 green and 31,968 red pixels
- EOP fence: 1 ms
- VideoOut: 1,800 accepted, 1,800 completed

Two diagnostic runs preceded the passing gate. A full-size viewport caused
metadata changes outside mip 1 even without a GPU fault, proving that the
application must restrict raster coverage to the mip attachment extent. The
failed diagnostic logs are retained with the passing raw log.

## Array-layer result

`agc_depth_htile_array.elf` binds layer 1 of a two-layer 1920x1080 D32 image.

- HTILE subresource offset: `0x30000`
- HTILE subresource size: `0x30000`
- Selected words changed: 18,013
- Words changed outside the selected range: 0
- Four stage markers: PASS
- Color outcome: PASS, 128,304 green and 128,304 red pixels
- EOP fence: 1 ms
- VideoOut: 1,800 accepted, 1,800 completed

Layer 0 remained byte-for-byte at the initial uncompressed-depth HTILE word.

## Layout conclusions

- gfx10 HTILE metadata for multiple mips is reverse ordered. Ordinary mips are
  placed from the smallest pre-tail mip toward mip 0; shared tail metadata, if
  present, starts at the layer slice base.
- Array layers are separated by `AgcGfx1013HtileLayout.slice_size`.
- gfx10 tiled depth mip chains use a two-dimensional mip-chain footprint.
  Summing independently aligned mip byte sizes can under-allocate the image.
- `DB_DEPTH_VIEW.MIPID` selects storage, but it does not replace the
  application's responsibility to set a mip-sized viewport and scissor.

## SHA-256 evidence

```text
07b8f3e9f3b295761b6ad8bbc55472db4e6b040f50e9e9054a9c4aca52d5f905  agc_depth_htile_mip.elf
350e104047a6e679391e52bbcb127e10f61ec0f34c5b5618b49d41d8453e06b0  agc_depth_htile_array.elf
d1274346054dccf6dbdb115cb281da1f0f4afe61e218a5b3f169a92c12c0f399  agc_depth_htile_mip_scissored.log
723323918cc9eecd52c105a7289e63fa67e4766a6c6f3155a512fad6690695d1  agc_depth_htile_array.log
```

Raw logs are retained locally in
`samples/hw_test/conformance-logs/htile-subresources-20260727/` and are not
committed because they contain console-specific operational details.
