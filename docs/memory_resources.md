# PS5 GPU memory and resource layouts

The native OpenAGC runtime suballocates PS5 GPU resources from reusable memory
blocks. The generic backend exercises the same ownership, layout, and allocator
logic using host memory; it is not a non-PS5 GPU backend.

## Heaps and allocation policy

`AGC_MEMORY_HEAP_FLEXIBLE` represents CPU-cached, CPU/GPU-visible PS5
flexible (onion) memory. Upload buffers, readback buffers, shader code, and
command storage use this heap. `AGC_MEMORY_HEAP_GARLIC` represents direct,
write-combined PS5 memory. Ordinary GPU buffers and images use garlic.

Applications select resource intent, not a firmware-specific allocator:

- a buffer with `AGC_BUFFER_CREATE_UPLOAD_BIT` is persistently mapped in
  flexible memory and accepts bounded `agcWriteBuffer` calls;
- a buffer with `AGC_BUFFER_CREATE_READBACK_BIT` is persistently mapped in
  flexible memory and accepts bounded `agcReadBuffer` calls;
- an ordinary buffer is placed in garlic;
- images are placed in garlic, with scanout images receiving dedicated blocks;
- `AGC_BUFFER_CREATE_DEDICATED_BIT` requests a dedicated buffer allocation;
- allocations larger than half a normal block become dedicated automatically.

Small resources share alignment-aware blocks. Freed ranges are discovered from
the sorted live-allocation map and reused without allocating a new direct-memory
object per resource. Empty pooled blocks stay cached until device destruction;
empty dedicated blocks are returned immediately.

## Visibility and staging

Upload and readback storage remains mapped for its entire buffer lifetime.
`agcWriteBuffer` validates `offset + size`, copies into the mapping, and
publishes the exact range with `agcGpuMemoryFlush`. `agcReadBuffer` invalidates
the exact range before copying it to the caller. Zero-length or out-of-range
operations fail without touching memory. An upload operation on a readback or
garlic buffer, and a readback operation on an upload or garlic buffer, also
fails closed.

These calls provide the CPU visibility half of staging. GPU resource-use
transitions remain owned by the later synchronization milestone; applications
must not infer or emit cache-control packets themselves.

## Image layouts

`agcGetImageLayout` and `agcGetImageSubresourceLayout` validate and calculate
allocation footprints without creating a device or mutating GPU state. The v1
layout contract covers mip chains, 2D arrays, cube-compatible layer groups,
3D depth slices, 1x/4x samples, BC1–BC7 blocks, split depth/stencil planes, and
an optional HTILE metadata plane. Every multiplication, addition, and alignment
round-up is checked for overflow.

Rows are padded to 256 bytes, subresources begin on 512-byte boundaries, and
the aggregate image allocation is padded to 64 KiB. The result reports block
dimensions, bytes per block, plane and subresource counts, total size and
alignment, plus the aggregate HTILE range where present. `array_layers`
includes cube faces and must be a multiple of six when
`AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT` is set.

The current native layout surface intentionally exposes only formats with a
qualified low-level path: RGBA8, all BC1–BC7 encodings, D16, D32, S8, and the
combined depth/stencil pairs. Tiled BC, color metadata (DCC/CMASK/FMASK), HDR,
and other packed formats remain unavailable until their exact PS5 layouts are
qualified. Unknown formats fail before allocation.

The v1 resource/memory layer is currently host-qualified through the generic
harness and warning-free in the Prospero cross-build. It has not yet completed
an on-console native-heap smoke test and is not hardware-qualified.

## Diagnostics and retirement

`agcGetObjectAllocationInfo` reports the backing heap, requested and padded
sizes, block offset, GPU virtual address, persistent CPU mapping, residency,
owner type, dedicated status, and optional 63-byte debug name. Names are set
with `agcSetObjectDebugName` and never affect allocation behavior.

`agcGetMemoryStats` reports live allocation/byte counts, per-heap block counts,
dedicated blocks, high-water marks, and pending deferred frees. A live count
that does not return to its baseline after a reload is a leak even when pooled
blocks remain cached.

`agcDestroyBufferDeferred` and `agcDestroyImageDeferred` retire an otherwise
unreferenced resource against a binary fence. Its allocation remains resident
and unavailable for reuse until the fence signals and
`agcCollectDeferredFrees` runs. The fence itself cannot be destroyed while it
owns pending retirements. Passing an already-signaled fence destroys the
resource immediately.
