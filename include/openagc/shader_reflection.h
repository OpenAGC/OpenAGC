/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Versioned compiler/runtime reflection contract for PS5 shader binaries.
 */

#ifndef OPENAGC_SHADER_REFLECTION_H
#define OPENAGC_SHADER_REFLECTION_H

#include <stdint.h>

#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_SHADER_REFLECTION_VERSION_1 1u
#define AGC_SHADER_COMPILER_API_VERSION 14u
#define AGC_SHADER_ENTRY_POINT_SIZE 64u
#define AGC_SHADER_MAX_DESCRIPTOR_BINDINGS 128u
#define AGC_SHADER_MAX_USER_SGPRS 64u
#define AGC_SHADER_MAX_PUSH_CONSTANT_RANGES 8u
#define AGC_SHADER_MAX_VERTEX_INPUTS 32u
#define AGC_SHADER_MAX_COLOR_EXPORTS 8u

typedef enum AgcShaderReflectionFlagBits {
    AGC_SHADER_REFLECTION_NGG_BIT = 1u << 0,
    AGC_SHADER_REFLECTION_FUSED_STAGE_BIT = 1u << 1,
    AGC_SHADER_REFLECTION_DUAL_SOURCE_EXPORT_BIT = 1u << 2,
    AGC_SHADER_REFLECTION_WRITES_DEPTH_BIT = 1u << 3,
    AGC_SHADER_REFLECTION_WRITES_STENCIL_BIT = 1u << 4,
    AGC_SHADER_REFLECTION_USES_SAMPLE_SHADING_BIT = 1u << 5,
    AGC_SHADER_REFLECTION_READS_TESS_FACTORS_BIT = 1u << 6
} AgcShaderReflectionFlagBits;
typedef uint32_t AgcShaderReflectionFlags;

typedef enum AgcShaderHashAlgorithm {
    AGC_SHADER_HASH_NONE = 0,
    AGC_SHADER_HASH_FNV1A64 = 1
} AgcShaderHashAlgorithm;

typedef enum AgcShaderDescriptorType {
    AGC_SHADER_DESCRIPTOR_SAMPLER = 0,
    AGC_SHADER_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
    AGC_SHADER_DESCRIPTOR_SAMPLED_IMAGE,
    AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE,
    AGC_SHADER_DESCRIPTOR_UNIFORM_TEXEL_BUFFER,
    AGC_SHADER_DESCRIPTOR_STORAGE_TEXEL_BUFFER,
    AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER,
    AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER,
    AGC_SHADER_DESCRIPTOR_INPUT_ATTACHMENT,
    AGC_SHADER_DESCRIPTOR_TYPE_COUNT
} AgcShaderDescriptorType;

typedef enum AgcShaderUserSgprKind {
    AGC_SHADER_USER_SGPR_DESCRIPTOR_SET = 0,
    AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER,
    AGC_SHADER_USER_SGPR_INLINE_PUSH_CONSTANT,
    AGC_SHADER_USER_SGPR_VERTEX_BUFFER_TABLE,
    AGC_SHADER_USER_SGPR_BASE_VERTEX,
    AGC_SHADER_USER_SGPR_START_INSTANCE,
    AGC_SHADER_USER_SGPR_INDIRECT_DESCRIPTOR_SETS,
    AGC_SHADER_USER_SGPR_DRAW_INDEX,
    AGC_SHADER_USER_SGPR_KIND_COUNT
} AgcShaderUserSgprKind;

typedef enum AgcShaderSystemSgprFlagBits {
    AGC_SHADER_SYSTEM_SGPR_BASE_VERTEX_BIT = UINT64_C(1) << 0,
    AGC_SHADER_SYSTEM_SGPR_START_INSTANCE_BIT = UINT64_C(1) << 1,
    AGC_SHADER_SYSTEM_SGPR_DRAW_INDEX_BIT = UINT64_C(1) << 2,
    AGC_SHADER_SYSTEM_SGPR_WORKGROUP_ID_BIT = UINT64_C(1) << 3,
    AGC_SHADER_SYSTEM_SGPR_NUM_WORKGROUPS_BIT = UINT64_C(1) << 4
} AgcShaderSystemSgprFlagBits;
typedef uint64_t AgcShaderSystemSgprFlags;

typedef enum AgcShaderVertexFormat {
    AGC_SHADER_VERTEX_FORMAT_R32_SFLOAT = 0,
    AGC_SHADER_VERTEX_FORMAT_R32G32_SFLOAT,
    AGC_SHADER_VERTEX_FORMAT_R32G32B32_SFLOAT,
    AGC_SHADER_VERTEX_FORMAT_R32G32B32A32_SFLOAT,
    AGC_SHADER_VERTEX_FORMAT_R8G8B8A8_UNORM,
    AGC_SHADER_VERTEX_FORMAT_R16G16_SFLOAT,
    AGC_SHADER_VERTEX_FORMAT_R16G16B16A16_SFLOAT,
    AGC_SHADER_VERTEX_FORMAT_COUNT
} AgcShaderVertexFormat;

typedef enum AgcShaderVertexInputRate {
    AGC_SHADER_VERTEX_INPUT_RATE_VERTEX = 0,
    AGC_SHADER_VERTEX_INPUT_RATE_INSTANCE = 1,
    AGC_SHADER_VERTEX_INPUT_RATE_COUNT
} AgcShaderVertexInputRate;

