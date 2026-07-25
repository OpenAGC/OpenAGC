# AGENTS.md — openagc

Guidance for AI agents (Claude, etc.) working in this repository.

## Project

`openagc` is a clean-rewrite PS5 AGC (Advanced Graphics Controller) library
providing `sceAgc*` / `sceAgcDriver*` ABI compatibility with the Sony PS5 SDK,
buildable without proprietary SDK headers. Targets two backends:

- `generic` — pure software host backend, used for tests
- `prospero` — native PS5 `/dev/gc` backend (implemented, awaiting hardware
  validation)

The codebase is C (C99-ish), Apache 2.0 licensed, and depends only on libc on the
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

PS5 (prospero) — requires the ps5-payload-sdk:

```sh
# One-time SDK setup (see "PS5 SDK" section below for details)
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk

# Build openagc for prospero
cmake -B build-prospero -DOPENAGC_PLATFORM=prospero -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-prospero

# Output: build-prospero/libopenagc.a (PS5 x86_64 static library)
```

The prospero build compiles `driver_prospero.c` with native `/dev/gc` ioctl calls.
It links against `kernel` and `SceAgcDriver` stubs from the SDK.

Expected host test result: `1675 passed, 0 failed`. Any change that drops this
count is a regression — fix it before declaring the task done.

## Verification Checklist

Before marking a task complete:

