# Runtime capture semantic records — host evidence (2026-07-31)

Runtime API v25 completes the Milestone 5 capture-v1 semantic record set while
preserving the existing little-endian framing and pointer-free local IDs.

## Covered records

- Buffer, image, image-view, and sampler descriptions.
- Shader stage, Gen5 record/front-record versions, binary sizes, FNV-1a-64
  hashes, reflection presence, and explicitly opted-in binary bytes.
- Normalized graphics and compute pipeline state with shader/resource-layout
  references.
- Typed buffer/image transitions with usage, owner, range, flags, and optional
  dependency-label point.
- Selected readback buffer/image range hashes. The runtime validates the
  object/range, invalidates CPU-visible memory, and hashes the bytes itself.

The host decoder validates record sizes and dynamic-array counts, names stable
object references and typed transition state, and continues to redact PM4
addresses by default.

## Verification

Focused build and tests after the implementation:

```text
cmake --build build --parallel                         PASS
build/openagc_tests                                    17187 passed, 0 failed
python3 tests/test_capture_decoder.py                  PASS
```

The runtime test covers default shader-byte omission, explicit byte opt-in,
all resource-description kinds, shader and compute-pipeline metadata, a typed
buffer transition, a selected readback hash, dense IDs, matching destruction,
and complete stream framing. The independent Python fixture covers deterministic
decode, malformed input rejection, semantic record rendering, and default PM4
address redaction.

The final checkpoint was rebuilt from a deleted generic build directory:

```text
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON   PASS
cmake --build build --parallel                                      PASS
ctest --test-dir build --output-on-failure                          8/8 PASS
build/openagc_tests                                                  17187/0
cmake --build build-prospero --parallel                             PASS
```

Both builds retained `-Wall -Wextra -Wpedantic` and emitted no new warning.
No PS5 replay is required for this serialization/host-decoder-only slice;
hardware remains reserved for the Milestone 5 reference qualification gate.

## Completion-audit coverage extension

The final Milestone 5 audit closed branch-level evidence gaps without changing
the format or implementation. The runtime fixture now captures two real v2
submissions and asserts stable label IDs/values for a signal followed by a
wait/signal dependency; both final injected command streams and bounded fence
results are present. It also creates both compute and graphics pipelines,
records buffer and image subresource transitions, and proves explicit shader-
byte opt-in emits exactly one primary and one front-half record.

The independent decoder fixture now parses and renders submission dependency
lists, both pipeline kinds, and both transition range kinds. It retains the
named packet/register/field, validation-warning, resource-reference, malformed-
stream, deterministic output, and default address-redaction checks.

Focused result before the clean checkpoint:

```text
build/openagc_tests                         17437 passed, 0 failed
python3 tests/test_capture_decoder.py       PASS
```

The completion-audit checkpoint then rebuilt from a deleted generic tree:

```text
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON   PASS
cmake --build build --parallel                                      PASS
ctest --test-dir build --output-on-failure                          12/12 PASS
build/openagc_tests                                                  17437/0
cmake --build build-prospero --parallel                             PASS
```

No new warning was emitted by either toolchain.
