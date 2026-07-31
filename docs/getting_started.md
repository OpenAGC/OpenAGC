# Installing OpenAGC and running the native examples

This guide uses the firmware-neutral native C runtime. Application code links
the exported `OpenAGC::openagc` target, queries capabilities at startup, and
never selects a firmware profile or emits PM4 directly.

## Build and install

For a host development installation:

```sh
cmake -S . -B build \
  -DOPENAGC_PLATFORM=generic \
  -DOPENAGC_BUILD_TESTS=ON \
  -DOPENAGC_PACKAGE_PSBC=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/openagc-install"
```

For a PS5 cross-build, select the Prospero toolchain and install into a staging
prefix. The application and its CMake file remain unchanged:

```sh
cmake -S . -B build-prospero \
  -DOPENAGC_PLATFORM=prospero \
  -DOPENAGC_BUILD_TESTS=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
cmake --build build-prospero --parallel
cmake --install build-prospero --prefix "$PWD/openagc-prospero-install"
```

`OPENAGC_PACKAGE_PSBC=ON` additionally installs the host shader compiler as
`bin/openagc-psbc`. It requires either an existing compiler executable or
`OPENAGC_BUILD_PSBC=ON` with the adjacent compiler source tree.

## Consume the installed package

A native application needs only the exported package target:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyOpenAGCApp LANGUAGES C)

find_package(OpenAGC CONFIG REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE OpenAGC::openagc)
```

Configure with the installation prefix:

```sh
cmake -S app -B app-build \
  -DCMAKE_PREFIX_PATH="$PWD/openagc-install"
cmake --build app-build --parallel
```

The installed tutorials are in
`share/doc/OpenAGC/examples`. They are independently configured consumers, so
their build does not inherit private source-tree include paths or targets:

```sh
cmake -S openagc-install/share/doc/OpenAGC/examples \
  -B examples-build \
  -DCMAKE_PREFIX_PATH="$PWD/openagc-install"
cmake --build examples-build --parallel
```

## First compute submission

[`examples/first_compute.c`](../examples/first_compute.c) demonstrates the
complete compute lifecycle:

1. Deserialize compiler reflection and create a device and compute queue.
2. Create the shader and compute pipeline from code plus reflection.
3. Allocate a readback storage buffer.
4. Record `Undefined -> ShaderWrite`, bind the reflected descriptor and push
   constants, dispatch, then record `ShaderWrite -> HostRead`.
5. Submit with a fence, wait for a finite 200 ms deadline, reset, and destroy
   children in reverse ownership order.

On the generic backend, successful execution proves validation, encoding,
submission, and lifecycle behavior; it does not claim shader execution. A
hardware-qualified Prospero profile executes the same application commands on
the GPU.

## First indexed triangle

[`examples/first_triangle.c`](../examples/first_triangle.c) demonstrates a
graphics resource-to-submit slice:

1. Create reflected NGG vertex and pixel shaders and a two-attachment graphics
   pipeline.
2. Upload vertex and index buffers and allocate two color targets.
3. Transition resources to graphics ownership, bind the pipeline, targets,
   vertex/index data, viewport, and scissor, then call `agcCmdDrawIndexed`.
4. Transition both targets to `HostRead`, submit, wait with a finite deadline,
   reset, and tear down in reverse order.

The two color targets are intentional: the tutorial pixel shader reflection
declares two exports, and pipeline creation rejects an attachment mismatch
before any command is emitted.

## Capability and error policy

Request only capabilities essential to application startup through
`AgcDeviceDesc.required_capability_bits`, then inspect `agcGetRuntimeInfo` for
optional features and qualification. Treat `AGC_ERROR_NOT_SUPPORTED` as a
feature/profile limitation, not as permission to choose raw firmware state.

Every wait must use a finite timeout. On failure, query `agcGetFenceInfo` (or
`agcGetGpuLabelInfo`) and preserve the selected profile, submission ID, marker,
and last wait result in the diagnostic report. See
[`native_runtime.md`](native_runtime.md),
[`memory_resources.md`](memory_resources.md), and
[`shader_pipelines.md`](shader_pipelines.md) for the underlying contracts.
