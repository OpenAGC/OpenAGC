# Native runtime contract

`openagc/runtime.h` is OpenAGC's firmware-neutral C99 application layer for the
PS5 GPU. Firmware-neutral means that one application ABI selects supported PS5
firmware and standard/Trinity PS5 profiles at runtime. It does not mean support
for non-PS5 GPUs. The generic backend is only a host validation harness. The
native API is separate from the recovered `sceAgc*` and `sceAgcDriver*`
compatibility API, which remains available for diagnostics and expert use.

## Versioning and validation

Every input and output descriptor starts with `struct_size` and `version`.
Initialize structures with their `AGC_*_INIT` macro. Runtime API v2 extends
shader and pipeline descriptors with the shared reflection contract while
preserving the accepted v1 shader prefix for legacy host-only fixtures.
Runtime API v3 adds `AgcColorTargetBinding` and `agcCmdBindColorTargets` for
typed graphics color attachments.
Runtime API v4 adds `AgcDepthStencilTargetBinding` and
`agcCmdBindDepthStencilTarget` for the qualified depth-surface path.
Runtime API v5 adds `AgcFenceInfo` for versioned bounded-wait diagnostics.
Runtime API v6 adds `AgcGpuLabel`, an explicit GPU-visible synchronization
word for the qualified same-queue signal/wait path.
Runtime API v7 adds `agcGetGpuLabelInfo` for scheduled-versus-observed label
diagnostics without exposing a GPU address.
Runtime API v8 adds the version-2 `AgcResourceTransition` release/acquire
handoff protocol, carrying an explicit GPU-label dependency without exposing
raw synchronization addresses or cache-control bits.
Runtime API v9 adds version-2 `AgcSubmitInfo` wait/signal lists through typed
`AgcGpuLabelPoint` entries while preserving the accepted 56-byte v1 prefix.
Runtime API v19 adds bounded host status/wait operations for monotonic label
points and a v2 `AgcGpuLabelInfo` diagnostic tail while preserving its
104-byte v1 prefix.
Runtime API v20 extends cross-queue ownership transfers to exact buffer byte
ranges and image aspect/mip/layer ranges, with multiple disjoint transfers
pending on one resource.
Runtime API v21 makes GPU command, submit-list, and ownership-acquire waits
timeline-aware: a submitted label value satisfies every earlier point.
Runtime API v22 adds `agcRecycleCommandBuffers`, which polls one completed
fence and atomically returns a validated command-buffer batch to `INITIAL`.
Runtime API v23 adds an optional allocation-free validation callback with
versioned, pointer-free message snapshots. It does not replace required safety
checks; see [validation.md](validation.md).
Runtime API v24 adds the endian-defined diagnostic capture stream and
pointer-redacting host decoder described in [capture.md](capture.md).
Runtime API v25 completes its semantic records for resources, shaders,
pipelines, transitions, opt-in shader bytes, and selected readback hashes.
OpenAGC rejects unknown versions, nonzero flags, or nonzero reserved fields
without partial object or command creation.

All public entry points use `PS5_SYSV_ABI`. Handles are opaque pointers; an
application must not inspect, copy, allocate, or free their storage. Optional
`AgcAllocationCallbacks` must provide both allocation and free callbacks and
remain callable until the device is destroyed.

Optional diagnostics are synchronously selected with `agcSetDebugCallback`.
Callbacks require the same external synchronization as the device and must not
re-enter it. Passing `NULL` disables messages without disabling validation.

## Ownership and synchronization

An `AgcDevice` owns every queue and child object created from it. The runtime
supports one active device because the underlying `/dev/gc` process state is
global. Calls on a device, its queues, and its child objects require external
synchronization.

Ownership dependencies are explicit:

- an image view retains its image;
- a present chain retains every registered scanout image;
- a graphics pipeline retains its vertex and pixel shaders;
- a compute pipeline retains its compute shader;
- an executable command buffer retains every pipeline, index/vertex/descriptor
  resource, color-target image, and command-owned resource-table allocation it
  references until reset or destruction;
- an executable command buffer retains every GPU label it waits on or signals
  until reset or destruction;
- a v2 submit retains its transient wait/signal labels on the submitted command
  buffer until that command resets;
- a device retains all child objects.

Destroying an object with a live dependent, recorded reference, or pending
submission returns `AGC_ERROR_BUSY` and changes nothing. Destroy children in
reverse creation/dependency order. Destroying the device shuts down its
selected backend only after all children are gone.

Buffers, images, shader code, and command storage are backed by the native PS5
heap allocator described in [memory_resources.md](memory_resources.md).
Fence-keyed buffer/image retirement extends these ownership rules without
returning pending GPU storage to a heap early.

Image layout queries take the owning `AgcDevice`; profile-dependent layout
policy is selected inside the runtime and is never supplied as a firmware
choice by the application.

