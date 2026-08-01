# PS5 GPU memory and resource layouts

The native OpenAGC runtime suballocates PS5 GPU resources from reusable memory
blocks. The generic backend exercises the same ownership, layout, and allocator
logic using host memory; it is not a non-PS5 GPU backend.

## Heaps and allocation policy

`AGC_MEMORY_HEAP_FLEXIBLE` represents CPU-cached, CPU/GPU-visible PS5
flexible (onion) memory. Upload buffers, readback buffers, shader code,
image/sampler descriptor slots, and command storage use this heap.
`AGC_MEMORY_HEAP_GARLIC` represents direct, write-combined PS5 memory.
Ordinary GPU buffers and images use garlic.

Applications select resource intent, not a firmware-specific allocator:

- a buffer with `AGC_BUFFER_CREATE_UPLOAD_BIT` is persistently mapped in
  flexible memory and accepts bounded `agcWriteBuffer` calls;
- a buffer with `AGC_BUFFER_CREATE_READBACK_BIT` is persistently mapped in
  flexible memory and accepts bounded `agcReadBuffer` calls;
- an ordinary buffer is placed in garlic;
- images are placed in garlic, with scanout images receiving dedicated blocks;
- `AGC_BUFFER_CREATE_DEDICATED_BIT` requests a dedicated buffer allocation;
- allocations larger than half a normal block become dedicated automatically.

Runtime API 36 adds `AGC_BUFFER_USAGE_INDIRECT_BIT`. Indirect draw and dispatch
arguments remain ordinary typed `AgcBuffer` objects: applications transition
the complete 16-byte draw, 20-byte indexed-draw, or 12-byte dispatch record
range to `kAgcResourceUsageShaderRead` on the consuming queue. The runtime
validates the final multi-draw record, retains the argument buffer through
command recycling, and keeps GPU addresses and encoder reservations private.

Runtime API 37 adds `AGC_BUFFER_USAGE_QUERY_BIT` and
`kAgcResourceUsageQueryWrite`. Query buffers may combine upload and readback
flags because host reset and bounded result retrieval are both part of the
same storage lifecycle.

Small resources share alignment-aware blocks. Freed ranges are discovered from
the sorted live-allocation map and reused without allocating a new direct-memory
object per resource. Empty pooled blocks stay cached until device destruction;
empty dedicated blocks are returned immediately.

Image views and samplers reserve one aligned, zero-initialized hardware-sized
descriptor slot apiece. The reflection/pipeline milestone will populate and
group those slots; Milestone 2 owns their GPU-visible allocation and lifetime.
If resource creation fails after acquiring a new heap block, the block and all
metadata are rolled back before the error is returned.

## Occlusion-query storage

Call `agcGetOcclusionQueryLayout` to size and align query storage. The returned
record size is opaque: applications do not consume render-backend count slots,
GPU addresses, or availability packet fields. A query buffer must use
`AGC_BUFFER_USAGE_QUERY_BIT`; host-reset/readback buffers normally use both
`AGC_BUFFER_CREATE_UPLOAD_BIT` and `AGC_BUFFER_CREATE_READBACK_BIT`.

`agcResetOcclusionQueryResults` clears complete records on the host.
`agcCmdResetOcclusionQueries`, `agcCmdBeginOcclusionQuery`, and
`agcCmdEndOcclusionQuery` retain the buffer through command recycling and
acquire each range into graphics-owned `QueryWrite` state internally. End
records a cache-flushing EOP availability write. `agcGetOcclusionQueryResult`
accepts either a zero-time poll or a finite nanosecond timeout, invalidates the
record, and returns one reduced 64-bit count plus availability. Infinite waits
are rejected.

## Visibility and staging

Upload and readback storage remains mapped for its entire buffer lifetime.
`agcWriteBuffer` validates `offset + size`, copies into the mapping, and
publishes the exact range with `agcGpuMemoryFlush`. `agcReadBuffer` invalidates
the exact range before copying it to the caller. Zero-length or out-of-range
operations fail without touching memory. An upload operation on a readback or
garlic buffer, and a readback operation on an upload or garlic buffer, also
fails closed.

Images with `AGC_IMAGE_USAGE_TRANSFER_DST_BIT` or
`AGC_IMAGE_USAGE_TRANSFER_SRC_BIT` use the equivalent bounded
`agcWriteImage` and `agcReadImage` operations. They transfer raw allocation
bytes and require the respective usage bit; callers obtain exact portable
subresource ranges from `agcGetImageSubresourceLayout`. This lets a native
application initialize or inspect a linear image without exposing a PS5 cache
operation, while preserving tiled-layout handling as an explicit later
transfer-path concern.

Runtime API 27 also exposes `AgcMemory` for Vulkan-style explicit binding.
`agcCreateMemory` allocates one flexible or garlic interval; applications may
map, flush, and invalidate bounded ranges without receiving firmware policy.
`agcCreatePlacedBuffer` and `agcCreatePlacedImage` bind aligned, in-range
resource intervals to that allocation, and overlapping bindings are permitted
for explicit aliasing. Buffers must match the heap implied by their upload or
readback flags. Linear and BC images may bind flexible or garlic memory when
the allocation satisfies `agcGetImageLayout`; tiled depth and MSAA layouts
retain 64-KiB alignment and therefore require garlic.

Destroying an `AgcMemory` handle releases the application reference. Existing
placed resources retain the allocation, so their commands and bounded transfer
operations remain valid until the last resource is destroyed. A released
memory handle cannot be mapped or used for another binding. Exact live-byte and
allocation baselines recover when the last placed resource releases storage.