/* These values are the gfx1013 SPI_SHADER_COL_FORMAT nibble encodings. */
typedef enum AgcShaderColorExportFormat {
    AGC_SHADER_COLOR_EXPORT_DEFAULT = 0,
    AGC_SHADER_COLOR_EXPORT_32_R = 1,
    AGC_SHADER_COLOR_EXPORT_32_GR = 2,
    AGC_SHADER_COLOR_EXPORT_FP16_ABGR = 4,
    AGC_SHADER_COLOR_EXPORT_UINT16_ABGR = 7,
    AGC_SHADER_COLOR_EXPORT_SINT16_ABGR = 8,
    AGC_SHADER_COLOR_EXPORT_32_ABGR = 9
} AgcShaderColorExportFormat;

typedef enum AgcShaderComponentClass {
    AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED = 0,
    AGC_SHADER_COMPONENT_UINT = 1,
    AGC_SHADER_COMPONENT_SINT = 2
} AgcShaderComponentClass;

typedef struct AgcShaderDescriptorMapping {
    uint32_t set;
    uint32_t binding;
    AgcShaderDescriptorType type;
    uint32_t array_size;
    uint32_t byte_offset;
    uint32_t byte_stride;
} AgcShaderDescriptorMapping;

typedef struct AgcShaderUserSgpr {
    AgcShaderUserSgprKind kind;
    uint32_t index;
    uint32_t register_offset;
    uint32_t dword_count;
} AgcShaderUserSgpr;

typedef struct AgcShaderPushConstantRange {
    uint32_t offset;
    uint32_t size;
    uint32_t alignment;
    uint32_t stage_mask;
} AgcShaderPushConstantRange;

typedef struct AgcShaderVertexInput {
    uint32_t location;
    uint32_t binding;
    uint32_t offset;
    uint32_t stride;
    AgcShaderVertexFormat format;
    AgcShaderVertexInputRate input_rate;
    uint32_t divisor;
    uint32_t component_mask;
} AgcShaderVertexInput;

typedef struct AgcShaderColorExport {
    uint32_t location;
    AgcShaderColorExportFormat format;
    AgcShaderComponentClass component_class;
    uint32_t write_mask;
    uint32_t flags;
} AgcShaderColorExport;

/*
 * Reflection is copied into AgcShader at creation. Pointer-free fixed-capacity
 * arrays make the record safe for offline serialization and cross-repository
 * compiler/runtime exchange. Fields are compiler facts, not application hints.
 */
typedef struct AgcShaderReflection {
    uint32_t struct_size;
    uint32_t version;
    AgcShaderStage stage;
    AgcShaderReflectionFlags flags;
    uint32_t shader_record_version;
    uint32_t compiler_api_version;
    uint32_t wave_size;
    AgcShaderHashAlgorithm hash_algorithm;
    uint64_t code_hash;
    char entry_point[AGC_SHADER_ENTRY_POINT_SIZE];
    AgcShaderSystemSgprFlags system_sgpr_mask;
    uint64_t stage_input_mask;
    uint64_t stage_output_mask;
    uint64_t patch_input_mask;
    uint64_t patch_output_mask;
    uint32_t descriptor_mapping_count;
    uint32_t user_sgpr_count;
    uint32_t push_constant_range_count;
    uint32_t vertex_input_count;
    uint32_t color_export_count;
    uint32_t push_constant_size;
    uint32_t push_constant_alignment;
    uint64_t inline_push_constant_mask;
    uint32_t code_offset;
    uint32_t code_size;
    uint32_t front_code_offset;
    uint32_t front_code_size;
    uint32_t local_size_x;
    uint32_t local_size_y;
    uint32_t local_size_z;
    uint32_t vertex_descriptors_per_attribute;
    uint32_t scratch_bytes_per_wave;
    uint32_t lds_size;
    uint32_t tessellation_patch_count;
    uint32_t tessellation_input_control_points;
    uint32_t tessellation_output_control_points;
    uint32_t tessellation_vertex_output_count;
    uint32_t tessellation_control_output_count;
    uint32_t tessellation_primitive_mode;
    uint32_t tessellation_reads_factors;
    uint32_t tessellation_lds_size;
    uint32_t pixel_shader_sample_count;
    uint32_t reserved0;
    uint64_t stage_linkage_hash;
    AgcShaderDescriptorMapping
        descriptor_mappings[AGC_SHADER_MAX_DESCRIPTOR_BINDINGS];
    AgcShaderUserSgpr user_sgprs[AGC_SHADER_MAX_USER_SGPRS];
    AgcShaderPushConstantRange
        push_constant_ranges[AGC_SHADER_MAX_PUSH_CONSTANT_RANGES];
    AgcShaderVertexInput vertex_inputs[AGC_SHADER_MAX_VERTEX_INPUTS];
    AgcShaderColorExport color_exports[AGC_SHADER_MAX_COLOR_EXPORTS];
    uint64_t reserved[8];
} AgcShaderReflection;

#define AGC_SHADER_REFLECTION_INIT \
    { .struct_size = sizeof(AgcShaderReflection), \
      .version = AGC_SHADER_REFLECTION_VERSION_1, \
      .stage = kAgcShaderStageCs, .hash_algorithm = AGC_SHADER_HASH_NONE }

_Static_assert(sizeof(AgcShaderDescriptorMapping) == 24u,
    "shader descriptor mapping size mismatch");
_Static_assert(sizeof(AgcShaderUserSgpr) == 16u,
    "shader user-SGPR size mismatch");
_Static_assert(sizeof(AgcShaderPushConstantRange) == 16u,
    "shader push-constant range size mismatch");
_Static_assert(sizeof(AgcShaderVertexInput) == 32u,
    "shader vertex-input size mismatch");
_Static_assert(sizeof(AgcShaderColorExport) == 20u,
    "shader color-export size mismatch");
_Static_assert(sizeof(AgcShaderReflection) == 5744u,
    "shader reflection v1 size mismatch");

#ifdef __cplusplus
}
#endif

#endif /* OPENAGC_SHADER_REFLECTION_H */
