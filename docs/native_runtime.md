# Native runtime contract

`openagc/runtime.h` is OpenAGC's firmware-neutral C99 application layer for the
PS5 GPU. Firmware-neutral means that one application ABI selects supported PS5
firmware and standard/Trinity PS5 profiles at runtime. It does not mean support
for non-PS5 GPUs. The generic backend is only a host validation harness. The
native API is separate from the recovered `sceAgc*` and `sceAgcDriver*`
compatibility API, which remains available for diagnostics and expert use.

## Versioning and validation

Every input and output descriptor starts with `struct_size` and `version`.
Initialize v1 structures with their `AGC_*_INIT` macro. OpenAGC accepts the
exact v1 size and version and rejects unknown versions, nonzero flags, or
nonzero reserved fields without partial object or command creation. Future
versions can extend the contract without exposing private object layouts.

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
- an executable command buffer retains every pipeline and index buffer it
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

## Command-buffer states

Command buffers have four public states:

| State | Allowed operations |
| --- | --- |
| `INITIAL` | begin, reset, destroy |
| `RECORDING` | bind and command calls, end, reset |
| `EXECUTABLE` | submit, reset, destroy |
| `PENDING` | status inspection only |

`agcBeginCommandBuffer` starts recording only from `INITIAL`.
`agcEndCommandBuffer` rejects an empty recording. `agcResetCommandBuffer`
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

## Current qualification boundary

The complete object lifecycle, validation, finite fence, compute recording,
and indexed-graphics recording path is host-qualified through the generic
backend. The same public header and implementation compile without warnings
for Prospero, and device creation owns exact backend selection, caller default
version, internal memory, and default-state initialization.

Prospero `agcQueueSubmit` currently returns `AGC_ERROR_NOT_SUPPORTED` before
GPU mutation. A native pipeline does not emit hardware shader/attachment binds
until compiler reflection and pipeline compatibility validation land. This is
intentional fail-closed behavior: submitting a draw or dispatch without that
state would be unsafe. Hardware promotion belongs to the reflection/pipeline
milestone, not to this host-qualified API-contract milestone.
