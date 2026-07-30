# openagc Plan

## Goal

Build a clean open-source PS5 AGC implementation in stages:

1. Recover the AGC packet, command-buffer, shader, queue, and submit model.
2. Make that model host-testable before touching hardware.
3. Implement a native PS5 `/dev/gc` backend once ioctl and memory ownership are
   understood.
4. Preserve that Sony-compatible low-level layer as the firmware and ABI
   foundation.
5. Build a safe, documented, firmware-neutral OpenAGC device, resource,
   pipeline, command, synchronization, and presentation API above it.

The project target is native PS5 AGC behavior, not PS4 GNM compatibility. GNM
is still a valuable reference because PS5 backward compatibility, GNM, AGC, and
AMD PM4 packet ancestry overlap in useful ways.

## Primary Product Requirement: One Firmware-Neutral Homebrew Binary

The main deliverable is one homebrew game binary linked with OpenAGC that can
run unchanged on every supported PS5 firmware. A game built on or for FW 11.60
must not require recompilation for FW 5.50, and a game built on or for FW 5.50
must not require recompilation for FW 11.60 or another supported profile.

Acceptance requires all of the following:

1. The application-facing OpenAGC headers, symbols, structs, and calling
   conventions are firmware-independent.
2. Production application builds contain no compile-time expected-firmware
   key. Test-only macros may assert the console under test, but they must not
   select the implementation linked into the game.
3. OpenAGC reads the console's full runtime version, for example
   `0x11600005`, normalizes only the ABI key (`0x1160`), and selects every
   firmware-dependent driver, defaults, memory, queue, submission, VideoOut,
   and optional-feature detail internally.
4. Every supported key is an exact table entry backed by its own SPRX facts.
   Numeric ranges, nearest-version fallback, and compatibility-group inference
   do not constitute support. Unknown keys fail closed without corrupting
   process or GPU state.
5. One pinned portability ELF and one SHA-256 digest exercise the baseline
   game lifecycle: application-neutral GPU authorization, `/dev/gc` init,
   internal memory, register defaults, async graphics, real GPU execution,
   bounded VideoOut presentation, teardown, and relaunch.
6. That identical ELF must pass on both available endpoint consoles: standard
   PS5 FW 5.50 and standard PS5 FW 11.60. Rebuilding with a different firmware
   macro or comparing two source-equivalent ELFs does not satisfy this gate.
7. Intermediate exact firmware profiles may be enabled from reproducible SPRX
   evidence when hardware is unavailable, but documentation must label them
   hardware-unverified. Optional capabilities unavailable on a profile are
   discovered at runtime and disabled without preventing the common baseline
   game path.

The active support floor remains FW 3.20. FW 1.x, FW 2.x, and FW 3.00 remain
archival unless that policy is explicitly changed with matching evidence and
hardware need.

## Authoritative Product Roadmap

This section is the authoritative forward plan. The detailed qualification,
reverse-engineering, and historical phase records below remain evidence, but
they do not override this order when an older paragraph names a different
"next" task.

The product direction is:

> Keep `sceAgc*` and `sceAgcDriver*` compatibility as the low-level
> foundation, and add a safe native OpenAGC device and pipeline API above it.

Do not require ordinary applications to assemble PM4, select cache-control
bits, allocate one direct-memory object per resource, or branch on firmware.
Keep the low-level builders public for ABI compatibility, diagnostics, and
expert use, but make the native API the recommended application surface.

### Roadmap corrections and ordering rules

1. **The one-binary portability gate is closed.** The pinned neutral ELF with
   SHA-256 `e04004fee2254e6169805f153ce4812197726ed5f53a9295a4493f0d8ac9a9ce`
   passed twice as identical bytes on both FW 5.50 and FW 11.60 after their
   cleanup stress prerequisites. Preserve this exact artifact as regression
   evidence. Milestones 1 through 3 are complete; Milestone 4 resource-state
   transitions and synchronization is now the active product task.
2. **Do not reopen completed format work.** All R/RG/RGBA16
   UNORM/SNORM/UINT/SINT tuples, all six 32-bit UINT/SINT color tuples, all 14
   BC1-BC7 sampling encodings, the planned D16/D32/S8/HTILE progression, and
   the planned 4x MSAA endpoint matrix are already host- and endpoint-qualified
   as recorded below. Preserve them in regression coverage. The remaining
   format track is tiled BC layout and copies, additional packed/alternate-swap
   formats, color metadata such as DCC/CMASK/FMASK, and further depth/MSAA
   combinations driven by an application requirement.
3. **Build vertical runtime slices before obscure packet breadth.** A resource
   that can be created, bound through a validated pipeline, transitioned,
   submitted, waited, presented, and destroyed safely is more valuable than an
   isolated builder with no application-facing owner. Add a packet builder
   early only when a native-runtime slice, hardware safety fix, or observed
   title import requires it.
4. **Make shader and attachment compatibility an early hard gate.** Reflection
   and pipeline validation must reject incompatible exports, formats, blend
   state, descriptor layouts, sample counts, and stage linkage before command
   emission. This precedes broad state-object or Vulkan feature expansion.
5. **Stabilize the native API before repairing `../Vulkan-PS5`.** Vulkan must
   translate into OpenAGC objects and capabilities; it must not grow a second
   PM4 backend, allocator, firmware selector, or synchronization model.
6. **Documentation and validation are part of every milestone.** Do not defer
   public API reference material, negative tests, or capability labels to the
   final release phase.

### Milestone 0: close portability and regression debt — complete

1. Run `cleanup_stress_fw550` from a fresh boot and require every launch and
   release result to pass with no residual process or kernel fault.
2. Run the already pinned `agc_portability.elf` bytes twice on FW 5.50. Verify
   the local and uploaded SHA-256 digest, normalized firmware key, selected
   profile, real GPU work, bounded presentation, teardown, and relaunch.
3. Keep the same artifact passing on FW 11.60. Any later runtime change must
   rebuild one new firmware-neutral artifact and qualify those exact bytes on
   both endpoints.
4. Preserve the completed color, BC sampling, depth/HTILE, MSAA, indexed, and
   indirect artifacts as a regression matrix. Re-run an exact artifact when a
   runtime change touches its allocator, transition, shader, queue, or submit
   path; do not rebuild merely to claim endpoint parity.
5. Keep unknown profiles and profile-specific optional capabilities fail
   closed. The FW 11.60 workload carrier remains disabled until new offline
   evidence establishes the missing GPU-side prerequisite.

Exit criteria: one hash-identical application-neutral ELF passes twice on FW
5.50 and FW 11.60, the FW 5.50 cleanup stress gate passes, and the support
matrix distinguishes exact-profile evidence from hardware qualification.

### Milestone 1: freeze the native C API contract — complete

Introduce an installable, C99, firmware-neutral API in a distinct public
header family. Use opaque handles and versioned descriptor structs so internal
layouts can evolve without breaking application ABI. The initial object model
is:

- `AgcDevice` and `AgcQueue`.
- `AgcBuffer`, `AgcImage`, `AgcImageView`, and `AgcSampler`.
- `AgcShader`, `AgcGraphicsPipeline`, and `AgcComputePipeline`.
- `AgcCommandBuffer` and `AgcFence`.

The minimum coherent path must support calls equivalent to:

```c
agcCreateDevice(&device_info, &device);
agcCreateBuffer(device, &buffer_info, &buffer);
agcCreateGraphicsPipeline(device, &pipeline_info, &pipeline);
agcCmdBindPipeline(command_buffer, pipeline);
agcCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
agcQueueSubmit(queue, &submit_info, fence);
```

Define lifecycle and ownership rules before adding convenience functions:

- Parent/child ownership and destroy order.
- Thread-safety and external-synchronization requirements.
- Command-buffer states: initial, recording, executable, pending, and reset.
- Resource use while pending and destruction/deferred-destruction behavior.
- Stable error results for invalid state, unsupported capability, exhausted
  command space, device loss, and timeout.
- A base binary-fence path with create/destroy, status, reset, and finite
  timeout wait operations; waits never default to an infinite deadline.
- Explicit structure-size/version fields and reserved-zero rules.
- Optional application allocation callbacks without adding a dependency.

Add `AgcRuntimeInfo` and `agcGetRuntimeInfo(device, &runtime_info)`. It reports
the full runtime version, normalized ABI key, exact selected profile, caller
AGC version/default-table choice, standard or Trinity hardware family,
capability bits, and a per-capability qualification class. Qualification must
distinguish host-tested, SPRX/profile-qualified hardware-unverified, and exact-
firmware hardware-qualified. Applications branch on capabilities, never on a
firmware number.

Internally, device creation owns selection of the submit and queue ABIs,
suspend/workload carriers, register defaults, register-shadow policy, VideoOut
patch policy, memory-region configuration, and optional features. Unknown keys
or unavailable required capabilities fail before mutating GPU or process
state.

Exit criteria: generic tests create and destroy every object, reject invalid
lifecycle transitions, and record one compute plus one indexed graphics
submission; the same public headers build unchanged for generic and Prospero;
the existing low-level ABI remains source- and binary-compatible.

Completed on 2026-07-30. `include/openagc/runtime.h` freezes the 64-bit v1
descriptor ABI with opaque handles, exact structure-size/version checks,
reserved-zero rules, optional allocation callbacks, explicit ownership, four
command-buffer states, finite-only binary-fence waits, stable native errors,
and capability plus qualification reporting. Device creation owns exact
backend/profile/default selection and fails unknown profiles closed.

The generic suite creates and destroys every object, covers invalid lifecycle
and dependency order, records and submits a compute ACB plus indexed graphics
DCB, and locks atomic command-space failure. The complete suite passes 12,398
assertions and all six CTest suites; ASan/UBSan also pass. Generic and Prospero
build the same headers and source without warnings, an installed-package
consumer compiles and runs, and symbol comparison removes no pre-existing
low-level public symbol. Native Prospero queue submission remains deliberately
fail-closed until Milestone 3 reflection can emit a complete validated hardware
pipeline bind; that hardware promotion is not part of this host-qualified
contract exit criterion. See `docs/native_runtime.md`.

### Milestone 2: resource creation and memory management — complete

Build reusable allocators above flexible/onion and garlic/direct memory while
retaining explicit heap properties. Do not expose raw allocation policy as a
firmware choice to the application.

1. Add heap blocks and alignment-aware suballocation for buffers, images,
   shader code, descriptors, command storage, render targets, depth/stencil,
   and scanout-capable images.
2. Centralize overflow-safe buffer and image layout computation, including
   mip chains, arrays, cube faces, BC block dimensions, sample counts,
   metadata planes, row/slice pitch, padding, and alignment.
3. Add persistently mapped upload staging and bounded readback staging with
   explicit cache/visibility rules.
4. Track allocation size, heap, GPU virtual address range, CPU mapping,
   residency, resource owner, and an optional debug name.
5. Add leak and high-water reporting in debug builds.
6. Add fence-keyed deferred frees after the base fence path exists. A pending
   resource must never be returned to a heap early.
7. Keep dedicated allocations available for scanout, unusual alignment,
   oversized resources, diagnostics, and future residency constraints.

Exit criteria: a stress test creates, streams, reuses, and destroys many
buffers and images from a bounded number of direct-memory blocks, reloads the
same asset set repeatedly, detects arithmetic overflow, and returns to the
original allocation count after all fences complete.

Completed with reusable flexible/onion and garlic/direct blocks,
alignment-aware gap reuse, automatic and explicit dedicated allocations,
persistently mapped upload/readback buffers, allocation ownership/debug
queries, leak/high-water statistics, and fence-keyed buffer/image retirement.
Image-view and sampler objects reserve aligned, zero-initialized GPU-visible
descriptor slots; shader, descriptor, upload, and command writes follow
explicit publication rules. Device-scoped layout queries reuse the qualified
gfx1013 BC, tiled depth/HTILE, and 4x color calculators and fail unsupported
profile-specific metadata policy closed.

Stress coverage places 128 buffers plus 16 images in one garlic block, then
creates, streams, destroys, and reloads an identical 32-buffer/eight-image
asset set eight times while proving stable range reuse and one direct-memory
block. It also covers scanout/oversized dedicated allocations, allocation-
record rollback, staging bounds, arithmetic overflow rejection, and both
buffer and image ranges remaining unavailable until their fences complete.
Live allocation and byte counts return to baseline. The complete generic suite
passes 13,614 assertions. See `docs/memory_resources.md`.

### Milestone 3: reflection and validated pipeline objects — complete

Host slice implemented on 2026-07-30: shared reflection ABI/API v15,
serialized-record and hash validation, reflected basic graphics/compute
pipelines, cached qualified bind/dispatch groups, typed descriptor/vertex/push
binding, command-owned resource tables, declared dynamic-state recording, and
a compatibility matrix with transactional negative fixtures. Native depth,
depth-bounds, independent front/back stencil, and dynamic reference/bias state
now package the existing qualified gfx1013 register builders. Direct and
compiler-emitted indirect descriptor-set SGPR layouts both use runtime-owned
tables and fail closed if mixed ambiguously within a shader. Compiler-emitted
NGG and tessellation main/front binary subtype pairs are validated as complete
logical shader bundles. Reflection v2 adds compiler-derived front-stage
interfaces and geometry topology/limit facts without changing its serialized
size; v1/API-14 artifacts remain accepted. The runtime recomputes the
compiler's FNV-1a stage-linkage hash over its four interface masks before
accepting a reflected shader. The full generic suite reports 14,419 passed.
The opt-in combined-tree contract test now compiles real `openagc-psbc`
vertex/fragment/compute output and creates OpenAGC graphics/compute pipelines
without sample-local register knowledge; its 256-byte code alignment and
no-GS NGG front-program patching are validated on the host.
For file-based consumers, `openagc-psbc --reflection-header` now emits the
matching pointer-free reflection sidecar, so a sample can use compiler facts
without reconstructing metadata from its shader record.
The generated `fill_color_native` compute binary/reflection pair is consumed
by the generic runtime-contract test and the separate
`agc_runtime_compute.elf` probe, which uses only native objects, reflected
descriptor/push binding, a bounded fence wait, and readback verification.
The existing manually assembled `agc_compute.elf` was the hardware-qualified
baseline. The public runtime artifact
`52a1e82a75cafe5b7541f130e862ae6cf4813ecedd460dd7017408ef2a254775`
now also passed reflected dispatch, bounded fence wait, readback, reset, and
full teardown on exact FW 5.50.
Runtime API v3 now adds typed `AgcColorTargetBinding` command recording for
reflected graphics exports. It validates exact image/attachment format and
sample agreement, matching dimensions, qualified target-base alignment, and
array/mip subresource layout before transactionally emitting the existing
gfx1013 color-target packets; targets are retained until command reset. Host
coverage rejects mismatched target counts and dimensions before retaining either
image, then locks both MRT base-register packets and lifetime release. A target
also needs an explicit `color-target`/graphics transition in the recording
command buffer; that state gate runs only after all target definitions validate,
so it cannot mask an atomic MRT mismatch. Artifact
`7f86dc3346e70212ce3469380639b0a4e49b8372a08dbd4a5624b9448a103429` passed
the public two-target graphics probe on exact FW 5.50; see
`analysis/runtime_color_target_state_gate_fw550_20260731.md`. This is not a
render-pass abstraction: clear/load/store remain open.
The native graphics counterpart now compiles a position/color triangle into a
real NGG main/front pair and a two-export fragment sidecar, consumes those
exact artifacts in a generic two-color-target/depth-target/vertex/index/
dynamic-state submission contract, and builds `agc_runtime_graphics.elf`.
The standalone probe prefills its two linear RGBA8 targets through the native
image-transfer API, then requires post-fence MRT readback to replace the
sentinel over matching coverage with distinct outputs. Its runtime graphics
bind starts with the exact 2,184-dword V8 default sequence before pipeline
state; the host capture fixture locks that prefix and the probe reserves 4,096
dwords.
The changed artifact `e7c3cb908910e28ea1ee1d9c3db0a887d45bd1d9e84e356cf6c8a159167d2941`
then passed once on exact FW 5.50: both MRT targets overwrote exactly 1,152
sentinel pixels with distinct outputs after the bounded fence wait, and every
runtime object reset/destroy call returned `AGC_OK`.
`agcGetRuntimeInfo` keeps native runtime capabilities host-tested until a
public runtime artifact passes its own exact-firmware oracle; hardware evidence
for an underlying direct carrier does not promote an unrun runtime slice.
The immutable graphics bind writes `SPI_SHADER_COL_FORMAT` from the validated
pixel-export reflection after the compiler register block. Exact host captures
cover the compiler-derived two-RGBA8 value `0x44` and a synthetic two-RGBA16F
fixture's `0x99`, so an accepted runtime pipeline does not depend on a possibly
stale compiler-record context value.
The host compatibility matrix now crosses every exposed color attachment format
with FP16/32-bit float, UINT, and SINT exports. It also proves `DEFAULT`,
`32_R`, and `32_GR` exports fail closed until native R/RG color-target formats
are part of a qualified runtime slice.
Dual-source reflection and `SRC1_*` blend factors also fail closed: the native
reflection ABI does not yet define a secondary-export-index contract, so the
low-level dual-source builder is not exposed through an ambiguous runtime API.
Runtime API v4 adds the equivalent typed depth/stencil binding. A graphics draw
with declared depth/stencil state now fails before emission until a matching
single-mip `AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT` image binds; the runtime derives
its qualified depth-surface packet and retains the image through reset. Generic
coverage locks format rejection, required-target gating, captured depth
registers, and release behavior. The bind now requires an explicit graphics-
owned typed state: `depth-stencil-write` for a pipeline that can write depth or
stencil (including reflected fragment writes), otherwise `depth-stencil-read`.
Artifact `39605323b4a2b5e15596fd3dd034680b97782e683df6b717e88d409edcfa8cc9`
passed the public D16 depth-write row on exact FW 5.50; see
`analysis/runtime_depth_stencil_state_gate_fw550_20260731.md`. Clears/load-
store and packed depth mips remain later work.
Vertex and index binds now require their buffers in graphics-owned
`shader-read` state; that typed state accepts uniform, storage, vertex, and
index buffer usages. Argument and reflected-layout validation precedes the
state check. Artifact
`d99d0ca7a37f4020e27c8ef1117e9d94643b736136c895ae08660f7982d6f9a4` passed
the public upload vertex/index row on exact FW 5.50; see
`analysis/runtime_graphics_input_state_gate_fw550_20260731.md`.
Compiler-fused VS-front/GS-back geometry pipelines are
host-packaged for the already-qualified triangle and line inputs plus compiler
invocation counts; redundant standalone VS handles, incomplete input
primitives, and unqualified point/adjacency forms fail before PM4 emission,
and host submission verifies continuation patching.
Host fixtures instantiate point input/output, line- and triangle-adjacency
input, and point output from triangle input; each is rejected transactionally
with no pipeline handle or command stream.
Compiler-fused VS/TCS plus TES/NGG and TES-to-geometry pipelines are also
host-packaged. They validate complete stage and patch linkage, derive off-chip
layouts from reflection, reuse runtime-owned device rings, require whole-patch
draws, and pass isolated and combined host submission tests.
On Prospero, native runtime submission carries GPU-visible graphics and
compute buffers through the direct DCB carrier and appends a runtime-owned EOP
completion fence before retaining recorded resources as pending. The first
public-runtime FW 5.50 compute attempt used its former user-special-queue ACB
route: submission returned `AGC_OK`, but the EOP fence timed out. The changed
direct-DCB route also returned `AGC_OK` and timed out at that fence.
`agc_runtime_eop.elf` then passed submit, its bounded wait, reset, and complete
teardown on exact FW 5.50. That isolates the remaining failure to the emitted
compute workload state rather than the direct carrier, command allocation,
EOP packet, fence allocation, or CPU visibility. It is not a reflected
compute-pipeline promotion; the next changed diagnostic must isolate the
state emitted before `DISPATCH_DIRECT`.
The first missing state is resolved: `agcCmdDispatch` emits the 174 V8 compute
SH defaults that precede the manually qualified compute dispatch, with
expanded command-space coverage and a clean PS5 cross-build. The changed
artifact `52a1e82a75cafe5b7541f130e862ae6cf4813ecedd460dd7017408ef2a254775`
then passed its exact FW 5.50 public-runtime compute lifecycle: reflected
pipeline dispatch, bounded fence wait, readback verification, reset, and full
teardown. The separate native graphics probe has now also passed its exact
FW 5.50 reflected MRT/depth draw and readback oracle. The remaining
unqualified geometry and fixed-function options stay explicitly fail-closed;
they do not prevent this milestone's validated-pipeline exit criteria.
Every current unsupported rasterization option (line/point polygon mode, depth
clamp, rasterizer discard, and non-unit line width) has a host fixture proving
`AGC_ERROR_NOT_SUPPORTED` and no pipeline output before command emission.

Version `AgcShaderRecord` metadata as a contract shared by `openagc-psbc` and
the runtime. Extend reflection only from compiler- or firmware-backed facts;
do not infer layouts from application convention.

Reflection must describe, where applicable:

- Shader stage, entry point, record version, code hash, and wave size.
- User-SGPR placement and system-SGPR requirements.
- Descriptor/resource table layout and resource classes.
- Push-constant ranges and alignment.
- Vertex inputs and interpolation requirements.
- Pixel-shader color exports, component types, write masks, and required MRTs.
- Scratch, LDS, tessellation, geometry/NGG, and stage-linkage requirements.

`AgcGraphicsPipeline` packages validated shader records, render-target and
depth/stencil formats, blend/raster/depth/stencil state, multisampling, vertex
layout, descriptor layout, dynamic-state mask, and required register state.
`AgcComputePipeline` packages its shader, descriptor/push layout, threadgroup
contract, resource limits, scratch/LDS, and dispatch constraints.

Pipeline creation must fail before PM4 emission for at least:

- FP/UNORM/SNORM export versus UINT/SINT attachment mismatches.
- Missing or extra color exports and unsupported component swaps.
- Blending on integer render targets.
- Depth/stencil or sample-count incompatibility.
- Vertex input, descriptor-table, push-constant, user-SGPR, wave-size, or
  stage-linkage mismatches.
- Unsupported firmware/profile capabilities.

Cache immutable register groups in the pipeline while keeping viewport,
scissor, blend constants, stencil reference, and other declared dynamic state
in the command buffer. Do not make an opaque pipeline hide an unqualified
register sequence.

Exit criteria: host tests cover a compatibility cross-product of shader
exports and attachment types, negative pipeline fixtures emit zero commands,
and hardware samples bind pipelines without sample-local register assembly.

Completed on 2026-07-31. The generic suite covers the compatibility
cross-product and transactional negative cases. The public compute oracle
passed its reflected dispatch/readback lifecycle, and public graphics oracle
`e7c3cb908910e28ea1ee1d9c3db0a887d45bd1d9e84e356cf6c8a159167d2941` passed
its reflected NGG/MRT/depth draw/readback lifecycle, both on exact FW 5.50
without application PM4 or firmware selection.

### Milestone 4: command recording, resource states, and synchronization

Give every buffer and image subresource an explicit usage state:

- Undefined/discard.
- Copy source or destination.
- Shader read or shader write.
- Color-render target.
- Depth/stencil read or write.
- VideoOut scanout.
- Host read or host write.

Expose a typed transition request using source/destination usage, queue owner,
and subresource range. The implementation derives the qualified gfx1013
`RELEASE_MEM`, `ACQUIRE_MEM`, flush, invalidate, event, and metadata actions.
Applications must not pass raw cache-control words through the native API.
Reject unsupported transitions and ambiguous ownership instead of issuing a
conservative packet sequence whose safety has not been hardware-qualified.

Start with explicit transitions. Optional automatic state tracking may infer
same-command-buffer prior state, but it must remain deterministic and provide
an escape hatch for imported/external resources. Cross-command-buffer state is
committed only after successful submit validation.

Synchronization grows in this order:

1. Harden the base binary fences with structured timeout diagnostics and
   submission ownership.
2. Multiple command buffers and wait/signal lists per submission.
3. GPU waits and signals backed by qualified memory labels/events.
4. Monotonic timeline-style counters with overflow rules.
5. Graphics/compute queue ownership transfers where genuinely distinct queue
   behavior is supported.
6. Fence-driven command allocator reuse and resource retirement.

Never convert a timeout into success or wait forever. Diagnostics should name
the queue, last submission, fence value, firmware profile, and most recent
completed marker without pretending to recover a lost device.

The first synchronization step is implemented as runtime API v5:
`agcGetFenceInfo` reports the last queue/submission identity, command state,
expected and observed marker, timeout count/deadline/result, and profile
snapshot. Generic coverage exercises unsignaled and timeout cases; artifact
`bd8545c05a7683bf4fb0c69e7c925317488ba7fd60e455ef7e1ecf715b477c9d` confirms
the completed public-compute snapshot on exact FW 5.50. Submit wait/signal
lists, timelines, and cross-queue work stay open.

The first submit fan-in path is also established: graphics `AgcSubmitInfo`
batches of 2–63 distinct nonempty command buffers use one recovered direct
kernel frame and one runtime-owned fence. Artifact
`30564bfdd87de4c89e575a03b7456aad57a2ca72af174aa41d1598a20322142b` passed a
two-DCB MRT/readback/reset/teardown oracle on exact FW 5.50. Compute batches
are submitted through the same direct multi-DCB carrier; their label-list
qualification is recorded below.

Runtime API v6 adds the first GPU-side label dependency. A producer records an
EOP release write through `agcCmdSignalGpuLabel`; a consumer records an exact
32-bit `WAIT_REG_MEM` through `agcCmdWaitGpuLabel`. Submission accepts the
consumer only after the matching producer value has submitted; labels retain
their backing word until both command buffers reset.
Artifact `1af09900242e5e0af40c12dfb68bd8ea4fb059bdb85654d969cfff88cb15d016`
passed a producer-then-consumer public compute oracle without a CPU wait
between submissions, followed by bounded-fence readback and full teardown on
exact FW 5.50. Labels now enforce strictly increasing 32-bit timeline points
and reject repeat, decreasing, or wraparound values before PM4 mutation. Submit
wait/signal lists were initially rejected.

Runtime API v9 adds a transactional submit-list path for one command buffer:
typed label waits are inserted before its command body and signals at its tail,
with a full command-storage snapshot restoring bytes/cursor on every rejected
path. The exact FW 5.50 standard-PS5 producer → wait/signal bridge → consumer
chain passed without CPU waits, artifact
`4e3f0e5996e9912a24ac476862c15901c3e4512b3e2fa19ec78df2bebef9d4e2`.
The same transaction now applies to graphics batches: waits prepend the first
DCB and signals append to the final DCB, with both endpoint storages rolling
back together. Generic coverage is complete, and the exact FW 5.50 standard
PS5 two-nonempty-DCB graphics-batch oracle passed a first-DCB wait and
final-DCB signal without CPU waits, artifact
`32112756c2446146758409b1605fa8c55a6385d270f454af2cadcfb4262d054b`.
The equivalent native compute-queue oracle also passed without CPU waits,
artifact `95caaab9277368f06db8907147604e8e8dbc3296189fd80e5e15a37f0d46f9a2`.
Artifact `b4d21c6673d74af2b997e695605018ff4499df4998782fc243f18523e7c7576e`
also passed a reflected compute dispatch in the first DCB, a verified label in
the second, one batch fence, and 64-word readback on exact FW 5.50. FW 11.60,
larger batches, and non-compute workload batch forms remain open.

Runtime API v10 adds `AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT` for an
explicit same-queue, earlier-DCB state dependency. Submission simulates the
recorded transition chain in descriptor order before any command storage or
submit-time command or driver mutation; reversed, missing, and single-DCB
dependencies fail closed,
and state commits only after a successful batch submission. Artifact
`0748f67a4eabb156bbf66f2ee18e0a20d268309ba1e3645a15997a04f09df5f3` passed
the reflected compute `undefined -> shader-write` transition in the first DCB
and dependent `shader-write -> host-read` transition in the second, then batch
fence/readback on exact FW 5.50. Cross-queue dependencies and other firmware
profiles remain open.

Runtime API v11 adds typed native buffer copies. `agcCmdCopyBuffer` rejects
untransitioned, unaligned, out-of-bounds, and overlapping ranges before packet
emission; host coverage verifies the `DMA_DATA` stream, transition validation,
retention, and host-read destination release. Artifact
`6724c1371af5cec112abbd2f60cca34dbd61d4631f8b9dd3f79ceb9e6f9a8822` passed
the public same-queue upload-to-copy-to-readback row (1,024 bytes, 256 matching
words, and one bounded fence) on exact standard PS5 FW 5.50. Multi-packet,
image, graphics-queue, cross-queue, and other firmware rows remain open.
Artifact `83342910f2fd15210f9219796eaccacead441f3bd02b8866d966b54c8a44675d`
then passed the complete same-queue compute-to-copy-to-shader-read slice: a
reflected producer dispatch, dependent `shader-write -> copy-source`, typed
copy, `copy-destination -> shader-read`, reflected consumer dispatch, and
final host readback in one three-DCB batch on exact standard PS5 FW 5.50.
Graphics consumers, cross-queue ownership, partial ranges, and other firmware
profiles remain open for buffer copies.

Runtime API v12 adds typed whole-image copies. `agcCmdCopyImage` accepts only
distinct images with identical dimensions, format, samples, mip/layer shape,
and computed allocation layout in explicit `copy-source`/`copy-destination`
state on one queue. It preflights every split DMA packet and resource retain
before emission; partial subresources and layout conversion fail closed. Host
coverage locks incompatible-layout rejection, retention, and a three-packet
four-megabyte copy. Exact artifact
`29110963a218ac7e5de2fc5073c23d5373e7eaa1365ccb3e2b6cf26fe1f85046`
passed twice on standard PS5 FW 5.50 with exact 256-word readback and complete
teardown; see `analysis/runtime_image_copy_fw550_20260731.md`.

Descriptor tables now fail closed before command emission unless their
resources have an explicit compatible typed state on the recording queue.
Read-only descriptor types require `shader-read`; storage descriptors accept
`shader-read` or `shader-write` pending per-binding access metadata in the
reflection contract. Generic negative coverage and the exact FW 5.50 reflected
compute-copy-shader workload passed the gate; see
`analysis/runtime_descriptor_state_gate_fw550_20260731.md`.

Runtime API v8 adds the first typed queue-ownership handoff: a v2 transition
releases a whole GPU-written resource on its source queue and writes the
caller-provided monotonic label value, while the matching destination-side v2
transition waits for that submitted value then emits the qualified acquire.
The source state remains committed until release submit; destination state is
published only by successful acquire submit. Generic fixtures cover premature
acquire rejection, exact dependency, reset reservation, and final destination
ownership. The exact FW 5.50 standard-PS5 whole-buffer compute `shader-write`
to graphics `shader-read` row passed without a CPU wait, artifact
`a8becfe1cf68a988c997fe506849bf549365a7ff6c472efe7b2504e6e2c41797`.
Generic coverage also passes a whole-image graphics `color-target` to compute
`shader-read` handoff, including the source EOP signal, destination wait plus
invalidate, pending-resource destruction guard, and final compute ownership.
The complementary whole-image compute `shader-write` to graphics `shader-read`
carrier passed without a CPU wait on exact FW 5.50, artifact
`70f15e0a5687e431f532d384f5ffb1062b4883bb99746dcaf3857e3dfc5cf7fd`.
The graphics-to-compute image carrier is now endpoint-qualified by the real
consumer row below; FW 11.60 and the remaining hardware resource-handoff rows
remain open.

The graphics-to-compute row now reaches an actual reflected consumer in the
generic contract. A combined image/sampler descriptor rejects the released but
unacquired image, accepts it only after the exact destination acquire, and
records the compute dispatch after the wait/invalidate stream. The compiler-
produced consumer copies a 64x64 RGBA8 image into a storage buffer. Exact
artifact `48a6bd30c5fdcf417be79859e9e3549ec3f3d495b2ec78b97ea192f487e96ea1`
passed twice on standard PS5 FW 5.50: a real MRT draw released its first target
to compute without a CPU wait, the reflected consumer copied all 4,096 pixels,
readback matched byte-for-byte, and teardown completed. See
`analysis/runtime_render_to_shader_fw550_20260731.md`.

Runtime API v13 now provides the host-tested opaque presentation boundary:
two to 16 dedicated scanout images are validated and retained by an
`AgcPresentChain`, and `agcPresent` requires Graphics-owned
`VideoOutScanout` state plus a finite readiness fence before the bounded flip.
Generic coverage locks registration ownership, wait rejection, initial
scanout, and `scanout -> color-target -> scanout -> present`. The first exact
FW 5.50 one-buffer registration rejected safely. A two-buffer combined probe
then stopped returning and made the console services
unreachable, so it is not qualification evidence and must not be rerun.
Resume only through the replacement five-stage guarded ladder after reboot;
see `analysis/runtime_present_attempt_fw550_20260731.md`.

Runtime API v14 closes the deferred-retirement contract gap exposed by the
exit audit. Submitted buffer/image references may now enter fence-keyed
retirement, while actual allocation reuse still waits for both fence completion
and command/dependency release. A 32-cycle generic stress test submits two
typed DCBs per cycle, queues both referenced resources, verifies collection is
busy before command reset, then proves deferred count, allocation count, and
live bytes return exactly to baseline. The matching Prospero artifact
`030fc66604db48d217eb7c4b140c16880516419ae82cd76ac829b9168fa47f1f`
is ready but hardware qualification awaits console recovery. See
`analysis/runtime_batch_deferred_retirement_host_20260731.md`.

Runtime API v15 completes the exact low-level host transition matrix and fixes
the runtime discard contract. All 100 before/after pairs now have computed and
emitted dword-count coverage plus exact release/NOP/acquire packet-shape gates.
`Undefined` is accepted as a zero-packet discard destination, including for a
prior writer, because discarded contents require neither writeback nor
invalidation. A public runtime `HostRead -> Undefined` command records,
submits, commits, resets, and destroys cleanly. See
`analysis/runtime_transition_matrix_host_20260731.md`.

Exit criteria: exact host fixtures cover the supported transition matrix and
atomic short-buffer failure; FW 5.50 and FW 11.60 gates cover render-to-shader,
compute-to-copy, copy-to-shader, host-read, and present-to-render; repeated
multi-command-buffer submission and deferred destruction complete without
leaks, stale ownership, or unbounded waits.

