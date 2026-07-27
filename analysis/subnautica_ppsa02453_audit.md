# Subnautica `PPSA02453` AGC compatibility audit

## Artifact

- Title: Subnautica
- Title ID: `PPSA02453`
- Content version: `01.022.394`
- Origin content version: `01.000.000`
- SDK version: `0x0400`
- Required system software: `0x1120`
- Decrypted `eboot.bin` size: 27,750,350 bytes
- SHA-256: `8d5cd4b6417363a0568ea8d3c28ebdbad01e9725edaf39c614d303b352dcaf07`

The executable is an ELF64 FreeBSD image with valid SCE dynamic metadata. The
binary itself is external evidence and is not committed to OpenAGC.

## Coverage result

```text
63 AGC imports, 0 unresolved, 0 declared-only
```

The strict coverage gate passes:

```sh
python3 tools/analyze_game_agc.py --require-covered /path/to/eboot.bin
```

The strict analyzer checks both public declarations and source definitions; a
header-only symbol is reported as `declared-only` and fails `--require-covered`.
The import surface contains 57 `libSceAgc` functions and 6
`libSceAgcDriver` functions. Fifty-eight resolve directly to public OpenAGC
definitions. Five intentionally resolve through versioned ABI wrappers:

- `sceAgcCreateInterpolantMapping_0100`
- `sceAgcFuseShaderHalves_0200`
- `sceAgcGetDataPacketPayloadAddress_0090`
- `sceAgcGetFusedShaderSize_0080`
- `sceAgcInit_0090`

The title also imports the tessellation-related driver controls
`sceAgcDriverSetHsOffchipParam` and `sceAgcDriverSetTFRing`, both of which are
implemented.

## Scope of the result

This proves static import, declaration, and definition coverage for this exact
executable. It is bounded ABI evidence, not an OpenAGC product or release goal.
It does not by itself prove that every imported function is reached at runtime,
nor does it constitute a full Subnautica play-through on FW `0x0550`.

Although the package metadata requires system software `0x1120`, the title is
useful compatibility evidence because it uses the SDK `0x0400` AGC surface and
older versioned entry points. Keep it in the corpus as a cross-version API
target, not as proof that an unmodified `0x1120` package launches natively on
FW `0x0550`.
