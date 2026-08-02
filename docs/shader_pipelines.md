# Shader reflection and pipelines

OpenAGC targets only the PS5 gfx1013 GPU. The generic backend exercises this
contract on a host; it is not a second GPU target.

## Shared reflection ABI

`openagc/shader_reflection.h` defines the pointer-free
`AgcShaderReflection` record shared with `openagc-psbc` API v15. The compiler
fills it from SPIR-V, NIR/ACO, and the generated `AgcShaderRecord`; applications
must not invent or patch compiler-derived fields. The record is copied by value
and includes explicit size/version fields and reserved-zero space.

The reflection identifies the stage and entry point, compiler and shader-record
versions, FNV-1a hash of the serialized main/front binaries, code ranges, wave
size, descriptor mappings, user/system SGPRs, push ranges, vertex inputs,
pixel exports, local size, scratch/LDS, sample behavior, NGG/fused-stage state,
adjacent-stage linkage masks, the embedded front stage and its interface masks,
and geometry input/output topology, vertex limits, and invocation count.
Reflection v2 retains the fixed 5,744-byte serialized size; the runtime also
accepts existing v1/compiler-API-14 records with their former reserved tail.
The compiler's FNV-1a stage-linkage hash covers the four interface masks; the
runtime recomputes it and rejects altered linkage metadata before creating a
shader handle.

For file-based builds, `openagc-psbc --reflection-header <path>` emits the
matching serialized reflection as a pointer-free C byte artifact. Pair that
header with the `.sb` output from the same compiler invocation and copy its
bytes into `AgcShaderReflection` before `agcCreateShader`; do not recreate
metadata from the shader record or application conventions. Use
`--reflection-symbol <identifier>` when a translation unit embeds more than
one artifact. Pass file-based resource layouts with
`--descriptor-binding SET:BIND:TYPE:COUNT` and
`--push-constant-size BYTES`; the generated reflection is the source of truth
for the corresponding native pipeline descriptor.

`samples/hw_test/shaders/runtime_triangle.*` is the small graphics artifact
pair used by the native-runtime host contract and `agc_runtime_graphics.elf`:
the vertex invocation emits an NGG GS-back record plus an ES-front executable
record and reflection sidecar; the fragment invocation emits two color exports,
a matching MRT0/MRT1 epilogue, and its reflection sidecar. The probe consumes
each record and sidecar exactly as produced, with no application-local register
metadata.

Create a reflected shader with `AGC_SHADER_DESC_INIT`, setting `code`,
`code_size`, and `reflection`; fused records also set `front_code` and
`front_code_size`. `agcCreateShader` validates the complete contract before the
handle becomes visible. `agcGetShaderReflection` returns the runtime-owned
validated copy. Compiler-emitted gfx1013 NGG bundles use a `GsBack` main record
and a `GsFront` front record (and tessellation control uses the analogous
`HsBack`/`HsFront` pair). The runtime validates both halves and rejects an
orphan back record rather than treating its binary-subtype byte as a standalone
shader stage.

## Pipeline validation

`AGC_GRAPHICS_PIPELINE_DESC_INIT` and `AGC_COMPUTE_PIPELINE_DESC_INIT` create
runtime API v2 descriptors. The graphics descriptor supplies reflected shader
handles plus exact vertex, descriptor, push, attachment, rasterization,
depth/stencil, multisample, and dynamic-state declarations. The compute
descriptor supplies its reflected shader, exact resource layout, and exact
threadgroup size.

Pipeline creation is transactional. An export/attachment class or width
mismatch, missing or extra MRT, integer blend, unsupported attachment,
depth/sample mismatch, stage-link failure, vertex-layout difference,
descriptor/push mismatch, unsupported wave/scratch/LDS requirement, or
unsupported state returns an error and leaves the output handle `NULL`.
Command binding preflights capacity and required resources; failure leaves the
command cursor unchanged.

The host compatibility matrix crosses every currently exposed color attachment
format with each supported reflected export class and width. `DEFAULT`, `32_R`,
and `32_GR` exports remain fail-closed because this runtime API does not expose
matching native R/RG color targets.

The graphics bind sequence derives the packed `SPI_SHADER_COL_FORMAT` nibbles
from the validated reflected pixel exports after applying the shader record.
This makes the immutable runtime pipeline contract authoritative even when a
compiler record contains a stale context-register value.

Dual-source exports and `SRC1_*` blend factors are not exposed by this runtime
slice. The reflection ABI has no secondary-export-index contract, so accepting
either form could bind an undefined source. Pipeline creation returns
`AGC_ERROR_NOT_SUPPORTED` before command emission; the lower-level dual-source
builder's separate evidence does not promote this native API path.