## Command-buffer states

Command buffers have four public states:

| State | Allowed operations |
| --- | --- |
| `INITIAL` | begin, reset, destroy |
| `RECORDING` | bind and command calls, end, reset |
| `EXECUTABLE` | submit, reset, destroy |
| `PENDING` | status inspection only |

`agcBeginCommandBuffer` starts recording only from `INITIAL`.
`agcEndCommandBuffer` permits an empty recording for fence-only submissions;
the runtime supplies the carrier packet internally. `agcResetCommandBuffer`
releases recorded object references and returns a non-pending command buffer
to `INITIAL`; it also provides recovery after validation or capacity failure.
`agcRecycleCommandBuffers` is the fence-driven batch form: it validates every
distinct member before polling, returns `AGC_ERROR_BUSY` without mutation when
the fence is incomplete, and releases references and resets storage only when
every member is complete. A rejected member leaves the whole batch unchanged.
No command call emits partial packets. Insufficient capacity returns
`AGC_ERROR_COMMAND_SPACE_EXHAUSTED` with the cursor unchanged.

The synchronization milestone supports a bounded 2–63 command-buffer batch on
the graphics and compute queues. Every member must be executable, nonempty,
distinct, and owned by the same queue; one fence tracks the complete batch and
releases all members after completion. Empty batch members and cross-queue
submission remain fail-closed.

For one command buffer, v2 `AgcSubmitInfo` can name bounded lists of
`AgcGpuLabelPoint` waits and signals. The runtime validates every exact prior
producer point before mutation, inserts waits before the command body and EOP
signals after it, and snapshots full command storage. Validation, capacity,
flush, or driver-submit failure restores bytes and cursor exactly and releases
temporary label retains. Signal points publish only after successful submit.
For graphics or compute batches, the first command receives the wait preamble
and the last receives the signal tail; both endpoint storages are restored
together on rejection. The exact FW 5.50 standard-PS5 oracle passed the
two-nonempty-DCB graphics-batch form with a first-DCB wait and final-DCB
signal, without CPU waits; see
[`runtime_batch_submit_label_lists_fw550_20260731.md`](../analysis/runtime_batch_submit_label_lists_fw550_20260731.md).
The equivalent label-only compute-queue form also passed; see
[`runtime_compute_batch_submit_label_lists_fw550_20260731.md`](../analysis/runtime_compute_batch_submit_label_lists_fw550_20260731.md).
The exact FW 5.50 compute-workload form also passed with reflected dispatch in
the first DCB, a verified label tail in the second, a batch fence, and readback;
see [`runtime_compute_batch_fw550_20260731.md`](../analysis/runtime_compute_batch_fw550_20260731.md).
Other batch forms, larger batches, and FW 11.60 remain hardware-unqualified.

Runtime API v10 adds `AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT`. A version-2
transition with that flag names state produced by an earlier DCB in the same
ordered, same-queue batch; it carries no label and cannot be combined with an
ownership release or acquire. The runtime validates the complete ordered state
chain before submit-time command or driver mutation. A reversed chain, a
missing producer,
or a single-command submit fails with `AGC_ERROR_INVALID_STATE`. The exact FW
5.50 reflected compute batch passed a first-DCB `undefined -> shader-write`
transition and second-DCB dependent `shader-write -> host-read` transition,
then readback; see
[`runtime_batch_transition_chain_fw550_20260731.md`](../analysis/runtime_batch_transition_chain_fw550_20260731.md).

Runtime API v11 adds `agcCmdCopyBuffer(command, source, sourceOffset,
destination, destinationOffset, size)`. Source and destination must have
explicit `CopySource` and `CopyDestination` usage on the command queue; ranges
are nonzero, four-byte aligned, in bounds, and non-overlapping. The runtime
retains both buffers and emits qualified `DMA_DATA` packets internally. The
same-queue upload-to-copy-to-readback row passed on exact standard PS5 FW 5.50:
the 1,024-byte artifact completed one bounded compute-queue fence and verified
all 256 destination words; see
[`runtime_copy_fw550_20260731.md`](../analysis/runtime_copy_fw550_20260731.md).
The same-queue compute-to-copy-to-shader-read row is also FW 5.50 qualified:
one reflected shader writes the transfer source, a typed copy produces the
consumer input, and a second reflected shader reads that input before final
host readback. It runs as one three-DCB batch with a bounded fence; see
[`runtime_compute_copy_shader_fw550_20260731.md`](../analysis/runtime_compute_copy_shader_fw550_20260731.md).
Large/multi-packet buffer copies, graphics queues, cross-queue ownership,
partial ranges, and other firmware profiles remain unqualified.

