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

Milestone 1 permits a single command buffer per `AgcSubmitInfo`. Multiple
command buffers and wait/signal lists are part of the synchronization
milestone and currently fail closed.

## Fences and errors

`AgcFence` is a binary fence. A successful generic submission signals its
fence; reset is legal only when the fence has no pending owner.
`agcGetFenceStatus` returns `AGC_OK` or `AGC_ERROR_BUSY`.
`agcWaitFence` always takes an explicit finite nanosecond timeout and returns
`AGC_ERROR_TIMEOUT` when the fence is unsignaled. Passing
`AGC_RUNTIME_INFINITE_TIMEOUT` is an error; the runtime never silently waits
forever.

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
and transitions remain explicit future runtime work rather than implicit
command-side policy.

## Current qualification boundary

The complete object lifecycle, validation, finite fence, memory/resource,
reflected pipeline validation, compute dispatch recording, indexed-graphics
recording, and typed color-target binding path is host-qualified through the
generic backend. The same public header and implementation compile for
Prospero, and device creation owns exact backend selection, caller default
version, internal memory, and default-state initialization.

Prospero `agcQueueSubmit` submits both current graphics and compute command
buffers through the direct DCB carrier only when the caller supplies an
unsignaled runtime fence. Compute-queue creation establishes the qualified
async setup state, but it does not create a user-special queue: the native
runtime's first FW 5.50 ACB submission was accepted yet its EOP fence timed
out. The changed direct-DCB artifact also reached submission and timed out at
the same fence. The runtime appends an EOP completion write, keeps the command
buffer and its recorded resources pending, and releases them only after status
polling or a finite fence wait observes the GPU-written value. The generic host
harness continues to use its ACB compute carrier for host carrier coverage.
The Prospero DCB policy has generic coverage and a clean cross-build, but it
does not resolve the public-runtime oracle and no hardware qualification is
implied. Broader graphics stages and unqualified fixed options such as
alpha-to-coverage and alpha-to-one remain fail-closed.

`samples/hw_test/agc_runtime_compute.elf` is the dedicated public-runtime
compute probe. It creates a device, compute queue, readback storage buffer,
shader, compute pipeline, command buffer, and fence from the generated
`fill_color_native` binary/reflection pair, then binds the reflected descriptor
and push constants before dispatch. The first FW 5.50 deployment reached
`agcQueueSubmit` successfully but timed out at the runtime EOP fence while
using the former ACB route. The changed direct-DCB artifact reached the same
timeout. Its generic artifact-contract test and revised Prospero cross-build
pass, but neither artifact is hardware-qualified. The next hardware attempt
must use a changed EOP-only diagnostic to isolate completion visibility from
the shader command stream.

`samples/hw_test/agc_runtime_eop.elf` is that bounded public-runtime
diagnostic. It creates the same device, compute queue, command buffer, and
fence path, but records no application commands; `agcQueueSubmit` supplies the
runtime-owned EOP completion packet. The generic suite verifies the equivalent
two-dword runtime NOP carrier and the complete fence lifecycle. Its Prospero
artifact cross-builds cleanly, but it has not been deployed or qualified.

`samples/hw_test/agc_runtime_graphics.elf` is the corresponding native graphics
submission probe. It creates upload vertex/index buffers, a reflected NGG
vertex/fragment pipeline, two RGBA8 color-target images and a D16 depth-target
image, a command buffer, and a fence; its compiler-derived fragment record
exports both color locations, and it binds all typed attachments plus dynamic
viewport/scissor state before its indexed triangle draw. The generic
compiler-artifact contract and its Prospero cross-build pass. Its fence
completion is not a pixel-output oracle, and the probe has not been deployed
or hardware-qualified.
