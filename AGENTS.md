# AGENTS.md — openagc

Guidance for AI agents (Devin, Claude, etc.) working in this repository.

## Project

`openagc` is a clean-rewrite PS5 AGC (Advanced Graphics Controller) library
providing `sceAgc*` / `sceAgcDriver*` ABI compatibility with the Sony PS5 SDK,
buildable without proprietary SDK headers. Targets two backends:

- `generic` — pure software host backend, used for tests
- `orbis` — native PS5 `/dev/gc` backend (implemented, awaiting hardware
  validation)

The codebase is C (C99-ish), MIT licensed, and depends only on libc on the
host. Do not introduce new dependencies without checking the build files first.

## Build & Test

Host (generic) — primary verification path:

```sh
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Or via Make:

```sh
make
make test
# or: make -B test
```

PS5 (orbis) — requires the ps5-payload-sdk:

```sh
# One-time SDK setup (see "PS5 SDK" section below for details)
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk

# Build openagc for orbis
cmake -B build-orbis -DOPENAGC_PLATFORM=orbis -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-orbis

# Output: build-orbis/libopenagc.a (PS5 x86_64 static library)
```

The orbis build compiles `driver_orbis.c` with native `/dev/gc` ioctl calls.
It links against `kernel` and `SceAgcDriver` stubs from the SDK.

Expected host test result: `519 passed, 0 failed`. Any change that drops this
count is a regression — fix it before declaring the task done.

## Verification Checklist

Before marking a task complete:

1. Build clean from scratch for the `generic` platform (rm -rf build, re-cmake).
2. `ctest --test-dir build --output-on-failure` passes with 519/519 (or the
   updated count if you intentionally added tests).
3. No new compiler warnings under `-Wall -Wextra -Wpedantic` (already enabled
   in both CMake and Makefile).
4. Any new public symbol is declared in the appropriate `include/*.h` with the
   `PS5_SYSV_ABI` calling-convention macro and matches Sony-style naming.
5. Any new struct layout that must match firmware ABI gets a
   `_Static_assert` size/offset check.
6. If you added a packet builder, add a test in `tests/test_cb.c` or
   `tests/test_dcb.c` covering header encoding and cursor advance.
7. Update `STATUS.md` "Implemented Packet Builders" list if you add a builder.

## Code Conventions

- **Language:** C, host-buildable. No C++ features, no compiler-specific
  extensions beyond what's already used (`PS5_SYSV_ABI`, `_Static_assert`).
- **Naming:**
  - Public Sony-compatible functions: `sceAgc*`, `sceAgcDriver*`,
    `sceAgcCb*`, `sceAgcDcb*`, `sceAgcAcb*` — match the firmware export names
    exactly.
  - Internal helpers: `agc*` / `agcCb*` / `agcPm4*` lowercase camelCase.
  - Types: `SceAgc*` / `Agc*` PascalCase.
  - Macros/constants: `AGC_*` / `AGC_PM4_*` UPPER_SNAKE.
- **ABI macros:** `PS5_SYSV_ABI` is required on every public function
  declaration. Use it on definitions too (see existing `src/cb_builders.c`).
- **Headers:** Public headers live in `include/`. Private helpers shared
  between `src/*.c` go in `src/` (not `include/`). The CMake target exposes
  only `include/` as `PUBLIC` and `src/` as `PRIVATE`.
- **Includes:** Public headers use `#include "agc_*.h"` for sibling headers.
  Implementation files include their public counterpart first, then
  `<string.h>` / `<stdint.h>` etc. as needed.
- **Error handling:** Return `int32_t` AGC error codes from public functions
  (see `include/agc_error.h`). Internal helpers return `NULL` / `0` / `false`
  on failure as the existing code does — do not add verbose try/catch-style
  error paths.
- **Static asserts:** Any struct that mirrors firmware layout must have
  `_Static_assert(sizeof(T) == expected, "...")` and, where relevant,
  `_Static_assert(offsetof(T, field) == expected, "...")`.
- **Code comments:**  Add or remove comments if it will help us in the development.

## Architecture Notes

- **Packet model:** Gen5 AGC uses type-3 PM4 packets with
  `length_dwords - 2` encoded in bits `29:16` of the header. Use
  `agcPm4Header3()` / `agcPm4Header3Sub()` from `agc_pm4.h` — never hand-pack
  headers.
- **Cursor model:** `SceAgcCb` is a cursor over a command buffer.
  `agcCbAllocDwords(cb, n)` advances the cursor and returns the write pointer.
  Builders must check the return for `NULL` and bail out cleanly.
- **Backend split:** `driver_generic.c` is the testable host backend;
  `driver_orbis.c` is the PS5 backend. Anything that touches `/dev/gc`,
  ioctls, or kernel objects belongs only in `driver_orbis.c` and must be
  guarded by `#ifdef OPENAGC_ORBIS`.
- **No firmware blobs:** Firmware SPRX modules and microcode from
  `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50` are RE references only.
  Never copy, embed, or commit them. Constants recovered from RE go in
  `include/agc_re.h`, `include/agc_nids.h`, and `include/agc_ioctl.h`
  (kernel-side ioctl/submit/queue layouts for FW 5.50).

## Reference Paths (host machine only — do not commit)

- Firmware dump: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50`
- Incomplete prior project: `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc`
- Emulator reference: `/Users/bizkut/Downloads/PS5/homebrew/sharpemu`
- GPU/PM4 reference: `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`

These are for cross-referencing ABI only. Do not copy code from them verbatim
— openagc is a clean rewrite.

## PS5 SDK (orbis backend toolchain)

The `orbis` backend builds against the **ps5-payload-sdk** — the open-source
SDK for building payloads for exploited PS5s.

- **Local source:** `/Users/bizkut/Downloads/PS5/sdk`
- **Install path:** `/Users/bizkut/ps5-payload-sdk` (set `PS5_PAYLOAD_SDK`
  env var). The official path is `/opt/ps5-payload-sdk` but that requires
  sudo; `~/ps5-payload-sdk` works without sudo.
- **Toolchain:** Clang 18 + LLD 18, targeting x86_64 PS5 FreeBSD-ish ABI.
  The toolchain defines `__ORBIS__`, which openagc's `agc_types.h` uses to
  resolve `PS5_SYSV_ABI` to `__attribute__((sysv_abi))`.
- **Provides:** `crt` (C runtime), `libc`, `libufs`, kernel headers
  (`include/freebsd`) for `ioctl`/`mmap`/`open`(`/dev/gc`), and the
  `sce_stubs` mechanism for linking SPRX exports.
- **macOS setup:** `brew install llvm@18 wget` then
  `export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config`.

### Linking AGC SPRX exports

The `sce_stubs` mechanism lets you link decrypted SPRX modules as stubs so
the orbis backend can call `sceAgc*` / `sceAgcDriver*` exports by NID:

```sh
ln -s /path/to/libSceAgc.sprx      $PS5_PAYLOAD_SDK/../sdk/sce_stubs/libSceAgc.sprx
ln -s /path/to/libSceAgcDriver.sprx $PS5_PAYLOAD_SDK/../sdk/sce_stubs/libSceAgcDriver.sprx
make -C sce_stubs stubs
sudo make DESTDIR=/opt/ps5-payload-sdk install
```

Decrypted SPRX files live at
`/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50/sprx/common_lib/`. Do not
commit the SPRX files themselves — only the generated stubs (if any).

### Deploy to PS5

Two deployment paths:

**1. ELF payload (exploited PS5):**

```sh
export PS5_HOST=ps5; export PS5_PORT=9021
prospero-deploy -h $PS5_HOST -p $PS5_PORT build-orbis/openagc_payload.elf
```

**2. Installable .pkg (debug-mode PS5):**

Use LibProsperoPkg (C++ rewrite by seregonwar) to fake-sign the ELF and
package it as an installable `.pkg`:

```sh
# Build LibProsperoPkg (one-time)
cmake -S /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar \
      -B /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build \
      -DCMAKE_BUILD_TYPE=Release -DLIBPROSPEROPKG_BUILD_TOOLS=ON
cmake --build /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build --parallel

# Fake-sign the payload ELF
PKG_TOOLS=/Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build
$PKG_TOOLS/prosperopkg-fself payload.elf payload.self

# Generate GP5 project from app folder
$PKG_TOOLS/prosperopkg-gp5 app_dir out.gp5 --flat --type app

# Build the .pkg from the GP5 project (use the C ABI or CLI)
# → produces an installable .pkg for debug-mode consoles
```

### PS5 packaging tool (LibProsperoPkg)

- **Local source:** `/Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar`
- **Language:** C++20, CMake 3.24+
- **Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLIBPROSPEROPKG_BUILD_TOOLS=ON && cmake --build build --parallel`
- **Output:** `build/prosperopkg-{fself,gp5,inspect,keys,lzn}` + `build/LibProsperoPkg.0.1.0.dylib`
- **CLI tools:**
  - `prosperopkg-fself <input.elf> <output.self>` — convert ELF to fake-SELF
  - `prosperopkg-gp5 <source-folder> <output.gp5> [--flat] [--type <type>]` — generate GP5 project
  - `prosperopkg-inspect <package.pkg>` — inspect PKG/CNT/FIH/UCP/SELF
  - `prosperopkg-keys <content_id> <seed_hex> <key_hex>` — derive EKPFS/PFS keys
  - `prosperopkg-lzn compress|decompress|bench` — LZN1/LZNB codec
- **Target:** Debug-mode PS5 (relaxes finalized-image verification)
- **License:** GPL-3.0-or-later

### CMake integration

The orbis CMake build (`-DOPENAGC_PLATFORM=orbis`) already links
`kernel` and `SceAgcDriver` (see `CMakeLists.txt` lines 58–63). Those
libraries must be available in the SDK's lib directory after stubs are
installed. The host build does not need the SDK.

### Hardware validation samples

`samples/hw_test/` contains two hardware validation programs adapted from
the `freegnm-examples` patterns for PS5. They should be run in order:

**1. `videoout_linear.elf` — VideoOut display pipeline smoke test**

Adapted from `freegnm-examples/videoout-linear/`. Tests the display
pipeline without any GPU commands:
- `sceVideoOutOpen` + `sceVideoOutGetResolutionStatus`
- `sceKernelAllocateDirectMemory` (garlic) + `sceKernelMapDirectMemory`
- `sceVideoOutRegisterBuffers` (linear A8B8G8R8_SRGB)
- CPU-fill color bars + `sceVideoOutSubmitFlip` in a loop

If this works, the display pipeline is functional.

**2. `agc_init.elf` — AGC init + NOP submit test**

Adapted from `freegnm-examples/triangle/` submit pattern. Tests the
native `/dev/gc` backend via `libopenagc.a`:
- `sce_agc_initialize()` — `/dev/gc` open + `FRAME_OPEN` ioctl
- `sce_agc_initialize_internal_memory()` — allocate 8 named internal regions
- `sceAgcDriverNotifyDefaultStates()` — build primary/internal register-defaults blobs
- `sceAgcDriverSuspendPointSubmitDirect()` — submit a suspend point
- `sceAgcDriverGetPaDebugInterfaceVersion()` — PA debug query
- `sceAgcDriverSubmitDcb()` — NOP packet submission
- `_sceAgcDriverCreateUserSpecialQueue()` / `DestroyUserSpecialQueue()`

**Build:**
```sh
cd samples/hw_test
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
make all              # both .elf targets
make videoout_linear.bin agc_init.bin  # fake-SELF versions
```

**Deploy (exploited PS5):**
```sh
make deploy_videoout  # prospero-deploy videoout_linear.elf
make deploy_agc       # prospero-deploy agc_init.elf
```

**Install (debug-mode PS5 via FTP):**
```sh
make install_videoout # uploads as eboot.bin
make install_agc      # uploads as eboot.bin
```

Files:
- `ps5_video_out.h` — PS5 VideoOut constants/decls (from ps5-openagc)
- `videoout_linear.c` — display smoke test
- `agc_init.c` — AGC init + submit test
- `Makefile` — builds both, with deploy/install targets
- `sce_sys/param.json` — app metadata (title ID `AGCT00001`)

## Current Roadmap

See `STATUS.md` and `PLAN.md`. Next RE tasks, in order:

1. **Hardware validation** — deploy `samples/hw_test/` to PS5 and validate:
   - `sce_agc_initialize` + `sce_agc_initialize_internal_memory`
   - `sceAgcDriverNotifyDefaultStates` + `CLEAR_STATE` submission
   - `sceAgcDriverSubmitDcb(NOP)`
   - `sceAgcDriverSuspendPointSubmitDirect` + `IsSuspendPointInFlightDirect`
   - `_sceAgcDriverCreateUserSpecialQueue` / `DestroyUserSpecialQueue`
2. **Validate default state blobs** — confirm the primary/internal
   register-defaults blobs built by `sceAgcDriverNotifyDefaultStates` are
   accepted by the kernel and produce the expected GPU state.
3. **Validate suspend ioctls** — confirm the argument layouts for
   `SUSPEND_16` (nr=0x1c) and `SUSPEND_39` (nr=0x39) by testing on hardware.
4. **Queue creation** — implement real `QUEUE_CREATE` / `QUEUE_DESTROY` in
   `sceAgcDriverSetupAsyncGraphics` and async-compute submission.

All userspace packet builders, patchers, LOD stats helpers, the FW 5.50
ioctl/submit/queue RE layer, the shader record parser, the register-defaults
blob builder/parser, and the native orbis backend (`driver_orbis.c`) are
implemented and build. The remaining work is hardware validation and the
async-compute queue setup path.

## Non-Goals

- No firmware blobs or proprietary microcode embedded in the repo.
- No claim that the `orbis` backend is hardware-ready.
- No claim of full official SDK drop-in completeness.
- No PS4 (`sceGnm*`) content here — that belongs in the sibling `opengnm`
  project, not `openagc`.

## Things To Avoid

- Do not introduce new third-party libraries. The host build must remain
  libc-only.
- Do not use C++ features or compiler extensions beyond `PS5_SYSV_ABI` and
  `_Static_assert`.
- Do not hand-pack PM4 headers — use `agcPm4Header3*`.
- Do not put PS5-specific `/dev/gc` code in `driver_generic.c`.
- Do not commit firmware binaries, SPRX modules, or microcode files.
- Do not delete or rewrite existing RE-provenance comments.
- Do not change `PS5_SYSV_ABI` calling convention or public function
  signatures without an explicit ABI reason.
