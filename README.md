# OpenAGC

OpenAGC is a clean-room, open-source implementation of the PlayStation 5 AGC
(Advanced Graphics Controller) userspace interface. It provides recovered
`sceAgc*` and `sceAgcDriver*` ABI compatibility, typed gfx1013 graphics and
compute helpers, and a direct PS5 `/dev/gc` backend without proprietary SDK
headers.

The project targets PS5's Oberon GPU (`gfx1013`, a custom RDNA2/GFX10 part).
It is written in C, licensed under Apache 2.0, and keeps the generic host
validation harness dependent only on libc. The generic build validates the PS5
contract using host memory; it is not a non-PS5 GPU backend.

> [!IMPORTANT]
> OpenAGC is an experimental homebrew GPU stack, not a complete official-SDK
> replacement. Support claims are capability- and exact-firmware-specific.
> Consult [STATUS.md](STATUS.md) before relying on a path on hardware.

## At a Glance

| Area | Current state |
| --- | --- |
| Low-level AGC compatibility | Implemented for the mapped `sceAgc*`, driver, cursor, packet, shader, queue, and submit surfaces |
| Host validation | Generic host-memory harness for native-runtime and low-level regression tests across six CTest suites; it is not a GPU backend |
| PS5 backend | Native `/dev/gc` implementation built with ps5-payload-sdk |
| Hardware evidence | Qualified subsets on standard PS5 FW 5.50 and FW 11.60 |
| Firmware profiles | 39 exact active ABI keys from FW 3.20 through FW 12.70; untested profiles remain hardware-unverified |
| Shader compiler | Packaged `openagc-psbc` workflow for SPIR-V → `AgcShaderRecord` |
| Application example | Installed-package rotating cube in `examples/cube` |
| License | Apache 2.0 |

## What OpenAGC Provides

### Driver and firmware foundation

- Native `/dev/gc` initialization, internal-memory management, queues,
  submission, suspend points, default-state construction, and clean shutdown.
- Exact runtime firmware selection. Unknown keys and unsupported optional
  operations fail before mutating GPU or process state.
- Firmware-keyed register defaults, memory-region configuration, submission
  carriers, queue behavior, register-shadow policy, and VideoOut handling.
- Recovered ABI structures protected by `_Static_assert` size and offset
  checks.
- NID and firmware-variant indexes for reverse-engineering and compatibility
  work.

### Commands, shaders, and graphics

- Type-3 PM4 header helpers and cursor-based ACB/DCB command construction.
- Typed draw, indexed draw, indirect draw, dispatch, copy, transition, event,
  fence, query, blend, raster, depth/stencil, multisample, and presentation
  helpers.
- gfx1013 buffer and image descriptors for raw, structured, 2D, array, cube,
  and multisample resources.
- `AgcShaderRecord` parsing, relocation, fused shader records, Wave32/Wave64
  selection, NGG geometry, and tessellation state.
- Application-neutral gfx1013 capability queries and exact command-buffer
  capacity validation.
- Atomic failure behavior: invalid inputs and short command buffers are
  rejected without partial packet emission.

### Memory, synchronization, and presentation

- GPU-visible flexible-memory allocation with explicit flush, invalidate, and
  release operations.
- Direct write-combined memory for scanout-compatible resources.
- Typed resource transitions covering render, depth/stencil, compute, copy,
  shader-read, host-read, and presentation use, including exact byte- and
  image-subresource-range graphics/compute ownership handoffs.
- Bounded label/fence waits; hardware samples never depend on an unbounded
  polling loop.
- Reusable VideoOut buffer registration, FIFO/VSYNC presentation, event
  handling, patch restoration, and teardown.

### Tooling and distribution

- `generic` and `prospero` CMake targets.
- Make-based host build and test workflow.
- Relocatable `OpenAGC::openagc` CMake package.
- Optional packaged `OpenAGC::psbc` executable and
  `openagc_compile_shader()` helper.
- Deterministic host fixtures, guarded hardware runners, pinned ELF hashes,
  packet decoders, firmware-corpus verifiers, and qualification reports.

## API Layers

OpenAGC deliberately separates compatibility from application ergonomics.

### Available today: low-level compatibility and typed gfx1013 helpers

The current public headers expose:

- `agcdriver.h` for recovered driver and initialization entry points.
- `agc_cb.h`, `agc_pm4.h`, and `agc_context.h` for command and context
  construction.