1. Build clean from scratch for the `generic` platform (rm -rf build, re-cmake).
2. `ctest --test-dir build --output-on-failure` passes (1/1 test suite, or the
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
- **Commit Messages:** Do not include AI assistant branding or automated co-author tags (e.g. Devin) in git commit messages.


## Architecture Notes

- **Packet model:** Gen5 AGC uses type-3 PM4 packets with
  `length_dwords - 2` encoded in bits `29:16` of the header. Use
  `agcPm4Header3()` / `agcPm4Header3Sub()` from `agc_pm4.h` — never hand-pack
  headers.
- **Cursor model:** `SceAgcCb` is a cursor over a command buffer.
  `agcCbAllocDwords(cb, n)` advances the cursor and returns the write pointer.
  Builders must check the return for `NULL` and bail out cleanly.
- **Backend split:** `driver_generic.c` is the testable host backend;
  `driver_prospero.c` is the PS5 backend. Anything that touches `/dev/gc`,
  ioctls, or kernel objects belongs only in `driver_prospero.c` and must be
  guarded by `#ifdef OPENAGC_PROSPERO`.
- **No firmware blobs:** Firmware SPRX modules and microcode from
  `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50` are RE references only.
  Never copy, embed, or commit them. Constants recovered from RE go in
  `include/agc_re.h`, `include/agc_nids.h`, and `include/agc_ioctl.h`
  (kernel-side ioctl/submit/queue layouts for FW 5.50).
- **NID table scope:** `analysis/agc_known_nids.tsv` and `include/agc_nids.h`
  contain only NIDs that exist in the **FW 5.50 SPRX** (our primary target).
  354 NIDs mapped (96.7% of 366 exports), 12 remain unidentified.
  NIDs from other firmware versions (3.20-only, 11.60-only) are in
  `analysis/agc_nids_version_variants.tsv` for cross-version reference.
  NIDs are **not stable** across firmware versions — some functions have
  different NIDs in 3.20 vs 5.50 (e.g. `sceAgcFuseShaderHalves`:
  3.20=`nApJjpKNBl4`, 5.50=`fd5Bp5tGTgo`). 9 functions have two NIDs in 5.50
  SPRX (old+new version exports), disambiguated with `_<NID>` suffix in the
  name. NID computation uses SHA1(name+salt) via the prospero-nid algorithm
  (see `/Users/bizkut/Downloads/PS5/sdk/host/bin/prospero-nid.c`).
  Since OpenAGC is a static library, NIDs are reference-only and not used at runtime.

## Reference Paths (host machine only — do not commit)

- Firmware dump: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50`
- Incomplete prior project: `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc`
  — **NOT proven working on hardware.** Contains known errors (wrong
  FRAME_OPEN ioctl, wrong queue create/destroy ioctls, wrong VMID mask).
  Use for NID mapping cross-reference only. Do NOT trust its ioctl layouts
  or memory region sizes without independent SPRX verification.
  See `analysis/ps5_openagc_audit.md` for the full audit.
- GPU/PM4 reference: `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`
- PS5 emulator (AGC reference): `/Users/bizkut/Downloads/PS5/homebrew/sharpemu`
  — C# PS5 emulator with a detailed AGC HLE implementation in
  `src/SharpEmu.Libs/Agc/AgcExports.cs`. Confirmed-correct for:
  - Shader type byte encoding at offset 0x5A (CS=0, PS=1, ES=2, VS=3, GS=4,
    HS=5, ES-alt=6, LS=7) — used by `sceAgcCreateShader` to patch PGM_LO/HI
  - Compute dispatch initiator: `(modifier & 0xA038) | 0x41`
  - PGM_LO/HI address encoding: `addr = (HI << 40) | (LO << 8)`
  - Compute register offsets: PGM_LO=0x20C, PGM_HI=0x20D, RSRC2=0x213,
    NUM_THREAD_X/Y/Z=0x207/0x208/0x209, USER_DATA_0=0x240
  - RSRC2 USER_SGPR field: bits [5:1], system SGPRs follow (TGID_X at bit 7)

These are for cross-referencing ABI only. Do not copy code from them verbatim
— openagc is a clean rewrite.

## PS5 SDK (prospero backend toolchain)

The `prospero` backend builds against the **ps5-payload-sdk** — the open-source
SDK for building payloads for exploited PS5s.

- **Local source:** `/Users/bizkut/Downloads/PS5/sdk`
- **Install path:** `/Users/bizkut/ps5-payload-sdk` (set `PS5_PAYLOAD_SDK`
  env var). The official path is `/opt/ps5-payload-sdk` but that requires
  sudo; `~/ps5-payload-sdk` works without sudo.
- **Toolchain:** Clang 18 + LLD 18, targeting x86_64 PS5 FreeBSD-ish ABI.
  The toolchain defines `__PROSPERO__`, which openagc's `agc_types.h` uses to
  resolve `PS5_SYSV_ABI` to `__attribute__((sysv_abi))`.
- **Provides:** `crt` (C runtime), `libc`, `libufs`, kernel headers
  (`include/freebsd`) for `ioctl`/`mmap`/`open`(`/dev/gc`), and the
  `sce_stubs` mechanism for linking SPRX exports.
- **macOS setup:** `brew install llvm@18 wget` then
  `export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config`.

### Linking AGC SPRX exports

The `sce_stubs` mechanism lets you link decrypted SPRX modules as stubs so
the prospero backend can call `sceAgc*` / `sceAgcDriver*` exports by NID:

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

Three deployment paths:

**1. ELF payload (exploited PS5):**

```sh
export PS5_HOST=10.0.1.41; export PS5_PORT=9021
prospero-deploy -h $PS5_HOST -p $PS5_PORT build-prospero/openagc_payload.elf
```

**2. Homebrew via websrv (exploited PS5 with etaHEN):**

The PS5 runs a `websrv` on port 8080 (HTTP) and 2121 (FTP) that serves as a
homebrew launcher. Upload the ELF via FTP, then launch it via HTTP:

```sh
# Upload ELF + icon to the websrv homebrew directory via FTP
curl -s "ftp://$PS5_HOST:2121/" --quote "MKD /data/homebrew/myapp" 2>&1
curl -s "ftp://$PS5_HOST:2121/" --quote "MKD /data/homebrew/myapp/sce_sys" 2>&1
curl -T myapp.elf "ftp://$PS5_HOST:2121/data/homebrew/myapp/eboot.elf"
curl -T icon0.png "ftp://$PS5_HOST:2121/data/homebrew/myapp/sce_sys/icon0.png"

# Launch the homebrew via the websrv /hbldr endpoint
# pipe=1 streams stdout back in the HTTP response
# daemon=0 runs in foreground (daemon=1 backgrounds it)
curl -s "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/myapp/eboot.elf"
```

The `/hbldr` endpoint loads the ELF into the PS5's homebrew loader and
executes it. With `pipe=1`, stdout/stderr are streamed back in the HTTP
response, making it easy to see printf output without a separate log
viewer. The app appears in the PS5 home menu with its icon.

**3. Installable .pkg (debug-mode PS5):**

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

The prospero CMake build (`-DOPENAGC_PLATFORM=prospero`) already links
`kernel` and `SceAgcDriver` (see `CMakeLists.txt` lines 58–63). Those
libraries must be available in the SDK's lib directory after stubs are
installed. The host build does not need the SDK.

### Hardware validation samples

`samples/hw_test/` contains four hardware validation programs adapted from
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
- `sce_agc_initialize()` — `/dev/gc` open + `CONTEXT_QUERY` ioctl (0xc004812e) + mmap GPU registers at 0xfe0200000
- `sce_agc_initialize_internal_memory()` — allocate 8 named internal regions
- `sceAgcDriverNotifyDefaultStates()` — build primary/internal register-defaults blobs
- `sceAgcDriverSuspendPointSubmitDirect()` — submit a suspend point
- `sceAgcDriverGetPaDebugInterfaceVersion()` — FW 5.50 permission-stub check
- `sceAgcDriverSubmitDcb()` — NOP packet submission
- `_sceAgcDriverCreateUserSpecialQueue()` / `DestroyUserSpecialQueue()`

**3. `agc_videoout.elf` — Combined AGC + VideoOut test**

Tests the full graphics pipeline together:
- GPU credential bypass (`set_gpu_credentials` from `gpu_credentials.h`)
- `sce_agc_initialize()` + `sce_agc_initialize_internal_memory()`
- `sceAgcDriverNotifyDefaultStates()` + `sceAgcDriverSetupAsyncGraphics()`
- `sceVideoOutOpen` + garlic direct memory + `sceVideoOutRegisterBuffers`
- `sceAgcDriverSubmitDcb()` NOP submission during the flip loop
- CPU-rendered SMPTE color bars + `sceVideoOutSubmitFlip` for 600 frames

Run this after both `videoout_linear` and `agc_init` pass individually.

**4. `agc_compute.elf` — Compute dispatch test (real GPU execution)**

Tests actual GPU compute shader execution — the first sample to submit
non-NOP commands to the GPU:
- GPU credential bypass + AGC init + VideoOut (same as agc_videoout)
- libSceVideoOut.sprx runtime patch (NOP the linear tiling check at 0x7e61)
- Load a compute shader binary (`fill_color.sb`) compiled by `openagc-psbc`
  from GLSL → SPIR-V → ACO → AgcShaderRecord format
- Upload shader code to GPU-accessible garlic memory
- Set SH registers: COMPUTE_PGM_LO/HI (shader address), RSRC1/2/3, NUM_THREAD
- Set user data: buffer pointer + push constants (total_pixels, color)
- `sceAgcDriverSubmitDcb()` with SET_SH_REG + DISPATCH_DIRECT packets
- Flip display to show GPU-rendered output

**Status:** PASS — 100% GPU compute shader execution verified on PS5 hardware! 2,073,600 / 2,073,600 pixels match `0xFF00FF00` (solid green GPU-rendered frame on TV/display).


The compute shader (`shaders/fill_color.comp`) writes a solid color to every
pixel of the display buffer. The full pipeline is: GLSL → `glslangValidator`
→ SPIR-V → `psbc` (NIR + ACO) → `AgcShaderRecord` .sb file → embedded as
C header → uploaded to GPU memory → dispatched via PM4 command buffer.

**Build:**
```sh
cd samples/hw_test
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config
make all              # all four .elf targets
make videoout_linear.bin agc_init.bin agc_videoout.bin  # fake-SELF versions
```

**Deploy (exploited PS5):**
```sh
make deploy_videoout      # prospero-deploy videoout_linear.elf
make deploy_agc           # prospero-deploy agc_init.elf
make deploy_agc_videoout  # prospero-deploy agc_videoout.elf
```

**Deploy via websrv (exploited PS5 with etaHEN):**
```sh
# Upload + launch any sample via the websrv homebrew launcher
PS5_HOST=10.0.1.41
curl -s "ftp://$PS5_HOST:2121/" --quote "MKD /data/homebrew/agc_compute" 2>&1
curl -s "ftp://$PS5_HOST:2121/" --quote "MKD /data/homebrew/agc_compute/sce_sys" 2>&1
curl -T agc_compute.elf "ftp://$PS5_HOST:2121/data/homebrew/agc_compute/eboot.elf"
curl -T sce_sys/icon0.png "ftp://$PS5_HOST:2121/data/homebrew/agc_compute/sce_sys/icon0.png"
curl -s "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_compute/eboot.elf"
```

**Install (debug-mode PS5 via FTP):**
```sh
make install_videoout     # uploads as eboot.bin
make install_agc          # uploads as eboot.bin
make install_agc_videoout # uploads as eboot.bin
```

**Build .pkg packages (debug-mode PS5 installation):**
```sh
make pkg_videoout      # builds pkg_out/videoout/*.pkg
make pkg_agc           # builds pkg_out/agc/*.pkg
make pkg_agc_videoout  # builds pkg_out/agc_videoout/*.pkg
make pkg_all           # builds all three
```

The `pkg_*` targets use `prosperopkg-pkg` (from LibProsperoPkg) to build
installable `.pkg` files. Each target:
1. Fake-signs the ELF via `prosperopkg-fself` → eboot.bin
2. Creates an app folder with eboot.bin + sce_sys/param.json
3. Builds a `FullDebug` .pkg via the `lpp_build_package` C API

Content ID: `UP9000-AGCT00001_00-OPENAGC000000000`
Passcode: `00000000000000000000000000000000` (debug)

Files:
- `ps5_video_out.h` — PS5 VideoOut constants/decls (adapted from ps5-openagc;
  VideoOut constants are well-known and not affected by ps5-openagc's ioctl errors)
- `gpu_credentials.h` — shared GPU credential bypass (cr_sceAuthId patching)
- `videoout_linear.c` — display smoke test
- `agc_init.c` — AGC init + submit test
- `agc_videoout.c` — combined AGC + VideoOut test
- `agc_compute.c` — compute dispatch test (real GPU shader execution)
- `shaders/fill_color.comp` — GLSL compute shader source
- `shaders/fill_color.spv` — compiled SPIR-V
- `shaders/fill_color.sb` — psbc-compiled AgcShaderRecord binary
- `shaders/fill_color_sb.h` — embedded shader header for agc_compute.elf
- `Makefile` — builds all four, with deploy/install targets
- `sce_sys/param.json` — app metadata (title ID `AGCT00001`)

## Current Roadmap

See `STATUS.md` and `PLAN.md`. Next RE tasks, by priority:

1. **Compute shader not writing to display buffer** (Priority 1, blocking) —
   the GPU accepts the dispatch DCB and the DISPATCH_DIRECT packet is
   processed (the GPU remains alive after dispatch — a WRITE_DATA placed
   after the dispatch executes correctly). However, the compute shader
   itself does not write to the display buffer. Only pixel[0] is written,
   and that's via a WRITE_DATA PM4 packet, not the shader. See the detailed
   debugging notes below.
2. **Graphics draw call** (Priority 2, blocked on #1) — once compute shader
   execution is confirmed, the next milestone is a real graphics draw call:
   VS+PS shaders, render target binding, viewport/scissor, blend state, and
   `IT_DRAW_INDEX_AUTO`. See PLAN.md Phase 7.
3. **Additional render-target formats** (Priority 3) — expand the validated
   graphics path beyond the current RGBA8 render target.
4. **Game compatibility** (Priority 4) — continue analyzing game binaries.
   Current: 3 games, 72 unique AGC functions, 100% implemented.

Closed background RE: FW 5.50 `sceAgcDriverGetPaDebugInterfaceVersion` is a
userspace permission stub returning `0x8A6D0001` without an ioctl, and
`FRAME_OPEN` is absent from the FW 5.50 kernel dispatcher.

### Compute shader debugging status (Priority 1)

**What works:**
- AGC init, internal memory allocation, NotifyDefaultStates, async graphics
  setup all succeed (AGC_OK).
- VideoOut open, garlic memory allocation, buffer registration all work.
- Command buffer submission via `sceAgcDriverSubmitDcb` works — the GPU
  processes PM4 packets in the DCB.
- WRITE_DATA packets execute correctly: markers appear in both flexible
  memory (CB+0x1000) and garlic memory (display buffer). This confirms the
  display buffer is GPU-accessible and writable via PM4.
- The GPU remains alive after the DISPATCH_DIRECT packet — a WRITE_DATA
  placed after the dispatch executes correctly, proving the dispatch does
  not cause a GPU hang.
- `sceAgcDriverNotifyDefaultStates` now succeeds (was previously failing
  with AGC_ERROR_INVALID_ARGUMENT; fixed by correcting the DDID allocation
  sizes: AGC_DDID_PRIMARY_SIZE=0x41000, AGC_DDID_INTERNAL_SIZE=0xc000).
- The CLEAR_STATE packet during NotifyDefaultStates was causing a GPU hang
  and has been temporarily disabled.

**What doesn't work:**
- The compute shader does not write to the display buffer. Only pixel[0] is
  written, and that's via a WRITE_DATA PM4 packet, not the shader. All other
  pixels remain at the 0xDEADBEEF pre-fill marker.

**PM4 packet sequence in the DCB (all confirmed executing except shader):**
1. CONTEXT_CONTROL (0x28) — load enable context state
2. SET_SH_REG (0x76, bit0=1 for compute) — COMPUTE_RESOURCE_LIMITS (0x215)
3. SET_SH_REG — COMPUTE_NUM_THREAD_X/Y/Z (0x207-0x209): 64,1,1
4. SET_SH_REG — COMPUTE_PGM_LO/HI (0x20C-0x20D): shader_addr>>8, 0
5. SET_SH_REG — COMPUTE_PGM_RSRC1/2 (0x212-0x213): from shader record
6. SET_SH_REG — COMPUTE_PGM_RSRC3 (0x228): from shader record
7. SET_SH_REG — COMPUTE_USER_DATA_0..5 (0x240-0x245): push constants
8. WRITE_DATA (0x37) — markers to flexible + garlic memory
9. DISPATCH_DIRECT (0x15, bit0=1 for compute) — 32400 workgroups × 64 threads
10. WRITE_DATA — post-dispatch marker (confirms GPU alive after dispatch)
11. ACQUIRE_MEM (0x58) — cache flush (coher_cntl=0x2ec47fc0 from freegnm)
12. NOP trailer

**Key fixes already applied:**
- **Compute shader type bit**: SET_SH_REG and DISPATCH_DIRECT headers now
  have bit 0 set (0xc0017601 instead of 0xc0017600). Without this, packets
  go to the graphics engine, not the compute engine. RE'd from freegnm
  `PKT3_SHADER_TYPE_S(1)`.
- **PGM_LO address format**: `(shader_addr >> 8)` for PGM_LO, with PGM_HI
  holding the remaining high bits. Confirmed from KytyPS5 disassembly:
  `(PGM_LO << 8) | ((PGM_HI & 0xFF) << 40)`.
- **COMPUTE_RESOURCE_LIMITS**: register at 0x215 (not 0x206), set to
  0x3FFFFFFF. Confirmed from KytyPS5 pm4.h.
- **WRITE_DATA packet length**: Fixed from 6 dwords to 5 dwords (count=5).
  The extra dword was corrupting the next memory location — in one run it
  overwrote the shader code at 0x201de9000.
- **User data SGPR layout**: Confirmed from openagc-psbc NIR postprocess output and KytyPS5/SharpEmu reference analysis.
  The shader's inline push constants start after ring_offsets (s0..s1 unused):
  - USER_DATA_0 (s0): ring_offsets low (unused, set to 0)
  - USER_DATA_1 (s1): ring_offsets high (unused, set to 0)
  - USER_DATA_2 (s2): buffer pointer low 32 bits (pc.buf low)
  - USER_DATA_3 (s3): buffer pointer high 32 bits (pc.buf high)
  - USER_DATA_4 (s4): total_pixels (pc.total_pixels)
  - USER_DATA_5 (s5): fill color (pc.color, RGBA8 packed)
  - s6: workgroup_id_x (system value, TGID_X_EN=1 in RSRC2)
- **RSRC2 register encoding**: RSRC2=0x0000008c decodes to USER_SGPR=6 (bits [5:1]=6, s0..s5) and TGID_X_EN=1 (bit 7=1, s6), perfectly matching the 6 user SGPRs + workgroup_id layout.

**Root cause of compute shader write failure (RESOLVED):**
- In `samples/hw_test/agc_compute.c`, user data registers were incorrectly offset by 1 dword (`buf_addr_lo` was placed in s1 instead of s2, `buf_addr_hi` in s2 instead of s3, etc.).
- When the shader attempted to read `pc.buf` from s2..s3, it read `(total_pixels << 32) | buf_addr_hi`, creating an invalid GPU virtual address and failing the `@store_global_amd` write.
- Fixed `agc_compute.c` user data assignment to map `s0=0, s1=0, s2=buf_addr_lo, s3=buf_addr_hi, s4=total_pixels, s5=color`.


**Reference implementations:**
- `freegnm` (`/Users/bizkut/Downloads/PS5/homebrew/ps4-freegnm/opengnm/`):
  PS4 GNM library. Compute dispatch uses SET_SH_REG with
  `PKT3_SHADER_TYPE_S(1)`, sets COMPUTE_PGM_LO/HI, RSRC1/2, NUM_THREAD,
  USER_DATA, then DISPATCH_DIRECT. Does NOT use RSRC3 (PS4 specific).
- `KytyPS5` (`/Users/bizkut/Downloads/PS5/homebrew/KytyPS5/`): PS5 emulator.
  Confirms PGM_LO address format and COMPUTE_RESOURCE_LIMITS at 0x215.
- `freegnm-examples/triangle/`: Full graphics draw call example with
  SET_CONTEXT_REG for render target, viewport, blend state, etc.

NID table status: 354/366 FW 5.50 SPRX exports identified (96.7%), 322
algorithm-verified. 12+32 unknown NIDs blocked on new external data sources.
All function names in source code match NID-verified correct names.

All userspace packet builders, patchers, LOD stats helpers, the FW 5.50
ioctl/submit/queue RE layer, the shader record parser, the register-defaults
blob builder/parser, the native prospero backend (`driver_prospero.c`), and
the openagc-psbc shader compiler (Mesa NIR + ACO → AgcShaderRecord) are
implemented and build. Hardware validation confirmed that init, memory
allocation, default states, NOP submit, async graphics setup, queue
create/destroy, suspend point submit, workload tracking, and **compute
dispatch DCB submission** all work correctly with the GPU credential
bypass (cr_sceAuthId = 0x4801000000000000). The remaining failures are the
PA debug ioctl (EPERM) and the compute shader not writing to the display
buffer (under active investigation).

## Non-Goals

- No firmware blobs or proprietary microcode embedded in the repo.
- No claim that compute shader execution works yet — the DCB submission
  path is validated, but the shader does not write to the display buffer.
  See "Compute shader debugging status" above.
- No claim that graphics draw calls work yet (blocked on compute shader
  validation — see PLAN.md Phase 7).
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

- Do not include AI assistant branding or automated co-author tags (e.g. Devin) in git commit messages.
