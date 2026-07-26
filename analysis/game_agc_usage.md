# FW 5.50 Game AGC Compatibility Corpus

## Method

`tools/analyze_game_agc.py` reads the program headers and dynamic tables from a
decrypted PS5 ELF. It does not require section headers, which are commonly
removed or point beyond the distributed image. The tool resolves SCE import
library identifiers, maps AGC NIDs through `analysis/agc_known_nids.tsv`, and
classifies coverage from OpenAGC's public declarations.

Only metadata, hashes, and import inventories are committed. Game executables
remain external reference material.

## Coverage

| Game | Title ID | Engine | SDK | AGC imports | Covered | Unresolved |
|------|----------|--------|-----|-------------|---------|------------|
| New Joe & Mac: Caveman Ninja 01.003 | PPSA02801 | Unity IL2CPP | 5.00 | 70 | 70 | 0 |
| Unknown backport 01.000.000 | PPSA09076 | Unknown | Unknown | 69 | 69 | 0 |
| Unknown | PPSA03157 | Unknown | Unknown | 58 | 58 | 0 |
| Subnautica 01.022.394 | PPSA02453 | Unity IL2CPP | 4.00 | 63 | 63 | 0 |

The four-title corpus contains **73 unique AGC functions, all implemented**.
The release target remains at least ten representative FW 5.50-compatible
titles across multiple engines and SDK vintages.

## Candidate scope

- `PPSA01325` (ASTRO's PLAYROOM) is excluded by project scope decision and
  must not be counted toward the ten-title release gate.
- `PPSA17942` (DRAGON QUEST VII Reimagined) is a hardware-proven FW `0x0550`
  backport target despite its `0x1202` metadata. It bundles AGC compatibility
  SPRXs and imports 253 AGC functions. The completed GetSize batches cover 191
  imports; 62 remain under analysis, so it is tracked separately from the four
  fully covered titles. AcquireMem packet size now follows the FW 5.50 title
  workaround mode: mode 1 uses 64 bytes, while modes 0 and 2 use 32 bytes.
- The durable exclusion list is `analysis/game_compat_exclusions.tsv`.

## PPSA02453 evidence

- Binary size: 27,750,350 bytes.
- SHA-256: `8d5cd4b6417363a0568ea8d3c28ebdbad01e9725edaf39c614d303b352dcaf07`.
- Unity build path identifies `PS5_4_00_nondev_i_m`.
- The exact 63-symbol inventory is in `analysis/game_compat_imports.tsv`.
- NID `HV4j+E0MBHE` is `sceAgcCreateInterpolantMapping_0100`, not
  `sceAgcDcbWaitFlip`. The name hashes to that NID, and FW 5.50 ordinal 133 at
  `0xdd60` implements the same three-argument interpolant-mapping ABI as the
  current export.
- OpenAGC exposes `_0100` as a forwarding wrapper to the already tested current
  implementation. Its output has an exact host fixture.

Reproduce the inventory without writing game data into the repository:

```sh
tools/analyze_game_agc.py --require-covered /path/to/decrypted/eboot.bin
```

## Existing three-title provenance

The original three binaries are no longer present in the indexed local game
trees. Their retained summaries remain valid compatibility evidence, but their
exact import lists must be regenerated if the binaries become available again.
The corpus manifest records those rows as `legacy-analysis-only` rather than
inventing hashes or provenance.