Graphics scratch is not yet packaged and therefore fails pipeline creation.
Compiler-reflected gfx1013 LDS sizes are bounded before bind generation;
tessellation additionally uses the explicit reflected hull-LDS requirement.

## Resource and dynamic binding

Indirect graphics commands derive base-vertex, first-instance, and optional
draw-index register locations solely from shader reflection. The public API
accepts only typed argument buffers, offsets, counts, and strides; it never
accepts raw GPU addresses or SGPR locations. A shader lacking the reflected
locations required by the hardware indirect packet fails closed.

After binding a pipeline, use `agcCmdBindDescriptors` for exact reflected
descriptor-array coverage, `agcCmdBindVertexBuffers` for every reflected vertex
binding, and `agcCmdPushConstants` for declared stage ranges. The runtime owns
the GPU-visible resource-table arena and derives descriptor bytes and user-SGPR
addresses; applications never supply raw table addresses or user-data
registers. A shader may reflect either one direct address SGPR per used set or
one indirect-set SGPR. For the indirect form, the runtime builds and publishes
an eight-entry table of 32-bit PS5 address-space pointers; mixed direct and
indirect declarations in one shader fail pipeline creation.

Each v1 descriptor write names one set, binding, and array element. A bind must
cover every declared element exactly once. Buffer/image/sampler type, usage,
device ownership, offset, range, and stride are validated as a transaction.
An explicitly written null descriptor is encoded as all-zero bytes. A null
buffer requires zero offset, range, and stride; combined image/sampler writes
may independently leave either typed handle null. Null writes still count
toward exact reflected array coverage but retain no absent resource.
Successful bindings retain their resources until command-buffer reset or
destruction; rejected bindings retain nothing. Push-constant coverage is
tracked per stage, so draw or dispatch fails with
`AGC_ERROR_RESOURCE_NOT_BOUND` until every reflected dword is initialized.
Sampled, combined image/sampler, storage-image, and input-attachment writes
also require the underlying whole image to have a compatible typed state on
the recording queue. Cross-queue release alone is insufficient: the descriptor
remains unavailable until the matching acquire records on the consumer command
buffer. The generic render-to-shader fixture locks this ordering before its
reflected compute dispatch. The exact FW 5.50 endpoint oracle additionally
renders an MRT triangle, hands one target to compute without a CPU wait, and
matches all 4,096 sampled results against direct image readback.

A graphics command buffer may bind a compute pipeline, transition storage
resources to `kAgcResourceUsageShaderWrite` with graphics ownership, and issue
dispatches in the same ordered DCB as graphics work. This is intentional: the
native graphics carrier supports compute packets and avoids splitting APIs
such as Vulkan into order-losing parallel streams. Compute command buffers
continue to support the same shader-write state; graphics-only attachment and
scanout states remain rejected on compute queues.

A graphics pipeline whose reflected pixel shader exports color must bind one
`AgcColorTargetBinding` per declared attachment with
`agcCmdBindColorTargets` before drawing. Version-2 bindings may select a
mutable-format-compatible attachment encoding; version 1 retains its original
reserved-zero field. The command validates the exact effective format, sample
count, dimensions, image usage, subresource layout, and target-base
alignment, then retains the images through command-buffer reset. Rebinding
targets in the same command buffer, incompatible MRT layouts, and a draw with
an unbound declared attachment fail before work emission. This is attachment
state only: clears, load/store operations, and transitions have not been
folded into the runtime yet.

For a graphics pipeline with a declared depth/stencil format,
`agcCmdBindDepthStencilTarget` is required before drawing. It validates the
exact depth format and sample count against a
`AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT` image, records the qualified gfx1013 depth
surface, and retains the image until reset. The current binding is intentionally
single-mip; packed depth-mip policy, clears/load-store, and transitions remain
fail-closed until their typed contracts are added.

Pipelines declare dynamic state through `dynamic_state_mask`. Viewport,
scissor, blend constants, stencil reference, depth bias, and line-width
setters emit the qualified command state and satisfy their corresponding bit.
A draw fails with
`AGC_ERROR_INVALID_STATE` while any declared bit is unset. Dynamic values and
resource bindings are command-buffer state and are cleared by reset.

`AgcDepthStencilPipelineState` v2 supplies depth compare/write, optional 0–1
depth bounds, and complete independent front/back stencil faces. Each face has
its compare and fail/depth-fail/pass operations, reference, compare mask, and
write mask. A dynamic stencil-reference update changes only the references and
preserves both static masks. The runtime normalizes the former 64-byte v1
depth-only layout with implicit 0–1 bounds; v1 stencil requests fail closed.

