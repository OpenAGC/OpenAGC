# openagc

## Reusable FW 5.50 Wave32 graphics

Include `agc_graphics.h` to use the hardware-validated gfx1013 Wave32 VS+PS
path. `agcGfx1013ValidateWave32VsPs` validates a fused NGG Gs(2) record and
Wave32 pixel record; `agcGfx1013BindWave32VsPs` derives primitive/interpolant
state, patches shader addresses, preflights command-buffer capacity, and emits
the reusable SH/CX/UC binding sequence. The path is validated on standard PS5
FW 5.50 through curl and etaHEN websrv.


openagc is a clean-rewrite PS5 AGC (Advanced Graphics Controller) library
for homebrew and emulator-port work. It follows the recovered `sceAgc*` /
`sceAgcDriver*` naming and command-buffer split used by the PS5 firmware, while
keeping the implementation buildable without proprietary SDK headers.

The current tree is a foundation layer, not a complete official-SDK drop-in.
Host builds provide testable AGC packet, command-buffer, submit-descriptor,
descriptor, and shader helpers. The PS5 `/dev/gc` backend is implemented and
builds with ps5-payload-sdk; hardware validation is the remaining step.

## Features

- **AGC driver API surface** — `sceAgcDriver*` submission, SDMA, setup
- **ACB command building** — `sceAgcAcb*` async compute buffer packets
  (event write, atomic mem/GDS, cond exec, wait-reg-mem, write/copy/dma data,
  mem semaphore, acquire mem, queue reset, rewind, set flip, workload
  markers, prime UTC L2)
- **DCB command building** — `sceAgcDcb*` draw command buffer packets
  (clear state, atomic GDS, context state ops, reset queue, set flip, workload
  markers, wait-safe, preemption stub)
- **Context state management** — default state queries, primary/internal
  register-defaults blob builder, and `CLEAR_STATE` submission path
- **Gen5 AGC/PM4 packet encoding** — PS5 packet header helpers and decoded
  opcode/subcommand constants (including kernel-side suspend-point marker)
- **RE packet model** — HLE reference/RPCSX-compatible type-3 packet length and
  AGC `IT_NOP` subcommand helpers
- **Command-buffer cursor model** — recovered `SceAgcCb` cursor offsets and
  host allocation helpers
- **Sony-style CB builders** — recovered `sceAgcCb*` / `sceAgcDcb*` packet
  builders for NOP, dispatch, SH registers, write-data, wait-reg-mem, DMA,
  indirect dispatch/base args, index buffers, indexed draws, markers, wait-safe,
  and flip
- **Submit packet model** — recovered DCB/ACB submit descriptor layout with
  generic validation/debug capture
- **Suspend points** — RE'd `SUSPEND_16`/`SUSPEND_39` ioctl argument layout and
  in-flight query placeholder
- **Known NID index** — local Gen5 AGC NID constants for mapped exports
- **Texture/buffer descriptors** — RDNA2 SQ_IMG_RSRC / SQ_BUF_RSRC
- **Application-neutral capabilities** — `agcGfx1013GetCapabilities` reports
  qualified gfx1013 dimensions, Wave32/compute limits, render/depth formats,
  sample counts, and flexible/direct memory profiles without Vulkan coupling
- **GPU-visible flexible memory** — `agcGpuMemoryAllocateFlexible` provides a
  unified CPU/GPU address on Prospero and an aligned host analogue for tests;
  matching flush, invalidate, and deterministic free operations keep kernel
  mapping details outside applications and higher-level drivers; bounded
  32-bit label waits support EOP completion without platform polling code
- **Compute resource tables** — `AgcGfx1013ComputeState` accepts compiler
  descriptor-set placeholder bindings; dispatch validation rejects unbound or
  malformed tables and patches the exact compute user-SGPR registers after
  shader state emission and before `DISPATCH_DIRECT`
- **Write-combined direct memory** —
  `agcGpuMemoryAllocateDirectWriteCombined` owns the 2 MiB-aligned kernel
  physical allocation, CPU/GPU mapping, and paired unmap/release lifecycle used
  for garlic resources and scanout-compatible storage
- **Shader binary format** — RDNA2 ISA shader header parsing
- **Two backend targets:**
  - `generic` — pure software implementation for host testing
  - `prospero` — native PS5 `/dev/gc` backend with ioctl submission and internal
    memory allocation
- **Binary-compatible struct layouts** — `_Static_assert` verified sizes
- **Hardware validation samples** — `samples/hw_test/` builds ELF + fake-SELF
  packages for VideoOut and AGC init smoke tests
