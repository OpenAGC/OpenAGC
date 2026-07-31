# Milestone 5 reference capture and installed examples — host evidence (2026-07-31)

This slice closes two independent Milestone 5 exit gates without changing GPU
packet policy or firmware selection.

## Deterministic captured reference frame

`tests/capture_reference_frame.c` records a real public-runtime compute frame
using the compiler-produced shader and reflection artifacts. The stream covers
the runtime/profile, resource and shader descriptions, normalized compute
pipeline, typed transitions, command boundaries, final submitted PM4, queue
submission, bounded fence result, selected readback hash, object names and
destruction, and terminal counts.

`tests/test_reference_capture.sh` launches the producer twice in separate
processes, decodes both streams with default address redaction, and requires
byte-identical decoded text. It also requires the full semantic chain, the
named `DISPATCH_DIRECT` packet, stable debug names, FNV-1a-64 readback hash,
and at least one redacted address.

Focused result:

```text
ctest --test-dir build -R reference_capture_deterministic --output-on-failure
1/1 PASS
```

## Installed-package application examples

The installed package now includes standalone first-compute and first-indexed-
triangle applications plus their generated shader/reflection artifacts. Their
CMake project calls `find_package(OpenAGC CONFIG REQUIRED)` and links only
`OpenAGC::openagc`.

`tests/test_installed_examples.sh` installs the current build into an empty
temporary prefix, configures the installed example sources using only that
prefix, builds both applications, and executes them on the generic backend.
The compute example completes a reflected storage dispatch submission. The
triangle completes a reflected two-target indexed draw submission. Both use
finite fence waits and reverse-order cleanup.

Focused result:

```text
ctest --test-dir build -R installed_package_examples --output-on-failure
1/1 PASS
```

The generic runs prove public package isolation, validation, command encoding,
submission, and lifecycle behavior. They intentionally do not claim host GPU
shader or raster execution. No PS5 replay is required for this packaging and
host-determinism slice; hardware qualification remains a separate checkpoint.

## Clean checkpoint

The final checkpoint deleted and regenerated the generic build tree:

```text
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON   PASS
cmake --build build --parallel                                      PASS
ctest --test-dir build --output-on-failure                          10/10 PASS
build/openagc_tests                                                  17340/0
cmake --build build-prospero --parallel                             PASS
```

Both toolchains retained the repository warning flags and emitted no new
warning.