The initial whole-buffer compute row is established on exact FW 5.50: public
runtime artifact `ab8852e9161c0f6ed1c373bc6de047bb9831df0d7cc7bc3df6d247baf549af31`
records `undefined -> shader-write -> host-read` around real shader execution,
then passes bounded-fence readback and teardown. This is only one row of the
exit matrix. Artifact
`8cd97b0b26d568c92870047d65698bd71fe31b72c162c7ca1a62c59d159bf643` also
passes the two-image `undefined -> color-target -> host-read` MRT row on exact
FW 5.50. Depth/stencil, copy/scanout, FW 11.60, and multi-command
synchronization remain open.

### Milestone 5: validation, diagnostics, capture, and documentation

Validation begins with Milestone 1 and becomes a separately selectable debug
layer here. It must detect invalid enums and object states, misaligned or out-
of-range GPU addresses, descriptor/reflection mismatches, shader-export and
attachment incompatibility, missing transitions, buffer/image overruns,
integer-target blending, unsupported capabilities, command-buffer exhaustion,
use-after-submit, and premature destruction.

Validation must be deterministic, allocation-aware, and independent of
assertions. It may be compiled out or disabled for release performance, but
public calls must still preserve required safety checks.

Add an optional capture stream with a versioned, endian-defined format. A
capture records:

- Runtime/profile and capability information.
- Object creation records and debug names.
- Resource descriptions and stable capture-local IDs, not raw host pointers.
- Shader record versions and hashes; include shader bytes only with an
  explicit application opt-in.
- Pipeline descriptions, transitions, command-buffer boundaries, PM4 dwords,
  submission order, waits/signals, fence results, and selected readback hashes.

Build a host decoder that prints named packets, fields, register names,
resource references, and validation warnings. Redact or omit process-specific
addresses by default. Captures are diagnostic records, not automatically safe
hardware replays; a future replay tool needs a separate address-remapping and
security design.

Create and maintain application documentation alongside the API:

- A native API overview and object-lifecycle guide.
- Installation/CMake and first-triangle/first-compute tutorials.
- Resource-memory and synchronization guides.
- Shader compilation, reflection, descriptor, and pipeline guides.
- Firmware-neutral capability/qualification semantics.
- Error, timeout, capture, and hardware-debugging guides.
- Generated or manually indexed reference pages for every public native type
  and function, including ownership, thread-safety, return values, and examples.

Exit criteria: intentionally invalid programs produce actionable diagnostics,
a captured reference frame decodes deterministically on the host, public API
examples build from the installed package, and documentation contains no
firmware branch in ordinary application code.

### Milestone 6: build a firmware-neutral reference game

Build a small source-available game-like workload with the native API rather
than another one-feature probe. Keep focused hardware samples for isolation,
but use this program as the integration and longevity gate.

It must exercise:

- Multiple textured meshes with vertex and index buffers.
- BC texture assets, image views, samplers, mip levels, and streaming.
- Depth buffering, alpha blending, and at least two graphics pipelines.
- Uniform/push data and storage buffers.
- A compute pass whose output is consumed by graphics.
- Explicit resource transitions, multiple command buffers, and fences.
- VideoOut acquisition/presentation and a bounded lifecycle reset.
- Repeated level reloads and long-running allocator stability.
- A capture mode and deterministic screenshot/readback hash at known frames.

Pin one ELF and run the identical bytes on FW 5.50 and FW 11.60. The workload
must select behavior only through native capability queries. Optional features
may degrade through documented capability paths, but the baseline scene and
all required validation results must remain the same.

Exit criteria: the same hash-identical ELF completes cold boot, a bounded
long-run test, repeated level reloads, teardown, and relaunch on both endpoint
firmwares with stable memory high-water marks and deterministic frame oracles.

### Milestone 7: rehabilitate `../Vulkan-PS5` above the native runtime

Begin only after Milestones 1-5 are stable and the reference game has exercised
the runtime contracts. First audit `../Vulkan-PS5`; retain useful API-facing
code, but remove or replace duplicated PM4 emission, firmware checks, memory
allocation, transition logic, shader metadata interpretation, and queue/fence
ownership.

Implement and publish an explicit constrained feature profile for common
homebrew needs:

- Instances, physical/logical device discovery, queues, and capability mapping.
- Buffers, images, views, samplers, memory requirements, and binding.
- Shader modules plus graphics and compute pipelines.
- Descriptor set layouts, pools, sets, push constants, and updates.
- Command pools/buffers, render passes or dynamic rendering, and barriers.
- Indexed/indirect draws, dispatch, copies, BC sampling, depth/stencil, and the
  hardware-qualified MSAA subset.
- Fences, semaphores, bounded waits, and swapchain/VideoOut integration.

Map Vulkan formats and access/layout transitions to native OpenAGC validation;
do not accept a Vulkan format merely because an enum value can be translated.
Add focused Vulkan Conformance Test coverage where feasible, but do not claim
Vulkan conformance or a version level until the corresponding required suite
passes and unsupported requirements are reported accurately.

Exit criteria: selected unmodified Vulkan homebrew samples run through the
OpenAGC native runtime on both endpoint firmwares, the Vulkan layer contains no
second hardware backend, and its advertised feature/format matrix is backed by
host and hardware evidence.

### Milestone 8: demand-driven feature and format expansion

Continue the format and packet tracks only where they unlock the reference
game, Vulkan profile, a real homebrew port, or a safety fix. Candidate work is:

1. Tiled BC1-BC7 layout, mip chains, array/cube use, copy, and mip-copy.
2. Additional packed/alternate-swap formats such as RGB10A2 UINT, RGB565,
   RGBA5551, and R9G9B9E5 FLOAT after primary evidence and demand.
3. Remaining depth/stencil sample-count and compressed combinations, each
   isolated before enabling combined modes.
4. Color metadata/compression and MSAA combinations as independent DCC, CMASK,
   and FMASK capabilities rather than implications of a base-format pass.
5. HDR render targets and VideoOut HDR signaling as separate qualifications.
6. Additional packet builders only for a native vertical slice or an observed
   compatibility import with a proven signature and behavior.

Every new tuple or packet retains the existing host, firmware-neutral pinned-
artifact, bounded hardware, two-pass, exact-hash, teardown, and qualification-
label requirements.

### Release gate

The first native-runtime release is ready when a third-party project can use
the installed C headers and CMake targets to compile shaders, create and stream
resources, build validated graphics and compute pipelines, record commands,
transition resources, submit with bounded synchronization, present, capture a
failure, and tear down without private headers or firmware checks. The generic
backend must exercise the same API for deterministic tests, and the same
reference-game ELF must pass on FW 5.50 and FW 11.60.

Publish a versioned native API/ABI policy, shader-record compatibility policy,
feature/format/firmware qualification matrix, migration guide, and semantic
release notes. Keep unsupported behavior explicit; symbol presence, SPRX
evidence, or one successful frame is not a release-quality support claim.

## Qualification and Reverse-Engineering Evidence Ledger

### Firmware-neutral binary portability gate

The direct backend already recognizes 39 exact active ABI keys from FW 3.20
through FW 12.70 and selects them from the runtime version; submit, memory,
queue, primary suspend, TF-ring, HS-offchip, and async carriers have per-key
SPRX evidence. That is necessary but not yet sufficient for a portable game.
The compile-time audit found no production expected-firmware build input. The
39-profile VideoOut ledger and exact runtime table are complete. Register-
default recovery is also complete: the selector is the caller's
`sceAgcInit(version)` argument stored in the runtime record, not an unavailable
hardware choice. Every active key now carries its exact accepted upper bound;
OpenAGC preserves the caller-selected version instead of using that maximum as
a selected policy. FW5.50/V8 and FW11.60/V12 are hardware-qualified.

Execute in this order:

1. **Complete:** audit all installed public-library and application-consumer code for
   compile-time firmware constants, exact-version branches, and hidden
   firmware-specific shader, packet, memory, or VideoOut assumptions.
2. **Complete:** recover register-default selection for every active exact
   profile by proving the `sceAgcInit` caller-argument flow into the runtime
   record, recording each dispatcher bound, and keeping selection distinct
   from that bound in the direct backend. Explicit V0-V12 blob layouts and
   allocation-fit tests cover every accepted caller choice.
3. **Complete:** extract and verify the linear VideoOut registration branch offset and full
   original instruction signature from every active profile's own
   `libSceVideoOut.sprx`, then generate the runtime table used by the core.
4. **Complete:** define the common baseline capability contract a normal game can require.
   Keep workload packets, EOP flip, non-empty HS patch lists, and other narrow
   operations optional and runtime-queryable rather than allowing them to make
   the baseline binary firmware-specific.
5. **Complete:** build one unpinned portability payload. It prints the detected full
   version and selected four-digit key, but it must not be compiled with
   `AGC_EXPECT_FIRMWARE_ABI_KEY` or link a firmware SPRX.
6. **Complete:** the pinned ELF passed twice on FW11.60 and the exact same
   bytes passed twice on FW5.50, with the authenticated cleanup payload
   immediately before every launch and file-backed bounded verdicts.
7. **Complete:** preserve the same artifact for future intermediate-firmware testing. Until
   matching hardware exists, run corpus verifiers and host fixtures for every
   exact profile and report those rows as SPRX-qualified/hardware-unverified.
8. **Complete:** recover the Sony indirect-draw public ABI across all 39
   active profiles, select the sole FW 3.20 initiator difference by exact
   runtime key, and lock modifier, count-address, GetSize, cursor, and
   short-buffer behavior with fixtures. The application-facing indirect
   compositor now defaults to the Sony 10-dword multi form for both single and
   multiple draws. Fixed-count non-indexed and indexed forms each passed twice
   on FW11.60 through cleanup-first bounded gates. Non-indexed and indexed
   `draw_count=2` plus GPU count-buffer selection each subsequently passed
   twice with distinct second-draw geometry. Repeat all current paths on
   FW5.50 when that console is available.
9. Resume higher-level parity work only with firmware-neutral artifacts so
   each new game-facing capability strengthens the one-binary contract.

The FW 11.60 public VideoOut lifecycle and flexible-memory relaunch stress are
already hardware-qualified supporting components. They do not by themselves
prove binary portability because their existing qualification payloads contain
test-only expected-firmware macros and have not run as identical bytes on both
endpoint consoles.

The neutral target is now `samples/hw_test/agc_portability.elf`, pinned as
SHA-256 `e04004fee2254e6169805f153ce4812197726ed5f53a9295a4493f0d8ac9a9ce`
before hardware execution. It contains no expected-firmware macro or AGC SPRX
dependency and uses the common V7 caller ABI. The exact bytes passed 2/2 on
standard FW `0x11600005` and 2/2 on standard FW `0x05500008`, including live
GPU execution, two flips, teardown, and relaunch on both endpoints. See
`analysis/firmware_neutral_portability_elf_20260730.md`.

The offline endpoint audit is complete. Full nonzero-suffix raw versions now
exercise normalization, exact selection, and common-V7 acceptance for all 39
profiles; the clean host suite passes 6,454 assertions. All relevant SPRX
ledgers reproduce from `/Volumes/Untitled/unp`, and
`tools/verify_fw550_fw1160_compatibility.py` locks every shared layout and
classified endpoint difference. The FW5.50 target now uses only preserved
inputs: it authenticates the payload, cleanup, and kernel-only firmware probe,
kills a stale renderer, rejects any console key other than `0x0550`, then runs
cleanup immediately before each payload. No rebuild is part of the remaining
gate. See `analysis/fw550_fw1160_offline_portability_audit_20260730.md`.

### Existing FW 11.60 versus FW 5.50 capability work

The complete FW 11.60-versus-FW 5.50 capability inventory and required gate
order are maintained in
`analysis/fw1160_fw550_parity_matrix_20260730.md`. On 2026-07-30 the audited
FW 5.50 mirrors for buffer copy, draw variants, NGG, tessellation, and
TES-to-NGG geometry all passed and reproduced the FW 11.60 hashes. Color and
uncompressed-depth mirrors remain pending; do not advance to HTILE or MSAA
until those baselines pass from one pinned shader-compiler revision.

The first FW 5.50 D32 mirror then kernel-panicked after 11 successful headless
graphics launches. Those launches had accumulated about 219 MiB because the
sample never released its command and graphics-pool flexible mappings before
`SIGKILL`. Graphics and compute teardown now owns those releases and fails the
gate on a release error; depth shader regeneration is explicit rather than an
implicit build side effect. Start with the fresh-boot cleanup stress gate in
`analysis/fw550_headless_flexible_memory_panic_20260730.md`. Do not run depth,
HTILE, or MSAA until it passes. The committed `cleanup_stress_fw550` target
runs 14 file-backed launches and checks every release result; FW 5.50 hardware
is currently unavailable, so this gate and all dependent work remain pending.
The `cleanup_stress_fw1160` twin passed 14/14 launches after its canary, proving
the runner and shared teardown on FW 11.60 only.

### FW 11.60 workload parity gate

- Keep the public workload capability disabled while testing isolated causes
  of the standard-console inline `SET_WORKLOAD` stall.
- Stage 14 completed once and still stalled after its ordinary preflight
  marker passed. The complete 40-dword flush and reset timer therefore rule
  out cache coherency as the missing requirement; do not repeat it.
- Stage 15 completed once and reproduced the stall after its exact 2 MiB
  aperture, two descriptors, and Gn2/Gn3/Gn4 publications all returned
  `AGC_OK`; do not repeat it. The ordinary preflight marker still completed in
  50 ms, so the recovered constructor state is not sufficient.
- Full FW 5.50/FW 11.60 builder comparison found no semantic difference in
  Sony's prefix-plus-nine-dword form. Stage 16 is now built as an isolated
  counterfactual using the separate three-dword direct form actually proven on
  FW 5.50, with the qualified defaults/async/preflight sequence unchanged.
  Stage 16 was run once: its ordinary marker completed in 50 ms and the exact
  `0xc0011e80`/`0xc0011e84` DCB submitted, but both following markers remained
  zero for 5 seconds. Cleanup removed PID 109. Do not repeat it; both known
  workload packet forms now fail on FW 11.60. Keep the public capability fail
  closed.
- Full-constructor tracing recovered one concrete state difference that stages
  11-15 missed: Sony fills the `GpuInfo + 0x3a000` slot table with `0xff`,
  seeds its first 16 bytes from an all-ones `0xc010813b` request, and leaves
  zero there only when that request fails. OpenAGC had zeroed all `0x200`
  bytes. Stage 17 was run once as stage 15 plus only this exact lifecycle. The
  seed request succeeded with values
  `fff0ffe0/fff0ffe0/ffffffff/ffffffff`, and its ordinary marker completed in
  50 ms, but the inline workload still stalled before either following marker.
  Cleanup removed PID 101. Do not repeat stage 17 unchanged. Recover a new
  GPU-side queue/register prerequisite before another workload gate.
- After a successful FW 11.60 candidate passes twice, rerun the corresponding
  direct path on FW 5.50 before enabling any capability. Corpus extraction
  now proves the
  standard Gn2/Gn3/Gn4 constructor state on every exact active key from 6.00
  through 12.70 and the reduced Gn2-only Trinity branch from 9.00 onward. This
  is ABI evidence, not workload qualification; keep untested firmware gated.
- Evidence and artifact hashes are recorded in
  `analysis/fw1160_register_shadow_20260729.md` and
  `analysis/agc_driver_shadow_facts.md`. The stage-16 boundary and artifact
  hash are in `analysis/fw1160_workload_stage16_plan_20260729.md`; the corrected
  slot lifecycle and stage-17 artifact are in
  `analysis/fw1160_workload_stage17_plan_20260730.md`.

### FW 11.60 graphics and compute parity gates

- The headless graphics baseline passed twice on standard FW `0x11600005`:
  exact profile, Wave32 NGG/PS audit, indexed draw, bounded fence, RGBA16F
  readback, clean shutdown, and no residual process. Baseline graphics is now
  hardware-qualified for FW 11.60.
- The headless compute artifact is audited, rebuilt, and does not call any
  workload API. It uses the exact `0x1160` standard profile,
  version-12 defaults, async setup, bounded completion/readback oracles, clean
  shutdown, and forced self-termination.
- Headless compute subsequently passed twice: completion fences at 2 ms and
  1 ms, exact 2,073,600/2,073,600 shader output, clean shutdown, and no
  residual process. Baseline Wave32 compute is hardware-qualified for standard
  FW 11.60.
- Workload stages 11-17 are closed failed gates and remain independent of the
  now-qualified graphics and compute paths. Require new offline evidence before
  another workload payload.
- The first advanced graphics gates were headless `R16_FLOAT`, then
  `RG16_FLOAT`. Both reuse the qualified baseline draw and differ only in the
  typed color-target tuple and native readback width. Exact FW 11.60 artifacts
  and target-specific guarded runner support are built. Both formats passed
  twice on standard FW `0x11600005`, with reproducible native hashes, 1-4 ms
  fences, clean shutdowns, and no residual process. Build and run matching
  modern headless FW 5.50 artifacts before promotion. See
  `analysis/fw1160_narrow_fp16_gate_audit_20260730.md`.
- The next unqualified 16-bit tuple, `R16_UNORM`, is now implemented as an
  appended public typed format and a single firmware-neutral headless ELF.
  The exact SHA-256
  `c0a5ad4732bf13c41f96560cb2dbfa3c39dffb9a47958ccbd2bef5754523220a`
  passed twice on standard FW `0x11600005`, reproducing 255,217 validated
  pixels, the full native `0x0000..0xffff` conversion range, and FNV64
  `0x4f17d5e6b1c0d45b`. The guarded runner validates both local and uploaded
  bytes and creates the neutral result directory before cleanup-first launch.
  Replay those exact bytes on FW 5.50 when available; all other active
  profiles remain SPRX-qualified/hardware-unverified. See
  `analysis/fw1160_r16_unorm_portable_qualification_20260730.md`.
- `RG16_UNORM` is now the second firmware-neutral UNORM16 tuple. Its exact
  SHA-256 `d004a33d1d1245964b08ee22b577948d36537c68d9d8c5241ba9e78e4a39f2fd`
  passed twice on standard FW `0x11600005`. Both lanes independently
  reproduced full `0x0000..0xffff` ranges, bounded coverage, eight-or-more
  values, and distinct hashes; the packed native FNV64 was
  `0xf0866450a3c42b45`. The exact R16, RG16, base portability, and three
  10-dword indirect artifacts now live under hash-named local `pinned/` paths.
  Their FW 5.50 replay targets have no build prerequisites, so later endpoint
  qualification cannot silently recompile different bytes. The three neutral
  indirect artifacts also passed twice each on FW 11.60. See
  `analysis/fw1160_rg16_unorm_and_endpoint_replay_20260730.md`.
- `RGBA16_UNORM` completes the first firmware-neutral UNORM16 group. The
  appended tuple uses gfx1013 `16_16_16_16`, UNORM, standard swap, eight bytes
  per pixel, and the FP16_ABGR export. Exact SHA-256
  `13ca0dfaa743438301ecbe5d5255c0168bb89a80bf3ff0e68cdeae8a34908c88`
  passed twice on standard FW `0x11600005`: 255,744 pixels in exact `768x665`
  bounds, four independently validated near-full-range lanes, pairwise-
  distinct lane hashes, packed FNV64 `0xbad47fbdb2e3991e`, immediate fences,
  and zero-valued teardown. Preserve those exact bytes for FW 5.50 replay.
  See `analysis/fw1160_rgba16_unorm_portable_qualification_20260730.md`.
- `R16_SNORM` is the first hardware-qualified SNORM tuple. It is append-only:
  gfx1013 `16`, SNORM, standard swap, two bytes per pixel, and FP16_ABGR
  export. Exact PM4, all-profile selection, 64-bit layout limits,
  invalid-enum behavior, and every 0-27-dword short-buffer boundary pass on
  the host. Its firmware-neutral hardware gate now uses a reusable signed
  four-lane fragment fixture and a sentinel-safe `int16_t` oracle with signed
  endpoint, diversity, coverage, independence, and raw-hash checks. The final
  firmware-neutral ELF was pinned before execution as SHA-256
  `e6aea5164b215d401244ebec13ace8e8ab95fe9e15a8e82d9a59310cfc09e1ef`;
  the identical bytes passed twice on standard FW `0x11600005`, reproducing
  signed range `-32751..32719`, 255,744 pixels, FNV64
  `0x3908f13005165ed7`, immediate fences, and zero-valued teardown. FW 5.50
  replay remains pending. See
  `analysis/fw1160_r16_snorm_portable_qualification_20260730.md`.
- `RG16_SNORM` is now host-implemented as append-only value 18 with gfx1013
  `16_16`, SNORM, standard swap, four bytes per pixel, and FP16_ABGR export.
  Exact PM4, all-profile selection, every short-buffer boundary, and 64-bit
  layout limits pass. Its firmware-neutral two-lane gate builds with the
  already-qualified signed shader/oracle. The final firmware-neutral ELF is
  pinned before execution as SHA-256
  `cc545a61e7b6689f63a905651acbee900acb52306ea0e732a664f8fe5e662352`.
  The identical bytes passed twice on standard FW `0x11600005`: both lanes
  independently reached the signed endpoints, reproduced distinct hashes,
  and produced packed FNV64 `0x4600d1f630de5ed7`, with immediate fences and
  zero-valued teardown. FW 5.50 replay remains pending.
- `RGBA16_SNORM` is host-implemented as append-only value 19 with gfx1013
  `16_16_16_16`, SNORM, standard swap, eight bytes per pixel, and FP16_ABGR
  export. Its exact PM4, all-profile, 0-27-dword, and 64-bit layout fixtures
  pass. Its four-lane firmware-neutral ELF is pinned before execution as
  SHA-256 `85ce21feba113b77b757dcf21f3292b9fb673e27707d72473bde258ca894748d`.
  The identical bytes passed twice on standard FW `0x11600005`: all four
  lanes independently reached signed endpoints, all pairwise lane hashes
  differed, and packed FNV64 reproduced as `0xd3b5d7c030de5ed7`, with
  immediate fences and zero-valued teardown. FW 5.50 replay remains pending.
  See `analysis/fw1160_rg_rgba16_snorm_portable_qualification_20260730.md`.
- `R16_UINT` is host-qualified as append-only value 20 with gfx1013 `16`,
  UINT, standard swap, two bytes per pixel, and UINT16_ABGR export 7. Exact
  PM4 (`CB_COLOR0_INFO=0x00070408`), every short-buffer boundary, all-profile
  selection, invalid-enum behavior, and maximum 64-bit layout arithmetic pass.
  The local Mesa/ACO evidence confirms the packed unsigned export contract.
  Sibling psbc commit `7706efb` now selects and verifies UINT16_ABGR. A
  dedicated coordinate-derived unsigned shader and exact per-pixel native
  oracle build as a firmware-neutral R16_UINT ELF. The unexecuted final bytes
  are pinned as SHA-256
  `aabefd4d05f8d7ea7f56f917ae79c23f60eccf801627f06a053451e74ae8bf18`;
  those identical bytes passed twice on FW `0x11600005`, reproducing exact
  `0x0000..0xffff`, zero mismatches, FNV64 `0x95703f620261e483`, immediate
  fences, and clean teardown. Exact FW 5.50 replay remains pending. See
  `analysis/fw1160_r16_uint_portable_qualification_20260730.md`.
  The retired first artifact proved GPU execution but exposed a stale psbc
  8-bit clamp; sibling commit `c624c5c` fixes it. See
  `analysis/fw1160_r16_uint_first_attempt_20260730.md`.
- `RG16_UINT` is host-qualified as append-only value 21 with gfx1013 `16_16`,
  UINT, standard swap, four bytes per pixel, and UINT16_ABGR export 7. Exact
  PM4 (`CB_COLOR0_INFO=0x00070414`), all-profile selection, every short-buffer
  boundary, invalid-enum behavior, and maximum 64-bit layout arithmetic pass.
  Its portable two-lane gate builds with exact coordinate-derived values,
  full-range/diversity checks, pairwise channel independence, deterministic
  per-lane and packed hashes, and firmware-neutral verification. Pin its final
  bytes before execution. The final unexecuted ELF is pinned as SHA-256
  `4195cc77045496d589aa846ec256116477fefb0d7b4cc5cd890155951cca596b`.
  The identical bytes passed twice on FW `0x11600005`: both lanes reproduced
  exact `0x0000..0xffff` ranges with zero mismatches, distinct hashes, packed
  FNV64 `0xb4bccb0f2909e483`, immediate fences, and clean teardown. Exact FW
  5.50 replay remains pending. See
  `analysis/fw1160_rg16_uint_portable_qualification_20260730.md`.
- `RGBA16_UINT` is host-qualified as append-only value 22 with gfx1013
  `16_16_16_16`, UINT, standard swap, eight bytes per pixel, and UINT16_ABGR
  export 7. Exact PM4 (`CB_COLOR0_INFO=0x00070430`), all-profile selection,
  every short-buffer boundary, invalid-enum behavior, and maximum 64-bit
  layout arithmetic pass. Its isolated four-lane portable gate now builds
  with the integer fragment shader and passes firmware-neutral dependency
  verification. The final unexecuted ELF is pinned as SHA-256
  `22bd65d4b3f0aec4659685d01b100b4e83617710c6fb01c82f04cd13f0a89a84`.
  Those identical bytes passed twice on FW `0x11600005`: all four lanes
  reproduced exact `0x0000..0xffff` ranges with zero mismatches, independent
  hashes, packed FNV64 `0x2aab55d32909e483`, immediate fences, and clean
  teardown. Exact FW 5.50 replay remains pending. See
  `analysis/fw1160_rgba16_uint_portable_qualification_20260730.md`.
- `R16_SINT` is host-qualified as append-only value 23 with gfx1013 `16`,
  SINT, standard swap, two bytes per pixel, and SINT16_ABGR export 8. Exact
  PM4 (`CB_COLOR0_INFO=0x00070508`), all-profile selection, every
  short-buffer boundary, invalid-enum behavior, and maximum 64-bit layout
  arithmetic pass. Its dedicated `ivec4` shader uses SINT16_ABGR export 8 and
  produces exact coordinate-derived values spanning `-32768..32767`. The
  isolated portable gate builds and passes firmware-neutral dependency
  verification. The final unexecuted ELF is pinned as SHA-256
  `3083e2f6ce3ff30d22508c93e51b58bb73f7a36e9ba5e2d255c3c32be7e79652`.
  Those identical bytes passed twice on FW `0x11600005`: 255,744 exact
  samples spanned the signed range with zero mismatches, deterministic FNV64
  `0x055e15e74e22e483`, immediate fences, and clean teardown. Exact FW 5.50
  replay remains pending. See
  `analysis/fw1160_r16_sint_portable_qualification_20260730.md`.
- `RG16_SINT` is host-qualified as append-only value 24 with gfx1013 `16_16`,
  SINT, standard swap, four bytes per pixel, and SINT16_ABGR export 8. Exact
  PM4 (`CB_COLOR0_INFO=0x00070514`), all-profile selection, every
  short-buffer boundary, invalid-enum behavior, and maximum 64-bit layout
  arithmetic pass. The portable gate now exercises two exact signed lanes,
  signed range/diversity, independent hashes, and the packed hash, and passes
  firmware-neutral dependency verification. The final unexecuted ELF is
  pinned as SHA-256
  `6b91d8e21d8a47e4fd3a529c4f5637c46fab42ba22a62d8bbaed6c74befeaac0`.
  Those identical bytes passed twice on FW `0x11600005`: both lanes spanned
  `-32768..32767` with zero exact mismatches, independent hashes, packed
  FNV64 `0xd283b845c78ce483`, immediate fences, and clean teardown. Exact FW
  5.50 replay remains pending. See
  `analysis/fw1160_rg16_sint_portable_qualification_20260730.md`.
- `RGBA16_SINT` is host-qualified as append-only value 25 with gfx1013
  `16_16_16_16`, SINT, standard swap, eight bytes per pixel, and
  SINT16_ABGR export 8. Exact PM4 (`CB_COLOR0_INFO=0x00070530`), all-profile
  selection, every short-buffer boundary, invalid-enum behavior, and maximum
  64-bit layout arithmetic pass. The portable gate now exercises four exact
  signed lanes, signed range/diversity, all six pairwise independence checks,
  and the packed hash, and passes firmware-neutral dependency verification.
  The final unexecuted ELF is pinned as SHA-256
  `b9bdb9641c22bfedfb9367fe60ba97baaef84af191d0326001c2e8285afbef34`.
  Those identical bytes passed twice on FW `0x11600005`: all four lanes
  spanned `-32768..32767` with zero exact mismatches, six passing independence
  comparisons, packed FNV64 `0x0a12ca15c78ce483`, immediate fences, and clean
  teardown. Exact FW 5.50 replay remains pending. See
  `analysis/fw1160_rgba16_sint_portable_qualification_20260730.md`.
- Seven additional offscreen format gates now build under the same exact
  profile and bounded runner: R8, RG8, RGB10A2, R11G11B10, R32, RG32, and
  RGBA32. All seven passed twice on FW 11.60; logged RG32 reproduced FNV64
  `0x806171be9908c276` and logged RGBA32 reproduced
  `0x1e8771ed63381dce`, with zero invalid samples and clean shutdowns.
  RGBA8/BGRA8 UNORM
  and sRGB variants now have real headless flexible-memory targets, unchanged
  native oracles, exact FW 11.60 artifacts, and exact FW 5.50 regression
  mirrors. Run them after RG32/RGBA32 on the clean boot. See
  `analysis/fw1160_color_format_gate_matrix_20260730.md` and
  `analysis/fw1160_rgba8_srgb_headless_gate_audit_20260730.md`.
  FW 11.60 websrv stopped returning foreground stdout even though the payload
  self-terminated cleanly, then stopped entering `main`. An exact logged RG32
  daemon-loader probe passed the full GPU, readback, shutdown, and final
  verdict. File-backed variants and a stale-proof FTP polling runner now use
  that headless-only loader path for RG32, RGBA32, and the four RGBA8 gates;
  use the same logged artifact for both qualifying passes.
  The first RGBA8_UNORM gate found a stale centered-square coverage oracle:
  GPU execution, fence, marker, and shutdown passed, but the current headless
  full-rectangle viewport produced 224,640 pixels rather than the obsolete
  126,293 expectation. The validator now uses rectangular area in headless
  mode and retains the square formula for display fixtures. After that fix,
  RGBA8_UNORM, BGRA8_UNORM, RGBA8_SRGB, and BGRA8_SRGB each passed twice with
  reproducible native hashes, zero sRGB mismatches, clean shutdowns, and no
  residual process. Matching FW 5.50 mirrors remain pending because that
  console is offline.
- The direct-indexed, non-indexed indirect, and indexed-indirect draw variants
  now have exact logged FW 11.60 gates and current-source headless FW 5.50
  mirrors. The shared runner additionally requires the exact intended draw
  path in the verdict, so a generic baseline PASS cannot qualify the wrong
  compile-time variant. All three passed twice on FW `0x11600005`, with
  immediate fences, 255,744 complete FP16 pixels, exact hash
  `0x4a40c2eb4f12bc26`, clean shutdowns, and no residual process. Run the
  current-source FW 5.50 mirrors when that console returns. See
  `analysis/fw1160_indexed_indirect_gate_plan_20260730.md`.
- The next non-tessellation geometry tier is prepared: NGG amplification,
  line topology, and multiple invocations. Exact logged FW 11.60 artifacts and
  current-source headless FW 5.50 mirrors build without warnings. An explicit
  variant identity is now part of each verdict and the runner rejects a
  baseline payload under a variant gate. All three passed twice on FW
  `0x11600005` with immediate fences, exact variant-specific FP16 coverage and
  hashes, clean shutdowns, and no residual process. Prepare isolated
  tessellation next; keep the current-source FW 5.50 mirrors pending. See
  `analysis/fw1160_ngg_geometry_gate_plan_20260730.md`.
- The isolated HS+TES+PS gate is now prepared with exact logged FW 11.60 and
  headless FW 5.50 artifacts. Its runner requires the explicit tessellation
  identity, an `AGC_OK` reusable binder, positive offchip mutation, exactly
  four whole-ring `4.0f` factor values, the ordinary exact FP16 oracle,
  bounded fence, and clean shutdown. An exploratory pair proved that the
  factor-ring slot rotates, so first-word sampling was replaced by a complete
  ring scan. The strengthened artifact then passed twice on FW `0x11600005`:
  immediate fences, offchip mutation `24`, four valid factors, 255,744
  complete pixels, exact hash `0x1754baabb2b216ca`, clean shutdowns, and no
  residual process. Prepare combined TES-to-NGG geometry next; keep the
  current-source FW 5.50 mirror pending.
  `analysis/fw1160_tessellation_gate_plan_20260730.md`.
- The four combined TES-to-NGG gates are now prepared: ordinary geometry,
  invocations, line strip, and direct BGRA8. Exact logged FW 11.60 artifacts
  and current-source FW 5.50 mirrors build without warnings. Each inherits the
  whole-ring four-`4.0f` tessellation oracle, explicit variant identity,
  bounded fence, target-specific readback, and clean shutdown. All four passed
  twice on FW `0x11600005` with exact repeated FP16/BGRA8 hashes, immediate
  fences, offchip mutation `24`, four valid factors, clean shutdowns, and no
  residual process. Keep the current-source FW 5.50 mirrors pending. See
  `analysis/fw1160_tess_geometry_gate_plan_20260730.md`.
- A standalone buffer-copy parity gate now removes the prior SDL consumer's
  non-exact image oracle. It copies the same 8,294,400 bytes through four raw
  gfx1013 `DMA_DATA` packets, waits on an EOP fence, invalidates the
  destination, and requires zero word mismatches plus identical native FNV64
  hashes. Exact logged FW 11.60 and headless FW 5.50 artifacts build without
  warnings. FW 11.60 passed twice with a 38-dword DCB, exact 8,294,400-byte
  transfer, zero mismatches, reproducible hash `0xdd3702089b80f950`, clean
  shutdown, and no residual process. Keep the FW 5.50 mirror pending. See
  `analysis/fw1160_buffer_copy_gate_plan_20260730.md`.