Runtime API v12 adds `agcCmdCopyImage(command, source, destination)`. It copies
the complete allocation only when both images have identical dimensions,
format, sample count, mip/layer shape, and computed layout and are in explicit
`CopySource`/`CopyDestination` state on the command queue. The runtime
preflights all split DMA packets and both image retains before emission.
Partial subresources, self-copy, and layout conversion fail closed. A
four-megabyte host fixture locks three-packet splitting; exact standard-PS5 FW
5.50 artifact
`29110963a218ac7e5de2fc5073c23d5373e7eaa1365ccb3e2b6cf26fe1f85046`
passed twice with exact 256-word readback. See
[`runtime_image_copy_fw550_20260731.md`](../analysis/runtime_image_copy_fw550_20260731.md).

Runtime API v41 adds `agcCmdCopyImageRegions`, `agcCmdCopyBufferToImage`, and
`agcCmdCopyImageToBuffer`. Each region names an image mip, array-layer span,
signed texel offset, and extent; buffer/image regions additionally name a byte
offset plus Vulkan-style row-length and image-height strides, where zero means
tightly packed. The runtime derives every image row from
`agcGetImageSubresourceLayout`, converts BC texels to blocks, permits a partial
final BC block only at the mip edge, verifies the complete final buffer
footprint, and reserves every row packet and resource retain before writing
the command stream. Both resources must already be in queue-owned typed copy
state. Single-plane color and BC images with one sample are supported. Depth,
stencil, metadata-bearing, multisample, format-converting, blit, clear, and
resolve transfers return a fail-closed error until dedicated native contracts
are qualified. The generic backend intentionally records rather than executes
GPU DMA; host coverage therefore checks the exact row-packet count and
lifecycle, while pixel results remain an FW 5.50 hardware oracle.

Runtime API v13 adds an opaque `AgcPresentChain`. Creation accepts two to 16
distinct, device-owned 1920x1080 linear `RGBA8_UNORM` images carrying
`AGC_IMAGE_USAGE_SCANOUT_BIT`; the runtime validates their common pitch,
registers their dedicated direct-memory mappings internally, and retains them
until chain destruction. Applications never receive a scanout pointer or
choose a VideoOut format, tiling mode, firmware patch, or user ID.

`agcPresent` requires the selected image to have committed
`VideoOutScanout` usage with Graphics ownership. It rejects infinite and zero
wait budgets, waits for the caller's readiness fence, then performs one
bounded FIFO/VSYNC flip. The readiness fence must cover the final submission
that writes or transitions the image. Each fence and flip wait has the supplied
finite ceiling. To render an image again, record a Graphics-owned
`VideoOutScanout -> ColorTarget` transition; return it to
`VideoOutScanout` before the next present.

The first FW 5.50 one-buffer registration failed safely; a two-buffer combined
registration/transition/flip attempt did not return and remains invalid. Do
not rerun that artifact. The replacement cleanup-first ladder passed
registration-only, transition-only, first-flip, round-trip-transition, and
final-flip stages with clean teardown as identical bytes on exact FW 5.50 and
FW 11.60. See
[`runtime_present_attempt_fw550_20260731.md`](../analysis/runtime_present_attempt_fw550_20260731.md)
and [`runtime_milestone4_fw1160_20260731.md`](../analysis/runtime_milestone4_fw1160_20260731.md).

Runtime API v14 makes fence-keyed retirement usable for resources referenced
by submitted command buffers. `agcDestroyBufferDeferred` and
`agcDestroyImageDeferred` immediately reject new resource use, but an existing
command, image view, or present chain may continue to retain the object. The
collector releases allocation storage only after the named fence has completed
and all retained references have been released by command reset/destruction or
dependent-object destruction. A completed fence alone cannot recycle live
command storage.

The generic stress fixture runs 32 two-command compute batches. Each batch
transitions one buffer and one image through a v2 batch dependency, retires
both objects against the batch fence while four command references remain,
uses a 200 ms finite wait, proves pre-reset collection returns
`AGC_ERROR_BUSY`, recycles both commands atomically, and returns deferred
count, live allocation count, and live bytes to the exact baseline. It also
proves a present-chain dependency delays image collection. The identical
Prospero artifact passes all 32 cycles with clean teardown on exact FW 5.50
and FW 11.60; see
[`runtime_batch_deferred_retirement_host_20260731.md`](../analysis/runtime_batch_deferred_retirement_host_20260731.md).

Runtime API v15 makes `Undefined` a valid destination as well as an initial
source. Transitioning to it discards prior contents and records no PM4 packet,
including when the prior usage was a writer; the runtime still commits the new
Host-owned state only after successful submission. A later use must transition
from `Undefined` to its required typed state. This avoids a pointless cache
writeback for data the application explicitly abandoned.

The low-level oracle enumerates every one of the 100 gfx1013 usage pairs. It
checks exact dword counts and release/NOP/acquire packet positions for each
pair, while the existing representative fixtures continue to lock every packet
word. The public runtime fixture also submits and resets a
`HostRead -> Undefined` buffer discard. See
[`runtime_transition_matrix_host_20260731.md`](../analysis/runtime_transition_matrix_host_20260731.md).