- `agc_graphics.h` for typed gfx1013 state, draw, dispatch, copy, transition,
  depth/stencil, MSAA, query, and synchronization helpers.
- `agc_memory.h` and `agc_videoout.h` for reusable memory and presentation
  lifecycles.
- `agc_shader.h` and `agc_texture.h` for shader records and resource
  descriptors.
- `agc_capabilities.h` and `agc_runtime_diag.h` for capability and runtime
  diagnostics.

These APIs are used by the hardware samples, the standalone cube, and the
current Vulkan-PS5 implementation.

### Available today: native application runtime contract

`openagc/runtime.h` provides a C99 API that is firmware-neutral across
supported PS5 firmware and PS5 hardware variants. It is not a portable GPU API;
OpenAGC targets only the PS5 GPU. The API uses opaque:

- `AgcDevice` and `AgcQueue`
- `AgcBuffer`, `AgcImage`, `AgcImageView`, and `AgcSampler`
- `AgcShader`, `AgcGraphicsPipeline`, and `AgcComputePipeline`
- `AgcCommandBuffer` and `AgcFence`

The implemented native contract includes versioned descriptors, reserved-zero
validation, optional allocation callbacks, explicit parent/child ownership,
command-buffer state validation, finite binary-fence waits, capability and
qualification reporting through `agcGetRuntimeInfo`, and an optional API-v23
validation callback with deterministic pointer-free messages. Version 2 also
consumes the shared
`AgcShaderReflection` emitted by `openagc-psbc`, verifies serialized shader
records and hashes, and creates fail-closed graphics and compute pipelines.
Applications query capabilities rather than branching on firmware.

Heap suballocation and fence-keyed deferred retirement are implemented.
Reflected Wave32 graphics/compute pipelines cache qualified gfx1013 bind and
dispatch groups that the generic host harness validates. Typed descriptor arrays, reflected
vertex tables, push constants, and declared viewport/scissor/blend/stencil-
reference/depth-bias dynamic state are recorded through the command buffer;
draw and dispatch fail until every reflected requirement is bound. Versioned
depth/stencil state covers depth bounds and independent front/back stencil
operations/masks; unqualified alpha-to-coverage and alpha-to-one fail closed.
Unsupported graphics forms and the remaining resource/shader/pipeline capture
records remain ordered follow-on work; capture framing, transitions, and
bounded presentation are implemented. The native Prospero
queue bridge is intentionally
limited to the qualified direct carriers and a runtime completion fence; it
does not make the host harness a hardware substitute. See
[docs/native_runtime.md](docs/native_runtime.md) for lifecycle rules,
[docs/validation.md](docs/validation.md) for optional diagnostics,
[docs/capture.md](docs/capture.md) for the versioned diagnostic stream and
host decoder,
[docs/shader_pipelines.md](docs/shader_pipelines.md) for the reflection and
pipeline contract, and [PLAN.md](PLAN.md) for the remaining dependency order.

### Sibling projects

- [openagc-psbc](../openagc-psbc/README.md) compiles SPIR-V and produces the
  shader records and reflection consumed by OpenAGC pipelines.
- [Vulkan-PS5](../Vulkan-PS5/README.md) is the Vulkan ICD. Its current direct
  integration is the qualified baseline; it will migrate to the native runtime
  after those contracts stabilize.

Neither project should grow a second firmware selector, memory manager, PM4
backend, resource-state model, or queue/fence implementation.

## Qualification and Firmware Support

OpenAGC tracks three evidence levels independently:

1. **Host-tested** — deterministic generic-backend fixtures pass.
2. **SPRX/profile-qualified, hardware-unverified** — the exact firmware ABI is
   recovered and selected, but matching hardware has not run it.
3. **Hardware-qualified on an exact firmware** — a bounded, deterministic
   payload passed the documented hardware gate.

An export, profile table entry, or successful ioctl return is not by itself a
hardware-support claim.

### Current hardware baseline

FW 5.50 and standard-PS5 FW 11.60 have qualified subsets covering:

- Driver initialization, defaults, async setup, queues, submission, cleanup,
  and relaunch.
