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

## Resource and dynamic binding

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
Successful bindings retain their resources until command-buffer reset or
destruction; rejected bindings retain nothing. Push-constant coverage is
tracked per stage, so draw or dispatch fails with
`AGC_ERROR_RESOURCE_NOT_BOUND` until every reflected dword is initialized.

Pipelines declare dynamic state through `dynamic_state_mask`. Viewport,
scissor, blend constants, stencil reference, and depth bias setters emit the
qualified command state and satisfy their corresponding bit. A draw fails with
`AGC_ERROR_INVALID_STATE` while any declared bit is unset. Dynamic values and
resource bindings are command-buffer state and are cleared by reset.

`AgcDepthStencilPipelineState` v2 supplies depth compare/write, optional 0–1
depth bounds, and complete independent front/back stencil faces. Each face has
its compare and fail/depth-fail/pass operations, reference, compare mask, and
write mask. A dynamic stencil-reference update changes only the references and
preserves both static masks. The runtime normalizes the former 64-byte v1
depth-only layout with implicit 0–1 bounds; v1 stencil requests fail closed.

The currently host-tested graphics subset is an NGG vertex shader plus a
Wave32 pixel shader, supported fill/cull/front-face rasterization, supported
color/depth/stencil formats, complete depth/stencil testing, the declared
dynamic states above, and the qualified gfx1013 bind groups. Compute supports
Wave32, at most 1,024 invocations per group, no scratch, and at most 64 KiB
LDS, including direct or indirect reflected descriptor-set addressing.
Alpha-to-coverage, alpha-to-one, tessellation and geometry pipeline
packaging, and Prospero submission remain fail-closed.

## Qualification

The generic suite covers valid float/normalized, UINT, and SINT attachment
pairs, negative compatibility/layout fixtures, successful descriptor/push and
vertex-table execution paths, direct and indirect set-address emission,
resource lifetime, exact depth/stencil register encoding, v1 state
normalization, multisample minimums, and required dynamic-state gating. These
results are host-tested only. A future explicit PS5 promotion gate will qualify
exact firmware artifacts; no hardware qualification is implied by pipeline
creation or a successful host command recording.