- The first uncompressed depth/stencil tier is also built for exact FW 11.60:
  D32, D16, S8-only, then D16+S8. All four passed twice on standard FW
  `0x11600005`, with exact native distributions, immediate-to-3 ms fences,
  clean shutdowns, and no residual process. D32 was missing from the original
  matrix; its exact logged `0x1160` artifact reproduced the full-rectangle
  color and native-depth oracle twice, and its exact headless `0x0550` mirror
  is built. Matching modern headless FW 5.50 artifacts are built; run D32,
  D16, S8-only, and D16+S8 once before promotion. Do
  not advance to HTILE, expclear, compressed metadata, or MSAA until that
  regression passes.
  See `analysis/fw1160_uncompressed_depth_gate_audit_20260730.md`.
- The FW 5.50 teardown prerequisite is now complete: the corrected-path pinned
  artifact passed one canary and 14/14 threshold launches with immediate
  fences, clean shutdown, and all four release results zero. Run the four
  current-source uncompressed D32/D16/S8/D16+S8 mirrors next. Their guarded
  recipes now require the exact full-rectangle depth and stencil distributions
  rather than relying only on the sample's internal verdict.
- The pinned FW 5.50 uncompressed matrix has now passed D32, D16, S8, and
  D16+S8 with the exact native distributions, clean teardown, and no residual
  process. This closes the endpoint replay for the first depth/stencil tier.
  Advance to ordinary D16 HTILE on FW 5.50; only after it passes may the
  corresponding FW 11.60 ordinary and expclear artifacts run twice.
- The pinned ordinary D16 HTILE artifact passed twice on FW 5.50 with exact
  D16 values and `7408` changed metadata words on both runs. That count is now
  frozen in the runner. Run the exact FW 11.60 ordinary mirror twice next;
  proceed to D16 expclear only if both passes reproduce one another.
- FW 11.60 ordinary D16 HTILE has now also passed twice with the same `7408`
  metadata count and exact D16 values. The ordinary tuple is qualified on both
  endpoints. Run pinned FW 11.60 D16 expclear pass 1 next, freeze its exact
  metadata count, replay once, then run the pinned FW 5.50 mirror.
- FW 11.60 D16 expclear and its pinned FW 5.50 mirror each passed twice with
  exact D16 values and `49152` changed HTILE words on every run. All runs
  completed cleanup-first, shut down cleanly, and left no residual process.
  D16 expclear is qualified on both endpoints. Advance to the already prepared
  D32 HTILE ordinary/decompress/resummarize tier next; keep combined D32+S8,
  subresources, and MSAA behind it.
- The next isolated compressed-depth artifacts are prepared but hardware
  gated: ordinary D16/HTILE first, then D16 HTILE expclear. Exact logged
  `0x1160` artifacts and exact headless `0x0550` mirrors build without
  warnings. The runner requires full-rectangle color/D16 distributions,
  positive metadata mutation, bounded completion, shutdown, and final PASS.
  Do not launch them until the modern FW 5.50 uncompressed depth regressions
  pass. See `analysis/fw1160_d16_htile_gate_plan_20260730.md`.
- The following D32 compressed-depth tier is also prepared offline: bounded
  ordinary/decompress/resummarize and expclear artifacts for exact FW `0x1160`,
  with matching headless FW `0x0550` mirrors. All four build without warnings,
  carry no `libSceAgc.sprx` or `libSceAgcDriver.sprx` dependency, and use the
  cleanup-first depth runner with exact D32 distributions and HTILE mutation
  checks. Two committed-shader relinks reproduced all four exact hashes; the
  artifacts are preserved under those hashes and their deploy recipes reject
  byte drift before network access. The prerequisite sequence is complete.
  Run the pinned FW 5.50 ordinary D32 HTILE mirror next, establish and freeze
  its exact metadata count, and replay once before moving to FW 11.60. See
  `analysis/fw1160_fw550_parity_matrix_20260730.md`.
- The pinned FW 5.50 ordinary D32 HTILE artifact passed twice with exact D32
  classes and `7408` changed metadata words. The second run recovered the
  numeric line hidden by the first run's filtered wrapper output. The recipe
  now freezes `7408`; run one enforcement replay next, then advance to the
  pinned FW 11.60 ordinary mirror if it remains exact and clean.
- The frozen FW 5.50 enforcement replay reproduced `7408`, exact D32 classes,
  clean shutdown, and no residual process. Ordinary D32 HTILE is qualified on
  FW 5.50. Run the pinned FW 11.60 ordinary mirror next, freeze its observed
  metadata count, and replay before beginning D32 expclear.
- FW 11.60 ordinary D32 HTILE pass 1 reproduced `7408`, exact D32 classes,
  immediate completion, clean shutdown, and no residual process. Its guarded
  recipe now freezes `7408`; run the identical artifact once more before
  promoting ordinary D32 HTILE and beginning expclear.
- The identical FW 11.60 replay reproduced `7408` and all exact D32, fence,
  shutdown, and process-cleanup invariants. Ordinary D32 HTILE is qualified on
  both endpoints. Run pinned FW 11.60 D32 expclear pass 1 next, freeze its
  exact metadata count, replay once, then run the pinned FW 5.50 mirror.
- FW 11.60 D32 expclear pass 1 reproduced exact D32 classes and changed
  `49152` HTILE words from `0xfffffff0`, with immediate completion, clean
  shutdown, and no residual process. Its recipe now freezes `49152`; run the
  identical replay next, then permit the FW 5.50 expclear mirror.
- The identical FW 11.60 D32 expclear replay reproduced `49152` and every
  D32, fence, shutdown, and cleanup invariant. Run the pinned FW 5.50 mirror
  next with `49152` frozen. A pass completes the D32 HTILE tier and unlocks
  combined D32+S8 ordinary HTILE.
- The pinned FW 5.50 D32 expclear mirror reproduced `49152`, exact D32
  classes, bounded completion, clean shutdown, and no residual process. D32
  HTILE ordinary/decompress/resummarize and expclear are qualified on both
  endpoints. Pin and qualify combined D32+S8 ordinary HTILE next; run its
  depth-only, stencil-only, and both-aspect expclear gates only afterward.
- Combined D32+S8 HTILE is now prepared offline in the same endpoint-paired
  form. Ordinary HTILE plus depth-only, stencil-only, and both-aspect expclear
  produce eight warning-free ELFs. The guarded runner pins exact D32 and S8
  distributions, HTILE mutation, aspect-specific masked RMW results, reserved
  bits, completion, shutdown, and final PASS; its host oracle rejects a wrong
  aspect. No combined artifact depends on either AGC SPRX. Hardware execution
  remains ordered after the FW 5.50 cleanup and uncompressed/D16/D32 HTILE
  baselines. See `analysis/fw1160_fw550_parity_matrix_20260730.md`.
- The prerequisite D32 tier is complete. All eight combined artifacts now
  reproduce across two committed-shader relinks, avoid both AGC SPRX
  dependencies, are preserved under full hashes, and fail before network
  access on byte drift. Run the pinned FW 5.50 ordinary combined D32+S8 HTILE
  gate next and freeze its exact metadata count before replay.
- FW 5.50 ordinary combined D32+S8 pass 1 reproduced exact D32 and S8
  distributions and changed `49152` HTILE words from `0xfffff30f`, with
  bounded completion, clean shutdown, and no residual process. Its guarded
  recipe now freezes `49152`; replay the identical artifact before FW 11.60.
- The identical FW 5.50 combined replay reproduced `49152` and all exact
  depth, stencil, fence, shutdown, and cleanup invariants. Ordinary combined
  D32+S8 HTILE is qualified on FW 5.50. Run the pinned FW 11.60 ordinary
  mirror next and freeze its observed count before replay.
- FW 11.60 ordinary combined pass 1 reproduced `49152`, the exact D32 and S8
  distributions, immediate completion, clean shutdown, and no residual
  process. Its guarded recipe now freezes `49152`; replay it once before
  beginning aspect-specific expclear.
- The identical FW 11.60 ordinary combined replay reproduced every invariant.
  Ordinary combined D32+S8 HTILE is qualified on both endpoints. Qualify
  depth-only expclear next in FW 11.60 pass-1/freeze/replay then FW 5.50 mirror
  order; only afterward proceed to stencil-only and both-aspect gates.
- The first depth-only attempt exposed a host-oracle bug rather than a GPU
  mismatch: combined expclear intentionally initializes all swizzled D32
  allocation elements, so the exact clear-one count is `1755648`, including
  padding, not the ordinary logical-surface `1617408`. The runner now supports
  a validated explicit D32 count; every combined expclear recipe requires the
  allocation-aware value, and the host fixture accepts it while rejecting the
  ordinary count. Rerun the identical pinned FW 11.60 depth-only artifact.
- The corrected-wrapper FW 11.60 depth-only pass succeeded with aspect `0x1`,
  zero RMW mismatches/outside changes, reserved bits preserved, `49152`
  changed words, exact allocation-aware D32 and S8 distributions, clean
  shutdown, and no residual process. Freeze `49152` and replay once.
- The identical FW 11.60 depth-only replay reproduced every exact invariant.
  Run the pinned FW 5.50 depth-only mirror next with `49152` frozen; a pass
  promotes depth-only expclear and unlocks stencil-only FW 11.60 pass 1.
- The pinned FW 5.50 depth-only mirror reproduced the exact aspect-`0x1` RMW,
  metadata, allocation-aware D32, S8, fence, shutdown, and cleanup oracles.
  Depth-only combined expclear is qualified on both endpoints. Run stencil-only
  FW 11.60 pass 1 next; keep both-aspect blocked.
- FW 11.60 stencil-only pass 1 reproduced aspect `0x2`, expected metadata
  `0xfffff0ff`, zero RMW mismatches/outside changes, `49152` changed words,
  exact allocation-aware D32 and S8 values, clean shutdown, and no residual
  process. Freeze `49152` and replay once before the FW 5.50 mirror.
- The identical FW 11.60 stencil-only replay reproduced every exact invariant.
  Run the pinned FW 5.50 stencil-only mirror next with `49152` frozen; keep
  both-aspect expclear blocked until that endpoint replay passes.
- The pinned FW 5.50 stencil-only mirror reproduced every aspect-`0x2`,
  metadata, allocation-aware D32, S8, fence, shutdown, and cleanup invariant.
  Stencil-only combined expclear is qualified on both endpoints. Run pinned FW
  11.60 both-aspect pass 1 next, then freeze/replay and mirror it on FW 5.50.
- FW 11.60 both-aspect pass 1 reproduced aspect `0x3`, expected metadata
  `0xfffc00f0`, zero RMW mismatches/outside changes, `49152` changed words,
  exact allocation-aware D32 and S8 values, clean shutdown, and no residual
  process. Freeze `49152` and replay once before the FW 5.50 mirror.
- The identical FW 11.60 both-aspect replay reproduced every exact invariant.
  Run the pinned FW 5.50 both-aspect mirror next with `49152` frozen. A pass
  completes combined D32+S8 HTILE and advances the plan to mip/array HTILE.
- The pinned FW 5.50 both-aspect mirror reproduced every exact aspect-`0x3`,
  metadata, allocation-aware D32, S8, fence, shutdown, and cleanup invariant.
  The complete combined tier is qualified on both endpoints. Add current-
  source firmware-keyed headless mip and array artifacts, guarded deploy
  targets, and exact selected-versus-outside metadata oracles next; do not
  reuse the historical FW 5.50 VideoOut ELFs as cross-firmware evidence.
- Exact-key headless mip-1 and array-layer-1 artifacts now build for both
  endpoints. The runner requires positive selected metadata mutation, zero
  outside mutation, optional frozen selected counts, and exact color coverage;
  the host fixture rejects wrong selected, outside, and color values. Relink
  all four artifacts twice, audit dependencies, preserve and hash-pin them,
  then add guarded deploy targets before hardware execution.
- All four subresource artifacts reproduce across two relinks, avoid both AGC
  SPRX dependencies, are preserved under full hashes, and have cleanup-first
  hash-pinned deploy targets. Mip requires exact `56832/56832` color coverage;
  array requires `228096/228096`; both require positive selected and zero
  outside metadata mutation. Run FW 5.50 mip first and freeze its selected
  count before replay.
- The first FW 5.50 mip attempt passed GPU execution, changed `7982` selected
  words and zero outside, shut down cleanly, and left no process, but the
  wrapper rejected the historical VideoOut color count. Current headless
  full-rectangle coverage is exactly `56832/56832`. Both endpoint recipes now
  require that value. Rerun the same pinned FW 5.50 artifact before freezing
  its selected count.
- The corrected FW 5.50 mip run passed all bounded oracles and left no process.
  The identical artifact's observed selected count is `7982`, with zero
  outside changes, so the guarded recipe now freezes `7982`. Run one
  enforcement replay before the FW 11.60 mip mirror.
- The frozen FW 5.50 mip replay reproduced `7982`, zero outside change, exact
  `56832/56832` color, clean shutdown, and no residual process. Current-source
  mip isolation is qualified on FW 5.50. Run the pinned FW 11.60 mip artifact
  next and freeze its selected count before replay.
- FW 11.60 mip pass 1 reproduced `7982`, zero outside change, exact
  `56832/56832` color, immediate completion, clean shutdown, and no residual
  process. Its guarded recipe now freezes `7982`; replay once before array
  isolation.
- The identical FW 11.60 mip replay reproduced every exact invariant. Mip-1
  HTILE isolation is qualified on both endpoints. Run the pinned FW 5.50
  array-layer-1 artifact next and freeze its selected count before replay.
- The pinned FW 5.50 array artifact passed; an identical cleanup-first count
  capture reported `32281` selected words, zero outside change, exact
  `228096/228096` color, clean shutdown, and no residual process. Its recipe
  now freezes `32281`; run one enforcement replay before FW 11.60.
- The frozen FW 5.50 array replay reproduced `32281`, zero outside change,
  exact `228096/228096` color, clean shutdown, and no residual process. Array-
  layer-1 isolation is qualified on FW 5.50. Run the pinned FW 11.60 array
  artifact next and freeze its selected count before replay.
- FW 11.60 array pass 1 reproduced `32281`, zero outside change, exact
  `228096/228096` color, immediate completion, clean shutdown, and no residual
  process. Its guarded recipe now freezes `32281`; replay once to complete the
  HTILE subresource tier.
- The identical FW 11.60 array replay reproduced every exact invariant.
  Current-source mip-1 and array-layer-1 isolation are qualified on both
  endpoints. Audit the historical 4x MSAA path, then build current-source
  exact-key headless endpoint gates with sample, resolve, fence, and cleanup
  oracles before hardware execution.
- Exact-key headless 4x MSAA artifacts now build warning-free for both
  endpoints. The runner requires a 4x shader-resolve verdict, the exact MSAA
  result line, positive resolved color/native D32 classes, and supports frozen
  exact color and D32 class counts; host coverage rejects missing resolve and
  wrong class counts. Relink both artifacts twice, audit dependencies,
  preserve and hash-pin them, then add guarded deploy targets before hardware.
- Both 4x MSAA artifacts reproduce across two committed-shader relinks, avoid
  AGC SPRX dependencies, are preserved under full hashes, and have cleanup-
  first hash-pinned deploy targets. Run FW 5.50 first, capture exact resolved
  green/red and native D32 one/near/far counts, freeze them, and replay before
  FW 11.60.
- The first FW 5.50 workload completed successfully but revealed that the host
  runner rejected a standalone `Depth+4xMSAA` label before its MSAA-specific
  checks. The fixed runner has host regression coverage; repeat the pinned FW
  5.50 artifact with its observed exact color `227610/227610` and D32
  `6469632/912384/912384` counts frozen to establish the first qualification
  pass.
- FW 5.50 corrected-gate pass 1 matched the frozen oracle and left no residual
  process. Replay the identical pinned bytes once more before moving to FW
  11.60.
- FW 5.50 pass 2 reproduced all exact 4x MSAA evidence and clean teardown, so
  that endpoint is qualified. Run the pinned FW 11.60 artifact, freeze its
  exact counts, and replay it once.
- FW 11.60 pass 1 matched FW 5.50's exact color and D32 counts and left no
  residual process. Replay its identical pinned bytes with those counts now
  frozen in the gate.
- FW 11.60 pass 2 reproduced every frozen oracle and clean teardown. Isolated
  D32+4x RGBA8 resolve is qualified on both endpoints. Audit and prepare the
  remaining OpenAGC sample-rate-shading gate; do not promote historical Vulkan
  evidence as OpenAGC qualification.
- Full-4x and partial-2x OpenAGC sample-rate gates are now host-covered,
  warning-free, dependency-audited, reproducible across two relinks, preserved
  under full hashes, and exposed only through cleanup-first pinned deploy
  targets. Run FW 5.50 full first, freeze exact per-sample/total counts, replay,
  then repeat that sequence for partial before moving to FW 11.60.
- FW 5.50 full-rate pass 1 produced exact samples
  `2529216,2530368,2529984,2529600` and total `10119168`, with intact guards,
  unchanged base oracles, and no residual process. Replay those frozen counts.
- FW 5.50 full-rate pass 2 reproduced every frozen oracle and clean exit; that
  mode is qualified on this endpoint. Run the pinned partial-2x artifact next.
- FW 5.50 partial-rate pass 1 produced exact tuple `0,0,0,0,5061792`, intact
  guards, unchanged base oracles, and no residual process. Replay it once.
- FW 5.50 partial-rate pass 2 reproduced every exact oracle. Both sample-rate
  modes are qualified there. Run the pinned FW 11.60 full-4x artifact next.
- FW 11.60 full-rate pass 1 exactly matched FW 5.50's per-sample and total
  tuple, guards, base oracles, and clean lifecycle. Replay the frozen tuple.
- FW 11.60 full-rate pass 2 reproduced every exact oracle. Full-4x parity is
  qualified on both endpoints. Run FW 11.60 partial-2x next.
- FW 11.60 partial-rate pass 1 exactly matched FW 5.50's zero counters and
  total `5061792`, with all lifecycle checks passing. Replay the frozen tuple.
- FW 11.60 partial-rate pass 2 reproduced every exact oracle. Isolated 4x
  resolve plus full-4x and partial-2x sample-rate shading are qualified on both
  endpoints; the planned MSAA parity tier is complete.
- Require two identical passes per capability, then rerun the corresponding FW
  5.50 paths before promotion. See
  `analysis/fw1160_graphics_compute_gate_audit_20260729.md` and
  `analysis/fw1160_graphics_qualification_20260730.md` and
  `analysis/fw1160_compute_qualification_20260730.md`. Exact modern headless
  FW 5.50 mirrors for all nine color formats are built and recorded in
  `analysis/fw550_headless_color_regression_matrix_20260730.md`.

### Render-target format expansion through the 128-bit regular-color ceiling

Completion audit on 2026-07-30 closes the planned milestone through BC
sampling, compressed depth/HTILE progression, and MSAA endpoint parity:

- R16/RG16/RGBA16 SNORM passed twice on exact FW 11.60.
- All six 16-bit UINT/SINT tuples passed twice on exact FW 11.60 using
  dedicated integer-output shaders.
- All six 32-bit UINT/SINT tuples passed twice on exact FW 11.60 using
  dedicated integer-output shaders.
- All 14 BC1-BC7 encodings have host-qualified linear layout and exact
  descriptor coverage, and the same hash-pinned direct-upload sampling ELFs
  passed twice on exact FW 5.50 and FW 11.60.
- D16 and D32 HTILE ordinary/decompress/resummarize and expclear, combined
  D32+S8 ordinary plus all three expclear aspect masks, and mip/array HTILE
  isolation are hardware-qualified on both endpoints. The D16 expclear mirror
  was replayed again so every endpoint now has two accepted passes.
- D32+4x RGBA8 resolve and full-4x/partial-2x sample-rate shading passed twice
  on both endpoints.

The clean generic suite reports `12240 passed, 0 failed`; all six CTest suites
pass. Keep tiled BC layout, BC copy/mip-copy, intermediate-firmware hardware
qualification, and unrelated graphics features as separately gated future
work rather than silently widening this completed milestone. See
`analysis/format_depth_msaa_goal_completion_20260730.md`.

Treat 128 bits per uncompressed format element (`RGBA32`, four 32-bit
components) as OpenAGC's regular color-buffer ceiling, then complete the useful
format matrix in increasing hardware-risk order. For a texture this element is
one texel; for a render target it is one color sample. MSAA multiplies the
per-pixel-location storage separately.

This boundary is supported independently by Mesa revision `44e18d3d` and Linux
revision `0ce37745d`: the largest regular-width gfx10.3 `ColorFormat` entry is
`COLOR_32_32_32_32=0x0e`, Mesa maps no wider ordinary color-buffer layout, and
RADV excludes 64-bit-component formats from color attachments. The evidence is
in `../mesa/src/amd/registers/gfx103.json`,
`../mesa/src/amd/common/ac_formats.c`,
`../mesa/src/amd/vulkan/radv_formats.c`, and
`../linux/drivers/gpu/drm/amd/include/navi10_enum.h`. This remains an OpenAGC
API boundary rather than a blanket claim about every special-purpose gfx1013
encoding, and each PS5 tuple still requires firmware evidence and hardware
qualification.

#### 1. Establish the format contract

Keep formats represented by four independent properties:

- Component layout: `16`, `16_16`, `16_16_16_16`, and so on.
- Number type: UNORM, SNORM, UINT, SINT, FLOAT, or SRGB.
- Component swap.
- Pixel size and matching pixel-shader export format.

Public enum values must only be appended so existing binaries retain their
ABI. Add a compile-time assertion ensuring every public format has exactly one
format-table entry.

Preserve the gfx10.3 usage boundaries recovered from Mesa and Linux:

- Plain three-channel `RGB8`, `RGB16`, and `RGB32` are not color-target
  layouts. In particular, Mesa exposes `32_32_32` as buffer-only.
- No format with a 64-bit component is a color attachment.
- Scaled number types are not regular color-buffer formats.
- UINT and SINT color attachments are valid but not blendable.
- Packed formats and component swaps remain distinct tuples even when their
  total element size matches a plain format.

#### 2. Complete 16-bit normalized formats

These are the best next targets because they reuse the already-qualified
16-bit layouts and FP16 pixel-shader export path. Qualify them in this order:

1. `R16_UNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
2. `RG16_UNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
3. `RGBA16_UNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
4. `R16_SNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
5. `RG16_SNORM` — complete on FW 11.60; exact FW 5.50 replay pending.
6. `RGBA16_SNORM` — complete on FW 11.60; exact FW 5.50 replay pending.

For every format, test:

- Exact CB format and number-type fields.
- Correct bytes per pixel.
- Correct shader-export selection.
- Surface pitch, padding, size, and alignment.
- Integer-overflow rejection.
- Short command-buffer behavior.
- Exact emitted PM4 stream.
- Invalid enum rejection.

UNORM hardware readback must prove:

- Expected triangle coverage.
- Multiple distinct values independently in every stored component.
- Values approaching both `0x0000` and `0xffff` independently in every stored
  component.
- Per-component coverage within the expected bounded window and unchanged
  sentinels outside it.
- Reproducible per-component and packed native-memory hashes.

Do not require every covered component to differ from its initialization
sentinel: every 16-bit bit pattern is a legal UNORM value, so interpolation can
legitimately reproduce the sentinel. Qualification must instead combine the
bounded coverage, range, diversity, hash, and, where applicable,
component-independence oracles.

SNORM must similarly demonstrate both negative and positive ranges in every
stored component, with exact native signed interpretation and independently
reproducible component hashes.

#### 3. Add 16-bit integer formats

After normalized formats pass, qualify:

1. `R16_UINT` — complete on FW 11.60; exact FW 5.50 replay pending.
2. `RG16_UINT` — complete on FW 11.60; exact FW 5.50 replay pending.
3. `RGBA16_UINT` — complete on FW 11.60; exact FW 5.50 replay pending.
4. `R16_SINT` — complete on FW 11.60; exact FW 5.50 replay pending.
5. `RG16_SINT` — complete on FW 11.60; exact FW 5.50 replay pending.
6. `RGBA16_SINT` — complete on FW 11.60; exact FW 5.50 replay pending.

The full 16-bit UINT/SINT matrix is hardware-qualified on FW 11.60. Preserve
all six pinned artifacts for identical-byte FW 5.50 replay and proceed to the
32-bit integer matrix.

- `R32_UINT` is host-qualified as append-only value 26 with gfx1013 `32`,
  UINT, standard swap, four bytes per pixel, and `32_R` export 1. Exact PM4
  (`CB_COLOR0_INFO=0x00070410`), all-profile selection, every short-buffer
  boundary, invalid-enum behavior, and maximum 64-bit layout arithmetic pass.
  Its dedicated `uvec4` shader uses `32_R` export 1 and emits exact
  coordinate-derived values spanning `0x00000000..0xffffffff`. The native
  oracle validates exact values, range, diversity, hashes, and bounded
  coverage, and the isolated portable gate passes firmware-neutral dependency
  verification. The final firmware-neutral artifact is pinned as SHA-256
  `d2eb57d8e6f5f664a72b1ddfa7452ceae2b72328e0bd49445e18fc9bb233ff8f`.
  Those exact bytes passed twice on FW 11.60 with 255,744 exact samples, full
  `0x00000000..0xffffffff` range, zero mismatches, and reproducible lane and
  packed hashes. Preserve FW 5.50 replay as pending. Implement `RG32_UINT`
  next.

- `RG32_UINT` is host-qualified as append-only value 27 with gfx1013 `32_32`,
  UINT, standard swap, eight bytes per pixel, and `32_GR` export 2. Exact PM4
  (`CB_COLOR0_INFO=0x0007042c`), all-profile selection, every short-buffer
  boundary, invalid-enum behavior, and maximum 64-bit layout arithmetic pass.
  Its dedicated `32_GR` shader and two-lane exact-value oracle build as a
  firmware-neutral portable gate. The final bytes are pinned as SHA-256
  `da7c0203a288d994986ff37100e3eac5f8cb962fca00a4821c53f54fc9cb9511`.
  Its first launch was inconclusive because the current websrv loader produced
  no fresh verdict; the already-qualified R32_UINT control then failed the
  same way. Reboot and reinject ps5debug-NG before running those exact bytes
  twice. Preserve FW 5.50 replay as pending. See
  `analysis/fw1160_rg32_uint_first_attempt_20260730.md`.

  The required clean-boot retry now passes once with both exact lanes, full
  unsigned range, zero mismatches, independent hashes, packed hash
  `0xe23c2e22716fa113`, clean teardown, and no residual process. Replay the same
  pinned bytes once before advancing to `RGBA32_UINT`.

  The replay reproduced every exact oracle and clean lifecycle. `RG32_UINT` is
  hardware-qualified on FW 11.60; advance to the pinned `RGBA32_UINT` gate.

- `RGBA32_UINT` is host-qualified as append-only value 28 with gfx1013
  `32_32_32_32`, UINT, standard swap, 16 bytes per pixel, and `32_ABGR`
  export 9. Exact PM4 (`CB_COLOR0_INFO=0x00070438`), all-profile selection,
  every short-buffer boundary, invalid-enum behavior, and maximum 64-bit
  layout arithmetic pass. Its dedicated `32_ABGR` shader and four-lane exact
  oracle build as a firmware-neutral portable gate. The gate expands its
  render-target allocation to the required 16 bytes per pixel. Its final bytes
  are pinned as SHA-256
  `81eaf3d07304cd4d1be7ccca8a332f3f40dac8e605be14fcc6ae87c0bcdd1de8`.
  Do not execute it until the FW 11.60 console has rebooted and RG32_UINT passes.

  The prerequisite is now satisfied and pinned RGBA32_UINT pass 1 succeeds:
  all four lanes contain 255,744 exact samples over the full unsigned range,
  zero mismatches, independent hashes, packed FNV64 `0x4d36e6ccd1b3e617`,
  and a clean lifecycle. Replay once before advancing to signed formats.

  The identical replay reproduced every exact oracle. `RGBA32_UINT` is
  hardware-qualified on FW 11.60; proceed to `R32_SINT`.

- `R32_SINT` is host-qualified as append-only value 29 with gfx1013 `32`,
  SINT, standard swap, four bytes per pixel, and `32_R` export 1. Exact PM4
  (`CB_COLOR0_INFO=0x00070510`), all-profile selection, every short-buffer
  boundary, invalid-enum behavior, and maximum 64-bit layout arithmetic pass.
  Its dedicated signed-coordinate shader and exact-value oracle build as a
  firmware-neutral portable gate. The final bytes are pinned as SHA-256
  `048b903713ce1a0b82e0d3dc5c01b37f2ea068da5e2ce6164bc5ae938c02f32f`.
  Keep execution behind the required clean reboot and UINT qualification order.

  The UINT prerequisites are complete and pinned R32_SINT pass 1 succeeds with
  255,744 exact samples spanning the full signed range, zero mismatches, packed
  FNV64 `0x7e0438a1fbf7bf83`, and a clean lifecycle. Replay once.

  The identical replay reproduced every exact oracle. `R32_SINT` is
  hardware-qualified on FW 11.60; proceed to `RG32_SINT`.

- `RG32_SINT` is host-qualified as append-only value 30 with gfx1013 `32_32`,
  SINT, standard swap, eight bytes per pixel, and `32_GR` export 2. Exact PM4
  (`CB_COLOR0_INFO=0x0007052c`), all-profile selection, every short-buffer
  boundary, invalid-enum behavior, and maximum layout arithmetic pass. Build
  and pin its two-lane signed gate offline. The firmware-neutral gate now
  builds with `32_GR` and exact independent-lane checks. Its final bytes are
  pinned as SHA-256
  `ef3871f19d6e706fe428eb5ac5df1af0b5796beb4fd47ff42c9e1a66a02327c4`.

  Pinned RG32_SINT pass 1 succeeds with two exact full-range lanes, zero
  mismatches, independent hashes, packed FNV64 `0x88da0ec4716fa113`, and a
  clean lifecycle. Replay once.

  The identical replay reproduced every exact oracle. `RG32_SINT` is
  hardware-qualified on FW 11.60; proceed to `RGBA32_SINT`.

- `RGBA32_SINT` is host-qualified as append-only value 31 with gfx1013
  `32_32_32_32`, SINT, standard swap, 16 bytes per pixel, and `32_ABGR`
  export 9. Exact PM4 (`CB_COLOR0_INFO=0x00070538`), all-profile selection,
  every short-buffer boundary, invalid-enum behavior, and maximum layout
  arithmetic pass. Its firmware-neutral four-lane signed gate now builds with
  a dedicated `32_ABGR` integer export and fail-closed per-lane oracle. Its
  final bytes are pinned as SHA-256
  `126c9920f8ea85c2d149c62150f40bbed695ef9f102f8ef3ab430df8f09e8f18`.
  Keep endpoint execution behind the clean-boot UINT qualification order.

  The prerequisites are complete and pinned RGBA32_SINT pass 1 succeeds with
  four exact full-range lanes, zero mismatches, independent hashes, packed
  FNV64 `0x7bd3db10d1b3e617`, and a clean lifecycle. Replay once.

  The identical replay reproduced every exact oracle. `RGBA32_SINT` and the
  complete six-tuple 32-bit UINT/SINT matrix are hardware-qualified on FW
  11.60. Preserve endpoint replay separately and begin BC1 sampling.

These formats need dedicated integer-output pixel shaders. Do not qualify them
using a floating-point shader and assume the conversion is correct. Their
oracles must validate exact integer values rather than approximate
interpolation. Integer color targets must be qualified with blending disabled;
attempting to enable blending must fail validation without emitting commands.

#### 4. Complete 32-bit integer formats

The float forms already reach the maximum widths. Add:

- `R32_UINT`, `RG32_UINT`, and `RGBA32_UINT`.
- `R32_SINT`, `RG32_SINT`, and `RGBA32_SINT`.

All six 32-bit UINT/SINT gates are hardware-qualified on FW 11.60 with two
identical-byte passes and independent exact-value oracles for every stored
lane. Exact FW 5.50 replay remains a separate endpoint qualification task.

`RGBA32_*` remains the regular color-buffer maximum:

| Format | Bits per element/sample |
| --- | ---: |
| `R32` | 32 |
| `RG32` | 64 |
| `RGBA32` | 128 |

Surface-layout arithmetic must safely handle 16 bytes per element/color sample
and MSAA multiplication without overflow. Do not add `RGB32_*` render-target tuples:
Mesa marks the 96-bit layout buffer-only. Do not infer `R64_*` color-target
support from its 64-bit element size; RADV explicitly excludes 64-bit-component
formats from color attachments.

Treat fast-clear and auxiliary-compression support as separate capabilities.
Mesa disables CMASK fast clear above 64 bits per element, so an `RGBA32_*`
color-target PASS must not imply CMASK fast-clear qualification. DCC, CMASK,
FMASK, and MSAA combinations require their own bounded gates.

#### 5. Consider packed formats separately

After the regular matrix is stable, evaluate evidence and homebrew demand for:

- `RGB10A2_UINT`.
- Additional packed 16-bit formats such as `RGB565` or `RGBA5551`.
- gfx10.3 `R9G9B9E5_FLOAT`, which Mesa exposes as a regular color-buffer tuple
  only from gfx10.3 onward.
- Alternate component swaps.
- Any firmware-observed special-purpose formats.

Do not add obscure hardware encodings merely to make the enum larger.

#### 6. Qualify block-compressed textures separately

The 128-bit regular-color ceiling applies only to uncompressed color surfaces
and uncompressed texture texels. Block-compressed textures use a separate
storage contract measured in bits per block. For the currently declared BC
formats, OpenAGC's product-scope ceiling is 128 bits per compressed 4x4 block,
not 128 bits per texel:

| Format family | Bytes per 4x4 block | Nominal full-block storage |
| --- | ---: | ---: |
| BC1, BC4 | 8 | 4 bits per pixel |
| BC2, BC3, BC5, BC6, BC7 | 16 | 8 bits per pixel |

Keep block-compressed textures distinct from DCC, CMASK, FMASK, and HTILE.
BC1-BC7 are application-visible sampled texture formats. DCC/CMASK/FMASK and
HTILE are auxiliary GPU metadata for otherwise ordinary color or depth
surfaces and do not change the surface's logical format or bits per pixel.

