# Firmware-neutral portability ELF (2026-07-30)

## Pinned artifact

- Build target: `samples/hw_test/agc_portability.elf`
- Preserved local copy:
  `samples/hw_test/pinned/agc_portability-e04004fee2254e6169805f153ce4812197726ed5f53a9295a4493f0d8ac9a9ce.elf`
- Size: 415688 bytes
- SHA-256: `e04004fee2254e6169805f153ce4812197726ed5f53a9295a4493f0d8ac9a9ce`

The preserved ELF is a local hardware artifact and is intentionally ignored by
Git. The digest in this document and the runner is the identity used for every
endpoint launch. Rebuilding does not produce a substitute for the FW5.50 run;
that future run must use these preserved bytes and this digest.

## Build and dependency contract

The Make target defines only `AGC_PORTABILITY_GATE` and the neutral result-log
path. It does not define `AGC_EXPECT_FIRMWARE_ABI_KEY`. The payload prints the
full raw system version and normalized four-digit key at runtime. It selects
caller ABI V7, the common register-default ABI accepted by every active exact
profile from FW3.20 through FW12.70.

`tools/verify_portability_elf.sh` verifies that the active image contains no
firmware expectation/path and has no dynamic dependency on `libSceAgc.sprx` or
`libSceAgcDriver.sprx`. Its only `DT_NEEDED` entries are VideoOut, kernel,
LibcInternal, and Net.

## Baseline lifecycle

One bounded launch covers:

1. application-neutral GPU authorization;
2. runtime firmware detection and exact profile selection;
3. `/dev/gc` initialization;
4. V7 caller selection, internal memory, defaults, and async graphics;
5. real GPU `WRITE_DATA` execution to a flexible-memory marker;
6. exact-profile VideoOut patch/restore, two bounded flips, and close;
7. driver shutdown plus flexible/direct-memory release;
8. self-termination with a file-backed verdict.

The runner performs two launches, with the renderer cleanup ELF launched
immediately before each payload, to prove teardown and relaunch using the same
pinned bytes.

## Qualification status

| Firmware | Artifact status | Hardware status |
|---|---|---|
| FW11.60 standard PS5 | exact pinned digest | 2/2 PASS |
| FW5.50 standard PS5 | same pinned bytes preserved | hardware unavailable |
| Other active exact profiles | same binary contract | SPRX-qualified, hardware-unverified |

No intermediate firmware is promoted by this artifact until the identical
digest passes on matching hardware.

## FW11.60 execution evidence

The runner downloaded the uploaded ELF and verified the pinned SHA-256 before
launching it. On standard PS5 FW11.60, both iterations ran the cleanup ELF
immediately before the portability payload and produced:

- raw system version `0x11600005`, normalized key `0x1160`;
- standard-PS5 exact runtime profile;
- common V7 defaults: 127 primary groups (`0x40230` bytes) and 22 internal
  groups (`0xb1f8` bytes);
- defaults and async setup `PASS/PASS`;
- exact-profile VideoOut registration patch and restoration `PASS`;
- real GPU marker `0x504f5254` observed after 50 ms;
- two bounded flips `PASS`;
- driver shutdown, flexible-memory release, display unmap, and direct-memory
  release all returned zero;
- final file-backed `Portability result: PASS` and self-termination.

The second launch independently repeated the same results, proving teardown
and relaunch on FW11.60 with the same artifact. Port 8080 and the result-log FTP
path remained responsive afterward.