The currently host-tested graphics subset is an NGG vertex shader plus a
Wave32 pixel shader or a compiler-fused VS-front/GS-back geometry bundle plus
a Wave32 pixel shader, supported fill/cull/front-face rasterization (including
exact `PA_SU_SC_MODE_CNTL` encoding), supported
color/depth/stencil formats, complete depth/stencil testing, the declared
dynamic states above, and the qualified gfx1013 bind groups. In the geometry
form, `geometry_shader` owns both compiler records and `vertex_shader` must be
`NULL`; supplying both is rejected instead of silently ignoring either
handle. Geometry accepts point, line, triangle, line-adjacency, and
triangle-adjacency inputs plus point, line-strip, and triangle-strip outputs.
Pipeline topology must match the reflected input form; indexed geometry draws
validate list/strip/fan and adjacency cardinality before emission. The native
primitive mapping uses hardware values 1–6, 9, and 10–13; in particular,
triangle fan is 5 and triangle strip is 6. The v5 graphics descriptor adds
`primitive_restart_enable` for line/triangle strips, triangle fans, and their
adjacency-strip forms. Indexed draws program the Vulkan-compatible fixed
restart value from the bound 16- or 32-bit index type; list and patch restart
requests fail before pipeline allocation.

Fill, line, and point polygon modes, depth clamp, explicit depth-clip enable
or disable, rasterizer discard, and
qualified 1–64 pixel static or dynamic line widths are encoded by the native
pipeline. Depth bias is either a validated `static_depth_bias` descriptor or
the dynamic state bit, never both. The v4 graphics descriptor carries all 16
logic operations, and v5 adds primitive restart without changing the 200-byte
ABI size. Distinct graphics and compute pipelines may be rebound in
one command buffer; each distinct pipeline is retained through reset and each
resource-bearing bind receives a separate command-owned resource arena.
Graphics command buffers may also bind compute pipelines and dispatch compute
packets. This preserves graphics/compute ordering in one hardware DCB for
universal API queues; compute command buffers remain available for dedicated
compute queues.

Tessellation uses compiler-owned fused bundles as well. Set
`tessellation_control_shader` to an HsBack/HsFront bundle whose front program
is the vertex stage, leave `vertex_shader` `NULL`, and supply exactly one
post-tessellation form:

- `tessellation_evaluation_shader` for a TES/NGG bundle, with
  `geometry_shader` `NULL`; or
- `geometry_shader` for a fused TES-front/GS-back bundle, with
  `tessellation_evaluation_shader` `NULL`.

Supplying redundant standalone front-stage handles is rejected. Pipeline
creation links the VS, TCS, TES, optional GS, and PS interfaces; matches patch
counts and control-point counts; and derives both off-chip layouts from the
compiler reflection. The runtime lazily owns one device-wide gfx1013 off-chip
ring, factor ring, and descriptor table, preflights TF-ring support, publishes
the table, and reuses the storage across tessellation pipelines. Indexed draws
must contain whole input-control-point patches. The combined geometry form
accepts the same qualified triangle/line and invocation metadata while its
indexed input remains a complete tessellation patch.

Compute supports Wave32, at most 1,024 invocations per group, no scratch, and
at most 64 KiB LDS, including direct or indirect reflected descriptor-set
addressing. Compiler API 16 marks pixel shaders whose epilog implements
alpha-to-one; the pipeline must request exactly the reflected state.
Alpha-to-coverage, point/adjacency geometry inputs, and unqualified pipeline
forms remain fail-closed.

## Qualification

The generic suite covers valid float/normalized, UINT, and SINT attachment
pairs, negative compatibility/layout fixtures, transactional two-target MRT
binding and CB0/CB1 packet capture, successful descriptor/push and vertex-table
execution paths, direct and indirect set-address emission, fused geometry
continuation patching, resource lifetime, exact depth/stencil register encoding,
v1 state normalization, multisample minimums, required dynamic-state gating,
device-wide tessellation-ring reuse, reflected off-chip layout patching,
whole-patch draw validation, TCS push binding, and isolated plus
TES-to-geometry submission. An opt-in combined-tree test also compiles
actual vertex, fragment, and compute SPIR-V through `openagc-psbc`, then
creates reflected OpenAGC graphics and compute pipelines without application
register assembly. It is deliberately outside the default libc-only build:

```sh
make -C ../openagc-psbc library
cmake -B build-psbc-contract -DOPENAGC_PLATFORM=generic \
  -DOPENAGC_BUILD_PSBC_INTEGRATION_TEST=ON \
  -DOPENAGC_PSBC_LIBRARY=../openagc-psbc/libopenagc_psbc.a \
  -DOPENAGC_PSBC_INCLUDE_DIR=../openagc-psbc/libopenagc_psbc
cmake --build build-psbc-contract
ctest --test-dir build-psbc-contract -R psbc_runtime_integration \
  --output-on-failure
```

These results are host-tested only. A future explicit PS5 promotion gate will qualify
exact firmware artifacts; no hardware qualification is implied by pipeline
creation or a successful host command recording.