The Mesa gfx10 resource table
(`../mesa/src/amd/registers/gfx10-rsrc.json`) explicitly contains native image
encodings for BC1 UNORM/SRGB, BC2 UNORM/SRGB, BC3 UNORM/SRGB, BC4 UNORM/SNORM,
BC5 UNORM/SNORM, BC6 UFLOAT/SFLOAT, and BC7 UNORM/SRGB. OpenAGC's corresponding
`kAgcDataFormatBc1` through `kAgcDataFormatBc7` values already exist in the
public texture enum, but register, enum, and descriptor coverage alone are not
PS5 hardware qualification. Before advertising a BC format, implement and test
a complete layout and sampling path with these independent properties:

The descriptor blocker is resolved offline: gfx10.3's image format field is
now validated as the hardware-defined 9-bit field instead of the previous
incorrect 6-bit ceiling. Exact host fixtures lock all native BC1-BC7 resource
encodings (`169..182`) and reject a tenth format bit without modifying the
destination descriptor. The direct-upload linear layout is also host-qualified
for all 14 encodings using the gfx10 AddrLib contract: 4x4 ceiling-divided
blocks, 8- or 16-byte block storage, 256-byte row/base alignment, smallest-mip-
first offsets, independent array/cube slices, and checked 64-bit sizes. Exact
fixtures cover partial blocks, odd-dimension ceiling-shifted mips, 1x1 tails,
arrays, six cube faces, maximum dimensions/layers, and invalid inputs. Tiled BC
layout and GPU sampling remain pending and must not be inferred from this
linear host qualification.

The sampled-image descriptor now has an append-only `mip_level_count` field.
For ordinary images it emits exact gfx10.3 `LAST_LEVEL` and `MAX_MIP` values;
MSAA retains its sample-log2 interpretation. A BC1 5x7, three-mip, two-layer
fixture locks all six populated descriptor dwords and rejects impossible mip
counts atomically. This removes the descriptor-side blocker for explicit-LOD
and array-layer sampling.

- Block width and height.
- Bytes per block.
- Compatible number type, including UNORM, SNORM, SRGB, or the signed/unsigned
  BC6 floating-point interpretation where applicable.
- Tiled mode, row pitch, slice size, alignment, mip offsets, array layers, and
  cube faces. The linear direct-upload subset is complete; tiled layouts remain.
- Component selection and exact texture-descriptor encoding.

Every BC layout must use checked arithmetic and ceiling-divided block counts.
Widths and heights below four texels still occupy at least one complete block.
Tests must cover partial edge blocks, 1x1 through 4x4 mip levels, non-multiple-
of-four dimensions, complete mip chains, arrays, cube faces, maximum accepted
dimensions, and overflow rejection.

Qualify the existing BC families in increasing decoding and oracle risk:

1. BC1 UNORM and SRGB.
2. BC4 UNORM and SNORM.
3. BC2 UNORM and SRGB.
4. BC3 UNORM and SRGB.
5. BC5 UNORM and SNORM.
6. BC7 UNORM and SRGB.
7. BC6 unsigned and signed floating point.

BC1 UNORM/SRGB now have firmware-neutral portable gates built from one
dedicated `sampler2DArray` shader. Each artifact directly uploads a 5x7,
three-mip, two-layer linear BC1 image with exact endpoint/index blocks. The
shader uses point `texelFetch` to select mip 0, mip 1, layer 0, layer 1, and a
partial edge block; the bounded CPU oracle independently decodes every covered
texel, applies the SRGB transfer for the SRGB variant, requires zero mismatches,
records a native FNV64, and fails unless all three selection regions execute.
Both ELFs pass firmware-neutral dependency verification. Preserve and pin their
final bytes before the first clean-boot FW 11.60 attempt. The preserved SHA-256
values are `db3965f2c8da26273b9683794595612c5b2c216b06a6b05ab05bb579a4842aa5`
for BC1 UNORM and
`1206fa93091cc0f12043617d9e3f83b4951ef5f727a3aca9a94af73c61d7353f` for
BC1 SRGB. Their FW 11.60 and FW 5.50 targets have no build prerequisites.

The first BC1 UNORM launch on the post-integer-test boot produced no fresh
file-backed verdict. After a clean reboot, the identical old artifact executed
and safely failed its CPU oracle: the GPU produced the fixed-point BC colors
171/84 while the oracle expected ideal thirds 170/85. The oracle now uses the
standard 43/21 six-bit BC weights for BC1, BC2, and BC3, and the runner surfaces
a completed fatal log instead of waiting for `Graphics result:`. A corrected
weight run then isolated a second fixture error: mip 1 is 2x3, while every BC
shader fetched coordinates through 2x3 inclusive. All mip-1 shader fetches and
CPU expectations are now clamped to the valid `x=0..1`, `y=0..2` extent.
Retry the newly pinned BC1 UNORM bytes twice before advancing.

The corrected BC1 UNORM artifact passed twice on FW 11.60 with identical
results: `224640` changed pixels, `{74880,74880,74880}` mip/layer-region
samples, zero exact mismatches, and native FNV64 `0x611e681989bb483d`.
Fence, shutdown, cleanup, and final verdict passed both times. BC1 UNORM is
hardware-qualified on FW 11.60; qualify BC1 SRGB next.

