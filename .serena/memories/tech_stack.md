# Tech stack

## Language & std

- C (C99-ish). No C++ features. Only compiler extensions in use: `PS5_SYSV_ABI` (`__attribute__((sysv_abi))` on prospero, empty on generic) and `_Static_assert`.
- Warnings enabled in both build systems: `-Wall -Wextra -Wpedantic`.

## Build tools

- **CMake** (primary, ≥3.15). Two platform modes via `-DOPENAGC_PLATFORM={generic,prospero,auto}`. Tests gated by `-DOPENAGC_BUILD_TESTS=ON`.
- **Make** (host fast path). Plain Makefile at repo root, builds `libopenagc.a` and `openagc_tests` for generic only.
- No package manager. Host build is libc-only — do NOT introduce third-party libs.

## Toolchains

- **Host (generic):** system `cc` (Apple clang on Darwin). No SDK needed.
- **PS5 (prospero):** ps5-payload-sdk — Clang 18 + LLD 18 targeting x86_64 PS5 FreeBSD-ish. Defines `__PROSPERO__`. Install path `~/ps5-payload-sdk` (env `PS5_PAYLOAD_SDK`). macOS setup: `brew install llvm@18 wget`, `export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config`.
  - Links `kernel` + `SceAgcDriver` stubs (sce_stubs mechanism, generated from decrypted SPRX at `FIRMWARE_FILES/5.50/sprx/common_lib/`).

## Deployment

- Exploited PS5: `prosperopkg-deploy` over FTP (`PS5_HOST`, `PS5_PORT=9021`).
- Debug-mode PS5: LibProsperoPkg (C++20, at `/Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar`) fake-signs ELF → `.pkg`.

## Reference repos (host-only, do NOT copy code)

- `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50` — firmware dump.
- `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc` — prior incomplete project; known ioctl errors. NID cross-ref only.
- `/Users/bizkut/Downloads/PS5/homebrew/sharpemu` — emulator ref.
- `/Users/bizkut/Downloads/PS5/homebrew/rpcsx` — GPU/PM4 ref.

See `mem:core` for invariants on these references.