Runtime API v16 adds read-only `AgcResourceStateInfo` diagnostics through
`agcGetBufferStateInfo` and `agcGetImageStateInfo`. A snapshot distinguishes
the committed usage/owner from a pending ownership-transfer destination and
its label point. It also reports whether the matching acquire is merely
recorded, the command/dependent-object reference counts that make destruction
busy, and fence-keyed deferred-retirement status. Recording never appears as
committed state: usage and ownership change only after successful submission.
The snapshot exposes no GPU address, allocation pointer, PM4 control, or
backend object. See
[`runtime_resource_state_info_host_20260731.md`](../analysis/runtime_resource_state_info_host_20260731.md).

Hardware qualification artifacts for presentation and deferred-retirement
stress must be launched through their `deploy_agc_runtime_*` Make targets. The
shared runner verifies the pinned ELF hash before network access, launches the
process-cleanup ELF first, and fails closed on firmware mismatch, missing exact
verdict, AGC error text, transport timeout, teardown failure, or lost websrv.

Descriptor binding is also state-gated before descriptor-table mutation:
sampled, uniform, and input descriptors require `ShaderRead`; storage
descriptors require `ShaderRead` or `ShaderWrite` for legacy reflection. The
FW 5.50 compute/copy/consumer
oracle passes this gate; see
[`runtime_descriptor_state_gate_fw550_20260731.md`](../analysis/runtime_descriptor_state_gate_fw550_20260731.md).
New `openagc-psbc` reflection packs NIR-derived storage read/write access into
the descriptor mapping, so new artifacts require the exact typed state while
legacy artifacts retain the conservative storage fallback.

`AgcGpuLabel` provides the first GPU-side dependency primitive. A producer
records `agcCmdSignalGpuLabel`; it emits the qualified EOP release write. A
consumer records `agcCmdWaitGpuLabel`; it emits a 32-bit `WAIT_REG_MEM`
greater-or-equal wait on that same runtime-owned word. The consumer submit is
rejected unless that point or a later point has already submitted. The producer
may be on the graphics or compute queue; queue ownership still requires the
explicit resource handoff described below.
This avoids an unbounded wait from an unproved dependency. Signal values are
strictly increasing 32-bit timeline points: `UINT32_MAX` is terminal and never
wraps, while a repeated or lower value is rejected before PM4 mutation.
Command-local tentative signals and ordered multi-DCB signals are revalidated
against the latest submitted point, so stale recordings cannot publish a
decreasing counter. This prevents a wait from passing on stale memory while a
later timeline point still satisfies every earlier dependency. The
exact graphics/compute label carrier and the Milestone 4 timeline-wait gate are
hardware-qualified on FW 5.50 and FW 11.60; single-command submit-list workload
coverage remains exact-FW-5.50-qualified. Event objects remain unsupported.

`agcGetGpuLabelStatus(label, value)` succeeds when the CPU-observed word is at
or beyond an already-scheduled point. `agcWaitGpuLabel` applies the same
monotonic comparison with a mandatory finite nanosecond deadline; waiting for
an unscheduled future point or passing `AGC_RUNTIME_INFINITE_TIMEOUT` fails
immediately. On PS5, the bounded wait uses the latest scheduled 32-bit word and
then rechecks the monotonic target.

`agcGetGpuLabelInfo` snapshots the most recently submitted timeline point, the
CPU-observed label word, producer queue/submission identity, and selected
firmware profile. Its v2 tail also reports the last host wait target/result,
timeout count, and last finite deadline. The v1 104-byte prefix remains
accepted.

## Fences and errors

`AgcFence` is a binary fence. A successful generic submission signals its
fence; reset is legal only when the fence has no pending owner.
`agcGetFenceStatus` returns `AGC_OK` or `AGC_ERROR_BUSY`.
`agcWaitFence` always takes an explicit finite nanosecond timeout and returns
`AGC_ERROR_TIMEOUT` when the fence is unsignaled. Passing
`AGC_RUNTIME_INFINITE_TIMEOUT` is an error; the runtime never silently waits
forever.

`agcRecycleCommandBuffers` polls its fence once rather than waiting. It accepts
1–63 distinct command buffers owned by the fence device. Members must already
be executable or pending during validation and must all be executable with no
pending submission references after the poll; a pending member must belong to
the supplied fence. Success releases every recorded object reference, resets
the command storage, and returns all members to `INITIAL` as one transaction.

`agcGetFenceInfo` provides a versioned diagnostic snapshot without exposing a
backend handle or GPU address. It reports the fence state, latest queue and
command-buffer ownership, submission and completion IDs, expected and observed
completion markers, timeout count/latest deadline/result, and the exact runtime
profile. An unsubmitted fence uses `UINT32_MAX` as its queue type. Applications
can log this snapshot after a bounded timeout; it describes the failure but does
not convert a timeout into success or attempt recovery.