- **Standalone homebrew example** — `examples/cube/` consumes an installed
  OpenAGC package and owns a FW 5.50 hardware-validated triple-buffered
  rotating-cube render loop

## Architecture Differences from opengnm (PS4)

| Aspect | PS4 GNM (opengnm) | PS5 AGC (openagc) |
|--------|-------------------|-------------------|
| GPU | AMD GCN (Liverpool/Gladius) | AMD RDNA2 (Oberon) |
| API prefix | `sceGnm*` | `sceAgc*` |
| Command buffers | PM4 packets (DCB/CCB) | RDNA2 PM4 (DCB/ACB) |
| Shader ISA | GCN (Southern Islands) | gfx1013 (Oberon, custom GFX10) |
| Wave size | 64 only | 32 or 64 |
| Shader stages | VS/PS/GS/HS/DS/CS | VS/PS/GS/HS/DS/CS + Mesh/Task |
| Context state | PM4 SET_*_REG inline | Flat context state block |
| Preemption | Limited | Full (suspend points) |
| Tiling | GCN macro/micro tile | RDNA2 swizzle modes |
| Memory | Garlic/Onion (GDDR5) | Unified (GDDR6) |
| GPU microcode | Tonga CE/ME/PFP/MEC | Oberon CE/ME/PFP/MEC/SDMA |

## Status

**Phase 1: Reverse-engineering foundation** — In progress.

Completed and tested:
- Core type definitions and error codes
- Driver API header for the currently mapped firmware-exported functions
- Reverse-engineering headers for packet layout, NIDs, shader offsets, and
  command-buffer offsets
- Cursor-based command-buffer allocation using recovered `SceAgcCb` offsets
- HLE-reference-confirmed `sceAgcCb*` and `sceAgcDcb*` packet builders for NOP,
  dispatch, SH registers, release memory, indirect register sets, write-data,
  wait-reg-mem, DMA, indirect dispatch/base args, index buffer setup, indexed
  draw packets, markers, wait-safe, flip, and LOD stats helpers
- ACB packet builders for event write, atomic mem/GDS, cond exec, wait-reg-mem,
  write/copy/dma data, mem semaphore, acquire mem, queue reset, rewind, set
  flip, workload markers, and prime UTC L2
- DCB/VSH packet builders for clear state, atomic GDS, context state ops, reset
  queue, set flip, workload markers, wait-safe, and preemption (SPRX-confirmed
  unimplemented VSH-only stub)
- In-place patchers for DMA-data destination, wait-reg-mem address, and
  end-of-pipe action addresses
- Generic `sceAgcDriverSubmitDcb` and `sceAgcDriverSubmitAcb` validation for
  recovered submit descriptors
- Generic backend with host-side validation/debug capture
- ACB and DCB raw pointer helpers with basic packet emission
- Texture/buffer descriptor helpers
- AGC shader record parser (magic, pointer fields, semantics counts, shader type)
- FW 5.50 register-defaults blob builder and parser with embedded primary/internal tables
- Native prospero `/dev/gc` backend with ioctl submission, internal memory allocation,
  default-state `CLEAR_STATE` submission, and suspend-point submit/query
- Hardware validation samples (`samples/hw_test/`) built as ELF and fake-SELF
- Build system (CMake + Makefile)
- Test suite with 4080 passing assertions on the host generic backend

Hardware-validated on real PS5 gfx1013 hardware running FW 5.50:
- Native `/dev/gc` initialization, queue setup, default states, and submission
- Compute dispatch and no-GS NGG indexed graphics with GPU readback
- Wave32 NGG and pixel execution, including compiler-record and final-PM4 checks
- Linear standard/alternate-swap RGBA8 UNORM/SRGB, RGB10A2,
  `R11G11B10_FLOAT`, and `R16G16B16A16_FLOAT` render targets

Firmware policy: FW 3.20 is the lowest active compatibility target. FW 1.00
and 2.x aliases are retained only as archival RE data and are not advertised
as supported; missing legacy-only operations remain fail-closed.

### Deploy the Wave32 graphics test with etaHEN websrv

Use this websrv path for OpenAGC hardware validation. Do not use
`prospero-deploy`; its direct-loader context did not foreground the VideoOut
surface reliably during Wave32 validation.

Build the Prospero library and hardware sample first, then upload the ELF and
icon over websrv FTP. `--ftp-create-dirs` makes the homebrew directory on the
first upload:

```sh
export PS5_HOST=10.0.1.41
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config

cmake -B build-prospero -DOPENAGC_PLATFORM=prospero \
    -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
cmake --build build-prospero
make -C samples/hw_test agc_graphics.elf

curl -sS --ftp-create-dirs \
    -T samples/hw_test/agc_graphics.elf \
    "ftp://$PS5_HOST:2121/data/homebrew/agc_wave32/eboot.elf"
curl -sS --ftp-create-dirs \
    -T samples/hw_test/sce_sys/icon0.png \
    "ftp://$PS5_HOST:2121/data/homebrew/agc_wave32/sce_sys/icon0.png"
curl -sS \
    "http://$PS5_HOST:8080/hbldr?pipe=1&daemon=0&path=/data/homebrew/agc_wave32/eboot.elf"
```

For the complete ordered FW 5.50 matrix, use the deterministic runner instead
of launching samples manually:

```sh
make -C samples/hw_test conformance_fw550_check
make -C samples/hw_test conformance_fw550 PS5_HOST="$PS5_HOST"
```

`conformance_fw550_check` validates that every local ELF in the matrix exists.
The hardware target uploads each ELF to a fresh per-run directory, launches it
in the foreground, enforces a bounded timeout, verifies the numeric `0x0550`
firmware profile and sample-specific output gates, and stops at the first
failure so loader processes cannot overlap. Raw logs are retained under
`samples/hw_test/conformance-logs/<run-id>/`. A timeout, disconnected curl,
instant close, missing gate, `FAIL`, or `FATAL` marker fails the run.

`pipe=1` streams the validation log to curl and `daemon=0` keeps the launch in
the foreground. The expected display is a dark-gray background with a centered,
blended-color triangle. The payload must report Wave32 record and PM4 passes,
the `0xDEADCAFE` marker, FP16 validation, and 1,800 completed flips.

### Firmware Reference

Reference inputs used for the open implementation:
- Local firmware dump: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50`
- AGC modules: `libSceAgc.sprx`, `libSceAgcDriver.sprx`, `libSceAgcVsh.sprx`
- GPU microcode names: `oberon_c0_{ce,me,mec,pfp,rlc,sdma0,sdma1}.bin`
- Incomplete reference project: `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc`
  — **NOT proven working.** Used for NID cross-reference only; contains known
  ioctl errors. See `analysis/ps5_openagc_audit.md`.
- Emulator reference: `/Users/bizkut/Downloads/PS5/homebrew/hle reference`
- GPU/PM4 reference: `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`

Firmware modules and microcode are used only as reverse-engineering references;
they are not embedded in this repository.

## Building

### Host (generic, for testing)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOPENAGC_PLATFORM=generic
cmake --build build
ctest --test-dir build
```

Or with Make:

```sh
make
make test
```

Current expected host result:

```text
4080 passed, 0 failed
```

### PS5

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
cmake -B build-prospero -DOPENAGC_PLATFORM=prospero -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-prospero
# Output: build-prospero/libopenagc.a
```

The `prospero` backend builds as a native PS5 `/dev/gc` driver backend. It
implements ioctl submission, internal memory allocation, default-state
submission, and suspend-point submission. It requires hardware validation
before it can be considered production-ready.

## Installable SDK and CMake package

Packaging is validated for both the generic host build and the Prospero
cross-toolchain. Each install contains the public headers, `libopenagc.a`, the
host-native `openagc-psbc` executable, relocatable CMake package metadata,
license, and documentation. The `package` target generates a versioned TGZ for
the selected platform.

When consuming a Prospero install outside the toolchain sysroot, pass the
package directory explicitly because the PS5 toolchain intentionally root-paths
package searches:

```sh
cmake -S app -B build-app \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake \
    -DOpenAGC_DIR=/path/to/openagc/lib/cmake/OpenAGC
```

OpenAGC installs the public headers, `libopenagc.a`, relocatable CMake package
metadata, documentation, and the host `openagc-psbc` shader compiler through
one workflow. When the sibling `../openagc-psbc/psbc` exists, compiler
packaging is enabled automatically:

```sh
cmake -S . -B build-sdk \
    -DOPENAGC_PLATFORM=prospero \
    -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_INSTALL_PREFIX="$PWD/openagc-sdk"
cmake --build build-sdk
cmake --install build-sdk
```

For a fresh compiler checkout without a built `psbc`, add
`-DOPENAGC_BUILD_PSBC=ON`. To supply another host build, set
`-DOPENAGC_PSBC_EXECUTABLE=/absolute/path/to/psbc`. The shader compiler is a
host program even when `libopenagc.a` targets Prospero; it is never compiled by
the PS5 cross-toolchain. `cmake --build build-sdk --target package` produces a
TGZ archive from the same install rules.

Downstream homebrew CMake usage:

```cmake
find_package(OpenAGC 0.1 CONFIG REQUIRED)