BC1 SRGB passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` region samples, zero exact mismatches, and FNV64
`0x7ed831bc232c8da1`. Both BC1 variants are hardware-qualified on FW 11.60.
Qualify BC4 UNORM and SNORM next.

BC4 UNORM/SNORM portable gates now build with dedicated scalar sampling
shaders. They reuse the 5x7, three-mip, two-layer direct-upload geometry but
encode independent 8-bit endpoints and 48-bit 3-bit-index payloads, including
both endpoint-order interpolation modes and the special 0/1 or -1/1 entries.
The SNORM shader maps `[-1,1]` to `[0,1]` before the already-qualified RGBA8
target so negative and positive ranges remain observable. The CPU oracle
decodes the block independently, requires exact UNORM output and at most one
stored-byte rounding unit for SNORM, validates all mip/layer regions, and emits
a native hash. The first UNORM run decoded perfectly but exposed a generic
eight-color smoke threshold that was too strict for the intentional six-color
scalar fixture. Compressed formats now use a three-color baseline diversity
floor while retaining their stronger format-specific oracles. Retry the
corrected pinned UNORM artifact twice, then SNORM twice.

Corrected BC4 UNORM passed twice on FW 11.60 with identical `224640` changed
pixels, `{74880,74880,74880}` regions, zero decode mismatches, zero maximum
error, and FNV64 `0x5327e8ad53b3a455`. BC4 UNORM is hardware-qualified on
FW 11.60; qualify BC4 SNORM next.

BC4 SNORM passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, zero decode mismatches, zero maximum error,
and FNV64 `0x16b22a8b52c7ce8d`. Both BC4 variants are hardware-qualified on
FW 11.60. Rebuild and freeze BC2 after the shared diversity-gate change, then
qualify BC2 UNORM/SRGB.

BC2 UNORM passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, alpha range `0..255`, zero exact mismatches,
and FNV64 `0xf3b07b5935bb483d`. BC2 UNORM is hardware-qualified on FW 11.60;
qualify BC2 SRGB next.

BC2 SRGB passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, alpha range `0..255`, zero exact mismatches,
and FNV64 `0x0a8a977e6f2c8da1`. Both BC2 variants are hardware-qualified on
FW 11.60. Rebuild and freeze BC3 next.

BC3 UNORM passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, alpha range `0..255`, zero exact mismatches,
and FNV64 `0xae513a67c9bb483d`. BC3 UNORM is hardware-qualified on FW 11.60;
qualify BC3 SRGB next.

BC3 SRGB passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, alpha range `0..255`, zero exact mismatches,
and FNV64 `0x4cef62aedf2c8da1`. Both BC3 variants are hardware-qualified on
FW 11.60. Rebuild and freeze BC5 next.

BC5 UNORM passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, full R/G `0..255` ranges, zero decode
mismatches/error, channel independence, and FNV64 `0x3bc37aa96460e455`.
BC5 UNORM is hardware-qualified on FW 11.60; qualify BC5 SNORM next.

BC5 SNORM passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, full R/G `0..255` remapped ranges, zero decode
mismatches/error, channel independence, and FNV64 `0xf1464077ada8ce8d`.
Both BC5 variants are hardware-qualified on FW 11.60. Rebuild and freeze BC7
next, followed by BC6.

BC7 UNORM passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, mode counts `{4:205880,6:18760}`, alpha range
`0..255`, zero exact mismatches, channel independence, and FNV64
`0xf46729633292d01b`. BC7 UNORM is hardware-qualified on FW 11.60; qualify
BC7 SRGB next.

BC7 SRGB passed twice on FW 11.60 with identical `224640` changed pixels,
`{74880,74880,74880}` regions, mode counts `{4:205880,6:18760}`, alpha range
`0..255`, zero exact mismatches, channel independence, and FNV64
`0x74a3526f9a3eef65`. Both BC7 variants are hardware-qualified on FW 11.60.
Rebuild and freeze BC6 UFLOAT/SFLOAT next.

BC6 UFLOAT passed twice on FW 11.60 with identical `224640` changed pixels,
fixture counts `{74880,74880,18760,56120}`, decoded range `0..220`, zero
decode mismatches/error, channel independence, and FNV64
`0x6d3c92b92851ab4a`. BC6 UFLOAT is hardware-qualified on FW 11.60; qualify
BC6 SFLOAT next.

BC6 SFLOAT passed twice on FW 11.60 with identical `224640` changed pixels,
fixture counts `{74880,74880,18760,56120}`, decoded range `1..213`, zero
decode mismatches/error, channel independence, and FNV64
`0x6a278a1c5abd3bfc`. Both BC6 variants, and the complete planned BC1-BC7
sampling matrix, are hardware-qualified on exact FW 11.60. Identical-byte FW
5.50 replay remains a separate endpoint qualification; do not infer it from
the FW 11.60 results.

FW 5.50 identical-byte endpoint replay has begun. BC1 UNORM used the same
SHA-256 `db3965f2c8da26273b9683794595612c5b2c216b06a6b05ab05bb579a4842aa5`
artifact twice and reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, zero exact mismatches, and FNV64
`0x611e681989bb483d` on both runs, with clean fence, shutdown, memory cleanup,
and process exit. BC1 UNORM is hardware-qualified on both endpoints; qualify
BC1 SRGB next.

BC1 SRGB on FW 5.50 used the same SHA-256
`1206fa93091cc0f12043617d9e3f83b4951ef5f727a3aca9a94af73c61d7353f`
artifact twice and reproduced `224640` changed pixels, regions
`{74880,74880,74880}`, zero exact mismatches, and FNV64
`0x7ed831bc232c8da1` on both runs. BC1 UNORM and SRGB are hardware-qualified
on both endpoints. Continue in the planned order with BC4 UNORM/SNORM.

BC4 UNORM on FW 5.50 used exact pinned SHA-256
`f74c393112fc465eace431a3fe288095ae4b3bf5ee993e8147ed0e9a2f22f2a4`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
zero decode mismatches/error, and FNV64 `0x5327e8ad53b3a455` on both runs. BC4
UNORM is hardware-qualified on both endpoints; qualify BC4 SNORM next.

BC4 SNORM on FW 5.50 used pinned SHA-256
`022f159f0186aab25222bfd882f9b59b8ab40bdfcf6d9c59da389d057454b28d`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
zero decode mismatches/error, and FNV64 `0x16b22a8b52c7ce8d` on both runs. Both
BC4 variants are hardware-qualified on both endpoints; qualify BC2 next.

BC2 UNORM on FW 5.50 used pinned SHA-256
`e86a53fdb7b3c65cf13dcf66ca0588867d1cb37fab6bbf5b446c654948847b5b`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
alpha range `0..255`, zero exact mismatches, and FNV64
`0xf3b07b5935bb483d` on both runs. BC2 UNORM is hardware-qualified on both
endpoints; qualify BC2 SRGB next.

BC2 SRGB on FW 5.50 used pinned SHA-256
`d182824d912f7473f25beeba80ffc58e3ceb31d8a5b7bff678d2237b24c9c5b8`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
alpha range `0..255`, zero exact mismatches, and FNV64
`0x0a8a977e6f2c8da1` on both runs. Both BC2 variants are hardware-qualified on
both endpoints; qualify BC3 next.

BC3 UNORM on FW 5.50 used pinned SHA-256
`54807cec76c1b1e0d6669d4e72110e1dec55933b76a4f40a5da79098bea0b1af`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
alpha range `0..255`, zero exact mismatches, and FNV64
`0xae513a67c9bb483d` on both runs. BC3 UNORM is hardware-qualified on both
endpoints; qualify BC3 SRGB next.

BC3 SRGB on FW 5.50 used pinned SHA-256
`7a5587e843d43217389b39b86129fa28de83da91d81648b37f4fa13b3fdb2b61`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
alpha range `0..255`, zero exact mismatches, and FNV64
`0x4cef62aedf2c8da1` on both runs. Both BC3 variants are hardware-qualified on
both endpoints; qualify BC5 next.

BC5 UNORM on FW 5.50 used pinned SHA-256
`2ecb276612e06b42e2408e5b5352272493cd2f167eb47bbee79ff4dc6ffebeb7`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
full R/G ranges, zero decode mismatches/error, channel independence, and FNV64
`0x3bc37aa96460e455` on both runs. BC5 UNORM is hardware-qualified on both
endpoints; qualify BC5 SNORM next.

BC5 SNORM on FW 5.50 used pinned SHA-256
`cf6fcaa788fe65fd7b0bb352888dce09be674ddfbbfde2d37faf0ab9cb6a3fe0`
twice and reproduced `224640` changed pixels, regions `{74880,74880,74880}`,
full remapped R/G ranges, zero decode mismatches/error, channel independence,
and FNV64 `0xf1464077ada8ce8d` on both runs. Both BC5 variants are
hardware-qualified on both endpoints; qualify BC7 next.

BC7 UNORM on FW 5.50 used pinned SHA-256
`a98adaa1125c6ab1590d5c3cb1d65e19b0573ccf4f5e5a1ec0b40ad93bb17db6`
twice and reproduced `224640` changed pixels, mode counts
`{4:205880,6:18760}`, alpha range `0..255`, zero exact mismatches, channel
independence, and FNV64 `0xf46729633292d01b` on both runs. BC7 UNORM is
hardware-qualified on both endpoints; qualify BC7 SRGB next.

BC7 SRGB on FW 5.50 used pinned SHA-256
`f6a97569f854ddad7080247c44bec6dd1edccc0b7c248a8b1f8701b276c8eb27`
twice and reproduced `224640` changed pixels, mode counts
`{4:205880,6:18760}`, alpha range `0..255`, zero exact mismatches, channel
independence, and FNV64 `0x74a3526f9a3eef65` on both runs. Both BC7 variants
are hardware-qualified on both endpoints; qualify BC6 next.

BC6 UFLOAT on FW 5.50 used pinned SHA-256
`37c1159701d9615fc015c3f5b887f88f00fe12e694df7ff92f442fb7165c072c`
twice and reproduced `224640` changed pixels, fixture counts
`{74880,74880,18760,56120}`, range `0..220`, zero decode mismatches/error,
channel independence, and FNV64 `0x6d3c92b92851ab4a` on both runs. BC6 UFLOAT
is hardware-qualified on both endpoints; qualify BC6 SFLOAT next.

BC6 SFLOAT on FW 5.50 used pinned SHA-256
`5bdffd1510c11ac7b334d630782fe931b9ca8b3f19ee31ab0b14b861eec32578`
twice and reproduced `224640` changed pixels, fixture counts
`{74880,74880,18760,56120}`, range `1..213`, zero decode mismatches/error,
channel independence, and FNV64 `0x6a278a1c5abd3bfc` on both runs. All 14
planned BC1-BC7 sampling encodings are hardware-qualified twice on exact FW
5.50 and FW 11.60 using identical hash-pinned firmware-neutral ELFs.
The final firmware-neutral bytes are pinned as SHA-256
`f74c393112fc465eace431a3fe288095ae4b3bf5ee993e8147ed0e9a2f22f2a4`
for BC4 UNORM and
`022f159f0186aab25222bfd882f9b59b8ab40bdfcf6d9c59da389d057454b28d`
for BC4 SNORM. Both endpoint targets are no-build guarded replays.

BC2 UNORM/SRGB portable gates now build by reusing the qualified
`sampler2DArray` shader and 5x7, three-mip, two-layer upload geometry. Their
16-byte blocks independently encode the 64-bit explicit-alpha field, RGB565
endpoints, and 2-bit color indices. The bounded CPU oracle decodes color and
alpha independently, applies SRGB conversion only to RGB, requires exact
RGBA8 agreement, proves alpha endpoints 0 and 255, validates all mip/layer
selection regions, and records a native hash. Preserve and pin the final
firmware-neutral bytes before any hardware execution. The exact SHA-256 values
are `e86a53fdb7b3c65cf13dcf66ca0588867d1cb37fab6bbf5b446c654948847b5b`
for BC2 UNORM and
`d182824d912f7473f25beeba80ffc58e3ceb31d8a5b7bff678d2237b24c9c5b8`
for BC2 SRGB. Their guarded endpoint targets have no build prerequisites;
FW 11.60 execution and identical-byte FW 5.50 replay remain pending.

BC3 UNORM/SRGB portable gates now build using the same bounded array/mip
sampling path. Each 16-byte block independently covers two 8-bit alpha
endpoints, the 48-bit 3-bit alpha-index stream, RGB565 color endpoints, and
the 2-bit color-index stream. Fixtures exercise both eight-alpha and
six-alpha-plus-terminal interpolation modes. The independent CPU oracle uses
the qualified BC4 rounding rule for interpolated alpha, applies SRGB only to
RGB, demands exact RGBA8 agreement and the full 0..255 alpha range, validates
all selection regions, and records a native hash. Pin the final neutral bytes
before any hardware attempt. The pinned SHA-256 values are
`54807cec76c1b1e0d6669d4e72110e1dec55933b76a4f40a5da79098bea0b1af`
for BC3 UNORM and
`7a5587e843d43217389b39b86129fa28de83da91d81648b37f4fa13b3fdb2b61`
for BC3 SRGB. Their FW 11.60 and identical-byte FW 5.50 guarded targets have
no build prerequisites.

BC5 UNORM/SNORM portable gates now build with dedicated two-channel shaders.
Each 16-byte block is treated as two independent BC4 streams, with distinct
endpoint ordering and index patterns in R and G. The SNORM shader remaps both
channels from `[-1,1]` to `[0,1]`. The CPU oracle independently decodes each
stream, covers both interpolation modes and terminal values, requires the full
0..255 range in both channels, proves channel independence and all mip/layer
regions, allows only the established one-byte SNORM rounding tolerance, and
records a native hash. The pinned SHA-256 values are
`2ecb276612e06b42e2408e5b5352272493cd2f167eb47bbee79ff4dc6ffebeb7`
for BC5 UNORM and
`cf6fcaa788fe65fd7b0bb352888dce09be674ddfbbfde2d37faf0ab9cb6a3fe0`
for BC5 SNORM. Their FW 11.60 and identical-byte FW 5.50 guarded targets have
no build prerequisites.

BC7 UNORM/SRGB portable gates now build with deterministic mode-4 and mode-6
blocks. Mode 4 covers separate two-bit color and three-bit alpha streams;
mode 6 covers endpoint p-bits and a shared four-bit RGBA stream. Their bit
placement, anchor-index shortening, endpoint expansion, and interpolation
weights were independently checked against Mesa's BPTC decompressor. The CPU
oracle recognizes both modes, applies SRGB only to RGB, requires exact RGBA8
agreement, full alpha range, channel independence, and all mip/layer regions,
and records a native hash. Other BC7 modes remain separate coverage expansion,
not a prerequisite for proving the native BC7 resource path. The pinned
SHA-256 values are
`a98adaa1125c6ab1590d5c3cb1d65e19b0573ccf4f5e5a1ec0b40ad93bb17db6`
for BC7 UNORM and
`f6a97569f854ddad7080247c44bec6dd1edccc0b7c248a8b1f8701b276c8eb27`
for BC7 SRGB. Their FW 11.60 and identical-byte FW 5.50 guarded targets have
no build prerequisites.

BC6 UFLOAT/SFLOAT portable gates now build with dedicated RGB sampling
shaders. Four deterministic mode-3 block classes and their independently
decoded expectations were generated offline with Mesa's BPTC reference
encoder/decoder; the ELFs contain only fixed blocks and bounded tables, not a
Mesa dependency. UFLOAT clamps positive RGB directly, while SFLOAT remaps
`[-1,1]` to `[0,1]`. The oracle requires all fixture classes, mip/layer and
partial-edge selection, broad range, channel independence, alpha one, a native
hash, and no RGB error above two stored-byte units. The pinned SHA-256 values
are
`37c1159701d9615fc015c3f5b887f88f00fe12e694df7ff92f442fb7165c072c`
for BC6 UFLOAT and
`5bdffd1510c11ac7b334d630782fe931b9ca8b3f19ee31ab0b14b861eec32578`
for BC6 SFLOAT. Their FW 11.60 and identical-byte FW 5.50 guarded targets have
no build prerequisites. See
`analysis/bc6_fixture_generation_20260730.md`.

Use dedicated, deterministic source blocks containing endpoint, index,
alpha, signed-range, and edge-block cases appropriate to each format. A
hardware gate must sample the compressed texture into an already-qualified
uncompressed render target, wait on a bounded fence, and validate exact or
format-tolerant decoded texels as appropriate plus a reproducible native
render-target hash. It must also prove mip and layer selection rather than
qualifying only base level zero.

Mesa records a GFX9-through-GFX11.5 limitation for indirect image copies of
BC mip chains. Therefore the first BC gates must use a direct, deterministic
upload followed by shader sampling. Direct image-copy qualification comes
after sampling; indirect BC image-copy and mip-copy behavior is an independent
late gate and must not be inferred from a successful sampling result.

BC formats are sampled-texture formats, not color-render-target formats; do
not route them through the color-target tuple table or assign them a
pixel-shader export format. Mesa's gfx10 native resource-format table contains
BC1-BC7 but no native ASTC entries. Do not add ASTC or another compression
family without primary PS5 firmware evidence and a concrete homebrew
requirement.

#### 7. Use firmware-neutral qualification artifacts

Every new hardware gate must:

- Contain no `AGC_EXPECT_FIRMWARE_ABI_KEY`.
- Have no dynamic dependency on either `libSceAgc.sprx` or
  `libSceAgcDriver.sprx`.
- Detect and normalize the full runtime version.
- Use the selected `/dev/gc` profile.
- Write a bounded, file-backed verdict.
- Shut down and self-terminate.
- Be copied to an immutable hash-named local path before its first hardware
  run, with the runner verifying both the local and uploaded hashes.

Use the identical ELF twice on FW 11.60. Later, run those exact bytes on FW
5.50; never rebuild between endpoint tests. A source-equivalent rebuild does
not satisfy the endpoint-portability gate.

#### 8. Qualification labels

Track three states independently:

- Host-tested.
- SPRX/profile-qualified, hardware-unverified.
- Hardware-qualified on an exact firmware.

A format passing FW 11.60 must not automatically be advertised as
hardware-qualified on FW 5.50 or the other 37 profiles. Identical SPRX/profile
findings strengthen ABI evidence but do not promote a profile to
hardware-qualified status.

#### 9. Guarded hardware sequence

For each new tuple:

1. Confirm websrv and debugger connectivity.
2. Run the process-cleanup ELF.
3. Verify no stale renderer remains.
4. Launch one bounded format gate.
5. Require fence, readback, shutdown, and final verdict.
6. Check for residual processes and kernel faults.
7. Repeat once using the identical ELF.
8. Stop immediately on a timeout, UI stall, reset, or unexpected hash.

#### Completed format milestone and remaining work

The planned regular-color, BC sampling, depth/HTILE, and 4x MSAA milestone is
complete on both endpoint firmwares as summarized at the start of this section.
R16/RG16/RGBA16 UNORM, SNORM, UINT, and SINT and all R32/RG32/RGBA32 UINT/SINT
tuples are regression coverage, not the next implementation goal.

Continue only with the demand-driven gaps named in the authoritative product
roadmap: tiled BC layout/copy/mips, useful packed or alternate-swap formats,
unqualified depth/sample/compression combinations, and independent color
metadata capabilities. Keep exact pinned endpoint artifacts available when a
native runtime change touches these proven paths.

### Gfx1013 multi-viewport state

- Provide one application-neutral typed array for up to 16 Vulkan-style
  viewport transforms and matching per-slot scissors.
- Reapply this state after baseline and tessellation shader binding so shader
  records cannot restore stale viewport registers.
- Exact packet, range, capacity, and draw-order host coverage is complete;
  FW 5.50 shader routing qualification proceeds through Vulkan-PS5.

### Gfx1013 array and cube image descriptors

- Add application-neutral 2D-array and cube-array resource types plus base and
  last accessible layers to the typed SQ image descriptor helper.
- Exact host tests cover full and subrange cube arrays, ordinary 2D arrays,
  and malformed cube face counts.
- Host implementation is complete; FW 5.50 sampling qualification proceeds
  through the Vulkan-PS5 `imageCubeArray` gate.

### Gfx1013 dual-source blend state

- Detect SRC1 factors in the application-neutral blend builder and reject
  their use outside MRT0.
- Disable RB+ dual-quad mode and clear all SX blend optimizations while dual
  source blending is active.
- Exact host tests pass, and two bounded FW 5.50 Vulkan probes passed the
  18,432-pixel green SRC1 readback oracle with clean process exit.
- This OpenAGC portion is complete; higher-level Vulkan feature exposure is
  tracked in Vulkan-PS5.

### Gfx1013 tessellation offchip concurrency gate

- Replaced the tessellation sample's single global HS offchip buffer with an
  Oberon-wide 160-buffer profile: four shader engines, two shader arrays per
  engine, five physical CUs per array, and four workgroups per CU.
- Mesa's gfx10.3 path confirms that `VGT_HS_OFFCHIP_PARAM` encodes the global
  count (`159`), unlike gfx11's per-shader-engine interpretation. Public
  constants and tests preserve the 40-per-engine/160-global distinction.
- The ring-table builder rejects state whose allocation cannot cover its
  encoded `VGT_HS_OFFCHIP_PARAM`, and the hardware sample derives
  non-overlapping pool offsets from the public ring sizes.
- This is the next qualification candidate for Vulkan's nondeterministic
  HS-store/TES-load path. Host tests pass. Two independent bounded Vulkan FW
  5.500.008 runs passed the hull, TES offchip-read, and 7200-pixel image oracles
  (`20260728T043915Z-tessellation-run1.log` and
  `20260728T044035Z-tessellation-run1.log`) and left the console responsive.
  The ring-concurrency correction is hardware-qualified at this scope.

### FW 5.50 application-neutral initialization gate

- Prospero initialization now owns the GPU ucred preparation required before
  `/dev/gc` access. The public ABI and generic backend remain firmware-neutral;
  the FW 5.50 thread-ucred offsets live only in `driver_prospero.c`.
- Higher-level consumers such as Vulkan-PS5 no longer need a sample-only
  credential header. The next gate is repeated FW 5.50 compute and graphics
  execution through foreground websrv.

### Hardware-validated gate: gfx1013 D32 depth surface

- `agcGfx1013GetDepthSurfaceLayout` calculates separate depth and stencil
  plane layouts for gfx1013 `64KB_Z_X`, including pitch, padded height,
  64 KiB alignment, per-layer mip-chain size, full array allocation, macroblock
  dimensions, and the first packed mip-tail level.
- Exact host fixtures cover D32, 4x MSAA arrays, D32 mip tails, split D16/S8
  planes, the largest bindable 64-bit allocation, invalid large dimensions,
  unsupported swizzles, and invalid multisampled mip chains.
- `agc_depth.elf` now consumes the query instead of reserving a hardcoded
  16 MiB depth image.
- FW `0x05500008` hardware passed the isolated D32 gate through curl/websrv:
  all markers, color coverage, raw depth values, and 1,800/1,800 flips passed
  without a hang or kernel panic.

### Hardware-validated: gfx1013 HTILE layout and compressed-depth gate

- `agcGfx1013GetHtileLayout` models non-RB+ gfx1013 pipe-aligned
  `64KB_Z_X` metadata. Address-pipe count remains an explicit input; the FW
  `0x0550` sample uses the hardware-proven eight-pipe layout.
- Exact fixtures cover metadata pitch, padded height, block geometry,
  alignment, slice size, layers, packed mip-tail accounting, multiple pipe
  profiles, the largest bindable 64-bit allocation, and atomic rejection.
- `agcGfx1013SetDepthSurface` explicitly emits
  `DB_HTILE_SURFACE.PIPE_ALIGNED`. The isolated sample initializes depth-only
  metadata to the gfx10.3 uncompressed value `0xfffc000f`.
- FW `0x05500008` passed `agc_depth_htile.elf`: all draw/fence/color checks,
  18,013 changed HTILE words, and 1,800/1,800 flips completed. The screen
  showed the expected green/red triangles on dark gray.
- User-ring `COPY_DATA` reads of privileged `GB_ADDR_CONFIG` are prohibited.
  The attempted diagnostic produced `GPU Bad packet error: Privilege reg` for
  register `0x13de`; FW reset and recovered the graphics rings automatically.

The earlier compressed-D16 deferral is closed for the FW `0x05500008`
gfx1013 profile. Uncompressed D16 passed twice before HTILE was enabled, and
two subsequent D16/HTILE runs independently reproduced exact decompressed D16,
metadata, color, fence, and flip oracles. The bounded evidence, including the
remaining teardown warnings, is recorded in
`analysis/fw550_d16_htile_qualification_20260727.md`.

### Hardware-validated: isolated stencil gate

- `agc_depth_stencil.elf` allocates separate typed D32 and S8 `64KB_Z_X`
  planes and leaves MSAA, HTILE, and expclear disabled.
- The gate uses front-face `ALWAYS` compare, `0xff` compare/write masks, and
  `REPLACE 0x5a` only on depth pass. Exact host fixtures lock the D32/S8
  layouts and the full 14-dword stencil packet stream.
- Completion requires the existing deterministic marker/color/D32 checks plus
  raw S8 containing only zero and the written `0x5a` reference.
- FW `0x05500008` websrv execution passed with 256,608 `0x5a` stencil bytes,
  2,364,832 zero bytes, no unexpected values, all depth/color checks, and
  1,800/1,800 completed flips without a hang or kernel panic.

### Hardware-validated: isolated 4x MSAA gate

- `agc_depth_msaa.elf` binds typed 4x RGBA8 `64KB_R_X` color and D32
  `64KB_Z_X` surfaces while leaving stencil, HTILE, expclear, CMASK, FMASK,
  and DCC disabled.
- The shader resolve transitions the multisample target to shader-read,
  restores 1x raster state, samples all four fragments, and draws into the
  registered VideoOut target. The sample compensates the source `ALT`
  red/blue storage and composites coverage over dark gray.
- Repeated FW `0x05500008` runs accepted the 5,131-dword DCB. All stage and
  completion markers, exact green/red interiors, raw 4x D32 classes, and
  1,800/1,800 flips passed without a hang or kernel panic.
- The captured framebuffer showed green and red triangles with resolved edges
  on dark gray. Black side pillars in the wider capture are outside the
  registered 1920x1080 framebuffer.

This section is the authoritative completion plan. The later phase sections
retain detailed history and evidence; they do not override this order.

### Definition of a finished FW 5.50 core

OpenAGC's FW 5.50 core is release-ready when all of these conditions hold:

1. The library, rather than a hardware sample, owns every PM4 state builder
   required by the hardware-proven Wave32 graphics paths. Samples may retain
   memory allocation, VideoOut presentation, diagnostics, and visual oracles.
2. Exact host fixtures lock the validated shader, render-target, viewport,
   scissor, target-mask, depth-disabled, draw, and synchronization packet
   streams, including cursor advance and atomic short-buffer failure.
3. One revision passes the clean generic build, the complete retained host
   suite, the Prospero build, and the complete FW 5.500.008 websrv hardware
   qualification sequence without a GPU hang or kernel panic.
4. Exact four-digit firmware selection remains fail-closed. The implementation
   never calls absent or permission-only FW 5.50 driver operations as if they
   were supported capabilities.
5. A representative homebrew application outside `samples/hw_test` builds
   against the installed public API and exercises initialization, uploaded
   shaders/resources, drawing, synchronization, and presentation without
   copying qualification-sample PM4 setup. Retail imports are evidence only and
   are never a release-completion metric.
6. Public declarations, calling conventions, firmware-layout static asserts,
   build instructions, deployment instructions, and supported/non-supported
   claims agree with the tested implementation.

This completion definition is for the stable FW 5.50 core, not a claim of full
official SDK parity, all-firmware support, VRS, or ray-tracing completeness.

### Audit of the proposed work

| Item | Status | Current evidence and remaining work |
| --- | --- | --- |
| 1. Harden FW 5.50 graphics and promote sample PM4 | **Complete** | Atomic reusable gfx1013 builders cover render-target, viewport, scissor, target-mask, depth-disabled, V8 defaults, and the baseline bind/index/instance/auto-index draw composition. Exact host fixtures and FW 5.50 RGBA16F/RGBA8 runs pass. |
| 2. Add exact Wave32 and fixed-function host fixtures | **Complete** | Exact fixtures cover the Wave32 VS/PS binder, hardware-proven RT/viewport/scissor/depth/default streams, and the 44-dword baseline draw wrapper, including post-bind overrides and atomic short-buffer rejection. |
| 3. Re-run the complete FW 5.50 websrv suite | **Complete** | Revision `c0633c7` passed the dependency-ordered base, compute, baseline/NGG, tessellation, and combined-stage matrix through curl/websrv. Required three-run repeats were deterministic, every applicable case completed 1,800/1,800 flips, and the sequential run had no hang, panic, or UI crash. |
| 4. Investigate FRAME_OPEN EINVAL and PA-debug EPERM | **Complete** | FW 5.50 `FRAME_OPEN` is absent from the kernel dispatcher. The PA-debug export is a userspace permission stub returning `0x8A6D0001`; neither result is an unresolved graphics blocker. |
| 5. Publish a homebrew-facing example | **Complete** | `samples/triangle` retains the minimal command-recording example. `examples/cube` is a separate installed-package consumer that owns allocation, shader upload, resource tables, triple-buffered frame resources, bounded fences, continuous vertex/index updates, VideoOut presentation, and cleanup. Its staged Prospero install/consumer build passes without repository include or library paths. Two FW `0x05500008` curl/websrv runs presented 3,600 rotating-cube frames and exited cleanly. Retail import audits remain bounded ABI evidence only. |
| 6. Add cross-firmware backend profiles | **Complete for the SPRX-qualified common subset** | All 39 exact active keys from FW 3.20 through FW 12.70 are runtime-selectable for their submit16, internal-memory, authenticated-queue, primary-suspend, public TF-ring, HS-offchip, and async carrier evidence. FW 5.50 and standard-PS5 FW 11.60 are hardware-qualified; other exact profiles remain hardware-unverified, and firmware-specific operations stay independently gated. |

### 1. Promote the hardware-proven graphics state into OpenAGC

Implement reusable gfx1013 state descriptions and atomic emitters for the
state that is still sample-only:

- Linear color targets covering the proven FP16 and RGBA8 formats, including
  base/base-ext, pitch, slice, info, attrib2/attrib3, color control, and target
  mask.
- Aspect-preserving viewport state, depth range, transform control, and full
  target scissor state.
- Depth-disabled color rendering state and the FW 5.50 graphics register
  defaults currently emitted by the sample.
- A baseline draw-state wrapper that composes the existing shader binder with
  primitive type, index type, instance count, and `DRAW_INDEX_AUTO` without
  hiding the lower-level packet builders.

Status: complete. The atomic gfx1013 builders cover the color target (RGBA16F
FLOAT/STD and RGBA8 UNORM/ALT), aspect-preserving viewport, all required
scissors, target mask, depth-disabled state, V8 SH/CX/UC defaults, and the
baseline draw-state wrapper. The wrapper composes the existing VS/PS binder,
primitive and index state, post-bind application overrides, instance count,
and `DRAW_INDEX_AUTO` without removing access to any lower-level builder.
Exact packet fixtures, short-buffer cursor-preservation tests, clean host
tests, and both FW 5.50 websrv hardware paths pass.

Each emitter must validate dimensions, alignment, format, and address range;
preflight its complete dword requirement; emit nothing on failure; and use
`agcPm4Header3*` plus the existing register helpers rather than hand-packed
headers. Public helper declarations use `PS5_SYSV_ABI`; firmware-layout structs
receive size and offset assertions.

Refactor `samples/hw_test/agc_graphics.c` to call these helpers. The sample may
still audit emitted registers, but it must no longer be the only implementation
of required render state. The exit gate is that deleting the sample-local
setup helpers does not change the hardware packet stream.

### 2. Lock exact host packet fixtures

Add golden host fixtures derived from the already validated OpenAGC output,
not copied firmware blobs. Normalize runtime addresses, then compare exact
dwords and cursor advance for:

- Wave32 fused NGG VS/PS binding and Wave32 HS/TES/NGG/PS binding.
- The 1536x1536 linear FP16 target and 1920x1080 linear RGBA8 display target.
- Square aspect-preserving viewports on square and 16:9 targets, depth range,
  full scissor, target mask, and disabled depth state.
- Primitive type, index type, one instance, `DRAW_INDEX_AUTO(3)`, completion
  marker, and the cache/synchronization tail used by the proven draw.
- FW 5.50 graphics-default emission counts and critical register sentinels.

Add negative fixtures for unaligned addresses, zero or excessive dimensions,
unsupported formats, invalid shader metadata, and every short-buffer boundary.
The exit gate is exact equality with the captured proven packet stream plus
zero cursor movement for every rejected state.

### 3. Qualify one revision on FW 5.50 hardware

After sections 1 and 2 land, build one clean revision and run the websrv suite
in dependency order through curl:

1. `videoout_linear.elf`.
2. `agc_init.elf`.
3. `agc_videoout.elf`.
4. `agc_compute.elf`.
5. Baseline `agc_graphics.elf` plus the vertex-fetch, indexed-draw, texture,
   repeated-submit, geometry invocation/amplification/topology, FP16, and RGBA8
   variants.
6. Isolated tessellation, TES-to-NGG geometry, combined invocation-count,
   combined line-strip, and combined RGBA8 variants.

Record the git revision, raw firmware value `0x05500008`, PM4 audit, readback
metrics, completion marker, flip count, and physical-display result for every
case. Run the baseline reusable binder and the combined tessellation path three
consecutive times from identical ELFs. The exit gate is all deterministic
oracles passing, 1,800/1,800 flips where applicable, and no hang, panic, or UI
crash. Use curl/websrv only; do not use `prospero-deploy`.

Qualification runners must wait for a successful foreground HTTP completion
before launching the next ELF. A timeout or disconnected `/hbldr` request is a
hard stop, not permission to overlap another homebrew process. The standalone
VideoOut smoke test patches the FW 5.50 linear-buffer check internally, runs a
finite 600-flip window, cleans up, and exits. The compute test submits and
waits for its display flip before returning. These lifecycle rules prevent a
successful persistent display case from occupying websrv during the next
qualification launch.

Status: complete on revision `c0633c7`, raw firmware `0x05500008`. All 14
qualification ELFs passed their deterministic oracles and physical-display
checks. The baseline binder and combined TES-to-NGG geometry paths each passed
three consecutive runs from identical hashed ELFs. See
`analysis/fw550_qualification_c0633c7.md` for the full matrix, readback metrics,
markers, flip counts, hashes, and lifecycle-incident analysis.

### 4. Preserve the closed FW 5.50 driver-gap results

Do not reopen PA-debug or `FRAME_OPEN` without contradictory firmware evidence.
Convert the conclusions into permanent capability/profile regressions:

- FW 5.50 must not issue `FRAME_OPEN`; its absence is expected behavior.
- `sceAgcDriverGetPaDebugInterfaceVersion` remains a permission-only userspace
  stub result, not a required ioctl or release gate.
- Unsupported optional operations return a stable fail-closed AGC error and do
  not mutate backend state.

### 5. Deliver OpenAGC as a homebrew GPU API

OpenAGC's primary product is a clean, usable GPU API and shader toolchain for
native homebrew applications and games running on jailbroken PS5 hardware. The
FW `0x0550` console in hand is the first qualification target. Compatibility
with official `sceAgc*` / `sceAgcDriver*` entry points remains important, but
retail-title import counts are supporting ABI evidence rather than the product
goal or a release gate.

Work in this order:

1. **Public-API vertical-slice audit.** Inventory the hardware samples and move
   every generally useful shader upload, resource binding, render-target,
   viewport/scissor, draw/dispatch, barrier, synchronization, and queue-submit
   operation out of sample-local raw PM4 into public OpenAGC builders or
   documented low-level escape hatches. VideoOut lifecycle remains platform
   integration rather than AGC command construction.
2. **Graphics and compute application path.** Provide one minimal graphics path
   and one compute path that use installed OpenAGC headers and `libopenagc.a`
   only. The application must not reproduce private register sequences from
   `samples/hw_test`. Cover shader records, GPU memory ownership/alignment,
   Wave32 VS/PS, render targets, viewport/scissor, indexed and non-indexed draw,
   dispatch, cache visibility, suspend points, and error propagation.
3. **Shader toolchain usability.** Make `openagc-psbc` a documented part of the
   homebrew SDK flow: GLSL or SPIR-V to gfx1013 shader records, deterministic
   artifacts, stage/link diagnostics, and examples for compute, VS/PS, NGG
   geometry, and tessellation. Keep PS5 gfx1013 behavior distinct from generic
   gfx1030 assumptions.
4. **SDK packaging.** Install public headers, `libopenagc.a`, CMake package
   metadata, and compiler tooling through one supported workflow. This now
   exports `OpenAGC::openagc`, `OpenAGC::psbc`, a relocatable package config,
   version metadata, `openagc_compile_shader()`, and TGZ generation. Document the
   public API boundary, ownership/lifetime rules, alignment requirements,
   numeric firmware profiles, error codes, raw-PM4 escape hatch, and websrv
   deployment without requiring proprietary SDK headers or firmware blobs.
5. **FW 5.50 conformance suite.** Run the display, initialization, compute,
   graphics, indexed draw, indirect draw, NGG geometry, and tessellation tests
   through curl/websrv. Add bounded waits and fail-closed state validation so a
   malformed builder input cannot submit a kernel-panic-prone packet. Record
   visual expectations and console results separately from host packet tests.
6. **Representative homebrew proof.** Build and run a small application or game
   outside `samples/hw_test` that uses the installed OpenAGC SDK. It must render
   continuously, upload/update resources, compile/load shaders, recover cleanly
   from application errors, and contain no copied sample-private PM4 setup.
   **Complete:** `examples/cube` is an
   independent installed-package consumer with application-owned PS5 memory,
   Wave32 shader upload/fusion, a public descriptor-backed indexed draw,
   three frame slots, two-second fence bounds, per-frame rotating-cube updates,
   finite VideoOut presentation, and teardown. The separate Prospero consumer
   build passes. Two consecutive FW `0x05500008` websrv runs presented all
   3,600 frames, showed the rotating colored cube, and exited cleanly without a
   hang, reset, or panic. Qualification evidence is in
   `analysis/fw550_standalone_cube_qualification_20260727.md`.
7. **Broaden capability after the vertical slice.** Reusable standard-swap
   RGBA8, RGB10A2, and R11G11B10 color targets are now host-tested and FW 5.50
   hardware-qualified, alongside the earlier alternate-swap BGRA8 and RGBA16F
   paths. RGBA8/BGRA8 sRGB encode behavior is also hardware-qualified. Next
   qualify additional 16-bit color tuples. After the color matrix is stable,
   D16, S8-only, D16+S8, compressed D16/HTILE, and D16 HTILE expclear are now
   hardware-qualified. Continue with blending, resource transitions,
   multi-buffer frame scheduling, timestamps/queries, and stable NGG
   geometry/tessellation APIs according to homebrew needs.
8. **Firmware and retail ABI evidence.** Preserve numeric firmware profiles and
   expand below/above FW `0x0550` only after the primary path is mature. Analyze
   retail binaries when they reveal an API contract needed by homebrew; do not
   chase dead imports, guess prototypes, or make a ten-title corpus a release
   requirement. FW `0x0320` remains the lowest active compatibility target;
   FW `0x0100`, 2.x, and `0x0300` remain archival evidence only and are
   rejected by runtime profile selection. FW `0x0320` is the lowest active
   compatibility target.

The sample-only PM4 audit is complete in
`analysis/sample_pm4_public_api_audit.md`. It classifies the exact reusable,
partial, missing, platform-only, and diagnostic operations in the FW 5.50
hardware samples.

The reusable compute vertical slice now has typed `CONTEXT_CONTROL`, gfx1013
compute validation/binding/dispatch state, and FW 5.50 compute-default emission.
`agc_compute.c` uses those APIs plus public `WRITE_DATA`, `ACQUIRE_MEM`, NOP,
and submission calls exclusively; the curl/websrv FW 5.50 run produced
2,073,600/2,073,600 matching pixels and completed the display flip.

The compute sample now places a public `ACQUIRE_MEM` before an ordered
GPU-written completion marker and polls that CPU-visible fence with a bounded
timeout. FW 5.50 reached the fence after 1 ms and retained full pixel coverage.

The public resource API now has byte-exact gfx1013 structured-buffer,
zero-record structured-buffer, byte-bounded raw-buffer, 2D-image, and 64-byte combined image/sampler
descriptor encoders matching the proven
sample layouts. Resource-table binding now resolves compiler placeholders
inside shader records, validates every required table atomically, enforces the
PS5 address32 range, and selects graphics/compute packet type internally. The
baseline draw state owns primitive and pixel resource tables so their resolved
values are emitted after shader placeholders and before draw packets. The VS/PS
state also owns the primitive back-program address and resolves the compiler
continuation placeholder internally.

The graphics sample conversion is complete for command construction. Baseline
and tessellation builds use typed resource encoders, state-owned placeholder
resolution, public context/default/fixed-function/draw calls, public cache and
marker packets, and bounded completion. FW 5.50 validated the baseline fence at
4 ms and tessellation at 9 ms; both passed FP16 validation, and tessellation
produced the expected ring writes. Tessellation ring promotion is now complete:
the public API owns the 128-byte descriptor table, ring sizes/slots, four UC
ring registers, and five post-bind CX registers. The sample-local ring header
and its Makefile dependencies are removed. A repeated FW 5.50 run retained the
9 ms fence, 24 offchip changes, four factor changes, and passing FP16 output.
Each closed goal must update the
documentation, pass clean generic and Prospero builds, pass host fixtures, and
be committed before hardware promotion.

#### Retail ABI evidence retained by the project

Subnautica (`PPSA02453`) content `01.022.394` is now re-audited from the
decrypted executable: all 63 AGC imports pass the strict coverage gate, with 58
direct implementations and five intentional versioned wrappers. The artifact
is pinned by SHA-256 in `analysis/subnautica_ppsa02453_audit.md`. Its metadata
requires system software `0x1120`, so this result proves SDK `0x0400` API
coverage rather than native package launch compatibility on FW `0x0550`.

Dragon Quest VII Reimagined (`PPSA17942`) is the fifth target in progress. It
is hardware-proven on FW `0x0550` and bundles AGC compatibility SPRXs despite
declaring `0x1202`. Its 253 imports currently have 252 covered and 1 unresolved
after completing the FW 5.50 GetSize imports, seven packet patchers, and the
data-packet payload-range, primitive-state update, and constant driver-status
ABIs. Its FW 5.50 workload-stream register/unregister pair and AGR multi-DCB
status path are also covered.
The analyzer merges named compatibility NIDs from the version-variant table,
including the three exact WriteData patch helpers used by the bundled SPRX.
The compatibility cursor ABI for `sceAgcAcbAtomicGds_0900` is also implemented
as an exact 11-dword packet builder.
The three FW 5.50 owner-management exports are covered with their exact
single-owner signatures and firmware `AGC_ERROR_NOT_SUPPORTED` behavior.
The compatibility-only `sceAgcGetIsTrinityMode` ABI is also recovered: it
writes the `sceKernelHasTrinityMode` result through a one-byte output pointer,
and the standard FW `0x0550` PS5 path reports false. The sole Dragon Quest call
site ignores the residual return register and consumes the stored byte.
The executed shader-instrumentation getter/setter and AMM semaphore-memory
path are recovered as well. The latter enforces 16 KiB base/size alignment,
returns 32-byte label records by index, and preserves the firmware's
already-initialized, not-initialized, and out-of-range errors.
The compatibility `ACQUIRE_MEM` engine patch and three async `WRITE_DATA`
patchers are recovered with their exact packet validation, masks, and
`0x8A6C000C` wrong-packet error behavior.
The complete ACB/DCB marker family now matches the cursor-based firmware ABI,
including explicit-length set/push span exports, color words, and distinct
`SET_MARKER`/`PUSH_MARKER` NOP subcommands.
The compatibility GS primitive-payload query scans the shader record's
firmware-counted CX register pairs and returns eight bytes only for register
`0x1C2` mode 2.
The compatibility GS-oversubscription query now reproduces the bundled
SPRX's `GE_PC_ALLOC` and `SPI_SHADER_PGM_RSRC4_GS` occupancy calculation,
including zero-limit defaults, forced-maximum state, shader-register-derived
bounds, and the fourth-argument floating-point interpolation factor.
`sceAgcCbMemsetExclusive` now follows the firmware's compute-dispatch
architecture rather than substituting DMA. It binds an aligned OpenAGC-owned
gfx1013 kernel compiled from `shaders/memset_exclusive.comp`, programs the
hardware-proven ring-offset/push-constant SGPR layout, and emits a 32-dword
Wave32 dispatch sequence. Host packet coverage is complete; execute the kernel
against GPU-visible memory on FW `0x0550` before promoting it to
hardware-validated status.
The five compatibility submit-validation controls reproduce the bundled
driver's unconditional `0x8A6C1000` debug-unavailable stubs without modifying
caller outputs or runtime state.
Eight resource/GDS exports reproduce the FW 5.50 userspace `0x8A6C9018`
status-only behavior with recovered SysV signatures, and capture start, stop,
and trigger reproduce their unconditional `0x8A6C1000` status. These stubs do
not inspect arguments or modify output storage.
`sceAgcDriverFindResourcesPublic` remains unresolved because the firmware body
proves only its constant status, not its public prototype; do not guess that
ABI.
Three otherwise-unknown NIDs (`7Wa3aeJgeVU`, `rP5xLdOf26k`, and
`Ikfdt-rIqCE`) now implement their exact FW 5.50/11.60 `IT_INDIRECT_BUFFER`
field-patcher ABIs. They validate opcode `0x3F`, patch the firmware-specific
dword offsets, preserve reserved bits, and return `0x8A6C000C` without mutation
for the wrong packet. Their NID-derived public labels are intentionally not
presented as recovered Sony names.
Two version-specific builder ABIs are also covered without changing the older
named source-compatible entry points. `-KRzWekV120` emits the exact four-argument
11.60 `SET_INDEX_SIZE` form, including its extra control bit, while
`zARR5aCmkoY` emits the exact 12-argument, 11-dword full atomic-GDS packet.
Both retain NID-derived public labels because matching firmware bodies prove
their packet layouts and SysV signatures but not their official names.
The NID-specific `qj7QZpgr9Uw` context-state transition ABI is recovered as an
exact two-argument builder. Its four operations emit the firmware-sized
`5/27/27/32`-dword sequences composed of `CONTEXT_CONTROL`, `COND_EXEC`,
`RELEASE_MEM`, `ATOMIC_MEM`, clear-state, and optional indirect CX restore
packets. The Prospero backend initializes the firmware-format `{0,1}`
GPU-visible synchronization label and flattened FW 5.50 v8 CX restore list in
`SceGnmMisc`. Host packet fixtures cover every mode and atomic short-buffer
failure; a focused FW `0x0550` hardware run remains required before this path
is labeled hardware-validated.
AcquireMem sizes follow the
firmware title-workaround mode: mode 1 returns 64 bytes and modes 0/2 return 32,
instead of hard-coding the emulator's 32-byte path. The FW 11.60
`dbOlWdppb4o` and `vieBRwlh1Lw` imports now use their recovered three-argument
interpolant-mapping ABI and exact enhanced descriptor transform. The create
variant fills the 32-entry identity tail, while the update variant leaves the
tail untouched. These are CPU-side shader metadata helpers covered by exact
host fixtures; game-runtime validation remains pending. Resolve the sole
remaining import, `sceAgcDriverFindResourcesPublic`, only from a game call site
or independently corroborated public prototype; its constant firmware stub is
not sufficient evidence to guess the ABI.
The completed cross-version audit confirms that FW `1.00` through `12.70` all
use the same six-byte `0x8A6C9018` return stub. Dragon Quest and the two FW
`0x0550` system applications that import it contain only dead local thunks, with
no code or relocated-data callers. See
`analysis/find_resources_public_audit.md`. Keep the NID unresolved until a live
PS5 caller or typed PS5 header becomes available; do not use the PS4 placeholder
prototype. Corpus expansion therefore requires another decrypted PS5 title
binary rather than more firmware variants of this stub.

When useful to homebrew API work, grow the optional evidence corpus with
FW 5.50-compatible binaries spanning multiple engines, SDK vintages, and
graphics workloads. For each title:

- Extract imported AGC NIDs and named functions into the analysis tables.
- Classify each import as implemented, forwarding wrapper, intentional
  fail-closed optional feature, or unresolved.
- Implement observed missing functions with ABI declarations, static asserts,
  packet/layout fixtures, and a game-provenance note.
- Add runtime fixtures for newly observed state combinations instead of
  declaring compatibility from symbol presence alone.

Prioritize functions used by real titles. The 12 unknown SPRX exports and 32
placeholder mappings are not release blockers unless a corpus title imports
or exercises them; if that occurs, obtain new evidence rather than guessing a
name or ABI. The exit gate is 100% named-import implementation across the
expanded corpus and no exercised path relying on a silent success stub.

Candidate selection is fail-closed. `PPSA01325` (ASTRO's PLAYROOM) is
explicitly excluded by project scope. Metadata-only candidates that have not
run on FW `0x0550` are ineligible. A hardware-proven backport is eligible even
when its metadata names newer firmware, provided its executable provenance and
bundled compatibility ABI are recorded. Record rejected candidates in
`analysis/game_compat_exclusions.tsv` so they are not counted or repeatedly
reinvestigated.

### 6. Run the FW 5.50 release audit

Before calling the FW 5.50 core complete:

- Audit all public symbols against FW 5.50 names, signatures, calling
  conventions, and NID provenance.
- Audit every firmware ABI struct for size and relevant offset assertions.
- Pass clean CMake and Make generic builds with no new warnings, the complete
  host suite, and a clean Prospero build.
- Ensure samples build only from public headers plus explicitly private sample
  support, and contain no second implementation of reusable PM4 state.
- Update `STATUS.md`, API/build/deployment documentation, the support matrix,
  and non-goals from the final qualification evidence.

### 7. Work deliberately deferred until after the FW 5.50 core

Per-operation direct-backend gates are complete for the recovered operations.
All 39 active SPRX pairs are grouped
by normalized wrapper and private-carrier fingerprints. Submit16, authenticated
queue management, primary suspend, public TF ring, HS offchip, async setup,
and standard/Trinity memory facts are exact-RE-qualified per firmware. FW 11.60
is the modern/Trinity reference and FW 3.20 remains the lowest active target.
Workloads remain disabled unless their exact direct contract is separately
proven. Register defaults are exact-profile gated: FW 5.50 version 8 and FW
11.60 version 12 are hardware-qualified. Version 12 maps to the recovered V10
tables and uses its exact larger internal DDID slot; two FW 11.60 runs built
both blobs, executed the post-default GPU markers, and shut down cleanly.
FW 11.60's separately guarded headless compute artifact passed twice with the
exact 2,073,600-pixel output oracle, driver shutdown, and forced process
termination. Both runs executed the gfx1013 shader and filled every pixel with
`0xff00ff00`; the original FW 5.50 compute-plus-VideoOut conformance sample then
passed unchanged. Presentation remains isolated because FW 11.60 rejects the
FW 5.50 `libSceVideoOut` linear-buffer patch offset; its VideoOut contract will
be qualified independently rather than guessed during compute testing.
The shared compute sample removes its flip event and closes VideoOut before
driver shutdown. It also reports best-effort unregister, event-queue, unmap,
and direct-memory results. FW 5.50 keeps the currently scanned buffer set
`RESOURCE_BUSY`, closes the event queue with the port (`EBADF` on a second
delete), and leaves the allocation process-owned (`EINVAL` on explicit
release); those diagnostic returns are not AGC qualification failures because
process exit reclaims them. Exact GPU output, completed presentation, event
removal, VideoOut close, and driver shutdown remain mandatory.
The FW 11.60 graphics gate reused the proven baseline NGG+PS shaders, 2,470-
dword PM4 path, flexible-memory FP16 target, completion fence, and exact
coverage/color oracle. It passed twice with 255,744 changed pixels, eight
sampled colors, no invalid components, and packed hash `0x4a40c2eb4f12bc26`.
The same revision then passed FW 5.50's 1,800-flip graphics conformance sample.
Only FW 11.60 presentation is skipped, keeping proven graphics execution
independent from its still-unqualified VideoOut linear-buffer contract.
FW 11.60 rejected OpenAGC's explicitly separate one-ID convenience extension:
both three-dword calls returned `AGC_OK`, but the ordered post-workload
`WRITE_DATA` marker timed out after five seconds. The capability is disabled
again. The strengthened marker oracle remains active for FW 5.50, and any FW
11.60 adapter must reproduce the recovered nine-dword Sony contract plus its
registered-stream state rather than treating submit acceptance as execution.
The registration blocker is now resolved at the ABI level: FW 5.50 and FW
11.60 both use the 32-entry GPU-visible table at standard-console
`SceGnmGpuInfo + 0x3a000`, with one 64-bit slot per stream; public registration
only maintains a parallel 32-byte userspace descriptor and bitmask. The exact
18-dword active and 12-dword complete standalone-buffer forms, including their
private `0x79`/`WRITE_DATA` prefixes, are recorded in
`analysis/agc_driver_workload_facts.md`. A bounded standard-PS5 FW 11.60
adapter using OpenAGC-owned stream slot 1 and separate 18/12-dword DCBs matches
the exact host fixtures, but its ordered hardware marker still timed out after
both calls returned `AGC_OK`. Attempting the separately observed GPU-info
process-property step then caused a kernel panic before a verdict. That call
used the wrong four-argument order and is removed. All 39 active SPRXs now
reproducibly prove the correct five-argument carrier as
`("Sce.Debug:Gnm", gpu_info_base, gpu_info_span, 0, 0)`, with a `0x100000`
standard span and `0x180000` Trinity span. The isolated
`agc_fw1160_stage10.elf` gate used that exact five-argument call, applied the
proven `SceGnmDumpArea` range name, performed no submission, returned `AGC_OK`,
shut down, and self-terminated on standard-PS5 FW `0x11600005`. The standard
FW 11.60 stage 11 then made that registration an idempotent prerequisite of
the exact Sony stream adapter and submitted active ID 1, complete ID 1, then
an ordered `WRITE_DATA` marker. Both workload submissions returned `AGC_OK`,
but the process and PS5 UI stalled before the bounded polling loop could print
its five-second verdict. ps5debug-NG still enumerated PID 104 but could not
attach; the process-cleanup ELF removed it and restored a no-stale-process
state. The FW 11.60 workload capability is disabled again. Next RE must recover
the missing registered-stream state or lifecycle beyond the already proven
process property, address table, and packet bytes; do not rerun stage 11
unchanged.
The recovered `libSceAgc` cursor wrappers show that Sony's lifecycle is
caller-owned and inline: DCB control 0, ACB control 1, with active and complete
appended to one command stream rather than separately submitted. Stage 12
tested that distinct sequence by registering stream 1 and building active →
marker A → complete → marker B in one 40-dword DCB. The single submit returned
`AGC_OK`, but neither marker verdict nor shutdown was reached and the PS5 UI
became unresponsive. The cleanup ELF removed the stale payload and ps5debug-NG
confirmed that no `eboot.elf` remained. Do not rerun stage 12 unchanged.
FW 11.60 workload remains disabled. Next, recover the Sony driver/module
initialization that precedes workload use—particularly GPU/register enable
state or kernel operations beyond the already proven process property, stream
table address, packet bytes, and inline cursor lifecycle—before constructing a
new isolated gate. Reboot the console before that future GPU test.
Offline tracing now proves the workload initializer itself contains no hidden
ioctl or GPU write: it only selects GPU-info region 2, validates its span and
alignment, reserves stream 0, initializes descriptors, and creates a mutex.
The builders also carry the address of the selected 64-bit slot, exactly as
OpenAGC does. Stage 13 tested the remaining evidenced difference from the FW
5.50 qualified path after a clean reboot. FW 11.60 default-state notification,
async setup, the exact process property, stream registration, and a normal
preflight marker all succeeded; the preflight marker completed in 50 ms. The
unchanged inline workload DCB then returned `AGC_OK` but produced no following
verdict before the 20-second transport timeout. The cleanup payload found no
stale `eboot.elf`, while websrv and ps5debug-NG port 744 remained reachable.
This rules out those surrounding prerequisites as the missing state. Do not
rerun stage 13 unchanged. Keep FW 11.60 workload disabled and recover the
GPU-side `SET_WORKLOAD` state transition or required queue/register
programming before constructing another gate.
The first opt-in installed-driver oracle was run once after a clean reboot. FW
11.60's matching module loaded, all exact exports resolved, the 18/12-dword
sizes matched, and async setup returned `AGC_OK`. Its ordinary `WRITE_DATA`
preflight returned `AGC_OK` but left the marker zero after 5,000 ms, so the
safety gate prevented stream registration and workload emission. Static review
then found that the preflight itself omitted the already hardware-proven NOP
trailer required to advance the final graphics descriptor in this payload
context. The failure is therefore inconclusive, not a Sony-backend rejection.
The revised oracle submits two observable DCBs plus a 16-dword NOP trailer
through Sony's multi-DCB export and flushes all cache lines occupied by the
40-dword workload DCB. It is build-qualified for one fresh-boot attempt; do not
rerun the original artifact. See
`analysis/fw1160_sony_workload_attempt_20260729.md`.
The revised artifact was subsequently run after another clean reboot. Sony's
multi-DCB export returned `AGC_OK`, but neither observable marker executed in
5,000 ms. The safety gate again prevented workload emission. This rules out
final-descriptor deferral and incomplete cache flushing, and proves that the
installed module cannot be the execution oracle under websrv. Do not rerun
either artifact. Resume direct `/dev/gc` recovery of the GPU-side
`SET_WORKLOAD` queue/register transition before constructing a new gate.

The Sony workload contract itself is recovered for all active firmware:
seven active-wrapper and three complete-wrapper groups converge on the same
nine-dword `0xc0071e00` packet and 18/12-dword maximum reservations. OpenAGC's
public DCB and ACB cursor ABIs now match those exact active/complete forms plus
the nine-dword inactive prefix. DCB passes control 0 and ACB control 1, matching
the FW 11.60 wrappers; registered stream state supplies the backend GPU slot
address. This fixes the userspace builders without re-enabling the distinct
one-ID submit-owning convenience operation on FW 11.60.

The fail-closed audit also separates FW 5.50-only EOP-flip evidence from the
common direct-operation group. Unimplemented target-ring and Razor/capture
operations must return `AGC_ERROR_NOT_SUPPORTED`; export-table presence or a
placeholder backend function is never a capability grant.

TF-ring and HS-offchip grouping now includes semantic payload verification:
all active images use `u64@0,u32@8` with commands `0x80108128` and
`0xc010812c`. FW 12.x explicitly zeroes reserved offset `0xc`; OpenAGC's typed
zero-initialized structures preserve that stricter form across the common
carrier groups.

Submission grouping is also semantic rather than hash-only: every active DCB,
ACB, and multi-DCB export group converges on the instruction-identical
`0xc0108102` carrier with a typed `u32@0,u32@4,u64@8` request.

Cache synchronization semantics follow this audit and become an earlier
blocker only if the expanded game corpus exercises an unresolved path. VRS and
ray tracing remain later feature tracks driven by verified FW 5.50 ABI evidence
and real-title demand.

Every completed goal requires updated documentation, host regression coverage,
the relevant Prospero build, hardware validation through curl/websrv when
hardware is available, and a focused git commit. Do not use `prospero-deploy`.

## Firmware Compatibility Strategy

OpenAGC's public API is firmware-agnostic. Private `/dev/gc` behavior is
represented by versioned ABI families selected from exact inspected firmware
aliases, never by assuming that every version in a numeric range is compatible.

Current backend coverage:

- Exact inspected builds from FW 1.00 through 12.70 remain registered as RE
  data through submit16 ABI profiles. Registration is not a support claim.
- FW 3.20 is the lowest active compatibility target. OpenAGC's hardware-proven
  submission request is `0xc0108102`; the later PID request is not required.
- FW 5.50: RE-verified and fully hardware-validated on a standard PS5. The
  console reports raw build `0x05500008` (`5.500.008`); profile selection uses
  its four-digit `0x0550` ABI key while diagnostics retain the complete value.
- Other registered FW 4.00-12.70 builds: exact selection, submit16,
  authenticated queues, primary suspend, public TF ring,
  HS offchip, async setup, and standard/Trinity memory layouts are RE-verified.
  They remain hardware-pending and are not a complete support claim.
- FW 1.00 and 2.x: archival RE profiles only. Known submit/EOP evidence is
  retained, including FW 1.00's `0x38000` offset, but missing legacy queue or
  optional-request ABIs will not be recovered. Unsupported operations remain
  explicitly fail-closed and these versions are not advertised as supported.
- FW 3.20: lowest active target, with local firmware references available for
  exact userspace ABI recovery. Hardware validation remains pending.
- PS5 Pro: FW 9+ resolves `sceKernelHasTrinityMode` and selects the firmware-
  proven 22 MiB CWSR allocation and related offsets. Hardware validation is
  still required on a PS5 Pro.

The next compatibility work is the safety correction and per-operation
qualification ladder below. Evidence and exact aliases are tracked in
`analysis/agc_driver_abi_families.tsv`,
`analysis/agc_driver_operation_facts.tsv`,
`analysis/agc_register_defaults_facts.tsv`, and
`analysis/agc_driver_abi_1160.md`.

## Target Priority

Games built with openagc must run on **real PS5 hardware first**, then on
emulators (reference implementation, HLE reference) as secondary dev/testing targets on PC.

This priority determines the trust hierarchy for packet encodings:

1. **SPRX disassembly (ground truth)** — the actual SDK functions that run on
   real PS5 hardware. openagc's SPRX-confirmed encodings are authoritative.
2. **The reference implementation** — best secondary reference. Its packet builders reproduce what
   the SDK outputs (real opcodes like `IT_DRAW_INDEX_AUTO`,
   `IT_SET_UCONFIG_REG_INDEX`), and its full PM4 command processor validates
   them. Games run through the reference implementation, so its encodings are empirically tested
   against game expectations.
3. **openagc** — this project. SPRX RE-confirmed encodings match the reference. Some
   builders still use NOP-wrapped stubs that need switching to real opcodes.
4. **HLE reference** — useful for NID discovery and cross-reference, but many of its
   packet encodings would fail on real PS5 hardware. Its NOP-wrapped stubs work
   only because the HLE reference's inline interpreter doesn't validate packet format.
   Do NOT adopt the HLE reference's packet encodings without independent SPRX or reference
   confirmation. The HLE reference's NID discoveries are safe to adopt regardless of
   encoding correctness.

## Evidence Levels

Use these labels in docs, analysis notes, and code comments:

- **Implemented**: present in openagc, covered by host tests.
- **SPRX-confirmed**: verified against firmware 5.50 SPRX disassembly — highest
  confidence for real-PS5 correctness.
- **reference-confirmed**: verified against the reference implementation (working PS5 emulator
  with full PM4 command processor). High confidence for real-PS5 correctness.
- **Observed**: found in HLE reference, RPCSX, ps5-openagc notes (NID mapping only —
  ps5-openagc is NOT proven working and contains known ioctl errors; see
  `analysis/ps5_openagc_audit.md`), firmware strings, or local analysis, but
  not implemented yet. HLE reference packet encodings are NOT trusted without
  independent SPRX or reference confirmation.
- **Inferred**: likely from AMD/RDNA2 behavior or reference projects, but not
  confirmed in AGC firmware paths yet.
- **Speculative**: plausible roadmap item with no local implementation evidence
  yet.

Do not promote a feature from inferred/speculative to implemented until there
is a packet, structure, register, test, or hardware validation artifact.

## Architecture Context

PS5 can run PS4 games through hardware-level backward compatibility modes. In
that path, the GPU exposes behavior compatible with PS4 GNM/GNMX command
streams and legacy GCN assumptions.

Native PS5 software targets AGC. AGC should be treated as a Gen5/RDNA2-facing
model with its own command buffers, packet wrappers, shader records, queues,
cache synchronization, and `/dev/gc` ioctl ABI.

Practical rule:

- Use GNM/RPCSX/opengnm for packet ancestry, descriptor patterns, tiling, and
  queue interpretation.
- Use the reference implementation as the highest-priority emulator reference for PS5 AGC packet
  encodings, register defaults, and PM4 command processing. The reference implementation is a
  working emulator with a full PM4 command processor — its packet builders
  use real AMD/AGC opcodes and are empirically validated against real games.
- Use firmware 5.50 SPRX disassembly as ground truth for real-PS5 correctness.
  SPRX-confirmed encodings are authoritative.
- Use HLE reference for NID discovery and cross-reference only. The HLE reference's packet
  encodings are NOT trusted — many use NOP-wrapped stubs that would fail on
  real PS5 hardware. The HLE reference's NID findings are safe to adopt.
- Avoid assuming a PS4 GNM packet is valid AGC behavior unless AGC evidence
  confirms it.

## GNM to AGC Capability Map

| Area | PS4 GNM / GNMX | PS5 AGC target | Current evidence |
|---|---|---|---|
| GPU architecture | GCN | RDNA2 / Gen5 AGC | Observed from platform context |
| Wavefront model | Wave64-centric | Wave32/Wave64 metadata and register state | Wave32 NGG+PS compiler records, PM4, readback, and display hardware-validated on gfx1013 |
| Geometry pipeline | VS/HS/DS/GS fixed-function path | AGC shader records and possible task/mesh-style paths | Speculative until firmware/shader evidence |
| Ray tracing | Software compute only | Ray acceleration/BVH state if exposed | Speculative until AGC evidence |
| Shading rate | Uniform rate | VRS/rate-image state if exposed | Speculative until register/packet evidence |
| Cache sync | Coarser GCN cache flush/invalidate model | AGC acquire/release/wait/cache-policy packets | Partially observed and partially implemented |
| Submission | GNM command buffers and PS4 ABI | AGC command buffers, submit descriptors, queues, `/dev/gc` ioctls | FW 5.50 hardware-validated; exact registry implemented; FW 3.20 is the lowest active target |

## Current State

Implemented and host-tested:

- Gen5 AGC/PM4 type-3 packet header helpers.
- AGC `IT_NOP` subcommand constants.
- Known NID table for mapped HLE reference exports.
- `SceAgcCb` cursor layout and allocation.
- Cursor-based `sceAgcCb*` and `sceAgcDcb*` packet builders:
  - `sceAgcCbNop`
  - `sceAgcCbDispatch`
  - `sceAgcCbSetShRegistersDirect`
  - `sceAgcDcbWriteData`
  - `sceAgcDcbWaitRegMem`
  - `sceAgcDcbDmaData`
  - `sceAgcDcbSetBaseIndirectArgs`
  - `sceAgcDcbDispatchIndirect`
  - `sceAgcDcbSetIndexBuffer`
  - `sceAgcDcbDrawIndexOffset`
  - `sceAgcDcbDrawIndexAuto`
  - `sceAgcDcbWaitUntilSafeForRendering`
  - `sceAgcDcbPushMarker`
  - `sceAgcDcbPopMarker`
  - `sceAgcDcbSetFlip`
  - `sceAgcCbReleaseMem`
  - `sceAgcDcbSetShRegistersIndirect`
  - `sceAgcDcbSetCxRegistersIndirect`
  - `sceAgcDcbSetUcRegistersIndirect`
  - `sceAgcDcbGetLodStatsGetSize`
  - `sceAgcDcbGetLodStats`
- In-place patchers:
  - `sceAgcDmaDataPatchSetDstAddressOrOffset`
  - `sceAgcWaitRegMemPatchAddress`
  - `sceAgcQueueEndOfPipeActionPatchAddress`
- DCB/ACB submit descriptor layout.
- Generic submit validation and debug capture.
- AGC shader record parser (magic, pointer fields, semantics counts, shader type).
- Shader linking (`agcShaderLinkHsGs`) and fused shader support
  (`sceAgcGetFusedShaderSize`, `sceAgcFuseShaderHalves` with register patching).
- Complete version 8 register defaults (703 public, 25 internal registers)
  from the reference implementation, replacing incomplete HLE-reference-derived data.
- ACB descriptor indirection (magic 0x5533ccaa) in prospero ACB submit.
- ACB packet builders for event write, atomic mem/GDS, cond exec, wait-reg-mem,
  write/copy/dma data, mem semaphore, acquire mem, queue reset, rewind, set
  flip, workload markers, and prime UTC L2.
- DCB/VSH packet builders for clear state, atomic GDS, context state ops, reset
  queue, set flip, workload markers, wait-safe, and preemption (SPRX-confirmed
  unimplemented VSH-only stub).
- Native prospero `/dev/gc` backend with ioctl submission, internal memory
  allocation, default-state `CLEAR_STATE` submission, and suspend-point
  submit/query. **Hardware-validated** on PS5 (FW 5.50, exploited).
- Hardware validation samples (`samples/hw_test/`) built as ELF and fake-SELF.
- **Compute shader execution verified on PS5 hardware** — 2,073,600 / 2,073,600
  pixels match expected output. GPU MMU memory mapping (flexible vs garlic),
  COMPUTE_STATIC_THREAD_MGMT_SE0..SE3, user data SGPR layout, and SH register
  defaults all confirmed.

Current expected host test result:

```text
12240 passed, 0 failed
```

## Phase 0: RE Groundwork

Status: complete.

Purpose:

Recover enough AGC packet and command-buffer behavior to make future work
measurable.

Done:

- Packet header model from HLE reference/RPCSX:

```c
0xC0000000u |
    (((length_dwords - 2u) & 0x3FFFu) << 16) |
    ((opcode & 0xFFu) << 8) |
    ((subcommand & 0x3Fu) << 2)
