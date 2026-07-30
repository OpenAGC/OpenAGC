# Shader reflection and pipelines

OpenAGC targets only the PS5 gfx1013 GPU. The generic backend exercises this
contract on a host; it is not a second GPU target.

## Shared reflection ABI

`openagc/shader_reflection.h` defines the pointer-free
`AgcShaderReflection` record shared with `openagc-psbc` API v14. The compiler
fills it from SPIR-V, NIR/ACO, and the generated `AgcShaderRecord`; applications
must not invent or patch compiler-derived fields. The record is copied by value
and includes explicit size/version fields and reserved-zero space.

The reflection identifies the stage and entry point, compiler and shader-record
versions, FNV-1a hash of the serialized main/front binaries, code ranges, wave
size, descriptor mappings, user/system SGPRs, push ranges, vertex inputs,
pixel exports, local size, scratch/LDS, sample behavior, NGG/fused-stage state,
and adjacent-stage linkage masks.

Create a reflected shader with `AGC_SHADER_DESC_INIT`, setting `code`,
`code_size`, and `reflection`; fused records also set `front_code` and
`front_code_size`. `agcCreateShader` validates the complete contract before the
handle becomes visible. `agcGetShaderReflection` returns the runtime-owned
validated copy.

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

The currently host-tested graphics subset is an NGG vertex shader plus a
Wave32 pixel shader, fill/no-cull rasterization, supported color/depth formats,
no stencil test, and the qualified gfx1013 bind groups. Compute supports
Wave32, at most 1,024 invocations per group, no scratch, and at most 64 KiB
LDS. Descriptor/push binding, tessellation and geometry pipeline packaging,
additional fixed/dynamic state, and Prospero submission remain fail-closed.

## Qualification

The generic suite covers valid float/normalized, UINT, and SINT attachment
pairs and negative compatibility/layout fixtures. These results are
host-tested only. A future explicit PS5 promotion gate will qualify exact
firmware artifacts; no hardware qualification is implied by pipeline creation
or a successful host command recording.