- Wave32 compute and graphics with exact GPU readback.
- Indexed, indirect, and indexed-indirect drawing.
- Buffer copies and typed resource transitions.
- NGG geometry and tessellation.
- Regular color targets through the 128-bit `RGBA32` ceiling.
- R/RG/RGBA16 UNORM, SNORM, UINT, and SINT.
- R/RG/RGBA32 UINT and SINT.
- All 14 declared BC1-BC7 direct-upload sampling encodings.
- D16, D32, S8, combined depth/stencil, HTILE, expclear, and qualified
  subresource cases.
- The planned 4x MSAA resolve and sample-rate-shading matrix.

The exact scope, exclusions, hashes, and per-firmware results are recorded in
[STATUS.md](STATUS.md) and
[analysis/format_depth_msaa_goal_completion_20260730.md](analysis/format_depth_msaa_goal_completion_20260730.md).

### Portability gate

Production code contains no compile-time expected-firmware selector. The
pinned firmware-neutral portability ELF passed twice as identical bytes on
both FW 5.50 and FW 11.60 after their cleanup-stress prerequisites. Its exact
hash remains the regression gate recorded in [STATUS.md](STATUS.md).

The FW 11.60 workload operation remains fail-closed: all known packet forms
returned from submission but stalled before ordered GPU markers. Closed failed
probes must not be rerun without new offline evidence.

### Deliberate boundaries

- FW 3.20 is the lowest active compatibility target.
- FW 1.x, 2.x, and 3.00 profiles are archival reverse-engineering data only.
- The other active exact profiles are not hardware-qualified merely because
  their SPRX contracts are registered.
- Tiled BC layout/copy/mips, additional packed formats, color metadata
  (DCC/CMASK/FMASK), HDR presentation, and remaining depth/MSAA combinations
  are demand-driven future work.
- ASTC and other unproven format families are not advertised.

## Build and Test

### Generic host validation harness

Requirements: a C compiler, CMake 3.15 or newer, and libc.

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DOPENAGC_PLATFORM=generic \
  -DOPENAGC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected result:

```text
14384 passed, 0 failed
```

The Make workflow is equivalent:

```sh
make
make test
# Force a complete test rebuild:
make -B test
```

### Prospero backend

The PS5 build uses
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk). Set the installed
SDK path explicitly:

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk

cmake -S . -B build-prospero \
  -DOPENAGC_PLATFORM=prospero \
  -DOPENAGC_BUILD_TESTS=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
cmake --build build-prospero
```

Output:

```text
build-prospero/libopenagc.a
```

The exported OpenAGC target links `kernel` and `SceVideoOut`. It does not
link or preload `libSceAgcDriver.sprx`; the Prospero backend owns direct
`/dev/gc` submission.

## Install the SDK

Install headers, `libopenagc.a`, CMake metadata, documentation, and optionally
the host shader compiler:

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk

cmake -S . -B build-sdk \
  -DOPENAGC_PLATFORM=prospero \
  -DOPENAGC_BUILD_TESTS=OFF \
  -DOPENAGC_BUILD_PSBC=ON \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
  -DCMAKE_INSTALL_PREFIX="$PWD/openagc-sdk"
cmake --build build-sdk
cmake --install build-sdk
```

`OPENAGC_BUILD_PSBC=ON` builds the sibling source tree selected by
`OPENAGC_PSBC_SOURCE_DIR` (default: `../openagc-psbc`). Alternatively,
provide an existing host executable:

```sh
cmake -S . -B build-sdk \
  -DOPENAGC_PLATFORM=prospero \
  -DOPENAGC_PACKAGE_PSBC=ON \
  -DOPENAGC_PSBC_EXECUTABLE=/absolute/path/to/psbc \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
```

Create a relocatable TGZ from the same configuration:

```sh
cmake --build build-sdk --target package
```

## Consume OpenAGC from CMake

```cmake
find_package(OpenAGC 0.2 CONFIG REQUIRED)

add_executable(my_homebrew main.c)
target_link_libraries(my_homebrew PRIVATE OpenAGC::openagc)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix, or pass:

```sh
-DOpenAGC_DIR=/path/to/openagc-sdk/lib/cmake/OpenAGC
```

When the package includes the shader compiler, it exports
`OpenAGC::psbc`, `OpenAGC_PSBC_EXECUTABLE`, and
`openagc_compile_shader()`:

```cmake
openagc_compile_shader(
  OUTPUT shaders/fill.sb
  SOURCE shaders/fill.spv
  STAGE compute
  RESULT FILL_SHADER
)