Runtime API 28 makes image tiling explicit. Linear color and depth images use
256-byte binding alignment and may reside in flexible or garlic memory;
optimal depth and 4x-MSAA color images retain their qualified 64-KiB tiled
layouts. Version-2 image views carry 2D/array/cube type plus component swizzles,
and version-2 samplers normalize mip filtering, wrap modes, anisotropy,
comparison, LOD, and fixed/custom border selection into native descriptors.
Single-mip 2D views are rebased to that mip's queried address and extent and
encoded as one-level resources. This keeps gfx1013 sampling consistent with
OpenAGC's explicit linear subresource layout, including nonzero base mips;
multi-mip views remain allocation-relative.
Version-3 sampler descriptions include the exact 128-bit custom value. The
device owns, flushes, and programs the indexed table; callers never provide its
GPU address.

Shader uploads and descriptor initialization are flushed when their objects
are created. Executable command storage is flushed over its used byte range at
submission. These publication operations use the same checked low-level cache
API as staging.

These calls provide the CPU visibility half of staging. GPU resource-use
transitions are recorded through `agcCmdTransitionResources`; applications must
not infer or emit cache-control packets themselves. The initial transition
contract accepts complete buffers and complete image ranges only, and publishes
their new state only after submit succeeds. See `native_runtime.md` for the
supported usage/ownership matrix and current fail-closed limits.

`agcCmdUpdateBuffer` embeds caller bytes in the recorded stream, while
`agcCmdFillBuffer` embeds a repeated 32-bit value. Both operate only on
four-byte-aligned, nonempty, in-bounds ranges that have been transitioned to
CopyDestination on the recording queue. Packet space and retained-resource
capacity are checked for the entire operation before any packet is committed.
The caller's update pointer therefore need not remain alive after the command
returns, while the destination buffer remains retained until command recycling.

## Image layouts

`agcGetImageLayout` and `agcGetImageSubresourceLayout` take the owning device,
validate the descriptor, and calculate allocation footprints without GPU
mutation. Device scope lets the runtime select exact profile policy internally.
The v1 contract covers mip chains, 2D arrays, cube-compatible layer groups, 3D
depth slices, 1x/4x samples, BC1–BC7 blocks, split depth/stencil planes, and an
optional HTILE metadata plane. Every multiplication, addition, and alignment
round-up is checked for overflow.

Linear uncompressed rows are padded to 256 bytes, their subresources begin on
512-byte boundaries, and their aggregate allocation is padded to 64 KiB.
Linear BC layouts delegate to the qualified gfx1013 BC surface/subresource
calculators and retain their 256-byte alignment and mip ordering. Four-sample
RGBA8 targets delegate to the qualified 64KB_R_X color calculator. Depth and
stencil allocations delegate to the qualified 64KB_Z_X plane calculator;
HTILE size and subresource offsets delegate to the qualified eight-pipe
calculator only for the generic fixture and the exact standard-PS5 FW 5.50 and
FW 11.60 profiles. Other firmware/profile combinations fail HTILE creation
closed rather than guessing a pipe count.

The result reports block dimensions, bytes per block, plane and subresource
counts, total size and alignment, plus the aggregate HTILE range where present.
`array_layers` includes cube faces and must be a multiple of six when
`AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT` is set. Multi-mip tiled depth allocation
is exact, but individual depth/stencil data-plane offsets remain opaque and
their subresource query returns `AGC_ERROR_NOT_SUPPORTED`; qualified HTILE,
linear, and BC subresource queries remain available.

The current native layout surface intentionally exposes only formats with a
qualified low-level path: RGBA8/BGRA8 UNORM, RGBA8 sRGB, RGBA16
FLOAT/UINT/SINT, RGBA32 FLOAT/UINT/SINT, all BC1–BC7 encodings, D16, D32, S8,
and the combined depth/stencil pairs. Tiled BC, color metadata
(DCC/CMASK/FMASK), HDR, and other packed formats remain unavailable until their
exact PS5 layouts are qualified. Unknown formats fail before allocation.

The v1 resource/memory layer is currently host-qualified through the generic
harness and warning-free in the Prospero cross-build. It has not yet completed
an on-console native-heap smoke test and is not hardware-qualified.

## Diagnostics and retirement

`agcGetObjectAllocationInfo` reports the backing heap, requested and padded
sizes, block offset, GPU virtual address, persistent CPU mapping, residency,
owner type and exact opaque owner handle, dedicated status, and optional
63-byte debug name. Names are set with `agcSetObjectDebugName` and never affect
allocation behavior.

`agcGetMemoryStats` reports live allocation/byte counts, per-heap block counts,
dedicated blocks, high-water marks, and pending deferred frees. A live count
that does not return to its baseline after a reload is a leak even when pooled
blocks remain cached. The stress contract reloads the same buffer/image asset
set eight times and requires stable offsets, bounded block counts, and zero
live allocations and bytes after every cycle.

`agcDestroyBufferDeferred` and `agcDestroyImageDeferred` retire an otherwise
unreferenced resource against a binary fence. Its allocation remains resident
and unavailable for reuse until the fence signals and
`agcCollectDeferredFrees` runs. The fence itself cannot be destroyed while it
owns pending retirements. Passing an already-signaled fence destroys the
resource immediately. New allocations made before completion must receive a
different range; after signal and collection, the retired range is reusable.
