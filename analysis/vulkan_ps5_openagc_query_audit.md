# Vulkan-PS5 OpenAGC Consumer Audit

## Scope and result

This audit checks the current `../Vulkan-PS5/src` consumer against the current
OpenAGC native runtime without requiring PS5 hardware. Vulkan-PS5 references 78
distinct public `agc*` entry points. They are provided by `OpenAGC::openagc`,
and a fresh generic relink reached the final consumer link for the Vulkan
library and query executables without a missing OpenAGC declaration or symbol.

Vulkan-PS5 does not call a Sony export, `/dev/gc`, a private OpenAGC driver
operation, or a backend selector. The installed-driver change is therefore
below its source-level contract. The only Vulkan-visible carrier-dependent
operations are device initialization, queue submission, fence completion,
tessellation-factor-ring setup, and teardown. Resource and state queries remain
implemented by the firmware-neutral OpenAGC runtime.

## Query contract

The consumer directly uses these twelve OpenAGC queries:

- `agcGetDeviceProperties`
- `agcGetDeviceAddress32High`
- `agcGetImageLayout`
- `agcGetImageSubresourceLayout`
- `agcGetObjectAllocationInfo`
- `agcGetCommandBufferState`
- `agcGetCommandBufferRangeStateInfo`
- `agcGetCommandBufferRangeStateSpan`
- `agcGetCommandBufferImageSubresourceStateInfo`
- `agcGetOcclusionQueryLayout`
- `agcGetOcclusionQueryResult`
- `agcGetLastDebugMessage`

Every versioned output passed directly by Vulkan-PS5 is initialized with the
matching `AGC_*_INIT` initializer before the query. Helper-local uninitialized
layout variables are output destinations of a helper which initializes and
validates its own OpenAGC query structure before copying the result. Query
errors are checked before values are consumed. State ambiguity returns a
bounded Vulkan recording error; image-layout failures become format or memory
errors; unavailable occlusion results remain unavailable unless Vulkan asked
for a finite five-second wait; and diagnostic lookup tolerates
`AGC_ERROR_NOT_FOUND`.

`agcGetDeviceProperties(NULL, ...)` is intentionally used before Vulkan device
creation to expose physical-device features and memory heaps. OpenAGC already
supports that contract. A dedicated regression assertion now covers the exact
pre-device call shape and verifies nonzero image limits and the complete heap
table. The remaining eleven query families already have direct host coverage,
including layouts, placed-object addresses, command state, fragmented buffer
spans, image subresource state, diagnostics, and occlusion record reduction.

None of these query implementations dispatches through `AgcDriverOps`. The
Sony carrier cannot change their values merely by being selected. Their
resource handles are created only after `agcCreateDevice` has completed, so an
absent or incomplete strict Sony profile fails closed before Vulkan can issue a
resource query.

## Offline verification on 2026-08-04

The current OpenAGC source was rebuilt as the `openagc` subproject of
`../Vulkan-PS5/build-panic-fix`. The OpenAGC archive, Vulkan object library,
shared/static Vulkan libraries, and query-focused executables compiled and
linked. Of the 61 runnable CTest entries after excluding the unavailable
pipeline test binary, 59 passed. Lifecycle, WSI, command recording, all tested
format/layout paths except the known integer-format fixture, resource
transitions, and loader/entrypoint checks passed.

The two runnable failures are consumer baseline drift outside this carrier
slice: the checked-in capability snapshot expects format 44 flags `56449`
while the current consumer reports `56451`, and the integer-format fixture
fails while creating `bgr10a2_unorm`. The full build also stops in
`tests/pipeline.c` because that test still calls an internal meta-attachment
helper with its former four-argument signature while the current declaration
requires eight arguments. These failures do not involve an OpenAGC query ABI,
Sony selection, or a missing OpenAGC symbol, and this audit does not modify the
clean Vulkan-PS5 worktree to conceal them.

The standalone query rendering probes submit and reach their finite fence on
the generic backend, then report zero samples/pixels because the generic
backend validates and captures GPU work but does not execute graphics. Their
hardware-result checks are therefore not host qualification tests. OpenAGC's
own occlusion host test instead constructs an available gfx1013 query record
and verifies the exact reduction and state transition used by Vulkan.

## Remaining hardware gate

Offline evidence establishes API compatibility and carrier independence of
the query layer. It cannot establish that the installed Sony carrier executes
the command buffers which produce query records or EOP fences. After the PS5
is available, the strict Sony artifact must still demonstrate an observable
marker, bounded fence completion, occlusion availability, compute/graphics,
tessellation, presentation, and teardown/reinitialization. `AGC_OK` without
observable GPU completion remains a failed gate, with no fallback to direct
`/dev/gc` after Sony selection.
