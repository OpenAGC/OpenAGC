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