```

- `SceAgcCb` cursor offsets:
  - `0x10`: cursor-up/write cursor
  - `0x18`: cursor-down/end cursor
  - `0x20`: callback
  - `0x30`: reserved dwords
- Submit descriptor layout:
  - `0x00`: command buffer address
  - `0x08`: dword count
  - `0x0C`: reserved

Remaining:

- 12 SPRX NIDs remain unidentified (not in any known database — aerolib.csv
  154k entries, flatz ps5_symbols.txt, reference emulator, ps5-openagc, FW 3.20
  genstubs all exhausted). These are blocked on new external data sources.
- 32 NIDs in the TSV are unverified placeholders (`sceAgcUnknown_*` /
  `sceAgcDriverUnknown_*`) — names not in any database. Same blocker.
- Add structured notes for unknown packet IDs instead of scattering comments.

Acceptance criteria:

- NID table has source labels for each entry. ✅ Done.
- Packet model tests pass on host. ✅ Done.
- Unknowns are tracked in analysis docs with evidence level labels. ✅ Done.

## Phase 1: Packet Builder Completion

Status: implemented.

Purpose:

Cover the HLE-reference-confirmed AGC packet builders before implementing hardware
submission.

Already implemented:

- NOP, dispatch, SH register writes.
- Release memory (`sceAgcCbReleaseMem`).
- SH/CX/UC indirect register packet builders.
- Write-data, wait-reg-mem, DMA.
- Index buffer setup and indexed draw packets.
- DCB flip and wait-safe packets.
- In-place patchers for DMA-data, wait-reg-mem, and end-of-pipe addresses.
- LOD stats helpers.
- DCB/ACB submit descriptor validation.
- ACB event write, atomic mem/GDS, cond exec, acquire mem, queue reset, rewind,
  set flip, workload markers, and prime UTC L2.
- DCB/VSH clear state, atomic GDS, context state ops, reset queue, workload
  markers, and preemption (SPRX-confirmed unimplemented VSH-only stub).

Remaining packet builders:

1. None identified; new firmware-derived variants will be added as they are
   discovered.

Acceptance criteria:

- Each builder has:
  - a declaration in `agcdriver.h`
  - an implementation in `src/cb_builders.c`, `src/acb.c`, or `src/dcb.c`
  - at least one test asserting opcode, subcommand, length, and key payload
    fields
  - a row in `analysis/agc_known_nids.tsv` when a NID is known

## Phase 2: Shader Records and Wavefront Metadata

Status: implemented for the currently exercised Gen5 records. Shader parsing,
specials, fusion, semantic mapping, NGG/PS Wave32 record state, and final-PM4
validation are host-tested and hardware-validated on gfx1013. Unobserved record
extensions remain evidence-gated.

Purpose:

Move shader support from placeholder headers toward AGC shader record
interpretation.

Current evidence:

- HLE reference records shader header offsets:
  - user data
  - code pointer
  - CX/SH registers
  - specials
  - input/output semantics
  - shader type
  - SH register count
- openagc stores these offsets in `agc_re.h`.
- openagc implements a read-only `AgcShaderRecord` parser with
  `_Static_assert` verified offsets and synthetic-record tests.
- openagc implements `agcShaderLinkHsGs` (SPRX-confirmed HS/LS + CS → GS
  shader record linking).
- openagc implements fused shader support: `sceAgcGetFusedShaderSize` and
  `sceAgcFuseShaderHalves` (reference-confirmed GS/HS front+back half fusion
  with SH register copy, SPI_SHADER_PGM_CHKSUM_GS/LO_ES/LO_LS address
  patching, and vgt_shader_stages_en mismatch validation).

Work:

1. Replace placeholder shader magic assumptions with documented AGC shader
   record structures. ✅ Done.
2. Add parser helpers for shader pointer fields and register blocks.
   Pointer-field accessors done; register-block parsing waits for observed
   block layout.
3. Track Wave32/Wave64 metadata without guessing missing fields.
   No observed Wave32/Wave64 offset yet — keep as analysis-only.
4. Add tests from synthetic records first, then captured records when available.
   ✅ Synthetic tests added.
5. Add fused shader support (GetFusedShaderSize / FuseShaderHalves).
   ✅ Done — reference-confirmed implementation with register patching.

Acceptance criteria:

- Shader parser can read the known offsets without mutating input.
- Wave-size metadata is represented only when backed by observed fields.
- Tests cover malformed records and valid synthetic records.

## Phase 3: Register Defaults and State Builders

Status: implemented and hardware-validated on FW 5.50. Primary/internal groups
are embedded, the complete v8 defaults are available, and Prospero
`NotifyDefaultStates` builds the GPU blobs and submits `CLEAR_STATE`.

Purpose:

Recover AGC default register state and state-construction helpers.

Current evidence:

- HLE reference has primary/internal register default groups (incomplete: 38
  public, 22 internal registers with many zero-placeholder values).
- Complete version 8 register defaults extracted from the reference
  implementation: 703 public registers across 127 groups, 25 internal
  registers across 22 groups. Stored in `src/register_defaults_v8.c` and
  exposed via `agcRegisterDefaultsV8GetPrimaryGroups()` /
  `agcRegisterDefaultsV8GetInternalGroups()`.
- openagc implements the `AgcRegisterDefaults` blob builder/parser with
  `_Static_assert` verified layout and tests.
- openagc implements `sceAgcDriverNotifyDefaultStates` to allocate GPU memory
  for the primary and internal blobs, build them with the correct GPU VA
  pointers, and submit an `IT_CLEAR_STATE` (0x14) DCB.

Work:

1. Convert register default groups into openagc analysis tables. ✅ Done.
2. Add typed structures for register default records. ✅ Done.
3. Implement read-only helpers first. ✅ Done.
4. Implement state builders only after tests lock down expected records.
   ✅ Builder and parser tested; prospero backend uses the builder.
5. Wire default-state submission via `IT_CLEAR_STATE`. ✅ Done.

Acceptance criteria:

- Default groups are represented in data tables with source labels.
- Tests verify register offsets, values, and group sizes.
- Builders do not silently drop unknown records.
- Hardware validation confirms the kernel accepts the `CLEAR_STATE` DCB.

## Phase 4: `/dev/gc` Ioctl and Queue Model

Status: implemented and hardware-validated for FW 5.50 submit, queue lifecycle,
suspend points, workloads, and multi-DCB submission. Exact firmware profiles
fail closed. Remaining FW 5.50 ioctl issues are non-blocking follow-up work;
cross-firmware ABI expansion is deferred until advanced graphics is stable.

Purpose:

Recover the native PS5 backend boundary before writing hardware submission
code.

Inputs:

- Firmware 5.50 dump.
- ps5-openagc NID tables (ioctl tables from ps5-openagc are NOT trusted —
  contains known errors; see `analysis/ps5_openagc_audit.md`).
- RPCSX queue/ring model for conceptual comparison.
- freegnm/opengnm and shadPS4 as lower-priority structural references.

Work:

1. Build an ioctl table with command IDs, input/output structure sizes, and
   firmware source references. ✅ Done (`analysis/ioctl_550.tsv`).
2. Identify memory objects required for AGC:
   - DDID
   - register shadow
   - CWSR/EOP/trap regions where applicable
   - queue ring buffers
   - doorbell/read-pointer areas
   ✅ Documented in `STATUS.md` and `driver_prospero.c`.
3. Model queue creation and destruction as host-testable structs first.
   ✅ `AgcProsperoQueue` modeled; `sceAgcDriverSubmitMultiCommandBuffersDirect` uses
   the submit descriptor layout.
4. Add native backend stubs only after structure sizes are known.
   ✅ `driver_prospero.c` implements private primary/final suspend carriers.
   Both public Direct exports preserve Sony's `0x8a6d0001` permission-stub ABI
   and never substitute the internal `QUEUE_STAT_16` operation.

Acceptance criteria:

- `analysis/ioctl_550.tsv` or equivalent exists.
- Every ioctl struct has size/alignment tests.
- Native submit path is implemented and builds; hardware validation is the
  remaining gate.

## Phase 5: Native PS5 Backend

Status: **implemented and hardware-validated** (NOP submit, compute dispatch
with 100% pixel output, queue create/destroy, suspend point, workload
tracking all confirmed on PS5 hardware).

Purpose:

Turn host packet builders into real native PS5 submission.

Work:

1. Open and validate `/dev/gc`. ✅ Done in `sce_agc_initialize`.
2. Allocate required kernel/direct memory regions.
   ✅ Done in `sce_agc_initialize_internal_memory`.
3. Create graphics and compute queues. ✅ Done — `sceAgcDriverSetupAsyncGraphics`
   uses `QUEUE_STATUS` ioctl (nr=0x26); `_sceAgcDriverCreateUserSpecialQueue`
   uses `QUEUE_CREATE` ioctl (nr=0x21, 64-byte RW with magic auth tokens);
   `_sceAgcDriverDestroyUserSpecialQueue` uses `QUEUE_DESTROY` ioctl (nr=0x0e,
   12-byte RW). All SPRX-confirmed.
4. Submit DCB/ACB buffers using recovered descriptors.
   ✅ `sceAgcDriverSubmitDcb` / `sceAgcDriverSubmitMultiCommandBuffersDirect`
   use the recovered descriptor layout.
5. Submit default state via `CLEAR_STATE`. ✅ Done in `sceAgcDriverNotifyDefaultStates`.
6. Submit suspend points and preserve the public Direct ABI. ✅ The private
   `sce_agc_internal_suspend_point_submit_primary` / `_final` carriers use the
   recovered ioctls; both Sony Direct exports return the exact permission error.
7. Add hardware smoke tests. ✅ Four ELF samples in `samples/hw_test/`:
   `videoout_linear.elf`, `agc_init.elf`, `agc_videoout.elf`, `agc_compute.elf`.
   All deployed and validated on PS5 hardware (FW 5.50, exploited).
8. Submit a compute dispatch with a real shader. ✅ Done — `agc_compute.elf`
   loads a psbc-compiled compute shader, sets SH registers (PGM_LO/HI,
   RSRC1/2/3, NUM_THREAD), sets user data (buffer pointer + push constants),
   dispatches via `DISPATCH_DIRECT`, and flips the display. GPU accepts the DCB
   (`SubmitDcb: 0x00000000`).
9. Verify compute shader pixel output. ✅ Done — 2,073,600 / 2,073,600 pixels
   match `0xFF00FF00` (solid green GPU-rendered frame). See "Key architectural
   discoveries" below.

### Key architectural discoveries (Phase 5 hardware validation)

These were the root causes of the compute shader not writing to the display
buffer. All four had to be fixed before the shader executed correctly:

1. **GPU MMU virtual memory mapping — flexible vs garlic memory.**
   Garlic memory (`sceKernelMapDirectMemory`) is NOT automatically mapped
   in the GPU's VMID address space. Only **flexible memory**
   (`sceKernelMapNamedSystemFlexibleMemory`) is automatically mapped in
   both CPU and GPU MMU spaces. This is why the AGC SPRX uses flexible
   memory for all internal GPU memory regions. The compute shader output
   buffer and shader code must be in flexible memory, not garlic memory.
   The display buffer (garlic) is registered with VideoOut separately and
   is GPU-accessible for display scanout, but not for general compute
   writes. Fix: allocate a 16MB flexible memory pool for compute output.

2. **Compute Unit enable — `COMPUTE_STATIC_THREAD_MGMT_SE0..SE3`.**
   Registers `0x216, 0x217, 0x219, 0x21A` must be set to `0xFFFFFFFF` to
   enable compute units on all shader engines (SE0-SE3). Without these,
   `DISPATCH_DIRECT` is processed but no workgroups actually execute on
   the CUs. Register `0x218` (`COMPUTE_TMPRING_SIZE`) is skipped in this
   contiguous block. This was the missing register that prevented shader
   execution even after all other state was correct.

3. **User data SGPR layout — `s2..s5` (confirmed by RDNA2 disassembly).**
   The openagc-psbc NIR postprocess shows `@load_scalar_arg_amd` base
   values, but the actual SGPR mapping was confirmed by disassembling the
   shader binary:
   - `s0`: unused (0)
   - `s1`: unused (0)
   - `s2`: buffer ptr low 32 bits
   - `s3`: buffer ptr high 32 bits
   - `s4`: total_pixels
   - `s5`: fill color (RGBA8 packed)

4. **FW 5.50 SH register defaults in the compute command buffer.**
   Applying the primary and internal SH register default groups (from
   `register_defaults_v8.c`) in the compute command buffer, with the
   compute shader type bit (bit 0) set on each `SET_SH_REG` packet header.
   This provides the baseline GPU state that the compute dispatch expects.

Acceptance criteria:

- ✅ Minimal DCB NOP submission does not fault on PS5 hardware.
- ✅ `NotifyDefaultStates` produces a valid `CLEAR_STATE` DCB and returns
  `AGC_OK` on hardware.
- ✅ Suspend-point ioctls return expected behavior.
- ✅ Failure paths return stable error codes.
- ✅ Compute dispatch accepted by GPU (non-NOP command buffer).
- ✅ Compute shader writes 100% of pixels correctly (2,073,600 / 2,073,600).

## Phase 6: Shader Compiler (openagc-psbc)

Status: **implemented and hardware-validated for compute and graphics
shaders.** User-data SGPR layout, no-GS NGG fusion, vertex/texture descriptors,
varying exports, and Wave32 NGG+PS state are confirmed by compiler fixtures and
real gfx1013 execution.

Purpose:

Compile GLSL → SPIR-V → NIR → ACO → PS5 `AgcShaderRecord` binary without
proprietary SDK tools. The `openagc-psbc` compiler (in `../openagc-psbc/`)
uses Mesa's NIR and ACO backends with a custom `AgcShaderRecord` emitter.

Work:

1. Build Mesa NIR + ACO subset as a standalone library. ✅ Done.
2. SPIR-V → NIR frontend. ✅ Done (Mesa `spirv_to_nir`).
3. NIR → ACO instruction selection. ✅ Done (Mesa `aco_select_nir`).
4. ACO → machine code assembly. ✅ Done (Mesa `aco_assembler`).
5. Emit `AgcShaderRecord` with SH/CX register blocks. ✅ Done.
6. Compute shader support. ✅ Done — `fill_color.comp` compiles to
   `fill_color.sb` and runs on PS5 hardware (100% pixel output verified).
7. Graphics shader support (VS/PS/GS/HS/LS). ✅ VS+PS tested on hardware.
   The gfx1013 no-GS NGG VS+PS path, RGB interpolants, and interleaved
   position/color vertex fetch are validated; GS, HS, and LS execution remain
   untested.

Bugs found and fixed during compute shader validation:

- **SH register offsets for compute were wrong.** The psbc compiler used
  `pgm_lo + 2/3/4` for RSRC1/2/3, but compute shaders have a different
  register layout: `COMPUTE_DISPATCH_PKT_ADDR_LO` sits at `pgm_lo + 2`,
  not `RSRC1`. Fixed to use explicit per-stage offset functions:
  CS: 0x212/0x213/0x228, PS: 0x00A/0x00B/0x007, etc.
  (Confirmed by sharpemu and AMD register headers.)

- **Shader type byte encoding was wrong.** The `AgcShaderType` enum had
  CS=6, but the firmware expects CS=0 at offset 0x5A of the shader record.
  Fixed to match sharpemu's confirmed encoding:
  CS=0, PS=1, ES=2, VS=3, GS=4, HS=5, ES-alt=6, LS=7.
  This matters because `sceAgcCreateShader` uses this byte to select which
  PGM_LO/HI register pair to patch with the shader code address.

- **User data SGPR layout confirmed.** The NIR postprocess output shows
  `@load_scalar_arg_amd` base values, but the actual SGPR mapping was
  confirmed by RDNA2 disassembly of the shader binary. The layout is:
  s0=unused, s1=unused, s2=buf_lo, s3=buf_hi, s4=total_pixels, s5=color.
  See "Key architectural discoveries" in Phase 5 for details.

Acceptance criteria:

- ✅ Compute shader compiles and executes on PS5 hardware.
- ✅ User data layout matches ACO's SGPR arg mapping (confirmed by RDNA2
  disassembly — 100% pixel output).
- ✅ Graphics shaders (VS/PS) compile and execute on PS5 hardware.
  The gfx1013 NGG front-entry probe executes, and the real ACO VS+PS path
  rasterizes an interpolated RGB triangle (1,036,796 changed pixels and eight
  distinct colors sampled by readback).

## Phase 7: Graphics Pipeline (Draw Calls)

Status: **complete on real PS5 hardware, including interpolants.** The DCB is
accepted, the gfx1013 NGG front program executes, and the pixel shader consumes
the vertex shader's `v_color` output to render a smooth RGB triangle. Readback
finds 1,036,796 changed pixels and eight distinct colors; visual confirmation
on the PS5 display matches the expected gradient. The post-draw WRITE_DATA
marker also confirms CP progress.

Purpose:

Submit a real graphics draw call — vertex shader + pixel shader + render
target + viewport + blend state + indexed draw — and display the result.

Prerequisites:

- ✅ Compute dispatch works with 100% pixel output (Phase 5).
- ✅ Shader compiler produces valid compute binaries (Phase 6).
- ✅ GPU MMU memory mapping understood (flexible memory for GPU writes).
- ✅ `NotifyDefaultStates` returns `AGC_OK` on hardware.
- ✅ Graphics shader compilation (Phase 6) — VS+PS compiled via psbc.

Key learnings from compute validation that apply to graphics:

- **Render targets can be in garlic memory** — the compute sample writes
  to garlic memory (display buffer) successfully. The graphics sample now
  renders directly to the display buffer (garlic) to avoid a copy step.
- **SET_SH_REG packets need the shader type bit** (bit 0): 0=graphics,
  1=compute. For VS/PS draw calls, use bit 0 = 0 (graphics engine).
- **Apply FW 5.50 SH register defaults** in the command buffer before
  setting shader-specific state. This provides the baseline GPU state.
- **CONTEXT_CONTROL packet is required** — opcode 0x28, 3 dwords,
  `LOAD_ENABLE_CONTEXT=0x80000000`. Same as compute.

### Critical issues discovered during hardware validation

1. **Non-contiguous register default groups corrupt GPU state.** Five
   register-default groups in `register_defaults_v8.c` have non-contiguous
   offsets but were being written as batch `SET_SH_REG`/`SET_CX_REG`
   packets (which assume contiguous offsets). Group 72 (128 CB_COLOR0
   registers) has offsets 0x318, 0x31b, 0x31c, 0x31d, 0x31e, 0x31f,
   0x321, 0x323... — writing contiguously overwrites unrelated registers
   and causes a GPU hang. **Fix:** write each register individually with
   `register_count=1`. The 5 non-contiguous groups: `_64`, `_72`, `_76`,
   `_90`, `internal_regs_21`.

2. **Tile mode 0 is Depth_2DThin_64, NOT linear.** For a linear color
   render target, use `kAgcTileDisplay_LinearGeneral` (31).
   `CB_COLOR0_ATTRIB = 0x0000001F` (tile_mode_index=31).

3. **SPI_SHADER_COL_FORMAT (0x1C5) and SPI_SHADER_POS_FORMAT (0x1C3) are
   NOT in shader records or register defaults.** They default to 0 (no
   export). Must set manually:
   - `SPI_SHADER_POS_FORMAT (0x1C3) = 1` (vec4 position)
   - `SPI_SHADER_Z_FORMAT (0x1C4) = 0` (no Z export)
   - `SPI_SHADER_COL_FORMAT (0x1C5) = 1` (8_8_8_8 color)
   - `CB_SHADER_MASK (0x08F) = 0x0F` (all RGBA to RT0)

4. **VGT_SHADER_STAGES_EN should be 0 (default) for VS+PS.** Do NOT set
   `ES_EN` — that routes VS through the ES stage, wrong for a type-3 VS.

5. **DB_Z_INFO must be explicitly disabled.** Default is `0x80000000`
   (depth enabled). Set `DB_Z_INFO = 0` and `DB_STENCIL_INFO = 0`.

6. **CB_COLOR0_PITCH uses 8-element tiles for linear mode.**
   `TILE_MAX = (pitch_elements / 8) - 1`. `SLICE = (tiles_per_row * height) - 1`.

7. **VS PGM_LO register offset mismatch (CRITICAL FIX).** `write_shader_sh_regs`
   previously only checked for `0x0C8` (ES) and `0x008` (PS). For a type-3
   (VS) vertex shader compiled by `openagc-psbc`, `SPI_SHADER_PGM_LO_VS` is
   at offset `0x048` (`0x049` for `PGM_HI`). Because `0x048` was not matched,
   the vertex shader code address was never patched, leaving `PGM_LO = 0`.
   The GPU attempted to execute the VS from address 0x0 (NULL), yielding 0
   vertices and leaving all pixels black. **Fix:** added `is_pgm_lo_off()` and
   `is_pgm_hi_off()` to match and patch `PGM_LO`/`HI` across all stages
   (PS `0x008`, VS `0x048`, GS `0x088`, ES `0x0C8`, HS `0x108`, LS `0x148`, CS `0x20C`).

8. **CB_COLOR0_BASE_EXT (0x390) required for 64-bit high bits.** The
   render target base address is in 64-bit GPU virtual memory. Writing only
   `CB_COLOR0_BASE` (`addr >> 8`) at 0x318 left high bits unpopulated.
   **Fix:** write `CB_COLOR0_BASE_EXT` at 0x390 with `(addr >> 40)`.

9. **Flexible memory required for render target writes.** Garlic memory
   direct writes from the Color Buffer engine are not GPU MMU-mapped in the
   same way flexible memory is. **Fix:** render into flexible memory
   (`compute_buffer + 0x10000`) and copy to garlic display buffer post-draw.

10. **PS CX block re-enables depth after explicit disable.** The PS
    shader's CX register block writes `DB_DEPTH_INFO` (0x00F) = 0x0F and
    `DB_SHADER_CONTROL` (0x203) = 0x10 *after* the code disabled depth.
    With no depth buffer bound, depth testing discards all fragments.
    **Fix:** override `DB_DEPTH_INFO`, `DB_Z_INFO`, `DB_STENCIL_INFO`,
    `DB_SHADER_CONTROL`, and `DB_DEPTH_CONTROL` to 0 *after* the PS CX
    block is written.

11. **`SPI_PS_INPUT_CNTL_0` register offset was wrong.** The code wrote
    to `0x1B8` (`AGC_REG_SPI_BARYC_CNTL`) instead of the correct
    `0x191` (`AGC_REG_SPI_PS_INPUT_CNTL_0`). **Fix:** use
    `AGC_REG_SPI_PS_INPUT_CNTL_0` with `OFFSET=0`.

12. **`VGT_SHADER_STAGES_EN` bit layout corrected.** Bit 8 is
    `dynamicHs`, not `ES_EN`. The real `esEn` field is at bits [4:3]
    (EsReal=2 → 0x10). For VS at ES PGM (0x0C8, as psbc outputs), set
    `VGT_SHADER_STAGES_EN = 0x10` (EsReal). Does not kernel panic.

13. **`SPI_SHADER_COL_FORMAT` must match `CB_COLOR0_INFO` format.**
    Set `SPI_SHADER_COL_FORMAT = 1` (8_8_8_8) to match
    `CB_COLOR0_INFO` format 1 (COLOR_8_8_8_8).

14. **`CB_COLOR0_ATTRIB2` (0x3B0) was missing.** Without MIP0 dimensions,
    the CB clips all writes to 0x0. **Fix:** set
    `CB_COLOR0_ATTRIB2 = ((height-1) & 0x3FFF) | (((width-1) & 0x3FFF) << 14)`
    and `CB_COLOR0_ATTRIB3 = 0`.

15. **Removed invalid partial NGG state.** A plain VS record has no
    GS/NGG `specials` block. `VGT_GS_OUT_PRIM_TYPE=4` was also wrong because
    that register uses the GS-output enum (`triangles=2`), not the input
    primitive enum, and the speculative `GE_CNTL` values were unsupported.

16. **`PA_CL_VS_OUT_CNTL` and `PA_CL_CLIP_CNTL` set to 0.** The previous
    non-zero values (USE_VTX_POINT_SIZE, CLIP_DISABLE) may interfere with
    NGG rasterization. Set both to 0 (defaults).

### Current hardware-validation candidate: corrected plain VS path

The generated vertex record declares `shader_type=VS` but stores its four
program registers at ES offsets. The sample now remaps those registers to
the VS quartet and uses `VGT_SHADER_STAGES_EN=0`. It also fixes four
independent state errors found by cross-checking KytyPS5 and sharpemu:

- `CB_COLOR0_INFO.FORMAT` is `10` for `8_8_8_8`, not `2`.
- `CB_COLOR_CONTROL.MODE` must be Normal (`0x00CC0010` with ROP3 copy),
  rather than zero/disabled (`0x00CC000F`).
- `SPI_SHADER_COL_FORMAT=4` selects FP16 ABGR shader export; it is not the
  same enum as the render-target channel layout.
- `VGT_PRIMITIVE_TYPE` is UCONFIG-only and must not also be written through
  `SET_CONTEXT_REG`.

A production PS5 NGG path still requires a real GS/NGG shader record and
its populated `specials` block. KytyPS5 and sharpemu both source stage,
GS-output, `GE_CNTL`, and `GE_USER_VGPR_EN` state from that block; partial
NGG state must not be synthesized around a plain VS.

### Real-PS5 NGG implementation direction

The plain-VS path is only a diagnostic experiment. The target architecture
for real PS5 hardware is the ES+GS/NGG pipeline indicated by both reference
emulators and must be validated against FW 5.50 SPRX behavior, game command
buffers, and hardware results.

Reference roles:

- **KytyPS5 is the primary low-level state reference.** Its recognized PS5
  vertex path uses an ES program, valid GS metadata/checksum, and
  `VGT_SHADER_STAGES_EN = 0x02002000`. It is the better reference for PM4
  register relationships and the minimum coherent NGG state.
- **sharpemu is the primary AGC ABI reference.** Its
  `sceAgcCreatePrimState` implementation requires an ES/geometry shader with
  a non-null `specials` block and copies `VGT_SHADER_STAGES_EN`,
  `VGT_GS_OUT_PRIM_TYPE`, `GE_CNTL`, and `GE_USER_VGPR_EN` from that shader.
  Its interpolant builder confirms that ES output semantics must be linked
  to PS input semantics.
- **Neither emulator is authoritative by itself.** Final constants and
  layouts must be confirmed from the FW 5.50 SPRX, captured game state, and
  real-PS5 execution. Do not replace shader-derived state with guessed
  constants merely because an emulator accepts them.

FW 5.50 SPRX verification (`libSceAgc.sprx`):

- `sceAgcCreatePrimState` (`D9sr1xGUriE`, `0xE2D0`, 255 bytes) confirms the
  five-argument ABI: CX output, UCONFIG output, optional hull shader,
  geometry/fused shader, and input primitive type.
- The firmware copies register/value pairs from the geometry shader
  `specials` block at `+0x00` (`GE_CNTL`), `+0x08`
  (`VGT_SHADER_STAGES_EN`), `+0x20` (`VGT_GS_OUT_PRIM_TYPE`), and `+0x28`
  (`GE_USER_VGPR_EN`). These are 8-byte register/value entries, not four
  packed `uint32_t` values. The former 16-byte `AgcShaderSpecials` model has
  been replaced with the verified 0x30-byte sparse register-pair layout.
- `CreatePrimState` tests GS-enable bit 5 in the stage-mask value. When GS is
  enabled it uses the shader-provided GS-output pair at `specials+0x20`;
  otherwise it derives the low three GS-output primitive bits from the
  firmware primitive lookup table. An optional hull shader contributes stage
  bits and replaces `GE_USER_VGPR_EN` with its own specials value.
- The SPRX does not hard-code `VGT_SHADER_STAGES_EN=0x02002000` in
  `CreatePrimState`; it copies the compiler-produced value. `0x02002000`
  remains a useful KytyPS5/game-state observation, not a constant OpenAGC
  should synthesize unconditionally.
- `sceAgcCreateInterpolantMapping` (`pdEV7bI6COI`, `0xD7F0`, 758 bytes)
  builds 32 register/value entries by matching PS input semantics at
  `PS+0x30` against geometry output semantics at `GS+0x38`. It preserves and
  transforms interpolation flags rather than emitting a simple identity map;
  OpenAGC must reproduce this firmware behavior for a production draw path.
- Both FW 5.50 fusion exports (`0xC770` and `0xCD40`) accept half-type pairs
  `4+6` and `5+7`, copy the back record, and emit fused record types `2` and
  `3` respectively. They compare stage-mask value bits 22/21, merge multiple
  SH resource fields, and patch the front program address into the fused SH
  register set. OpenAGC's current simplified fusion/type model requires an
  SPRX-accurate correction before it can produce real NGG shaders.

Implementation work:

1. ~~Correct `AgcShaderSpecials` to the verified sparse register-pair layout,
   add size/offset static assertions, and update all typed accessors/users.~~
   Done.
2. ~~Correct fused-shader half validation, output types (`2`/`3`), stage-bit
   checks, SH resource merging, and front-program address patching against
   the `0xC770` and `0xCD40` firmware implementations.~~ Done.
3. ~~Implement the firmware `sceAgcCreatePrimState` behavior with tests
   covering exact output register pairs, hull merging, and primitive lookup.~~
   Done.
4. ~~Implement `sceAgcCreateInterpolantMapping` semantic mapping with exact
   FW 5.50 flag/default transformations and all 32 output entries.~~ Done.
5. ~~Extend `openagc-psbc` to emit a real PS5 ES+GS/NGG shader record rather
   than a plain VS record containing ES register offsets.~~ Done.
6. ~~Generate or fuse the required GS front/back halves, including a valid
   `SPI_SHADER_PGM_CHKSUM_GS` and the complete shader `specials` block.
   ~~ Done.
7. ~~Bind vertex/export code through `SPI_SHADER_PGM_LO/HI_ES` and bind all
   required GS program/resource state from the generated record.~~ Done.
8. ~~Apply the shader-provided `VGT_SHADER_STAGES_EN`. Preserve
   `0x02002000` as a captured/reference value only; never substitute it for
   the compiler-produced specials entry without matching shader metadata.~~
   Done.
9. ~~Apply shader-provided `GE_CNTL` and `GE_USER_VGPR_EN`.~~ Done.
10. ~~Set `VGT_GS_OUT_PRIM_TYPE=2` and input
    `VGT_PRIMITIVE_TYPE=4`.~~ Done.
11. ~~Generate ES-to-PS interpolant registers from shader semantics.~~ Done.
12. ~~Validate with post-draw markers and render-target readback.~~ Done on
    real PS5 hardware.

Safety constraints:

- Do not enable `GE_NGG_SUBGRP_CNTL` or an NGG stage mask without a valid
  GS/NGG shader and checksum; the partial configuration already caused a
  kernel panic.
- Do not dual-bind the same program to ES and VS register spaces.
- Keep compute and graphics work in separate DCB submissions during NGG
  bring-up.

### Immediate Phase 7 execution order

The next work should proceed in this order. Do not begin another round of
manual PM4 tuning before steps 1-4 are complete.

1. **Fix the shader ABI model.** Done. `AgcShaderSpecials` now models the
   0x30-byte FW 5.50 sparse register/value layout and has size and offset
   assertions for the entries at `0x00`, `0x08`, `0x20`, and `0x28`.
2. **Make shader fusion SPRX-accurate.** Done for both FW 5.50 exports,
   including half pairs `4+6` and `5+7`, fused types `2` and `3`, stage-mask
   checks, version-specific SH resource merging and user-data behavior,
   scratch copying, checksum transfer, and front-address patching.
3. **Implement `sceAgcCreatePrimState`.** Done. It emits the exact two
   context and three UCONFIG register/value entries recovered from FW 5.50
   at `0xE2D0`, including optional hull-stage merging and primitive lookup.
4. ~~**Implement `sceAgcCreateInterpolantMapping`.** Reproduce the semantic
   matching and flag transformation recovered from the FW 5.50 function at
   `0xD7F0`. Test missing semantics, identity fallback, flat interpolation,
   defaults, and all 32 output entries.~~ Done.
5. ~~**Add a compiler fixture before changing `psbc`.** Use synthetic shader
   records and metadata only; do not introduce proprietary firmware or
   shader data.~~ Done. The host fixture fuses GS-front/GS-back records, then
   derives primitive and interpolant state from the fused record.
6. ~~**Extend `openagc-psbc` for the real NGG path.** Emit the required shader
   halves and fuse them through OpenAGC rather than inventing another record
   format in the sample.~~ Done for the RDNA2 no-GS NGG vertex path. The
   compiler runs RADV NGG lowering, computes subgroup/LDS state, emits the
   monolithic ACO program plus fusion-compatible front/back records, complete
   specials, semantic maps, and corrected named CX register offsets.
7. ~~**Replace the graphics sample's plain-VS binding.** Consume the fused
   shader record plus the primitive-state and interpolant builders, removing
   remaining guessed stage and semantic state.~~ Done. The sample relocates
   file records, uploads both halves, fuses with the real GPU addresses, and
   binds executable code through the ES program pair and resources through
   the GS register block.
8. ~~**Validate in layers.**~~ Done. The front-entry probe wrote
   `0x4E474721`; the real ACO path changed 1,036,800 pixels and produced the
   expected magenta triangle on PS5 hardware.

**Interpolant validation is complete.** The fragment shader consumes
`vec4(v_color, 1.0)` and renders an RGB gradient on real PS5 hardware. This
proves that compiler-generated ES/NGG output semantics,
`sceAgcCreateInterpolantMapping`, PS input registers, and parameter exports
work together. The compiler fix assigns standalone vertex-stage user varyings
to RADV parameter-export slots before NGG lowering; parameter 0 now exports
all three `v_color` channels instead of falling back to constant ones.

After interpolants pass, proceed in this order:

1. Maintain compiler regression coverage for NGG record placement and
   multi-component parameter exports.
2. Keep `AGC_NGG_ENTRY_PROBE` as an optional diagnostic and reduce temporary
   PM4 audit output in the normal hardware sample.
3. ✅ Validate vertex-buffer input. A 20-byte interleaved `float2` position +
   `float3` color binding executes on real gfx1013 hardware through RADV-style
   buffer descriptors and the compiler-recorded descriptor-table user SGPR;
   GPU readback and the displayed RGB-gradient triangle both pass.
4. ✅ Validate index-buffer binding and indexed drawing. A bound 16-bit
   `{1,2,3}` index buffer skips a decoy vertex 0 and reproduces the exact
   1,036,796-pixel RGB triangle on real gfx1013 hardware using
   `DRAW_INDEX_OFFSET_2`; GPU readback and the PS5 display both pass.
5. ✅ Validate texture and sampler binding. A 2x2 linear RGBA8 image and
   bilinear clamp sampler execute through a RADV combined descriptor on real
   gfx1013 hardware; exact readback, sampled-color variation, and the PS5
   display all pass.
6. ✅ Validate an additional render-target format. A 1536x1536 linear
   `R16G16B16A16_FLOAT` target executes on real gfx1013 hardware with CB
   format `0x0c`, FLOAT number type `7`, and standard component swap. Readback
   finds 255,744 covered pixels, eight distinct FP16 colors, opaque samples,
   zero components outside `[0,1]`, and a live completion marker. The 1:1
   RGBA8 preview was visually confirmed as a centered, smoothly textured
   equilateral triangle.
7. ✅ Resolve repeated multiple-DCB execution. The public wrappers issue one
   descriptor-array submit after the SPRX-confirmed nr=1 frame-state ioctl.
   Unique-marker runs proved the FW 5.50 exploited-payload graphics ring defers
   the final descriptor until the next submit. The Prospero backend now appends
   a 64-byte GPU-visible NOP IB trailer carved from unused `SceGnmDdid` space,
   making the deferred descriptor harmless without a standalone 16 KiB VM
   resource. Standalone `sceAgcDriverSubmitDcb` also emits the frame-state
   operation and trailer so its caller DCB executes in the current submit. Two
   immediate deployments each passed three repeated two-DCB
   iterations with unique ordered markers and zero polling delay. Vulkan-PS5
   additionally passed two standalone compute and two standalone triangle
   EOP/readback runs through this path on FW `0x05500008`.
   A later VideoOut teardown gate proved the standalone allocation was not the
   source of its final `0x4000` VM warning. The VideoOut patch had restored its
   originally execute-only text range as read/execute; the Prospero backend now
   restores the exact execute-only protection so the kernel can coalesce the
   temporary text mapping. A follow-up reproduced the warning, falsifying that
   hypothesis too. All 27 retained OpenAGC graphics klogs contain the same
   single-page warning; the remaining unmatched lifecycle object is the flip
   event. Teardown now explicitly unregisters it before closing VideoOut and
   deleting its still-live equeue. The Vulkan-PS5 SystemService-only baseline at
   `20260728T064628Z-system-exit-probe-target.klog` then reproduced exactly one
   `set:1, res:0, amount:0x4000` warning without loading OpenAGC or using GPU,
   VideoOut, equeues, or custom memory. This proves the warning belongs to FW
   5.50/raw-ELF container teardown rather than OpenAGC's VideoOut lifecycle.
8. ✅ Validate isolated Wave32 tessellation. Fused HS and TES records execute
   through the reusable gfx1013 binder and the recovered non-Direct TF-ring
   ABI. The factor ring contains four `4.0` factors, readback contains 255,744
   valid FP16 pixels within the expected equilateral bounds, and the PS5
   displays the centered interpolated triangle with equal sides without a hang
   or kernel panic.
9. ✅ Combine the validated tessellation path with a real NGG geometry shader.
   The centroid-shrink GS executes after TES, producing 155,321 changed FP16
   pixels versus 155,419 expected, eight sampled colors, zero out-of-range
   components, a live completion marker, and 1,800/1,800 display flips. The
   physical display shows the expected equal-sided tessellated triangle with
   dark seams around each colorful microtriangle. The isolated tessellation
   and geometry samples remain controls.
10. Expand Wave32 graphics coverage, then VRS and ray tracing where supported
   by gfx1013 and the PS5 AGC ABI. Combined GS `invocations=2` is
   hardware-validated with two half-scale tessellated copies, deterministic
   127,488-pixel FP16 coverage, and `VGT_GS_INSTANCE_CNT=0x9`. Combined
   line-strip output is also hardware-validated with `out_prim=1`, a colorful
   6,749-pixel wire grid, and retained tessellation state. The isolated direct
   RGBA8 target completes the matrix with repeatable 76,803-pixel coverage and
   physical confirmation of the subdivided centroid-shrink triangle.
11. ✅ Close PA-debug and FRAME_OPEN RE for FW 5.50. The PA-debug version
   export is a userspace permission stub returning `0x8A6D0001` without an
   ioctl, while `FRAME_OPEN` is absent from the kernel dispatcher.

Work:

1. ~~Compile a vertex + pixel shader pair~~ ✅ Done.
2. ~~Set up render target state~~ ✅ Done (CB_COLOR0, tile mode 31, BASE_EXT 0x390, ATTRIB2/3).
3. ~~Set up graphics state~~ ✅ Done (viewport, scissor, blend, prim type, GE_CNTL).
4. ~~Submit a draw call~~ ✅ Done (DCB accepted, GPU alive after draw).
5. ~~Copy render target to display buffer~~ ✅ Done (flexible memory RT → garlic display buffer).
6. ~~Fix VS PGM_LO register patching~~ ✅ Done (0x048/0x049 VS PGM_LO/HI matched).
7. ~~Fix depth re-enable by PS CX block~~ ✅ Done (override after PS CX).
8. ~~Fix SPI_PS_INPUT_CNTL_0 offset~~ ✅ Done (0x191 not 0x1B8).
9. ~~Fix VGT_SHADER_STAGES_EN bit layout~~ ✅ Done (esEn=EsReal 0x10).
10. ~~Fix SPI_SHADER_COL_FORMAT / CB_COLOR0_INFO mismatch~~ ✅ Done (both 8_8_8_8).
11. ~~Add CB_COLOR0_ATTRIB2 for MIP0 dimensions~~ ✅ Done.
12. ~~Remove invalid partial NGG state and correct color/primitive state~~ ✅ Done.
13. **Optionally test the plain-VS diagnostic on hardware.** The corrected
    sample may still determine whether a usable legacy path exists, but this
    is no longer the Phase 7 critical path and must not delay the real NGG
    implementation.

Acceptance criteria:

- ✅ A triangle is visible on the PS5 display, rendered by the GPU.
- ✅ The draw call DCB is accepted by `sceAgcDriverSubmitDcb` without error.
- ✅ Vertex and pixel shaders execute correctly (correct position and RGB gradient).
- ✅ Render target is written correctly by the GPU.
- ✅ RGB vertex-to-pixel interpolation is confirmed by readback and display.

### Experimental approaches that caused a kernel panic (DO NOT RETRY)

These were attempted and caused a PS5 kernel panic (system freeze +
reboot). Do not re-apply these changes without careful analysis:

1. **Mixing compute dispatch into a graphics DCB.** Inserting a
   `DISPATCH_DIRECT` (compute) packet into the same DCB as a graphics
   `IT_DRAW_INDEX_AUTO` draw call, before the graphics state setup,
   caused a kernel panic. The CP may not support switching between
   compute and graphics modes within a single DCB submission. Keep
   compute and graphics in separate DCB submissions.

2. **Enabling RDNA2 NGG (Next-Gen Geometry) mode.** Setting
   `GE_NGG_SUBGRP_CNTL (0x2D3) = 1` and `VGT_SHADER_STAGES_EN (0x2D5) =
   0x8110` (NGG_EN bit + ES_STAGE_REAL) caused a kernel panic. The NGG
   mode requires a valid GS (geometry shader) or NGG passthrough shader
   to be bound; without one, the geometry pipeline crashes the GPU. Do
   not enable NGG mode without a proper NGG-compatible shader setup.

3. **Dual-binding ES and VS SH registers.** Copying VS shader registers
   to both the VS (0x048-0x04B) and ES (0x0C8-0x0CB) stage register
   spaces simultaneously caused instability. The PS5 GPU expects a
   single active vertex-processing stage; dual-binding confuses the
   shader scheduler.

## Phase 8: Firmware Forward Compatibility

Status: common direct-backend profile selection complete. All 39 exact active
keys from FW 3.20 through FW 12.70 are runtime-selectable with per-operation
gates. FW 5.50 and standard-PS5 FW 11.60 are hardware-qualified; other
firmware/model profiles, including PS5 Pro, remain hardware-unverified. FW 1.00,
2.x, and 3.00 remain archival RE profiles, and Sony-export GPU submission is
not selected automatically.

Purpose:

Allow a single game-facing OpenAGC ABI to run across supported PS5 firmware
versions without exposing firmware-private ioctl layouts to applications.
Keep OpenAGC independent of the installed userspace driver: select an exact
firmware profile and issue only statically verified `/dev/gc` operations.

Architecture:

```text
Game -> stable OpenAGC public ABI
     -> exact per-firmware /dev/gc backend
     -> safe AGC_ERROR_UNSUPPORTED result for unknown interfaces
