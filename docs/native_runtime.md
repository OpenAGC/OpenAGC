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
OpenAGC rejects unknown versions, nonzero flags, or nonzero reserved fields
without partial object or command creation.

All public entry points use `PS5_SYSV_ABI`. Handles are opaque pointers; an
application must not inspect, copy, allocate, or free their storage. Optional
`AgcAllocationCallbacks` must provide both allocation and free callbacks and
remain callable until the device is destroyed.

## Ownership and synchronization

An `AgcDevice` owns every queue and child object created from it. The runtime
supports one active device because the underlying `/dev/gc` process state is
global. Calls on a device, its queues, and its child objects require external
synchronization.

Ownership dependencies are explicit:

- an image view retains its image;
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
Other batch forms, compute workload batching, and FW 11.60 remain
hardware-unqualified.

`AgcGpuLabel` provides the first GPU-side dependency primitive. A producer
records `agcCmdSignalGpuLabel`; it emits the qualified EOP release write. A
consumer records `agcCmdWaitGpuLabel`; it emits a 32-bit `WAIT_REG_MEM` exact
equality wait on that same runtime-owned word. The consumer submit is rejected
unless a matching producer value has already submitted. The producer may be on
the graphics or compute queue; queue ownership still requires the explicit
resource handoff described below.
This avoids an unbounded wait from an unproved dependency. Signal values are
strictly increasing 32-bit timeline points: `UINT32_MAX` is terminal and never
wraps, while a repeated or lower value is rejected before PM4 mutation. This
prevents a wait from passing on stale memory. The exact graphics/compute label
carrier and single-command submit lists are hardware-qualified on FW 5.50;
event objects remain unsupported.

`agcGetGpuLabelInfo` snapshots its most recently submitted timeline point,
the CPU-observed label word, producer queue/submission identity, and the
selected firmware profile. It is intended for bounded diagnostics, not polling
as a replacement for a fence wait.

## Fences and errors

`AgcFence` is a binary fence. A successful generic submission signals its
fence; reset is legal only when the fence has no pending owner.
`agcGetFenceStatus` returns `AGC_OK` or `AGC_ERROR_BUSY`.
`agcWaitFence` always takes an explicit finite nanosecond timeout and returns
`AGC_ERROR_TIMEOUT` when the fence is unsignaled. Passing
`AGC_RUNTIME_INFINITE_TIMEOUT` is an error; the runtime never silently waits
forever.

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
subresource; the runtime validates device ownership, usage, exact attachment
format, sample count, matching target dimensions, and the proven gfx1013 base
alignment before emitting any packet. Multi-render-target validation is
transactional: a mismatched target count or extent retains no image and emits
no target state. Bound targets cannot be replaced within a command buffer and
remain retained until reset. Color target binds cover the qualified 1x linear
and RGBA8 4x layouts.

For a pipeline with a declared depth/stencil format, bind one matching
`AgcDepthStencilTargetBinding` before drawing. The v4 path validates exact
format/sample agreement, image usage, layer extent, and retained ownership,
then emits the existing gfx1013 depth-surface packet. It supports the
directly-queryable single-mip depth layouts and one array layer per command
binding; other depth mip layouts fail closed. Load/store operations, clears,
and transitions remain explicit rather than implicit command-side policy.

## Explicit resource transitions

`agcCmdTransitionResources` records versioned `AgcResourceTransition` entries;
each names one buffer or image, its source and destination `AgcResourceUsage`,
and explicit host/graphics/compute ownership. Applications never supply cache
control words. The runtime maps supported requests to the qualified gfx1013
release/flush and acquire/invalidate sequence.

The initial v1 implementation deliberately has a narrow, deterministic scope:
it accepts whole buffers and complete image mip/layer/aspect ranges only.
HTILE images, partial ranges, graphics-to-compute ownership transfers, and
unqualified usage combinations fail closed. A resource may appear only once in
one transition batch; applications record a later state change in a separate
call. Command buffers retain transitioned resources, track their requested
state while recording, and publish that state only after a successful submit.
Resetting or rejecting a command therefore never changes cross-command state.

The supported usages are undefined/discard, copy source/destination, shader
read/write, color target, depth/stencil read/write, VideoOut scanout, and host
read/write. The command queue must own GPU destinations; host destinations use
the host owner.

Version 2 adds a narrowly-scoped GPU-to-GPU handoff. The source queue records
`AGC_RESOURCE_TRANSITION_RELEASE_BIT` with a strictly increasing
`dependency_label`/`dependency_value`; its resource release writes that word at
EOP. Only after source submission may the destination queue record the matching
`AGC_RESOURCE_TRANSITION_ACQUIRE_BIT`; it emits an exact `WAIT_REG_MEM` then
the qualified all-cache invalidate. The source state remains committed until
release submit and destination state publishes only after acquire submit. A
pending handoff accepts one acquire command; reset releases that reservation.
The current v2 scope is whole resources whose source usage writes GPU memory;
partial ranges, HTILE, submit lists, and other handoff forms fail closed.

## Current qualification boundary

The complete object lifecycle, validation, finite fence, memory/resource,
reflected pipeline validation, compute dispatch recording, indexed-graphics
recording, typed color-target binding, and the initial explicit whole-resource
transition matrix are host-qualified through the generic backend. The same
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
Depth/stencil, copy, scanout, remaining v2 handoff rows, non-batch submit-list
rows, and timeline synchronization remain host-only.

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