add_custom_target(my_homebrew_shaders DEPENDS "${FILL_SHADER}")
add_dependencies(my_homebrew my_homebrew_shaders)
```

The compiler is a host tool even when `libopenagc.a` targets Prospero.

## Shader Workflow

The supported shader path is:

```text
GLSL or another SPIR-V source language
   ↓
SPIR-V
   ↓ openagc-psbc (Mesa NIR + ACO)
AgcShaderRecord + gfx1013 executable + reflection
   ↓ OpenAGC shader/pipeline binding
PM4 command stream
```

The current low-level path consumes `AgcShaderRecord` data directly. The
planned native pipeline API will use the compiler's versioned reflection to
reject descriptor, push-constant, user-SGPR, color-export, attachment,
blend/sample, wave, tessellation, geometry/NGG, and stage-linkage mismatches
before command emission.

See [openagc-psbc/README.md](../openagc-psbc/README.md) for compiler-library and
CLI options.

## Examples and Hardware Validation

### Standalone rotating cube

`examples/cube` is the application-facing example. It consumes an installed
OpenAGC package, records public gfx1013 commands, presents a triple-buffered
rotating cube, bounds every GPU wait, and tears down after 3,600 frames.

See [examples/cube/README.md](examples/cube/README.md) for build, shader
regeneration, and websrv deployment.

### Focused hardware samples

`samples/hw_test` contains guarded payloads for VideoOut, initialization,
compute, graphics, formats, depth/stencil, HTILE, MSAA, queues, portability,
teardown, and firmware-specific diagnostics. These programs are qualification
fixtures, not templates for ordinary application structure.

Build the full sample set with:

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
export LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config
make -C samples/hw_test all
```

Use guarded deployment targets and etaHEN websrv. Do not use
`prospero-deploy`; its direct-loader context does not reliably foreground
VideoOut for these graphics tests.

The ordered FW 5.50 matrix is:

```sh
make -C samples/hw_test conformance_fw550_check
make -C samples/hw_test conformance_fw550 PS5_HOST="<console-ip>"
```

Hardware runners verify firmware identity, pinned hashes, cleanup-first launch,
bounded completion, sample-specific readback, teardown, residual processes, and
kernel faults. They stop at the first failure.

## Repository Layout

```text
include/          Public compatibility and typed gfx1013 headers
src/              Generic and Prospero implementations
tests/            Host ABI, packet, runtime, layout, and runner fixtures
examples/cube/    Installed-package application example
samples/hw_test/  Guarded PS5 hardware qualification payloads
shaders/          Reusable clean-room shader sources and records
tools/            Firmware, packet, capture, ELF, and corpus verification tools
analysis/         Reverse-engineering evidence and hardware qualification logs
cmake/            Relocatable package configuration
```

## Documentation

- [PLAN.md](PLAN.md) — authoritative product roadmap and detailed evidence
  ledger.
- [STATUS.md](STATUS.md) — current qualification status and chronological
  hardware results.
- [AGENTS.md](AGENTS.md) — build, coding, ABI, safety, and hardware-validation
  rules.
- [REFERENCE_FINDINGS.md](REFERENCE_FINDINGS.md) — consolidated recovered AGC
  behavior.
- [analysis/](analysis/) — source-specific RE notes, audits, artifact hashes,
  and bounded hardware reports.

## Scope and Non-Goals

- No firmware SPRX modules, proprietary microcode, or SDK headers are embedded
  or redistributed.
- No PS4 `sceGnm*` implementation; that belongs in the sibling `opengnm`
  project.
- No assumption that NIDs or private ABIs are stable across firmware.
- No success stubs for unsupported operations.
- No claim of full official SDK drop-in completeness or Vulkan conformance.
- No support claim beyond exact host, SPRX/profile, and hardware evidence.

## Reference Policy

OpenAGC is a clean rewrite informed by independently inspected PS5 firmware
interfaces, AMD/Mesa/Linux gfx10.3 information, and emulator or open-source GPU
projects used for cross-checking. Reference code is not copied verbatim.

The incomplete `ps5-openagc` project is used only for NID cross-reference; its
ioctl layouts and memory facts are not trusted without independent firmware
verification. See
[analysis/ps5_openagc_audit.md](analysis/ps5_openagc_audit.md).

Firmware binaries and microcode remain external reverse-engineering inputs and
must never be committed to this repository.

## License

Apache License 2.0. See [LICENSE](LICENSE).