The completed-fence snapshot is hardware-qualified on the exact FW 5.50 public
compute path by artifact
`bd8545c05a7683bf4fb0c69e7c925317488ba7fd60e455ef7e1ecf715b477c9d`; see
[`runtime_fence_diagnostics_fw550_20260731.md`](../analysis/runtime_fence_diagnostics_fw550_20260731.md).
Pending timeout diagnostics remain host-tested until a bounded on-console
timeout oracle is safe and useful.

The two-DCB graphics batch is hardware-qualified on exact FW 5.50 by artifact
`30564bfdd87de4c89e575a03b7456aad57a2ca72af174aa41d1598a20322142b`; see
[`runtime_multi_graphics_fw550_20260731.md`](../analysis/runtime_multi_graphics_fw550_20260731.md).

The same-queue GPU-label signal/wait path is hardware-qualified on exact FW
5.50 by artifact
`1af09900242e5e0af40c12dfb68bd8ea4fb059bdb85654d969cfff88cb15d016`; see
[`runtime_gpu_labels_fw550_20260731.md`](../analysis/runtime_gpu_labels_fw550_20260731.md).

Stable native errors include invalid argument/state, busy ownership,
unsupported capability, command-space exhaustion, timeout, out of memory,
submission failure, and device loss. An uninitialized backend submission is
reported as `AGC_ERROR_DEVICE_LOST` rather than being treated as success.

## Runtime information and qualification

`agcGetRuntimeInfo` reports:

- the native API version and caller-selected AGC/default-table version;
- the full runtime firmware value and normalized exact ABI key;
- a host-test marker or the selected standard-PS5/Trinity-PS5 family and exact
  profile name;
- capability bits and one qualification class per capability.

Qualification classes distinguish host-tested behavior,
SPRX/exact-profile-qualified but hardware-unverified behavior, and
exact-firmware hardware qualification. Unknown profiles fail during device
creation through the existing exact firmware registry. Applications branch on
capabilities, never on `firmware_version` or `firmware_abi_key`.

Qualification applies to the native runtime operation being reported, not to a
lower-level driver carrier that happens to be available on the same profile.
Until a public runtime artifact passes its own exact-firmware oracle, the
runtime capability is reported as host-tested even when the selected direct
carrier has separate hardware evidence.

## Shader and pipeline validation

Runtime API v2 consumes pointer-free `AgcShaderReflection` records shared with
`openagc-psbc` API v15. Shader creation checks the reflection version, compiler
and shader-record versions, stage, entry point, FNV-1a hash, serialized record
type, executable offsets, descriptor and push layouts, SGPR records, vertex
inputs, color exports, wave size, scratch/LDS, and stage-specific limits before
allocating a live shader object. `agcGetShaderReflection` returns the validated
copy owned by the shader.

Graphics pipeline creation currently supports reflected gfx1013 NGG vertex
plus Wave32 pixel shaders. It validates stage linkage, exact vertex and
resource layouts, color export count/class/width/write masks, integer blending,
depth/stencil requirements, sample count, and supported fixed-function state.
Compute pipelines validate Wave32, threadgroup size, descriptor/push layout,
scratch, and LDS limits. Rejected objects are returned as `NULL`; failed binds
leave the command cursor unchanged. Immutable qualified register groups are
cached in the pipeline. The runtime derives `SPI_SHADER_COL_FORMAT` from the
validated reflected pixel exports, so a compiler-record context write cannot
contradict the accepted attachment contract. See
[shader_pipelines.md](shader_pipelines.md).

`AgcDepthStencilPipelineState` v2 adds explicit 0–1 depth bounds and complete
front/back stencil face state. All compare and stencil operations, references,
compare masks, and write masks are immutable pipeline state unless stencil
reference is declared dynamic. The runtime accepts the former 64-byte v1
depth-only layout and normalizes its implicit bounds to 0–1; v1 stencil remains
unsupported because that layout did not carry the operations or masks needed
to reproduce a safe pipeline.

Command recording binds exact reflected descriptor arrays, vertex tables, and
push ranges into a runtime-owned GPU-visible arena. Draw and dispatch validate
complete binding before emitting work. Declared viewport, scissor, blend-
constant, stencil-reference, and depth-bias state is recorded dynamically and
must be set before a graphics draw.