```

### Deferred: FW 3.20 lowest active cross-firmware profile

Recovery and implementation sequence:

1. Inventory FW 3.20 `libSceAgc.sprx` and `libSceAgcDriver.sprx` exports,
   versions, initialization calls, and firmware-sensitive wrappers.
2. Decode queue create/destroy, submit16, suspend, workload, TF-ring, HS
   offchip, internal-memory sizes, and default-state selection from the local
   FW 3.20 references.
3. Compare every private request and structure against FW 5.50. Share code only
   where request values, sizes, field offsets, and semantics match.
4. Add or refine an exact FW 3.20 runtime profile using its four-digit ABI key.
   Keep absent optional operations fail-closed.
5. Add `_Static_assert` checks and byte-exact host fixtures for every differing
   private structure or ioctl argument.
6. Build the generic and Prospero targets. Without matching FW 3.20 hardware,
   label the result **SPRX-confirmed, hardware pending**.

| Capability | Direct `/dev/gc` status |
| --- | --- |
| Firmware selection | Exact four-digit aliases; incomplete profiles fail closed |
| Submission | Submit16 command and layout recovered across inspected families; hardware-qualified on FW 5.50 |
| Queue management | Per-key request, token, and layout evidence required before enabling |
| Suspend points | Primary/final submit and query are independently capability-gated |
| Workloads | The one-ID OpenAGC convenience submit is distinct from Sony's multi-argument nine-dword builders and is enabled only with direct evidence |
| TF ring | Public `0x80108128` and privileged `0xC0108120` roles are separate |
| HS offchip | FW 5.50 and 11.60 use `0xC010812C`; unrelated `0xC008812D` assumptions are rejected |
| Internal memory | Standard and Trinity sizes require exact per-key facts |
| Default states | Register-defaults version is selected only from an exact profile fact |

### Priority 0: Correct unsafe or overstated assumptions

1. Rename the `0xC0108139` verifier check to its actual suspend-final role.
2. Add independent TF-ring checks for the public and privileged commands,
   including direction, immediate size, field offsets, and call semantics.
3. Resolve the HS-offchip `0xC008812D` versus `0xC010812C` discrepancy from
   named wrapper disassembly before enabling either command for another key.
4. Remove the hardcoded defaults-version 8 selection. Add an exact
   `register_defaults_version` fact to each qualified firmware profile; return
   `AGC_ERROR_NOT_SUPPORTED` when it is unknown.
5. Split broad backend eligibility from operation support. An exact firmware
   match may select a backend, but each direct operation must check its own
   capability bit before issuing an ioctl or PM4 submission.
6. Correct status text and verifier success messages so installed export
   presence, RE-qualified direct ABI, and hardware qualification are reported
   as distinct evidence levels.

Acceptance criteria:

- No verifier label names a different ioctl than the command it checks.
- No direct operation runs on a firmware key without an exact capability fact.
- Unknown defaults, TF-ring, HS-offchip, workload, or internal suspend-query ABIs fail
  with `AGC_ERROR_NOT_SUPPORTED` rather than reusing FW 5.50 behavior.
- Generic and Prospero builds pass with no new warnings, and host tests cover
  each disabled/enabled capability boundary.

### Per-operation firmware profile model

Each active four-digit key should resolve to a record containing, at minimum:

- direct capability flags for submission, queue, suspend-submit,
  suspend-query, workload, TF-ring, HS-offchip, memory, and default states;
- ioctl command words and typed argument-layout identifiers for every enabled
  private operation;
- queue tokens, ring/read-pointer/metadata offsets, and authentication policy;
- standard and Trinity memory sizes and working offsets where applicable;
- exact register-default table version;
- provenance identifying the SPRX wrapper, unique fingerprint group, and
  hardware status.

A function name or NID must never enable a direct ioctl without independent
command, layout, and behavior evidence.

### Cross-firmware recovery workflow

1. Extract the relevant named wrapper bodies from all active SPRXs and compute
   normalized fingerprints that ignore load addresses and relocation noise.
2. Group identical implementations. Inspect one representative of every
   unique body, then verify that every profile assigned to the group matches
   the complete command/layout/constant set.
3. Record positive and negative evidence per operation. Missing wrappers and
   permission stubs become explicit unsupported capabilities.
4. Generate verifier fixtures from the recorded facts; do not use a single
   incidental hexadecimal constant as proof of an operation.
5. Add host tests for profile lookup, capability gating, typed structure
   layout, command selection, and nearby-key rejection.
6. Run the clean generic and Prospero builds. Mark the result
   **RE-verified, hardware pending** until matching hardware passes the ordered
   smoke tests.

The generated `analysis/agc_driver_operation_facts.tsv` now provides the
39-key operation ledger and normalized wrapper-group mapping. It is
deliberately conservative: all keys receive only the common carrier-proven
submit16, internal-memory, authenticated-queue, primary-suspend, public TF-ring,
HS-offchip, and async subset. FW 5.50 and standard-PS5 FW 11.60 add only their
separately hardware-qualified operations; remaining capabilities require
exact layout recovery and matching hardware evidence.
The companion `analysis/agc_driver_command_carriers.tsv` now groups the full
private ioctl carrier functions. It proves one common submit16, primary
suspend, and privileged-TF carrier, while preserving the multiple queue,
public-TF, HS, final-suspend, and async groups for explicit review.

### Completed: FW 11.60 modern standard-console reference

FW 11.60 provides the later-firmware reference and includes the runtime
`sceKernelHasTrinityMode` branch. Its queue, suspend, TF-ring, HS-offchip,
memory, default-state, and workload boundaries are recovered. The standard
console passed the complete staged and public-path ladders; Trinity remains
SPRX-qualified and hardware-unverified.

Acceptance criteria:

- Every enabled FW 11.60 direct operation has named-wrapper disassembly,
  command/layout fixtures, and explicit standard/Trinity memory facts.
- Standard and Trinity direct profiles expose distinct, accurate diagnostics.
- Standard-PS5 status is hardware-qualified after two complete public-path
  runs; Trinity stays hardware-unverified until matching hardware is tested.

### Priority 2: FW 3.20 lowest active cross-firmware profile

Recovery and implementation status:

1. FW 3.20 `libSceAgc.sprx` and `libSceAgcDriver.sprx` exports and
   firmware-sensitive wrappers are inventoried.
2. Submit16 and a subset of queue/memory constants are recovered as the
   legacy-v3 family. Suspend, TF-ring, HS-offchip, workload, and default-state
   details still require the workflow above.
3. Exact key `0x0320` selects the legacy-v3 direct family; missing operations
   remain unavailable instead of inheriting the FW 4.00+ surface.
4. Profile-selection tests and the current subset verifier pass. This is not a
   complete direct-backend qualification. Matching FW 3.20 hardware is still
   required before any hardware-supported claim.

Acceptance criteria:

- FW 3.20 is the documented lowest active target and gains a complete
  provenance record for every enabled private operation.
- No FW 5.50 private request is reused solely because FW 3.20 is nearby.
- Exact FW 3.20 aliases select only capabilities proven by its firmware.
- Unknown, FW 1.00, and FW 2.x missing operations continue to fail closed.
- Hardware support is not claimed until the ordered websrv smoke tests pass on
  a matching FW 3.20 console.

### Deferred archival profiles: FW 1.00 and 2.x

- Preserve exact aliases, known submit-family data, FW 1.00's `0x38000` EOP
  evidence, and existing regression fixtures for research value.
- Do not recover the FW 1.00 pre-authentication special queue or other missing
  legacy-only optional ABIs unless real users and matching hardware appear.
- Keep explicit `AGC_ERROR_NOT_SUPPORTED` behavior and do not advertise these
  profiles as supported.

### Priority 2: Stable direct-backend dispatch

- ✅ `AgcDriverOps` preserves the public ABI across generic and Prospero
  implementations.
- ✅ Exact firmware detection, backend selection, and per-operation direct
  capability gates fail closed. FW 5.50 retains its hardware-qualified one-ID
  workload convenience path without claiming Sony export ABI compatibility;
  Standard-PS5 FW 11.60 passed its wrapper-proven operation set twice, plus
  version-12/V10 defaults and real compute execution. Workloads, suspend-query,
  EOP flip, and non-empty HS patch lists remain disabled.
- ✅ The primary Prospero target links no `SceAgcDriver` stub and the
  resulting hardware-test ELF has no `libSceAgcDriver.sprx` dependency.
- ✅ The direct `/dev/gc` backend passed the clean FW 5.500.008 init,
  multi-DCB marker, async queue, suspend-point, and workload sequence
  (`20260729T091752Z-72225`).
- ✅ All direct extractors use `analysis/agc_firmware_versions.tsv`; no
  installed-driver profile is required for runtime selection or RE validation.
- Maintain per-firmware NID/module aliases without assuming NID stability.

### Completed hardware gate: standard-PS5 FW 11.60

- ✅ The original 2026-07-29 power-off was followed by an operation-at-a-time
  staged ladder, with the process-cleanup ELF immediately before every payload.
- ✅ Added unbuffered `agc_fw1160_stage0.elf` (identity only) and
  `agc_fw1160_stage1.elf` (corrected init plus immediate shutdown). Their runner
  launches the process-cleanup ELF before every stage.
- ✅ FW 11.60 stages 0 and 1 each passed twice on standard hardware reporting
  raw `0x11600005` and SoC `0x00840f60`.
- ✅ FW 11.60 stage 2 passed twice: exact internal-memory mappings, DDID
  initialization, and shutdown completed without a freeze or power-off.
- ✅ FW 11.60 stage 3 passed twice: the shared standard-group submit policy
  executed a five-dword `WRITE_DATA` marker after 50 ms and 0 ms, then released
  its memory and shut down cleanly. The same revision passed FW 5.50's complete
  init/multi-DCB/wait/async/queue/suspend/workload regression lifecycle first.
- ✅ FW 11.60 stage 4 passed twice: async setup and direct shutdown returned
  `AGC_OK`. The foreground app's black screen was a loader lifecycle artifact;
  probes now flush PASS and self-terminate instead of requiring manual UI kill.
- ✅ FW 11.60 stage 5 passed twice: authenticated queue create returned handle
  0 and queue destroy plus shutdown returned `AGC_OK`.
- ✅ FW 11.60 stage 6 passed twice: primary suspend returned `AGC_OK` while the
  qualified queue was active; teardown succeeded and ps5debug-NG confirmed no
  residual `eboot.elf` process.
- ✅ FW 11.60 stage 7 passed twice: final suspend and the complete queue
  teardown returned `AGC_OK`.
- ✅ FW 11.60 stage 8 passed twice: an aligned mapped 16 KiB TF ring was
  accepted, followed by clean context shutdown and memory release.
- ✅ FW 11.60 stage 9 passed twice: the HS-offchip zero-entry carrier accepted
  an aligned list pointer. This qualifies the ABI boundary, not non-empty
  patch-list execution.
- ✅ Exact runtime key `0x1160` passed the ordinary public init, memory,
  multi-DCB marker, nine-dword wait64, async, queue, suspend, release, and
  shutdown lifecycle twice. ps5debug-NG found no residual process.
- ✅ The same teardown revision passed the complete FW 5.50 lifecycle,
  including V8 defaults and the FW 5.50 workload extension.
- ✅ FW 11.60 version-12/V10 register defaults passed twice with exact
  79/29/20 primary and 9/15/3 internal dimensions, post-default GPU markers,
  and clean shutdown. FW 5.50 retained its original V8 DDID layout.
- ✅ FW 11.60 headless compute passed twice: the gfx1013 dispatch reached its
  completion fence in 1-2 ms and exactly 2,073,600/2,073,600 pixels matched
  `0xff00ff00`. The original FW 5.50 compute-plus-VideoOut sample then passed.
- ✅ FW 11.60 headless graphics passed twice with Wave32 NGG+PS execution,
  255,744 FP16 pixels, eight sampled colors, no invalid components, and exact
  FNV64 `0x4a40c2eb4f12bc26`. FW 5.50 then passed the same draw and all 1,800
  VideoOut flips.
- Workloads, EOP flip, and non-empty HS patch-list execution remain
  fail-closed or unadvertised.
- FW 11.60 VideoOut preparation no longer reuses the unsafe FW 5.50
  `+0x7e61` patch. Exact SPRX comparison recovered the evolved branch at
  `+0x9922`; the core verifies a firmware-keyed six-byte signature and fails
  closed for other layouts. The pinned public gate now combines live V12
  defaults/async state, a GPU marker, two bounded flips, complete teardown,
  file-backed verdicts, and self-termination. It passed twice on standard FW
  `0x11600005`, including 50 ms GPU markers and zero-valued teardown results;
  ports 8080 and 744 remained reachable. Graphics/compute scanout content is
  still a separate gate; see `analysis/videoout_linear_patch_versions_20260730.md`.
- The public `sceAgcDriverIsSuspendPointInFlightDirect` gap is closed: all 39
  active drivers return userspace permission error `0x8a6d0001` without an
  ioctl. OpenAGC preserves the 32-bit return ABI instead of truncating it to
  `bool` or substituting internal `QUEUE_STAT`. The separate CDBG helper
  remains fail-closed pending its private carrier.
- Other exact active firmware/model profiles are enabled from reproducible
  SPRX evidence but remain hardware-unverified until matching consoles exist.
- Keep deterministic PM4 builders, descriptors, shader parsing/fusion,
  primitive state, and interpolant mapping inside OpenAGC.

Safety and compatibility rules:

- OpenAGC's public ABI remains firmware-independent; games must not include
  private ioctl structures or firmware-specific backend headers.
- Runtime export resolution must use private function-pointer names. Directly
  importing a firmware symbol that OpenAGC also defines risks self-resolution
  or duplicate-symbol behavior.
- Loading the installed Sony driver does not bypass GPU credentials,
  `cr_sceAuthId`, module-global initialization, or other permission checks.
- Unknown firmware must fail safely with `AGC_ERROR_UNSUPPORTED`; it must not
  guess an ioctl ABI.
- Firmware support is declared only after real-hardware validation. Matching
  RDNA2 PM4 packets alone is not sufficient evidence that the userspace/kernel
  submission ABI is compatible.
- Keep firmware binaries, generated proprietary stubs, and microcode outside
  the repository.

Acceptance criteria:

- Existing generic and FW 5.50 behavior is preserved behind `AgcDriverOps`.
- A Sony-export backend resolves and calls installed driver exports without
  colliding with OpenAGC's public symbols.
- Backend selection is deterministic, capability-tested, and fails safely.
- FW 5.50 continues to complete the ordered websrv hardware smoke tests.
- Other firmware families are not called supported until the same tests pass
  on matching hardware.
- Each supported firmware has recorded export provenance, structure
  size/offset assertions, and explicit hardware-validation results.

The internal operations-table migration is complete. The next bounded change
is the FW 5.50 NGG geometry milestone described below. The FW 3.20 exact-profile
audit remains the first cross-firmware task after FW 5.50 matures.

## Phase 9: Higher-Level AGC Features

Status: reusable Wave32 VS+PS is complete on FW 5.50 gfx1013 hardware.
The library validates fused Gs(2)+PS records, patches program addresses, derives
primitive/interpolant state, preflights command-buffer capacity, and emits the
hardware-proven state. Advanced graphics proceeds in this order: NGG geometry,
tessellation, combined tessellation-plus-geometry, cache/synchronization
hardening, VRS, and ray tracing. Each rendering stage must have host fixtures
and a websrv hardware test before the next stage begins.

These are long-term goals and should not block draw-call work.

### Wave32 / Wave64

Status: Wave32 graphics is hardware-validated on real PS5 gfx1013 hardware.
`openagc-psbc --wave32` compiles the no-GS NGG stage with 32-lane waves while
gfx1013 pixel and compute stages remain Wave32 by default. The generated
records carry `VGT_SHADER_STAGES_EN.GS_W32_EN` and
`SPI_PS_IN_CONTROL.PS_W32_EN`; the hardware sample rejects missing bits both
before fusion and in the final PM4 stream.

Compute dispatch now exposes named Wave32 and Wave64 initiator modifiers.
Wave32 ACO compute records must be dispatched with
`AGC_GFX1013_COMPUTE_DISPATCH_WAVE32`; leaving the modifier at its zero
Wave64 default causes divergent Wave32 state to execute with the wrong lane
model. Exact host packet coverage locks the Wave32 initiator to `0x8041`.

Three FW 5.50 runs programmed NGG stage state `0x02412010` and PS control
`0x00008001`, submitted successfully, executed the post-draw marker, and
passed FP16 readback with 255,744 changed pixels, eight sampled colors, and
zero out-of-range components. The display path now matches the hardware-proven
VideoOut contract: a 1920x1080 linear scanout, a centered 768x768 downsampled
preview mirrored into both registered buffers, and one flip-completion wait per
frame. The websrv run completed 1,800/1,800 vsync flips over 30 seconds and was
visually confirmed as a dark-gray background with a centered blended-color
triangle. The validation also corrected the sample's
direct-memory physical offset from a truncating `int32_t` to the ABI-correct
`off_t` and decoupled the FP16 render-pool size from the scanout dimensions.

Goal:

Represent wave-size metadata and shader register state accurately.

Rule:

Do not hard-code performance claims into API behavior. Track wave mode only
where AGC shader records or registers expose it.

### Geometry / Mesh-Style Processing

Goal:

Recover native PS5 geometry setup beyond the classic GNM-style pipeline.

Current FW 5.50 gfx1013 sequence:

1. **Pass-through visual gate closed.** The split-GS ESGS ABI and GFX10
   `GE_CNTL` programming are fixed. Three identical FW 5.50 runs produced
   255,744 covered FP16 pixels, centered bounds, eight sampled colors, no
   out-of-range components, a live marker, and 1,800/1,800 completed flips.
   The physical display confirmed the centered colorful triangle on a gray
   background.
2. **Probe-free path promoted.** The compiler NGG-record regression and OpenAGC
   generic suite pass, and the documentation records the hardware-confirmed
   implementation.
3. **Geometry amplification validated.** The `triangle_amplify.geom` path emits
   two distinct half-scale textured triangles. Captured runs consistently
   produce 127,488 changed pixels, bounds `x=346..1189, y=602..933`, eight
   sampled colors, zero out-of-range components, a live marker, and
   1,800/1,800 completed flips; the physical display was confirmed three times.
4. **Extended geometry coverage validated.** A line-list input (`VGT_PRIMITIVE_TYPE=2`,
   two input vertices) reconstructs the full triangle; `invocations=2` emits two
   half-scale copies with deterministic 127,488-pixel FP16 coverage; and the
   isolated RGBA8 target produces 126,360 changed pixels against a
   dimension-derived expectation of 126,293 +/- 1,024. All three cases preserve
   eight sampled colors, a live marker, and 1,800/1,800 flips, and were confirmed
   on the physical display. The centered square viewport prevents 16:9 scanout
   stretching. Keep render-target variants isolated to one draw per process until
   same-process second-submit sequencing is characterized independently.
5. **Proceed to tessellation only after geometry passes.** Geometry remains
   ahead of tessellation and combined tessellation-plus-geometry so later
   failures do not conflate hull/domain ABI work with geometry ABI work.

Rule:

Do not add task/mesh public APIs until firmware exports, shader metadata, or
register evidence is found.

### Ray Tracing

Goal:

Identify whether AGC exposes ray acceleration state through exports, packets,
registers, shader records, or compiler intrinsics.

Rule:

Keep this as analysis-only until concrete evidence exists.

### Cache Synchronization

Goal:

Map AGC acquire/release/wait/cache-policy fields to observed behavior.

Rule:

Preserve raw fields in builders even before their full meaning is known.

### Variable Rate Shading

Goal:

Recover VRS/rate-image state if firmware/register evidence exists.

Rule:

No placeholder VRS enums in public headers until evidence is found.

## Reference Inputs

- Firmware dump: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50`
- Existing open AGC notes: `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc`
  (NOT proven working — NID mapping only; see `analysis/ps5_openagc_audit.md`)
