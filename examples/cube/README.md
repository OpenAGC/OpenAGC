# Standalone OpenAGC cube

This is a small PS5 homebrew application, not a hardware-test fixture. It
consumes an installed OpenAGC package through `find_package(OpenAGC)`, owns its
GPU-visible memory and VideoOut buffers, and records every frame through public
OpenAGC APIs.

The application rotates a six-face cube on the CPU and updates one interleaved
position/color vertex buffer every frame. Six faces are sorted back-to-front,
then submitted as one 36-index gfx1013 Wave32 NGG draw. Three frame slots own
independent render targets, DCBs, vertex/index buffers, descriptor tables, and
completion fences. Every GPU wait has a two-second bound.

The presentation path intentionally follows the FW 5.50 hardware-proven memory
model: GPU rendering targets flexible memory, and a completed frame is copied
to a registered garlic VideoOut buffer. The local `ps5_platform.c` contains the
etaHEN launch-context and linear-VideoOut compatibility setup; it is not part of
the OpenAGC API.

## Build against an installed OpenAGC

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk

cmake -S ../.. -B ../../build-prospero-install \
  -DOPENAGC_PLATFORM=prospero \
  -DOPENAGC_BUILD_TESTS=OFF \
  -DOPENAGC_PACKAGE_PSBC=OFF \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake"
cmake --build ../../build-prospero-install
cmake --install ../../build-prospero-install \
  --prefix ../../build-prospero-stage

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
  -DOpenAGC_DIR="$(cd ../../build-prospero-stage && pwd)/lib/cmake/OpenAGC"
cmake --build build
```

The output is `build/openagc_cube.elf`.

## Regenerate shaders

The checked-in shader headers let application builds remain independent of the
compiler toolchain. Regenerate them after editing `shaders/*.glsl`:

```sh
PSBC=../../../openagc-psbc/psbc ./regenerate_shaders.sh
```

The script requires `glslangValidator`, `xxd`, and an OpenAGC `psbc` executable.
The NGG program is compiled for Wave32 with one 24-byte binding containing
`float3` position and `float3` color attributes.

## Deploy through websrv

Do not use `prospero-deploy`; launch through etaHEN websrv so VideoOut is
foregrounded reliably:

```sh
PS5_HOST=10.0.1.41 ./deploy_websrv.sh build/openagc_cube.elf
```

The expected image is a rotating, smoothly colored cube on a dark-gray
background. The process runs for 3,600 presented frames, restores its local
launch compatibility state, releases VideoOut/direct memory, unmaps flexible
memory, and exits.