For a graphics pipeline with color exports, bind exactly one
`AgcColorTargetBinding` per declared color attachment before drawing. Each
binding names an `AGC_IMAGE_USAGE_COLOR_TARGET_BIT` image plus a mip/layer
subresource. Runtime API 53 adds the version-2 `format` field: an image created
with `AGC_IMAGE_CREATE_MUTABLE_FORMAT_BIT` may select a view-compatible native
attachment encoding, while `AGC_FORMAT_UNDEFINED` preserves the image's base
format. Version-1 bindings retain the old reserved-zero contract. The runtime
validates device ownership, usage, exact effective attachment format, sample
count, matching target dimensions, and the proven gfx1013 base
alignment before emitting any packet. Multi-render-target validation is
transactional: a mismatched target count or extent retains no image and emits
no target state. Bound targets cannot be replaced within a command buffer and
remain retained until reset. Each target must already have an explicit
whole-image `color-target` state owned by the graphics queue in the recording
command buffer. This state check occurs after complete target-definition
validation, so an invalid MRT definition retains its atomic validation result
rather than being hidden by an unrelated state mismatch. The exact FW 5.50
two-target graphics probe passed this gate; see
[`runtime_color_target_state_gate_fw550_20260731.md`](../analysis/runtime_color_target_state_gate_fw550_20260731.md).
Color target binds cover the qualified 1x linear and RGBA8 4x layouts.

For a pipeline with a declared depth/stencil format, bind one matching
`AgcDepthStencilTargetBinding` before drawing. The v4 path validates exact
format/sample agreement, image usage, layer extent, and retained ownership,
then emits the existing gfx1013 depth-surface packet. It supports the
directly-queryable single-mip depth layouts and one array layer per command
binding; other depth mip layouts fail closed. The required typed state is
derived from the graphics pipeline: `depth-stencil-write` when depth/stencil
or reflected fragment-shader writes are possible, otherwise
`depth-stencil-read`. The whole image must be owned by the graphics queue
before target binding. A write-requiring pipeline requires
`depth-stencil-write`; a read-only pipeline accepts either
`depth-stencil-read` or the conservative `depth-stencil-write` superset used
by Vulkan attachment layouts. The exact FW 5.50 D16 depth-write row
passed the public probe; see
[`runtime_depth_stencil_state_gate_fw550_20260731.md`](../analysis/runtime_depth_stencil_state_gate_fw550_20260731.md).
Load/store operations and clears remain explicit rather than implicit
command-side policy.

Vertex and index buffers must likewise be transitioned to `shader-read` and
owned by Graphics before `agcCmdBindVertexBuffers` or `agcCmdBindIndexBuffer`.
That state accepts uniform, storage, vertex, and index buffer usage, keeping
GPU-readable buffers in one typed state. Binding validates all buffer/layout
arguments before testing state. The exact FW 5.50 reflected graphics probe
passed the upload vertex/index row; see
[`runtime_graphics_input_state_gate_fw550_20260731.md`](../analysis/runtime_graphics_input_state_gate_fw550_20260731.md).

## Explicit resource transitions

`agcCmdTransitionResources` records versioned `AgcResourceTransition` entries;
each names one buffer or image, its source and destination `AgcResourceUsage`,
and explicit host/graphics/compute ownership. Applications never supply cache
control words. The runtime maps supported requests to the qualified gfx1013
release/flush and acquire/invalidate sequence.

`agcCmdMemoryBarrier` covers global dependencies that do not identify a
resource or one destination usage. On a graphics command buffer it flushes
color and depth metadata, performs an EOP data/cache release, and follows it
with a full GPU-cache acquire. It deliberately leaves every resource's typed
state unchanged. Compute-queue global barriers remain fail-closed until their
separate packet contract is qualified.

The initial v1 implementation accepted whole buffers and complete image
mip/layer/aspect ranges. Runtime API v17 extends buffer transitions to bounded
byte ranges. Internally, sorted contiguous intervals split on submission and
merge when adjacent usage/owner states become equal. Tentative command state
can span several recorded ranges, but committed intervals change only after
successful submit validation. Copies and buffer descriptors validate their
exact byte ranges; vertex and index bindings validate from their binding
offset through the represented tail. `agcGetBufferRangeStateInfo` queries a
uniform range, while the whole-buffer query returns `AGC_ERROR_NOT_SUPPORTED`
when the buffer is fragmented.

Runtime API v58 adds `agcGetCommandBufferRangeStateSpan`. It reports the
effective usage and owner of the first uniform prefix in a command-local byte
range, together with that prefix's nonzero size. Translators can therefore
walk and transition fragmented ranges exactly; the existing uniform query
continues to reject mixed state. Transition preflight accounts buffer and
image reference journals independently and charges only resources not already
retained by the command buffer. Persistent buffer intervals have a separate
1,048,577-range bound (at most 16 MiB of interval metadata per maximally
fragmented buffer), because their lifetime can span many 512-transition
commands. This covers one complete 128 MiB streaming-buffer cycle with
disjoint ranges at the qualified 256-byte allocation alignment; adjacent equal
states continue to merge eagerly.

