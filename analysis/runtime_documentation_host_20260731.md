# Milestone 5 native documentation contract — host evidence (2026-07-31)

The application documentation now covers every guide and reference category
required by the Milestone 5 roadmap.

## Task-oriented guides

- `docs/native_runtime.md`: versioned objects, external synchronization,
  lifecycle states, submission, fences, runtime information, and qualification.
- `docs/getting_started.md`: installation, exported CMake target, installed
  first-compute and first-indexed-triangle tutorials.
- `docs/memory_resources.md`: heaps, suballocation, staging, overflow-safe
  buffer/image layouts, explicit state, statistics, and deferred retirement.
- `docs/shader_pipelines.md`: compiler reflection, descriptors, push constants,
  vertex inputs, exports, linkage, and graphics/compute pipeline validation.
- `docs/validation.md`: optional allocation-free callback, deterministic message
  contract, filtering, and unchanged fail-closed release behavior.
- `docs/capture.md`: endian-defined v1 stream, semantic records, shader-byte
  opt-in, selected hashes, decoder, redaction, and non-replay security boundary.
- `docs/capabilities_debugging.md`: capability/qualification policy, error
  classes, bounded timeout response, capture/validation evidence, guarded
  console workflow, and minimum support bundle.

## Complete API index

`docs/api_reference.md` indexes every public native handle, callback, enum,
flag, descriptor, query structure, and function. It states the root/retained/
borrowed ownership model, external synchronization rule, global/common result
contract, operation-specific state and return values, and example links.

`tests/test_native_api_reference.py` extracts public typedef aliases from
`openagc/runtime.h`, `openagc/capture.h`, and
`openagc/shader_reflection.h`, plus public native functions from those headers
and `agc_error.h`. It fails if any extracted symbol lacks a backtick-indexed
entry or if the required ownership/thread/return/type/function/example sections
disappear. Current coverage is 119 types and 87 functions.

## Firmware-neutral documented code

`tests/test_documented_firmware_neutrality.py` scans every C code block in the
installed Markdown guides and both ordinary installed examples. It rejects
firmware/profile inspection, Prospero compile branches, Sony-compatible
exports, PM4 helpers, and `/dev/gc` access. Diagnostic prose may explain those
fields, but application decisions use capability bits and the runtime-owned
profile policy.

## Clean checkpoint

The final checkpoint deleted and regenerated the generic build tree:

```text
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON   PASS
cmake --build build --parallel                                      PASS
ctest --test-dir build --output-on-failure                          12/12 PASS
build/openagc_tests                                                  17340/0
cmake --build build-prospero --parallel                             PASS
```

Both toolchains retained `-Wall -Wextra -Wpedantic` and emitted no new warning.
This slice changes documentation, install metadata, and host documentation
checks only; it changes no runtime packet, submission, memory, or firmware
policy and requires no PS5 replay.
