# OpenAGC

OpenAGC is a clean-room C99 library that provides a native application layer
and low-level ABI-compatible helpers for the PlayStation 5 Advanced Graphics
Controller (AGC). It targets the PS5 gfx1013 GPU and is designed for native
homebrew and other projects that need explicit control over resources,
pipelines, command buffers, queues, synchronization, and presentation.

The project has two backends:

- `generic` is a libc-only host backend for API and packet-construction work.
- `prospero` is the PS5 backend using the ps5-payload-sdk and VideoOut.

Hardware behavior is firmware- and profile-specific. A successful host build
does not imply that an operation is qualified on a console.

## Public API

The preferred application-facing API is declared in
[`include/openagc/runtime.h`](include/openagc/runtime.h). It provides opaque
handles for:

- devices, queues, memory, buffers, images, image views, and samplers;
- shader records and graphics or compute pipelines;
- command buffers, fences, GPU labels, and presentation chains.

The runtime validates descriptors, ownership, resource states, shader
reflection, pipeline compatibility, command-buffer capacity, and finite wait
timeouts before emitting work. Capability and qualification data are available
through `agcGetRuntimeInfo`.

The lower-level headers retain the recovered compatibility and packet-building
surface, including `sceAgc*`, `sceAgcDriver*`, cursor builders, PM4 helpers,
shader records, register defaults, and VideoOut support. Applications that do
not need that level of control should use the native runtime API.

## Build the host library

Requirements are CMake 3.15 or newer and a C99-capable compiler.

```sh
cmake -S . -B build \
  -DOPENAGC_PLATFORM=generic \
  -DOPENAGC_BUILD_TESTS=OFF
cmake --build build
```

The equivalent Make workflow is:

```sh
make
```

The host backend is intended for deterministic API and command-stream
validation. It is not a software GPU and does not emulate PS5 rendering.

## Build for Prospero

Install the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), then
configure with its toolchain:

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk

cmake -S . -B build-prospero \
  -DOPENAGC_PLATFORM=prospero \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
cmake --build build-prospero
```

This produces `build-prospero/libopenagc.a`. The default Prospero build uses
the installed Sony AGC carrier when available. An explicit direct `/dev/gc`
development build can be selected with:

```sh
cmake -S . -B build-direct \
  -DOPENAGC_PLATFORM=prospero \
  -DOPENAGC_PREFER_INSTALLED_AGC_DRIVER=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
```

The exact link requirements depend on the payload SDK and the selected
carrier. Consult the SDK documentation for the target environment.

## CMake consumption

After installation, consumers can use the exported target:

```cmake
find_package(OpenAGC 0.2 CONFIG REQUIRED)

add_executable(my_homebrew main.c)
target_link_libraries(my_homebrew PRIVATE OpenAGC::openagc)
```

The public headers are installed below `include/`, and the static library is
installed through the normal CMake install directories.

## Repository layout

```text
include/     Public compatibility and native-runtime headers
src/         Generic and Prospero implementations
cmake/       Relocatable CMake package configuration
shaders/     Small reusable shader sources
tools/       Firmware, packet, capture, and artifact utilities
```

Hardware qualification programs, host regression tests, development notes,
extended documentation, and examples are maintained separately from this
public library distribution.

## Scope and safety

OpenAGC does not include proprietary firmware modules, microcode, or SDK
headers. Firmware-specific behavior is selected explicitly and unsupported or
unknown profiles fail closed. NIDs and private interfaces are not assumed to
be stable across firmware versions.

This project is not an implementation of the official Sony SDK, does not
claim complete ABI coverage, and is not a Vulkan implementation.

## License

OpenAGC is licensed under the Apache License 2.0. See [`LICENSE`](LICENSE).