Runtime API v18 applies the same transactional contract to image aspects,
mips, and array layers. State storage stays uniform until the first partial
transition, then uses a compact lazy table and collapses again when every
subresource converges. Color and depth/stencil targets validate the selected
mip/layer/aspects, and descriptors validate the exact image-view range.
Whole-image copy and VideoOut presentation require a uniform complete-image
state. `agcGetImageSubresourceStateInfo` queries a uniform selected range;
`agcGetImageStateInfo` returns `AGC_ERROR_NOT_SUPPORTED` when any aspect,
mip, or layer differs. HTILE images and unqualified usage combinations still
fail closed. A resource may appear
only once in one transition call; applications record disjoint or later state
changes in ordered calls.

The supported usages are undefined/discard, copy source/destination, shader
read/write, color target, depth/stencil read/write, VideoOut scanout, and host
read/write. The command queue must own GPU destinations; host destinations use
the host owner.

Version 2 adds a narrowly-scoped GPU-to-GPU handoff. The source queue records
`AGC_RESOURCE_TRANSITION_RELEASE_BIT` with a strictly increasing
`dependency_label`/`dependency_value`; its resource release writes that word at
EOP. Only after source submission may the destination queue record the matching
`AGC_RESOURCE_TRANSITION_ACQUIRE_BIT`; it emits a reached-or-passed
`WAIT_REG_MEM` then
the qualified all-cache invalidate. The source state remains committed until
release submit and destination state publishes only after acquire submit. A
pending range accepts one exact acquire command; reset releases only that
reservation. Runtime API v20 permits several non-overlapping pending transfers
on one resource. Releases may cover exact buffer byte ranges or image
aspect/mip/layer ranges; acquires must match a committed range, destination
state, label, and value exactly. Overlapping pending or batch releases reject
before driver mutation, while disjoint ranges remain usable. Range diagnostics
report one uniform pending transfer and reject mixed pending/nonpending or
differently keyed coverage as ambiguous. Pending ranges retain their labels
until acquire submission. HTILE and other unqualified handoff forms remain
fail-closed. Submit-list label dependencies remain available independently.
Several disjoint releases may use increasing points from one label; a later
submitted point cannot invalidate an earlier range's acquire.

The reached-or-passed timeline carrier and corrected two-range buffer and
image ownership carriers pass without CPU inter-submit waits on exact FW 5.50.
See
[`runtime_milestone4_fw550_20260731.md`](../analysis/runtime_milestone4_fw550_20260731.md).

## Current qualification boundary

The complete object lifecycle, validation, finite fence, memory/resource,
reflected pipeline validation, compute dispatch recording, indexed-graphics
recording, typed color-target binding, the explicit transition matrix,
same-queue buffer byte ranges, and image aspect/mip/layer states are
host-qualified through the generic backend.
The same
public header and implementation compile for Prospero, and device creation owns
exact backend selection, caller default version, internal memory, and
default-state initialization. The exact FW 5.50 compute row
`undefined -> shader-write -> host-read` is hardware-qualified by the
public-runtime artifact recorded in
[`runtime_transitions_fw550_20260731.md`](../analysis/runtime_transitions_fw550_20260731.md).
The exact FW 5.50 1x linear RGBA8 MRT row
`undefined -> color-target -> host-read` is also hardware-qualified, with two
resources in each transition batch, as recorded in
[`runtime_graphics_transitions_fw550_20260731.md`](../analysis/runtime_graphics_transitions_fw550_20260731.md).
The exact FW 5.50 standard-PS5 whole-buffer compute `shader-write` to graphics
`shader-read` v2 handoff passed its no-CPU-wait release/acquire oracle, recorded
in [`runtime_crossqueue_resource_handoff_fw550_20260731.md`](../analysis/runtime_crossqueue_resource_handoff_fw550_20260731.md).
The whole-image graphics `color-target` to compute `shader-read` v2 row is
host-qualified: the generic fixture locks the source EOP release, destination
exact-value wait and cache invalidate, pending destruction guard, and final
compute-owned state. See
[`runtime_image_handoff_host_20260731.md`](../analysis/runtime_image_handoff_host_20260731.md).
That host row also reaches a reflected compute consumer: its combined
image/sampler descriptor fails while the image is merely released, succeeds
after the destination acquire, and records dispatch only after the exact wait
and invalidate. See
[`runtime_sampled_image_handoff_host_20260731.md`](../analysis/runtime_sampled_image_handoff_host_20260731.md).
The identical application-facing row is hardware-qualified on exact standard-
PS5 FW 5.50: a real indexed MRT draw releases one target to compute, the
reflected sampled-image consumer submits without an intervening CPU wait, and
all 4,096 packed RGBA8 results match direct image readback. See
[`runtime_render_to_shader_fw550_20260731.md`](../analysis/runtime_render_to_shader_fw550_20260731.md).
The complementary whole-image compute `shader-write` to graphics `shader-read`
carrier passed without a CPU wait on exact standard-PS5 FW 5.50, including both
bounded fences and full teardown. See
[`runtime_image_handoff_fw550_20260731.md`](../analysis/runtime_image_handoff_fw550_20260731.md).
The same public graphics probe qualified upload vertex/index buffers in
`shader-read`/Graphics state; see
[`runtime_graphics_input_state_gate_fw550_20260731.md`](../analysis/runtime_graphics_input_state_gate_fw550_20260731.md).
Other depth/stencil, copy, scanout, remaining v2 handoff rows, non-batch
submit-list rows, and timeline synchronization remain host-only.

