# Milestone 5 invalid-program diagnostic matrix — host evidence (2026-07-31)

The optional runtime validation messenger now covers every failure class named
by the Milestone 5 contract while preserving the existing fail-closed result
and mutation boundary.

## Matrix

The host test deliberately triggers and verifies one diagnostic per invalid
operation. Each row asserts callback count, category, unchanged result, public
function name, and a corrective message fragment.

| Failure class | Representative public path | Category |
| --- | --- | --- |
| invalid enum/descriptor | `agcCreateSampler`, `agcCreateBuffer` | parameter |
| misaligned/out-of-range interval | `agcCmdCopyBuffer`, `agcReadBuffer`, `agcReadImage` | parameter |
| missing transition/ownership | `agcCmdCopyBuffer` | resource state |
| descriptor/reflection mismatch | `agcCmdBindDescriptors` | compatibility |
| missing reflected binding | `agcCmdDispatch` | resource state |
| shader export/attachment and integer blend | `agcCreateGraphicsPipeline` | compatibility |
| unsupported wave/capability | `agcCreateComputePipeline` | capability |
| command dword exhaustion | `agcCmdDispatch` | command capacity |
| use after submit without reset | `agcBeginCommandBuffer` | object state |
| premature resource destruction | `agcDestroyBuffer` | lifetime |

The implementation also reports normalized graphics/compute pipeline stage,
linkage, vertex-input, push-constant, depth/stencil, rasterization,
multisample, scratch/LDS, topology, and dual-source failures at their public
creation boundary.

## Determinism and allocation independence

Diagnostics use a fixed-size stack `AgcDebugMessage` and the installed
synchronous callback. A custom-allocation test arms the very next allocator
attempt to fail, invokes an invalid sampler creation, receives the exact
diagnostic, and proves the allocator attempt count did not change. The source
contains no assertion-dependent validation path.

Focused verification before the clean checkpoint:

```text
cmake --build build --parallel        PASS
build/openagc_tests                   17340 passed, 0 failed
```

Final checkpoint verification rebuilt the generic tree from deletion:

```text
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON   PASS
cmake --build build --parallel                                      PASS
ctest --test-dir build --output-on-failure                          8/8 PASS
build/openagc_tests                                                  17340/0
cmake --build build-prospero --parallel                             PASS
```

Generic and Prospero builds retained `-Wall -Wextra -Wpedantic` with no new
warning. No GPU packet encoding, submit carrier, firmware selection, or
hardware policy changed, so this host-validation slice does not require a PS5
replay.