- RPCSX GPU/PM4/GNM reference: `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`
- PS4 GNM clean rewrite reference: `../opengnm`
- SharpEmu PS5 emulator (AGC HLE reference): `/Users/bizkut/Downloads/PS5/homebrew/sharpemu`
  — C# PS5 emulator with detailed AGC implementation in
  `src/SharpEmu.Libs/Agc/AgcExports.cs`. Confirmed-correct for:
  - Shader type byte encoding at offset 0x5A (CS=0, PS=1, ES=2, VS=3, GS=4,
    HS=5, ES-alt=6, LS=7)
  - Compute dispatch initiator: `(modifier & 0xA038) | 0x41`
  - PGM_LO/HI address encoding: `addr = (HI << 40) | (LO << 8)`
  - Compute register offsets (PGM_LO=0x20C, RSRC2=0x213, NUM_THREAD=0x207-9,
    USER_DATA_0=0x240)
  - RSRC2 USER_SGPR field (bits [5:1]) and system SGPR layout
  - CreateShader PGM_LO/HI patching (scans SH table, handles missing pairs)
- openagc-psbc shader compiler: `../openagc-psbc/`
  — Mesa NIR + ACO based compiler that produces PS5 AgcShaderRecord binaries
  from GLSL/SPIR-V input. Hardware-validated for compute and Wave32 NGG+PS
  graphics shaders.

## Reference Alignment Action Items

Completed:
- [x] Fix `sceAgcDcbDrawIndexAuto` to use `IT_DRAW_INDEX_AUTO (0x2D)` directly
- [x] Fix `sceAgcDcbWaitRegMem` 32-bit variant to 7 dwords with proper control word
- [x] Add reference-confirmed patchers (GetPacketSize, SetPacketPredication,
      SetRangePredication, CondExecPatch*, WriteDataPatchSetAddressOrOffset,
      JumpPatchSetTarget, SetNumRegisters variants)
- [x] Add reference-confirmed GetSize helpers (WriteData, Jump, Rewind, CondExec,
      WaitOnAddress)
- [x] Add missing driver functions (IsCaptureInProgress, DeleteEqEvent,
      GetEqEventType, GetDefaultOwner, InitResourceRegistration, etc.)
- [x] Add ACB descriptor indirection (magic 0x5533ccaa) to prospero backend
- [x] Fix PM4 opcodes (DISPATCH_DRAW_PREAMBLE 0x3A, SET_CONTEXT_REG_INDIRECT 0x9F)
- [x] Update register defaults from reference v8 (703 public registers across
      127 groups, 25 internal registers across 22 groups; replaces incomplete
      HLE-reference-derived data that had only 38/703 public registers with
      wrong zero-placeholder values)
- [x] Add fused shader support (sceAgcGetFusedShaderSize / sceAgcFuseShaderHalves
      with SH register patching, SPI_SHADER_PGM_CHKSUM_GS/LO_ES/LO_LS address
      patching, and vgt_shader_stages_en mismatch validation)
- [x] Replace HLE-reference-derived register defaults with complete v8 data
      as default for NotifyDefaultStates
- [x] Add SubmitMultiDcbs, SubmitCommandBuffer, SubmitMultiCommandBuffers,
      SubmitMultiAcbs convenience wrappers
- [x] Cross-check builder encodings against the reference — fixed 5 builders:
      WriteData (direct IT_WRITE_DATA 0x37), ReleaseMem (cmd[1]/cmd[2] fields),
      CondExec (5 dwords, count at cmd[4]), EventWrite (IT_EVENT_WRITE 0x46,
      variable length), DrawIndexOffset (decode_draw_index_initiator)
- [x] Switch from MIT to Apache 2.0 license (LICENSE file, SPDX headers)

Pending:
- [x] Add version selection for register defaults (v0, v4, v5, v7, v8, v9, v10, v11)

Completed (Phase 5-6, hardware validation):
- [x] Build openagc-psbc shader compiler (Mesa NIR + ACO → AgcShaderRecord)
- [x] Compute dispatch on PS5 hardware (agc_compute.elf — GPU accepts DCB)
- [x] Fix psbc compute SH register offsets (RSRC1=0x212, RSRC2=0x213, RSRC3=0x228)
- [x] Fix AgcShaderType enum encoding (CS=0, PS=1, ES=2, VS=3, GS=4, HS=5, LS=7)
  — confirmed by sharpemu PatchShaderProgramRegisters
- [x] libSceVideoOut.sprx runtime patch (NOP linear tiling check at 0x7e61)
- [x] Websrv homebrew deployment (FTP upload + HTTP /hbldr launch)
- [x] Cross-reference sharpemu AGC implementation for compute dispatch encoding
- [x] Fix DCB submit descriptor (SUBMIT_16 format, 0xC0108102)
- [x] Fix DDID allocation sizes for NotifyDefaultStates (primary=0x41000, internal=0xc000)
- [x] Set compute shader type bit on SET_SH_REG and DISPATCH_DIRECT headers
- [x] Fix PGM_LO address format (shader_addr >> 8, confirmed from KytyPS5)
- [x] Fix WRITE_DATA packet length (5 dwords, was corrupting shader code)
- [x] Discover GPU MMU mapping: flexible memory is GPU-visible, garlic is not
- [x] Enable Compute Units via COMPUTE_STATIC_THREAD_MGMT_SE0..SE3 (0x216/0x217/0x219/0x21A)
- [x] Apply FW 5.50 SH register defaults in compute command buffer
- [x] Confirm user data SGPR layout by RDNA2 disassembly (s2..s5)
- [x] **100% compute shader pixel output verified on PS5 hardware** (2,073,600 / 2,073,600 pixels match 0xFF00FF00)

## Working Rules

- Prefer source-backed packet fields over guessed abstractions.
- Keep host tests ahead of native backend work.
- Keep analysis artifacts next to code:
  - `analysis/agc_known_nids.tsv`
  - `analysis/agc_packet_model.md`
  - future ioctl/register/shader tables
- Separate raw recovered APIs from convenience wrappers.
- Treat PS5 hardware validation as a separate milestone with explicit smoke
  tests.

## Prior Product-Roadmap Checkpoint (Superseded)

### Historical FW 5.50 conformance checkpoint (2026-07-27)

The sample completion path now uses a gfx1013 `RELEASE_MEM` EOP fence rather
than treating a later `WRITE_DATA` marker as proof that shader writes are
globally visible. Compute repeated with 2,073,600/2,073,600 matching pixels;
the corrected-fence NGG baseline, amplification, line, invocation, and
tessellation-geometry-line cases also passed. The line case has direct Chiaki
evidence that the white outer and internal edges connect at the intended
vertices. The complete generic suite passes.

The hardware-proven completion tail and remaining tessellation draw tail are
now public typed APIs. `agcGfx1013SignalEopFence` owns the exact gfx1013 EOP
event/GCR/cache-policy sequence, while `agcGfx1013DrawTessIndexAuto` owns the
validated HS/TES/GS/PS bind, resource, post-bind context, override, instance,
and draw ordering. Hardware samples no longer open-code either sequence.

The remaining frame/launch sequence is also promoted. A single
`AgcGfx1013FrameState` now drives the atomic context-control, clear-state, V8
defaults, color-target, viewport, scissor, target-mask, vertex-bound, and NGG
launch prologue. Both baseline and tessellation draw composers apply its
depth-disabled and clip/raster state after shader binding, preserving the
hardware-proven overwrite order.

The complete 17-sample invocation of
`make -C samples/hw_test conformance_fw550` now passes on a fresh FW `0x0550`
console session. The runner preserves logs and fails closed on a transport
timeout, instant close, firmware mismatch, missing output gate, or application
failure. Revision `03b43f2` completed 17/17 sequential launches in 434 seconds;
retained evidence is summarized in
`analysis/fw550_qualification_03b43f2.md`.

The additional render-target host milestone is implemented as a typed gfx1013
format table rather than more sample-local CB constants. Eleven linear UNORM
and FLOAT presets resolve their exact CB format, number type, component swap,
pixel size, and SPI export format, and every resulting 28-dword color-target
stream is locked by a host fixture. New presets beyond the hardware-proven
RGBA8/BGRA8 and RGBA16 FLOAT paths remain explicitly hardware-unqualified;
future websrv runs should advance them individually before enabling compression
or multisampling.

The host-side synchronization milestone is implemented as typed gfx1013
resource transitions spanning render, compute, copy, shader-read, presentation,
and host-read usage. Producer completion preserves the hardware-proven
`RELEASE_MEM + NOP` stream, while GPU consumers append the authoritative full
gfx103 `ACQUIRE_MEM` packet and cache controls. Exact order, no-op, and atomic
failure fixtures are required gates. The acquire-bearing transitions remain
hardware-unqualified until the FW `0x0550` websrv matrix can exercise
render-to-shader, compute-to-copy, copy-to-shader, and present-to-render cases.

Typed blend and depth/stencil control is implemented as deterministic gfx1013
packet groups. Blend covers eight MRT equations, write masks, and constants;
depth/stencil covers depth test/write/bounds plus independent front/back
compare, operations, reference, compare mask, and write mask. Host fixtures
lock the full streams and atomic failures. This completes control-state
encoding only: depth-surface allocation/binding and FW `0x0550` execution must
be qualified before depth or stencil support is advertised as hardware-ready.

The section below is the prior SDK and FW 5.50 release-plan snapshot. It is
retained to preserve completed milestone context, but the authoritative product
roadmap at the top of this file now governs ordering. In particular, native
device/pipeline objects, shader/attachment validation, resource ownership,
firmware-neutral endpoint qualification, and the reference game take priority
over speculative low-level breadth.

Keep implementation and validation work narrowly focused on GPU ABI
compatibility, shader compilation, PM4 state, resource management, submission,
and display integration. Exploit setup and security-bypass mechanics belong to
the launcher environment and hardware-test scaffolding, not the public OpenAGC
API.

## Prior Release Definition (Superseded)

OpenAGC reaches its first usable release when a third-party homebrew project
can install the SDK, use documented public headers and CMake targets, compile
shaders with `openagc-psbc`, create the required GPU resources, record and
submit compute or graphics work, synchronize it, and present stable frames on
FW 5.50. The same application must build against the generic host backend for
packet and ABI tests. Failures must return documented errors rather than hang
the GPU, freeze the UI, or panic the kernel.

## Prior Execution Plan (Superseded)

This snapshot remains useful as a record of the consumable-SDK and low-level
builder program. Do not use its phase numbers to choose new work; use the
authoritative product roadmap near the top of this file.

### Phase 1: Ship a consumable SDK (complete)

1. Finish and validate install/export support for both `generic` and
   `prospero` builds.
2. Install public headers, `libopenagc.a`, the host `openagc-psbc` compiler,
   CMake package metadata, namespaced targets, license, and documentation.
3. Prove a clean downstream project can use `find_package(OpenAGC)`, link
   `OpenAGC::openagc`, invoke `OpenAGC::psbc`, and compile a shader through the
   provided CMake helper.
4. Produce a relocatable versioned archive suitable for a homebrew toolchain.
5. Keep the public package free of firmware blobs, decrypted modules,
   launcher-specific credential code, and host-machine paths.

Exit criteria met: clean generic and Prospero installs and downstream builds
pass; the installed host compiler compiles the real compute SPIR-V fixture from
both consumer configurations; and versioned generic and Prospero TGZ archives
are generated. The package contains the public headers, `libopenagc.a`,
`openagc-psbc`, relocatable CMake metadata, license, and documentation.

### Phase 2: Stabilize the FW 5.50 runtime boundary

1. Turn the hardware-proven `/dev/gc` initialization, internal-memory,
   default-state, queue, submit, suspend-point, and workload paths into a
   coherent runtime lifecycle.
2. Define explicit ownership and teardown rules for contexts, queues, mapped
   memory, command buffers, shaders, and display buffers.
3. Validate all sizes, alignments, firmware-profile fields, packet capacities,
   and user pointers before issuing ioctls or submissions.
4. Add bounded waits, submission diagnostics, and recovery-safe error paths so
   malformed or unsupported state fails before reaching the kernel.
5. Treat `FRAME_OPEN` returning `EINVAL` and PA debug returning `EPERM` as
   documented FW 5.50 capability results unless new firmware evidence proves a
   supported userspace path.
6. Audit every path associated with prior UI freezes and kernel panics,
   especially NGG state, queue lifetime, command-buffer bounds, synchronization,
   and register programming. Never expose uncertain state as a default builder.

Exit criteria: repeated websrv runs of init, compute, graphics, queue teardown,
and relaunch complete without a GPU hang, UI crash, or kernel panic.

### Phase 3: Complete reusable command and resource APIs

1. Replace sample-only PM4 sequences with typed OpenAGC builders for shader
   binding, render targets, viewport, scissor, draw, dispatch, barriers,
   cache management, events, and synchronization.
2. Provide resource helpers for garlic/onion memory, GPU virtual addresses,
   alignment, buffers, textures, render targets, depth targets, and shader
   records without hiding required hardware constraints.
3. Add state objects or descriptor structs that separate validation from PM4
   emission and make command-buffer capacity requirements predictable.
4. Preserve low-level builders for expert users while offering a documented
   minimal path for ordinary homebrew applications.
5. Add host fixtures for every hardware-proven packet stream, including exact
   Wave32 VS/PS, compute, render-target, viewport, scissor, draw, NGG, geometry,
   and tessellation state where validated.

Exit criteria: hardware samples use public library builders rather than local
packet assembly, and generic fixtures lock down headers, payloads, cursor
advance, rejected inputs, and ABI layouts.

### Phase 4: Make shader compilation a supported pipeline

1. Define and version the `AgcShaderRecord` contract shared by
   `openagc-psbc`, the public headers, and runtime binders.
2. Validate gfx1013 explicitly. Do not silently treat the PS5 as gfx1030 or
   infer behavior solely from a generic `gfx103.json` profile.
3. Support documented compute, vertex, pixel, NGG, geometry, hull, and domain
   stage inputs in an incremental feature ladder.
4. Validate register metadata, user-SGPR layout, Wave32/Wave64 requirements,
   fused-stage records, resource limits, scratch/LDS use, and stage linkage at
   compile time whenever possible.
5. Emit actionable compiler diagnostics for unsupported SPIR-V capabilities,
   stage combinations, interpolation, tessellation modes, and resource use.
6. Add compiler fixtures and end-to-end tests from source shader to installed
   compiler output to runtime binding.

Exit criteria: shader records are reproducible, version checked, rejected when
incompatible, and consumed without sample-specific register knowledge.

### Phase 5: Finish the graphics feature ladder on gfx1013

Advance only after the preceding rung is stable under repeated FW 5.50 runs:

1. Compute dispatch and memory visibility.
2. Wave32 VS/PS triangle, indexed and non-indexed draws, multiple draws, and
   correct render-target synchronization.
3. Additional render-target, vertex, index, texture, sampler, blend, depth,
   stencil, and multisample formats/states.
4. Stable pass-through NGG geometry with conservative, validated defaults.
5. NGG geometry-shader amplification and stream behavior.
6. Tessellation control/evaluation, ring allocation, factor buffers, patch
   constants, and HS/DS linkage.
7. Offscreen rendering, render-to-texture, mip levels, and copy/resolve paths.
8. HDR rendering and presentation, kept as two independently qualified gates:
   add `R11G11B10_FLOAT` and other HDR-capable typed targets; implement FP16
   or 10-bit presentation buffers; add Rec.2020/PQ conversion and any required
   VideoOut HDR metadata; lock the color pipeline with deterministic host and
   GPU-readback fixtures; then verify on FW 5.50 that the connected display
   enters HDR mode. HDR-range offscreen rendering must not be reported as HDR
   presentation until the VideoOut signal is hardware-confirmed.

Every rung needs a deterministic host fixture, a minimal hardware sample, a
documented expected screen/result, repeated websrv validation, and a negative
test that rejects unsafe configuration. A single successful frame is evidence,
not completion.

### Phase 6: Stabilize the homebrew-facing API from real requirements

1. Stabilize homebrew initialization, memory, shader, resource, draw, compute,
   synchronization, and presentation APIs as coherent vertical slices.
2. Move generally useful gfx1013 state and PM4 construction into reusable
   builders; do not add title-specific runtime paths.
3. Give every feature an exact host fixture and a focused FW 5.50 hardware
   sample before calling it supported.
4. Maintain buildable examples and public documentation for homebrew authors.
5. Retain Sony-compatible names when they improve source portability, while
   keeping unsupported behavior explicit rather than adding success stubs.
6. Treat Subnautica, DRAGON QUEST VII Reimagined, and other retail binaries as
   bounded ABI/NID evidence only. Audit one only when it resolves a missing
   structure, calling convention, firmware variant, or common API shape.

Exit criteria: representative homebrew samples build against documented public
APIs and pass deterministic host plus focused PS5 hardware gates. Retail import
counts are never a release criterion.

### Phase 7: Establish a release-grade validation matrix

1. Keep the generic clean build and complete host suite as the mandatory gate
   for every goal.
2. Add ABI size/offset assertions, packet golden fixtures, malformed-input
   tests, compiler fixtures, and downstream package-consumer tests.
3. Maintain FW 5.50 websrv tests for VideoOut, initialization, compute,
   graphics, NGG, geometry, tessellation, stress/relaunch, and teardown.
4. Record each hardware run with build commit, firmware ID (`0x0550`), sample,
   expected result, observed result, iteration count, and any crash/hang.
5. Require repeated cold-launch and relaunch success before marking a hardware
   feature stable.
6. Keep hardware-test-only launcher setup outside reusable OpenAGC code.

Exit criteria: a release checklist can be executed from a clean checkout and
produces traceable host, packaging, compiler, and FW 5.50 hardware evidence.

### Phase 8: Add firmware profiles without destabilizing FW 5.50

1. Identify firmware numerically with four hex digits such as `0x0550` and
   `0x0320`; aliases are optional display metadata, never lookup keys.
2. Isolate firmware-dependent ioctls, queue layouts, memory regions, exports,
   and capabilities behind explicit profiles selected at runtime.
3. Keep FW 5.50 as the reference profile until the API and graphics ladder are
   mature.
4. Bring up FW 3.20 next using the same host fixtures and hardware-validation
   gates where hardware becomes available.
5. Preserve FW 1.00 and 2.x evidence as archival data only. Do not spend release
   work on their legacy special-queue ABI or advertise them as supported.
6. Refuse unknown firmware conservatively instead of guessing a close profile.

Exit criteria: firmware variation is data-driven, unsupported versions fail
safely, and adding a profile does not fork the public API.

### Phase 9: Release and maintain

1. Publish versioned API/ABI and shader-record compatibility policies.
2. Provide minimal compute and graphics starter applications using only the
   installed SDK surface.
3. Document supported firmware, hardware, shader stages, formats, known errors,
   and validation evidence without overclaiming game compatibility.
4. Add semantic release notes and an upgrade guide for behavior or ABI changes.
5. Keep `README.md`, `STATUS.md`, this plan, installed package metadata, and
   samples synchronized at every completed goal.

## Immediate next goals

### Completed host goal: typed gfx1013 depth-surface binding

OpenAGC now has a reusable, atomic `agcGfx1013SetDepthSurface` builder for
gfx1013 depth/stencil memory state. It covers typed D16, D32, S8, D16+S8, and
D32+S8 surfaces; independent depth/stencil read and write bases; gfx103
swizzle modes; mip and array-layer views; 1x/2x/4x/8x samples; read-only
aspects; and optional HTILE/expclear state. Exact host fixtures lock the
27-dword register stream, including explicit `DB_HTILE_SURFACE`, 48-bit
address splitting, and negative
fixtures prove that malformed formats, aspect/address combinations, sample
counts, alignment, and undersized command buffers fail atomically.

The isolated FW `0x0550` D32, stencil, 4x MSAA, compressed HTILE, and typed
HTILE decompress/resummarize qualifications are complete. Compressed depth is
validated through deterministic color outcomes and changed metadata. The
typed operation gate then expands it to exact host-readable D32 and rebuilds
nontrivial HTILE ranges in a separate full-surface raster pass.

The first hardware sample is `samples/hw_test/agc_depth.elf`.
It uses an uncompressed D32-only 64KB-Z-X surface with HTILE disabled, performs
GPU initialization plus deterministic near-pass, overlap-fail, and independent
far-pass draws, and checks four stage markers, an EOP completion marker, RGBA8
samples, coverage, and raw D32 values. FW `0x05500008` produced all four stage
markers, 128,304 green and 128,304 red pixels, expected raw depth values, and
1,800/1,800 completed flips without a hang or kernel panic.

Depth/stencil synchronization is also typed. `DEPTH_STENCIL_WRITE` releases
gfx1013 DB metadata with event `0x2c`, releases DB data with timestamp event
`0x2b`, and uses the shared GCR acquire when the consumer remains on-GPU.
`DEPTH_STENCIL_READ` participates as a read-only usage, so read-to-read changes
remain explicit zero-dword transitions. Exact host fixtures cover DB-to-DB,
DB-to-host, read-to-read, and undersized atomic failure paths. The depth sample
uses separate color and depth-to-host transitions before CPU readback.

The reusable `agcGfx1013SetHtileOperation` builder emits an atomic three-dword
`DB_RENDER_CONTROL` packet. Depth decompression uses
`DEPTH_COMPRESS_DISABLE | DECOMPRESS_ENABLE` (`0x1040`), depth
resummarization uses `RESUMMARIZE_ENABLE` (`0x0010`), and the neutral state
restores zero. The hardware sample disables ordinary depth and color writes,
uses full-surface raster passes, brackets the modes with typed DB
release/acquire transitions, and restores neutral state before completion.
FW `0x05500008` accepted the 2,695-dword DCB, reached its fence in 1 ms,
recovered exact D32 counts of 909,792 clear, 128,304 near, and 128,304 far
words, produced 4,226 non-initial HTILE words after resummarization, and
completed 1,800/1,800 flips.

Depth-only expclear is also hardware-validated. The typed
`agcGfx1013SetDepthExpclear` builder accepts only the gfx10.3 canonical 0.0 or
1.0 clear values and emits `DB_DEPTH_CLEAR` atomically. The isolated gate
initializes HTILE to the depth-one clear encoding `0xfffffff0`, enables
`DB_Z_INFO.ALLOW_EXPCLEAR`, and deliberately omits the old full-surface depth
initialization draw. Near-pass, overlap-fail, and independent far-pass draws
therefore consume only metadata-backed clear depth. The proven decompression
pass recovered 918,432 exact 1.0 D32 words, 128,304 near words, and 128,304 far
words; all 49,152 HTILE words changed after resummarization; and the FW
`0x05500008` run completed 1,800/1,800 flips. The physical display showed the
expected green and red triangles on dark gray.

Combined D32+S8 HTILE is hardware-validated separately with expclear disabled.
Shared metadata starts at gfx10.3 combined uncompressed `0xfffff30f`. The
typed combined decompression mode emits
`STENCIL_COMPRESS_DISABLE | DEPTH_COMPRESS_DISABLE | DECOMPRESS_ENABLE`
(`0x1060`), followed by the proven resummarization and neutral-state restore.
FW `0x05500008` recovered 909,792 clear, 128,304 near, and 128,304 far D32
words; raw S8 contained 2,364,832 zero bytes, 256,608 `0x5a` bytes, and no
other values; all 49,152 HTILE words changed; and VideoOut completed
1,800/1,800 flips without a hang or reset.

The isolated HTILE subresource milestone is complete on FW `0x05500008`.
Mip 1 of a two-level D32 image and layer 1 of a two-layer D32 image both passed
exact selected-versus-untouched metadata readback gates and sustained preview.
The mip run also established that viewport/scissor must match the selected mip
extent; using the base extent legitimately rasterized beyond the mip attachment
and changed adjacent metadata. See
`analysis/fw550_htile_subresources_20260727.md`.

The hardware-sample PM4 promotion audit is also complete. Normal real-PS5
sample paths contain no hand-packed headers, direct command allocation, or raw
register emission. Intentional markers, repeated diagnostic draws, the PM4
decoder, and emulator export-conformance calls remain low-level by design; see
`analysis/sample_pm4_public_api_audit.md`.

Combined stencil/HTILE expclear is complete and hardware-enabled. The typed
plan, exact-range Wave32 compute RMW, selective clear-register and depth-surface
state, and DB/compute synchronization are covered by exact host fixtures.
Depth-only, stencil-only, and both-aspect FW `0x0550` cases each passed twice
with the public gate off: exact selected-range values, zero metadata spill,
reserved-bit preservation, D32/S8 readback, fences, draw markers, and
1,800/1,800 flips all passed without a reset or panic. See
`analysis/fw550_combined_expclear_qualification_20260727.md`.

Next execution order:

4. **Host complete:** application-facing typed indexed drawing now composes
   u16/u32 buffers, first-index address adjustment, instance count, and
   `DRAW_INDEX_2`. Exact fixtures lock packet order, range encoding, and atomic
   short-buffer rejection. Base-vertex values remain shader ABI state; the PM4
   packet exposes no direct base-vertex value field.
5. **Host complete:** application-facing single/multi indirect composition now
   covers indexed and non-indexed arguments, argument-base and offset
   alignment, stride validation, register locations, and exact golden streams.
   The hardware-qualified consumer retains its PS5 7-dword fixed-count form.
   The separately recovered Sony public exports now expose their native
   ten-dword count-address layout across all 39 active profiles, but remain
   SPRX-qualified/hardware-unverified and are not substituted into this path.
   A bounded 2026-07-28 test of a different Mesa-style gfx10+ ten-dword form
   caused a GPU fault and reset. DrawIndex control stays reserved in the typed
   consumer until the Sony form is independently hardware-qualified.
6. **Hardware complete:** direct u16 indexed, non-indexed indirect, and u16
   indexed-indirect each passed separately on FW `0x0550` through curl/websrv
   with exact FP16 coverage, completion fence, Wave32 audit, and 1,800 flips.
   The recovered `SET_BASE` wrapper requires canonical header control zero;
   passing one timed out and is now covered by an exact regression fixture.
7. **Hardware complete:** RGBA8 SRGB and BGRA8 SRGB append public enum values
   12 and 13 without renumbering existing formats. Exact fixtures lock CB
   format `0x0a`, CB sRGB number type `6`, standard/alternate component swaps,
   FP16_ABGR export, and `CB_COLOR0_INFO` values `0x00010628`/`0x00010e28`.
   Both identical ELFs passed twice on FW 5.50 with native packed-memory
   transfer oracles, exact coverage, Wave32 PM4 state, two completion fences,
   and 1,800/1,800 flips.
8. **Hardware complete:** R16_FLOAT and RG16_FLOAT use typed CB formats `0x02`
   and `0x05`, FLOAT number type `7`, standard swap, and FP16_ABGR export.
   Each format passed twice on FW `0x05500008` with deterministic native
   packed-memory hashes, exact complete-component checks, Wave32 audits,
   completion fences, and 1,800/1,800 flips. Live ps5debug-NG logs contained
   no GPU fault or reset signatures. One earlier R16 launch coincided with an
   unlogged console shutdown, so its cause remains unproven; event setup and
   VideoOut teardown are now checked explicitly.
9. **Hardware complete:** uncompressed D16 depth-only uses the typed
   `D16_UNORM` `64KB_Z_X` layout and surface binder with HTILE, stencil, MSAA,
   and expclear disabled. Two identical FW `0x05500008` runs produced exact
   128,304-pixel near/far color outcomes and exact 909,792/128,304/128,304
   native clear/near/far D16 counts, passed all markers and fences, completed
   1,800/1,800 flips, and left clean live kernel logs.
10. **Hardware complete:** uncompressed S8-only uses the typed `S8_UINT`
   `64KB_Z_X` layout and binder with no depth plane, zero depth addresses, and
   depth testing/writes disabled. Two identical FW `0x05500008` runs proved
   stencil replace and compare rejection with exact 128,304 green/red color
   counts, 2,364,832 zero plus 256,608 `0x5a` S8 bytes, all markers/fences,
   1,800/1,800 flips, and clean live kernel logs.
11. **Hardware complete:** uncompressed D16+S8 uses separate typed
   `64KB_Z_X` planes with HTILE, MSAA, and expclear disabled. Two identical FW
   `0x05500008` runs reproduced the exact D16 and S8 native counts from the
   independent gates, exact green/red color counts, all markers/fences,
   1,800/1,800 flips, and clean live kernel logs.
12. **Hardware complete:** compressed D16/HTILE uses the recovered gfx1013
   2048x1152 D16 and 2048x1536 metadata layouts, depth-only `0xfffc000f`
   initialization, D16 `ZRANGE_PRECISION`, and typed depth decompression plus
   resummarization. Two identical FW `0x05500008` runs each changed 4,226
   metadata words, recovered exact 909,792/128,304/128,304 native D16 counts,
   produced exact 128,304 green/red color counts, passed all markers and 1 ms
   fences, completed 1,800/1,800 flips, and left clean live kernel logs.
13. **Hardware complete:** D16 HTILE expclear uses the exact host-locked
   `DB_Z_INFO` word `0xaf800181` (`ALLOW_EXPCLEAR=1`,
   `DECOMPRESS_ON_N_ZPLANES=15`, and `ZRANGE_PRECISION=1`) and canonical
   depth-one metadata `0xfffffff0`. Two identical FW `0x05500008` runs each
   changed all 49,152 metadata words, recovered exact
   918,432/128,304/128,304 native D16 counts, produced exact 128,304 green/red
   color counts, passed every marker and 1 ms fence, completed 1,800/1,800
   flips, and left clean live kernel logs.
14. **Hardware complete:** linear R8 and RG8 UNORM use CB formats `0x01` and
   `0x03`, number type `0`, standard swap, and FP16_ABGR export selected by the
   reusable frame post-bind builder. Two identical runs per format produced
   stable 255,043/255,744-pixel coverage and FNV64
   `0x6fe253259c7b0455`/`0x6babce1afaa81b2c`, passed Wave32/marker/fence checks,
   completed 1,800/1,800 flips, and left clean live kernel logs.
15. **Hardware complete:** linear R32 FLOAT uses CB format `0x04`, FLOAT number
   type `7`, standard swap, and the format-derived 32_R shader export. Two
   identical runs each stored 255,744 complete pixels, eight distinct values,
   no invalid float components, and FNV64 `0x43e0f1986c4ec883`; all checks and
   1,800/1,800 flips passed with clean kernel logs.
16. **Hardware complete:** linear RG32 FLOAT uses CB format `0x0b`, FLOAT
   number type `7`, standard swap, and the format-derived 32_GR export. Two
   identical runs each stored two complete components for 255,744 pixels,
   eight distinct values, no invalid floats, and FNV64 `0x806171be9908c276`;
   all checks and 1,800/1,800 flips passed with clean logs.
17. **Hardware complete:** linear RGBA32 FLOAT uses CB format `0x0e`, FLOAT
   number type `7`, standard swap, the format-derived 32_ABGR export, and a
   16-byte-per-pixel allocation. Two identical runs each stored four complete
   components for 255,744 pixels, eight distinct values, no invalid floats,
   and FNV64 `0x1e8771ed63381dce`; all checks and 1,800/1,800 flips passed with
   clean logs. The R8/RG8/R32/RG32/RGBA32 format-expansion goal is complete.
18. Complete the bounded Subnautica evidence cleanup, then return to stable
   homebrew-facing vertical slices and examples. Do not expand Subnautica or
   DRAGON QUEST VII into retail-runtime compatibility goals; consult those
   binaries only when a missing ABI contract blocks reusable homebrew work.

## Gfx1013 4x MSAA depth gate (hardware validated)

The reusable FW `0x0550` preparation for the isolated 4x gate is complete:

- Typed `agcGfx1013SetSampleState` emits `PA_SC_AA_CONFIG`, `DB_EQAA`,
  `PA_SC_MODE_CNTL_0`, `PA_SC_MODE_CNTL_1`, four sample-location registers,
  centroid priority, and both coverage masks for exact 1x/4x state. Baseline
  and tessellation draw states apply it after shader binding; 1/2/4 pixel
  iterations are host-locked and the 2x/4x modes are FW 5.50-qualified.
- Typed `agcGfx1013GetColorSurfaceLayout` covers 4x `64KB_R_X` color
  allocations, while the existing D32 layout covers 4x `64KB_Z_X`.
- Color-target binding now types the sample/fragment counts and swizzle mode;
  gfx1013 image descriptors type 2D-MSAA, log2 sample count, and R_X swizzle.
- `agcGfx1013ResolveColor4x` performs the render-to-shader transition, builds
  the 1x destination frame, restores 1x state after defaults, and executes a
  caller-supplied fullscreen shader draw. It intentionally does not use the
  unsupported gfx10 legacy `CB_RESOLVE` mode.
- `agc_depth_msaa.elf` renders the D32 pass/fail fixture into separate 4x
  color/depth images and averages four samples into the 1x VideoOut buffer.
  Stencil and HTILE are explicitly disabled.

FW `0x05500008` passed this gate repeatedly through curl/websrv on 2026-07-27.
The 5,131-dword DCB produced 127,818 exact green and 127,818 exact red pixels,
all four draw-stage markers, the completion fence, and nonzero initialization,
near, and far raw D32 classes. VideoOut completed 1,800/1,800 flips. The
captured physical result showed the expected green/red triangles and resolved
edges on the dark-gray framebuffer without a hang or kernel panic.