Prospero `agcQueueSubmit` submits both current graphics and compute command
buffers through the direct DCB carrier only when the caller supplies an
unsignaled runtime fence. Compute-queue creation establishes the qualified
async setup state, but it does not create a user-special queue: the native
runtime's first FW 5.50 ACB submission was accepted yet its EOP fence timed
out. The changed direct-DCB workload artifact also reached submission and
timed out at the same fence. The dedicated EOP-only FW 5.50 oracle then passed
submit, bounded wait, reset, and full teardown, proving this runtime's direct
carrier, command allocation, completion packet, fence memory, and CPU
visibility path. The remaining compute failure is therefore in the emitted
workload state, not completion. The generic host harness continues to use its
ACB compute carrier for host carrier coverage. This exact fence-only result
does not hardware-qualify a reflected shader/pipeline/dispatch workload.
Broader graphics stages and unqualified fixed options such as alpha-to-coverage
and alpha-to-one remain fail-closed.

`samples/hw_test/agc_runtime_compute.elf` is the dedicated public-runtime
compute probe. It creates a device, compute queue, readback storage buffer,
shader, compute pipeline, command buffer, and fence from the generated
`fill_color_native` binary/reflection pair, then binds the reflected descriptor
and push constants before dispatch. The first FW 5.50 deployment reached
`agcQueueSubmit` successfully but timed out at the runtime EOP fence while
using the former ACB route. The changed direct-DCB artifact reached the same
timeout. The EOP-only oracle then isolated the carrier and completion path;
the subsequent V8-default artifact passed its full reflected workload on exact
FW 5.50, as recorded below.

The first isolated difference is now fixed in `agcCmdDispatch`: it emits all
174 qualified V8 compute SH-default registers before programming shader,
resource, user-data, and dispatch state. This matches the ordering of the
hardware-proven manual compute sample and is covered by a host assertion that
the runtime stream contains the full default sequence plus the dispatch. The
changed artifact (`52a1e82a75cafe5b7541f130e862ae6cf4813ecedd460dd7017408ef2a254775`)
passed on exact FW 5.50: submission, bounded fence wait, readback verification,
reset, and every object teardown returned `AGC_OK`. This hardware-qualifies the
reflected native compute slice for this exact profile. The separate graphics
sample is qualified below; unrun optional pipeline features remain unqualified.

`samples/hw_test/agc_runtime_eop.elf` is that bounded public-runtime
diagnostic. It creates the same device, compute queue, command buffer, and
fence path, but records no application commands; `agcQueueSubmit` supplies the
runtime-owned EOP completion packet. The generic suite verifies the equivalent
two-dword runtime NOP carrier and the complete fence lifecycle. On exact FW
5.50 it passed `agcQueueSubmit`, its 200 ms bounded wait, reset, and complete
object teardown. This qualifies only the fence-only native-runtime slice, not
the broader reflected compute pipeline.

`samples/hw_test/agc_runtime_graphics.elf` is the corresponding native graphics
submission probe. It creates upload vertex/index buffers, a reflected NGG
vertex/fragment pipeline, two RGBA8 color-target images and a D16 depth-target
image, a command buffer, and a fence; its compiler-derived fragment record
exports both color locations, and it binds all typed attachments plus dynamic
viewport/scissor state before its indexed triangle draw. The generic
compiler-artifact contract and its Prospero cross-build pass. Both linear
RGBA8 targets are prefilled through `agcWriteImage`, then read through
`agcReadImage` after the bounded fence wait; the probe requires each MRT output
to replace the sentinel over matching coverage and to differ from the other
output. Every native graphics bind also begins with the exact 2,184-dword FW
5.50 V8 graphics-default prefix before shader and fixed-function state; the
probe therefore reserves 4,096 dwords. Artifact
`e7c3cb908910e28ea1ee1d9c3db0a887d45bd1d9e84e356cf6c8a159167d2941` passed
once on exact FW 5.50: both targets changed exactly 1,152 sentinel pixels,
every changed pair differed, the bounded fence completed, and every object
reset/destroy call returned `AGC_OK`. This qualifies the native baseline
graphics slice for that profile, not unrun optional pipeline features.