target_link_libraries(my_homebrew PRIVATE OpenAGC::openagc)

openagc_compile_shader(
    OUTPUT shaders/fill.sb
    SOURCE shaders/fill.spv
    STAGE compute
    RESULT FILL_SHADER)
add_custom_target(my_homebrew_shaders DEPENDS "${FILL_SHADER}")
add_dependencies(my_homebrew my_homebrew_shaders)
```

The installed package exports `OpenAGC::openagc`, `OpenAGC::psbc`,
`OpenAGC_PSBC_EXECUTABLE`, and `openagc_compile_shader()`. Set
`CMAKE_PREFIX_PATH` to the install prefix, or pass
`-DOpenAGC_DIR=<prefix>/lib/cmake/OpenAGC`.

The complete independent Prospero consumer is in `examples/cube`. Its README
documents staged installation, shader regeneration, and foreground curl/websrv
deployment. Unlike `samples/hw_test`, it contains an application-owned finite
frame loop with per-frame resource updates and cleanup. Two FW `0x05500008`
runs presented all 3,600 frames and exited cleanly; see
`analysis/fw550_standalone_cube_qualification_20260727.md`.

## Project Structure

```
openagc/
├── include/
│   ├── agc_types.h          # Core type definitions
│   ├── agc_error.h          # Error codes
│   ├── agc_cb.h             # SceAgcCb cursor helpers
│   ├── agc_driver_debug.h   # Generic backend submit debug accessors
│   ├── agc_pm4.h            # RDNA2 PM4 packet definitions
│   ├── agc_nids.h           # Known Gen5 AGC NIDs
│   ├── agc_re.h             # RE constants and offsets
│   ├── agc_texture.h        # Texture/buffer descriptor types
│   ├── agc_shader.h         # Shader binary format
│   └── agcdriver.h          # Main AGC driver API (sceAgc* functions)
├── analysis/
│   ├── agc_packet_model.md  # Decoded packet header/subcommand model
│   └── agc_known_nids.tsv   # Known AGC NID table
├── src/
│   ├── driver_generic.c     # Generic (host) backend
│   ├── driver_prospero.c       # PS5 hardware backend
│   ├── cb.c                 # SceAgcCb cursor allocation
│   ├── cb_builders.c        # sceAgcCb*/sceAgcDcb* packet builders
│   ├── context_state.c      # Context state management
│   ├── acb.c                # ACB command building
│   ├── dcb.c                # DCB command building
│   ├── texture.c            # Texture descriptor helpers
│   └── shader.c             # Shader binary utilities
├── tests/
│   ├── test.h               # Test framework
│   ├── test_main.c          # Test runner
│   ├── test_types.c         # Type/constant tests
│   ├── test_acb.c           # ACB command tests
│   ├── test_cb.c            # Cursor command-buffer tests
│   ├── test_dcb.c           # DCB command tests
│   ├── test_driver.c        # Submit descriptor tests
│   ├── test_texture.c       # Texture descriptor tests
│   └── test_shader.c        # Shader binary tests
├── samples/
│   └── hw_test/             # PS5 hardware validation payloads
│       ├── videoout_linear.c # VideoOut display pipeline smoke test
│       ├── agc_init.c       # AGC init + NOP submit test
│       └── Makefile         # ELF + fake-SELF build targets
├── CMakeLists.txt           # CMake build
├── Makefile                 # Make build
├── PLAN.md                  # Architecture and reverse-engineering roadmap
├── STATUS.md                # Current milestone and roadmap
├── REFERENCE_FINDINGS.md    # Detailed RE notes and conclusions
├── AGENTS.md                # Agent guidance and conventions
├── LICENSE                  # Apache 2.0 license
└── README.md                # This file
```

## Sources

- Clean rewrite, based on local PS5 firmware ABI analysis
- PS5 firmware 5.50 SPRX module references
- AMD RDNA2 public documentation and Mesa packet/register references
- Existing `ps5-openagc` notes for NID maps (ioctl tables and PM4 cataloging
  from ps5-openagc are NOT trusted — contains known errors; see
  `analysis/ps5_openagc_audit.md`)
- `hle reference` as a PS5 HLE/runtime behavior reference
- `rpcsx` as a GPU/PM4/GNM queue, packet, tiler, and shader reference

## License

Apache 2.0, see [LICENSE](LICENSE).
