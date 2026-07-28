#ifndef _AGC_GRAPHICS_H_
#define _AGC_GRAPHICS_H_

#include <stdint.h>

#include "agcdriver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_GFX1013_SPI_PS_IN_CONTROL_PS_W32_EN    0x00008000u
#define AGC_GFX1013_VGT_SHADER_STAGES_EN_HS_W32_EN 0x00200000u
#define AGC_GFX1013_VGT_SHADER_STAGES_EN_GS_W32_EN 0x00400000u

#define AGC_GFX1013_COLOR_FORMAT_8             0x01u
#define AGC_GFX1013_COLOR_FORMAT_16            0x02u
#define AGC_GFX1013_COLOR_FORMAT_8_8           0x03u
#define AGC_GFX1013_COLOR_FORMAT_32            0x04u
#define AGC_GFX1013_COLOR_FORMAT_16_16         0x05u
#define AGC_GFX1013_COLOR_FORMAT_10_11_11      0x06u
#define AGC_GFX1013_COLOR_FORMAT_10_10_10_2    0x08u
#define AGC_GFX1013_COLOR_FORMAT_8_8_8_8       0x0Au
#define AGC_GFX1013_COLOR_FORMAT_32_32         0x0Bu
#define AGC_GFX1013_COLOR_FORMAT_16_16_16_16   0x0Cu
#define AGC_GFX1013_COLOR_FORMAT_32_32_32_32   0x0Eu
#define AGC_GFX1013_SURFACE_NUMBER_UNORM        0u
#define AGC_GFX1013_SURFACE_NUMBER_SNORM        1u
#define AGC_GFX1013_SURFACE_NUMBER_UINT         4u
#define AGC_GFX1013_SURFACE_NUMBER_SINT         5u
#define AGC_GFX1013_SURFACE_NUMBER_SRGB         6u
#define AGC_GFX1013_SURFACE_NUMBER_FLOAT        7u
#define AGC_GFX1013_SURFACE_SWAP_STD            0u
#define AGC_GFX1013_SURFACE_SWAP_ALT            1u
#define AGC_GFX1013_SURFACE_SWAP_STD_REV        2u
#define AGC_GFX1013_SURFACE_SWAP_ALT_REV        3u
#define AGC_GFX1013_SPI_EXPORT_32_R              1u
#define AGC_GFX1013_SPI_EXPORT_32_GR             2u
#define AGC_GFX1013_SPI_EXPORT_FP16_ABGR         4u
#define AGC_GFX1013_SPI_EXPORT_32_ABGR           9u
#define AGC_GFX1013_TARGET_MASK_RGBA0            0x0Fu

#define AGC_GFX1013_TESS_FACTOR_RING_SLOT         5u
#define AGC_GFX1013_TESS_OFFCHIP_RING_SLOT        6u
#define AGC_GFX1013_TESS_RING_DESCRIPTOR_DWORDS   4u
#define AGC_GFX1013_TESS_RING_TABLE_DWORDS       32u
#define AGC_GFX1013_SHADER_ENGINE_COUNT            4u
#define AGC_GFX1013_SHADER_ARRAYS_PER_ENGINE       2u
#define AGC_GFX1013_MAX_CUS_PER_SHADER_ARRAY       5u
#define AGC_GFX1013_TESS_OFFCHIP_WG_PER_CU         4u
#define AGC_GFX1013_TESS_OFFCHIP_BUFFER_DWORDS 0x2000u
#define AGC_GFX1013_TESS_OFFCHIP_BUFFERS_PER_SE \
    (AGC_GFX1013_SHADER_ARRAYS_PER_ENGINE * \
     AGC_GFX1013_MAX_CUS_PER_SHADER_ARRAY * \
     AGC_GFX1013_TESS_OFFCHIP_WG_PER_CU)
#define AGC_GFX1013_TESS_OFFCHIP_BUFFER_COUNT \
    (AGC_GFX1013_SHADER_ENGINE_COUNT * \
     AGC_GFX1013_TESS_OFFCHIP_BUFFERS_PER_SE)
#define AGC_GFX1013_TESS_OFFCHIP_RING_SIZE \
    (AGC_GFX1013_TESS_OFFCHIP_BUFFER_COUNT * \
     AGC_GFX1013_TESS_OFFCHIP_BUFFER_DWORDS * 4u)
#define AGC_GFX1013_TESS_FACTOR_RING_SIZE     0x1e000u
/* Gfx10.3 encodes the global buffer count here. Gfx11 changed this field to a
 * per-shader-engine count; do not divide the Oberon value by ENGINE_COUNT. */
#define AGC_GFX1013_TESS_OFFCHIP_PARAM \
    (AGC_GFX1013_TESS_OFFCHIP_BUFFER_COUNT - 1u)
#define AGC_GFX1013_TESS_OFFCHIP_LAYOUT   0x21042108u
#define AGC_GFX1013_RAW_R32_DESCRIPTOR_WORD3 0x31016FACu

#define AGC_GFX1013_EOP_FENCE_DWORDS             10u
#define AGC_GFX1013_OCCLUSION_SNAPSHOT_DWORDS      4u
#define AGC_GFX1013_OCCLUSION_QUERY_OP_DWORDS       7u
#define AGC_GFX1013_OCCLUSION_QUERY_MAX_RBS       16u
#define AGC_GFX1013_OCCLUSION_QUERY_STRIDE       256u
#define AGC_GFX1013_EOP_CACHE_FLUSH_EVENT      0x14u
#define AGC_GFX1013_DB_META_FLUSH_DWORDS           2u
#define AGC_GFX1013_DB_DATA_FLUSH_EVENT          0x2Bu
#define AGC_GFX1013_DB_META_FLUSH_EVENT          0x2Cu
#define AGC_GFX1013_EOP_GCR_CONTROL            0x603u
#define AGC_GFX1013_EOP_CACHE_POLICY_LRU          3u
#define AGC_GFX1013_ACQUIRE_MEM_DWORDS             8u
#define AGC_GFX1013_TRANSITION_MAX_DWORDS          20u
#define AGC_GFX1013_HTILE_RMW_DWORDS                83u
#define AGC_GFX1013_HTILE_RMW_LOCAL_SIZE            64u
#define AGC_GFX1013_ACQUIRE_POLL_INTERVAL        0x0Au
#define AGC_GFX1013_ACQUIRE_GCR_ALL            0xC3B1u

#define AGC_GFX1013_FRAME_PROLOGUE_BASE_DWORDS  2275u
#define AGC_GFX1013_FRAME_PROLOGUE_DWORDS       2471u
#define AGC_GFX1013_FRAME_POST_BIND_DWORDS        24u
#define AGC_GFX1013_BLEND_STATE_DWORDS            22u
#define AGC_GFX1013_DEPTH_STENCIL_STATE_DWORDS    14u
#define AGC_GFX1013_DEPTH_BIAS_STATE_DWORDS       10u
#define AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS    5u
#define AGC_GFX1013_DEPTH_BIAS_RASTER_MODE         0x00001800u
#define AGC_GFX1013_VULKAN_CLIP_CONTROL             0x00080000u
#define AGC_GFX1013_DEPTH_CLAMP_CLIP_CONTROL        0x0c080000u
#define AGC_GFX1013_DEPTH_SURFACE_DWORDS           27u
#define AGC_GFX1013_HTILE_OPERATION_DWORDS          3u
#define AGC_GFX1013_DEPTH_EXPCLEAR_DWORDS            3u
#define AGC_GFX1013_DEPTH_STENCIL_EXPCLEAR_MAX_DWORDS 4u
#define AGC_GFX1013_HTILE_UNCOMPRESSED_DEPTH       0xfffc000fu
#define AGC_GFX1013_HTILE_UNCOMPRESSED_D16 \
    AGC_GFX1013_HTILE_UNCOMPRESSED_DEPTH
#define AGC_GFX1013_HTILE_UNCOMPRESSED_DEPTH_STENCIL 0xfffff30fu
#define AGC_GFX1013_HTILE_CLEAR_DEPTH_ZERO          0x00000000u
#define AGC_GFX1013_HTILE_CLEAR_DEPTH_ONE           0xfffffff0u
#define AGC_GFX1013_HTILE_DEPTH_ASPECT_MASK          0xfffff00fu
#define AGC_GFX1013_HTILE_STENCIL_ASPECT_MASK        0x000003f0u
#define AGC_GFX1013_COMBINED_HTILE_EXPCLEAR_ENABLED          1u
#define AGC_GFX1013_SWIZZLE_64KB_Z_X               24u
#define AGC_GFX1013_SWIZZLE_64KB_R_X               27u
#define AGC_GFX1013_64KB_SURFACE_ALIGNMENT    0x10000u
#define AGC_GFX1013_SAMPLE_STATE_DWORDS             29u
#define AGC_GFX1013_MAX_COLOR_TARGETS              8u
#define AGC_GFX1013_CONTEXT_CONTROL_ENABLE 0x80000000u
#define AGC_GFX1013_NGG_MODE_CONTROL        0x00000200u
#define AGC_GFX1013_VERTEX_REUSE_BLOCK              14u

typedef struct AgcGfx1013ShaderBinding {
    const AgcShaderRecord *record;
    const AgcRegisterValue *sh_registers;
    uint32_t num_sh_registers;
    const AgcRegisterValue *cx_registers;
    uint32_t num_cx_registers;
    uint64_t code_address;
} AgcGfx1013ShaderBinding;

typedef struct AgcGfx1013Wave32VsPsState {
    AgcGfx1013ShaderBinding primitive;
    AgcGfx1013ShaderBinding pixel;
    uint64_t primitive_back_code_address;
    uint32_t primitive_type;
} AgcGfx1013Wave32VsPsState;

typedef struct AgcGfx1013Wave32TessVsPsState {
    AgcGfx1013ShaderBinding hull;
    AgcGfx1013ShaderBinding primitive;
    AgcGfx1013ShaderBinding pixel;
    uint64_t hull_back_code_address;
    uint64_t primitive_back_code_address;
    uint64_t ring_descriptor_address;
    uint32_t tcs_offchip_layout;
    uint32_t tes_offchip_layout;
    uint32_t hull_lds_size;
    uint32_t primitive_type;
} AgcGfx1013Wave32TessVsPsState;

typedef enum AgcGfx1013ColorTargetFormat {
    AGC_GFX1013_RT_FORMAT_R8_UNORM = 0,
    AGC_GFX1013_RT_FORMAT_RG8_UNORM,
    AGC_GFX1013_RT_FORMAT_RGBA8_UNORM,
    AGC_GFX1013_RT_FORMAT_BGRA8_UNORM,
    AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM,
    AGC_GFX1013_RT_FORMAT_R16_FLOAT,
    AGC_GFX1013_RT_FORMAT_RG16_FLOAT,
    AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT,
    AGC_GFX1013_RT_FORMAT_R32_FLOAT,
    AGC_GFX1013_RT_FORMAT_RG32_FLOAT,
    AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT,
    AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT,
    AGC_GFX1013_RT_FORMAT_RGBA8_SRGB,
    AGC_GFX1013_RT_FORMAT_BGRA8_SRGB,
    AGC_GFX1013_RT_FORMAT_COUNT
} AgcGfx1013ColorTargetFormat;

typedef struct AgcGfx1013ColorTargetFormatInfo {
    uint32_t color_format;
    uint32_t number_type;
    uint32_t component_swap;
    uint32_t bytes_per_pixel;
    uint32_t spi_shader_export_format;
} AgcGfx1013ColorTargetFormatInfo;

typedef enum AgcGfx1013BlendFactor {
    AGC_GFX1013_BLEND_ZERO = 0,
    AGC_GFX1013_BLEND_ONE = 1,
    AGC_GFX1013_BLEND_SRC_COLOR = 2,
    AGC_GFX1013_BLEND_ONE_MINUS_SRC_COLOR = 3,
    AGC_GFX1013_BLEND_SRC_ALPHA = 4,
    AGC_GFX1013_BLEND_ONE_MINUS_SRC_ALPHA = 5,
    AGC_GFX1013_BLEND_DST_ALPHA = 6,
    AGC_GFX1013_BLEND_ONE_MINUS_DST_ALPHA = 7,
    AGC_GFX1013_BLEND_DST_COLOR = 8,
    AGC_GFX1013_BLEND_ONE_MINUS_DST_COLOR = 9,
    AGC_GFX1013_BLEND_SRC_ALPHA_SATURATE = 10,
    AGC_GFX1013_BLEND_BOTH_SRC_ALPHA = 11,
    AGC_GFX1013_BLEND_BOTH_INV_SRC_ALPHA = 12,
    AGC_GFX1013_BLEND_CONSTANT_COLOR = 13,
    AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_COLOR = 14,
    AGC_GFX1013_BLEND_SRC1_COLOR = 15,
    AGC_GFX1013_BLEND_ONE_MINUS_SRC1_COLOR = 16,
    AGC_GFX1013_BLEND_SRC1_ALPHA = 17,
    AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA = 18,
    AGC_GFX1013_BLEND_CONSTANT_ALPHA = 19,
    AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_ALPHA = 20,
    AGC_GFX1013_BLEND_FACTOR_COUNT = 21
} AgcGfx1013BlendFactor;

typedef enum AgcGfx1013BlendOp {
    AGC_GFX1013_BLEND_OP_ADD = 0,
    AGC_GFX1013_BLEND_OP_SUBTRACT = 1,
    AGC_GFX1013_BLEND_OP_MIN = 2,
    AGC_GFX1013_BLEND_OP_MAX = 3,
    AGC_GFX1013_BLEND_OP_REVERSE_SUBTRACT = 4,
    AGC_GFX1013_BLEND_OP_COUNT = 5
} AgcGfx1013BlendOp;

typedef enum AgcGfx1013LogicOp {
    AGC_GFX1013_LOGIC_CLEAR = 0,
    AGC_GFX1013_LOGIC_AND = 1,
    AGC_GFX1013_LOGIC_AND_REVERSE = 2,
    AGC_GFX1013_LOGIC_COPY = 3,
    AGC_GFX1013_LOGIC_AND_INVERTED = 4,
    AGC_GFX1013_LOGIC_NO_OP = 5,
    AGC_GFX1013_LOGIC_XOR = 6,
    AGC_GFX1013_LOGIC_OR = 7,
    AGC_GFX1013_LOGIC_NOR = 8,
    AGC_GFX1013_LOGIC_EQUIVALENT = 9,
    AGC_GFX1013_LOGIC_INVERT = 10,
    AGC_GFX1013_LOGIC_OR_REVERSE = 11,
    AGC_GFX1013_LOGIC_COPY_INVERTED = 12,
    AGC_GFX1013_LOGIC_OR_INVERTED = 13,
    AGC_GFX1013_LOGIC_NAND = 14,
    AGC_GFX1013_LOGIC_SET = 15,
    AGC_GFX1013_LOGIC_OP_COUNT = 16
} AgcGfx1013LogicOp;

typedef struct AgcGfx1013ColorBlendTargetState {
    uint32_t enable;
    AgcGfx1013BlendFactor color_source;
    AgcGfx1013BlendFactor color_destination;
    AgcGfx1013BlendOp color_operation;
    uint32_t separate_alpha;
    AgcGfx1013BlendFactor alpha_source;
    AgcGfx1013BlendFactor alpha_destination;
    AgcGfx1013BlendOp alpha_operation;
    uint32_t write_mask;
} AgcGfx1013ColorBlendTargetState;

typedef struct AgcGfx1013ColorBlendState {
    uint32_t target_count;
    AgcGfx1013ColorBlendTargetState targets[AGC_GFX1013_MAX_COLOR_TARGETS];
    uint32_t logic_enable;
    AgcGfx1013LogicOp logic_operation;
    float constants[4];
} AgcGfx1013ColorBlendState;

typedef enum AgcGfx1013CompareOp {
    AGC_GFX1013_COMPARE_NEVER = 0,
    AGC_GFX1013_COMPARE_LESS = 1,
    AGC_GFX1013_COMPARE_EQUAL = 2,
    AGC_GFX1013_COMPARE_LESS_EQUAL = 3,
    AGC_GFX1013_COMPARE_GREATER = 4,
    AGC_GFX1013_COMPARE_NOT_EQUAL = 5,
    AGC_GFX1013_COMPARE_GREATER_EQUAL = 6,
    AGC_GFX1013_COMPARE_ALWAYS = 7,
    AGC_GFX1013_COMPARE_COUNT = 8
} AgcGfx1013CompareOp;

typedef enum AgcGfx1013StencilOp {
    AGC_GFX1013_STENCIL_KEEP = 0,
    AGC_GFX1013_STENCIL_ZERO = 1,
    AGC_GFX1013_STENCIL_REPLACE = 3,
    AGC_GFX1013_STENCIL_INCREMENT_CLAMP = 5,
    AGC_GFX1013_STENCIL_DECREMENT_CLAMP = 6,
    AGC_GFX1013_STENCIL_INVERT = 7,
    AGC_GFX1013_STENCIL_INCREMENT_WRAP = 8,
    AGC_GFX1013_STENCIL_DECREMENT_WRAP = 9
} AgcGfx1013StencilOp;

typedef struct AgcGfx1013StencilFaceState {
    AgcGfx1013CompareOp compare_operation;
    AgcGfx1013StencilOp fail_operation;
    AgcGfx1013StencilOp depth_fail_operation;
    AgcGfx1013StencilOp pass_operation;
    uint32_t reference;
    uint32_t compare_mask;
    uint32_t write_mask;
} AgcGfx1013StencilFaceState;

typedef struct AgcGfx1013DepthStencilState {
    uint32_t depth_test_enable;
    uint32_t depth_write_enable;
    AgcGfx1013CompareOp depth_compare_operation;
    uint32_t depth_bounds_enable;
    float min_depth_bounds;
    float max_depth_bounds;
    uint32_t stencil_test_enable;
    uint32_t back_face_enable;
    AgcGfx1013StencilFaceState front;
    AgcGfx1013StencilFaceState back;
} AgcGfx1013DepthStencilState;

typedef struct AgcGfx1013ColorTargetState {
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t color_format;
    uint32_t number_type;
    uint32_t component_swap;
    uint32_t sample_count;
    uint32_t fragment_count;
    uint32_t swizzle_mode;
} AgcGfx1013ColorTargetState;

typedef struct AgcGfx1013ColorSurfaceLayoutInput {
    uint32_t width;
    uint32_t height;
    uint32_t layer_count;
    uint32_t mip_level_count;
    uint32_t sample_count;
    AgcGfx1013ColorTargetFormat format;
    uint32_t swizzle_mode;
} AgcGfx1013ColorSurfaceLayoutInput;

typedef struct AgcGfx1013ColorSurfaceLayout {
    uint64_t allocation_size;
    uint64_t slice_size;
    uint32_t pitch;
    uint32_t padded_height;
    uint32_t alignment;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t first_mip_in_tail;
} AgcGfx1013ColorSurfaceLayout;

typedef struct AgcGfx1013SampleState {
    uint32_t sample_count;
    uint32_t pixel_shader_sample_count;
    uint32_t sample_mask;
} AgcGfx1013SampleState;

typedef enum AgcGfx1013DepthSurfaceFormat {
    AGC_GFX1013_DEPTH_FORMAT_D16_UNORM = 0,
    AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT,
    AGC_GFX1013_DEPTH_FORMAT_S8_UINT,
    AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT,
    AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT,
    AGC_GFX1013_DEPTH_FORMAT_COUNT
} AgcGfx1013DepthSurfaceFormat;

typedef struct AgcGfx1013DepthBiasState {
    AgcGfx1013DepthSurfaceFormat format;
    float constant_factor;
    float clamp;
    float slope_factor;
} AgcGfx1013DepthBiasState;

typedef enum AgcGfx1013HtileOperation {
    AGC_GFX1013_HTILE_OPERATION_NONE = 0,
    AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH,
    AGC_GFX1013_HTILE_OPERATION_DECOMPRESS_DEPTH_STENCIL,
    AGC_GFX1013_HTILE_OPERATION_RESUMMARIZE_DEPTH,
    AGC_GFX1013_HTILE_OPERATION_COUNT
} AgcGfx1013HtileOperation;

typedef struct AgcGfx1013DepthExpclearState {
    float clear_depth;
} AgcGfx1013DepthExpclearState;

typedef struct AgcGfx1013DepthStencilExpclearState {
    uint32_t aspects;
    float clear_depth;
    uint32_t clear_stencil;
} AgcGfx1013DepthStencilExpclearState;

_Static_assert(sizeof(AgcGfx1013DepthStencilExpclearState) == 12,
    "gfx1013 depth/stencil expclear state must be 12 bytes");

typedef enum AgcGfx1013DepthStencilAspect {
    AGC_GFX1013_DEPTH_STENCIL_ASPECT_DEPTH = 1u << 0,
    AGC_GFX1013_DEPTH_STENCIL_ASPECT_STENCIL = 1u << 1,
} AgcGfx1013DepthStencilAspect;

typedef struct AgcGfx1013HtileExpclearPlanState {
    uint32_t aspects;
    float clear_depth;
    uint32_t clear_stencil;
    uint32_t has_stencil;
} AgcGfx1013HtileExpclearPlanState;

typedef struct AgcGfx1013HtileExpclearPlan {
    uint32_t write_value;
    uint32_t write_mask;
    uint32_t requires_read_modify_write;
    uint32_t hardware_enabled;
} AgcGfx1013HtileExpclearPlan;

_Static_assert(sizeof(AgcGfx1013HtileExpclearPlanState) == 16,
    "gfx1013 HTILE expclear plan state must be 16 bytes");
_Static_assert(sizeof(AgcGfx1013HtileExpclearPlan) == 16,
    "gfx1013 HTILE expclear plan must be 16 bytes");

typedef struct AgcGfx1013DepthSurfaceState {
    uint64_t depth_read_address;
    uint64_t depth_write_address;
    uint64_t stencil_read_address;
    uint64_t stencil_write_address;
    uint64_t htile_address;
    uint32_t width;
    uint32_t height;
    AgcGfx1013DepthSurfaceFormat format;
    uint32_t depth_swizzle_mode;
    uint32_t stencil_swizzle_mode;
    uint32_t mip_level;
    uint32_t mip_level_count;
    uint32_t first_layer;
    uint32_t last_layer;
    uint32_t sample_count;
    uint32_t depth_read_only;
    uint32_t stencil_read_only;
    uint32_t htile_enable;
    uint32_t allow_expclear;
    uint32_t expclear_aspects;
    uint32_t htile_stencil_disable;
} AgcGfx1013DepthSurfaceState;

typedef struct AgcGfx1013DepthSurfaceLayoutInput {
    uint32_t width;
    uint32_t height;
    uint32_t layer_count;
    uint32_t mip_level_count;
    uint32_t sample_count;
    AgcGfx1013DepthSurfaceFormat format;
    uint32_t depth_swizzle_mode;
    uint32_t stencil_swizzle_mode;
} AgcGfx1013DepthSurfaceLayoutInput;

typedef struct AgcGfx1013DepthPlaneLayout {
    uint64_t allocation_size;
    uint64_t slice_size;
    uint32_t pitch;
    uint32_t padded_height;
    uint32_t alignment;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t first_mip_in_tail;
} AgcGfx1013DepthPlaneLayout;

typedef struct AgcGfx1013DepthSurfaceLayout {
    AgcGfx1013DepthPlaneLayout depth;
    AgcGfx1013DepthPlaneLayout stencil;
} AgcGfx1013DepthSurfaceLayout;

typedef struct AgcGfx1013HtileLayoutInput {
    uint32_t width;
    uint32_t height;
    uint32_t layer_count;
    uint32_t mip_level_count;
    uint32_t first_mip_in_tail;
    uint32_t pipe_count;
    uint32_t swizzle_mode;
} AgcGfx1013HtileLayoutInput;

typedef struct AgcGfx1013HtileLayout {
    uint64_t allocation_size;
    uint64_t slice_size;
    uint32_t pitch;
    uint32_t padded_height;
    uint32_t alignment;
    uint32_t meta_block_size;
    uint32_t meta_block_width;
    uint32_t meta_block_height;
    uint32_t meta_blocks_per_slice;
} AgcGfx1013HtileLayout;

typedef struct AgcGfx1013HtileSubresourceLayout {
    uint64_t offset;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t padded_height;
    uint32_t in_mip_tail;
    uint32_t reserved;
} AgcGfx1013HtileSubresourceLayout;

_Static_assert(sizeof(AgcGfx1013DepthPlaneLayout) == 40,
    "gfx1013 depth plane layout must be 40 bytes");
_Static_assert(sizeof(AgcGfx1013DepthSurfaceLayout) == 80,
    "gfx1013 depth surface layout must be 80 bytes");
_Static_assert(sizeof(AgcGfx1013HtileLayout) == 48,
    "gfx1013 HTILE layout must be 48 bytes");
_Static_assert(sizeof(AgcGfx1013HtileSubresourceLayout) == 40,
    "gfx1013 HTILE subresource layout must be 40 bytes");
_Static_assert(sizeof(AgcGfx1013ColorSurfaceLayout) == 40,
    "gfx1013 color surface layout must be 40 bytes");

typedef struct AgcGfx1013ViewportState {
    uint32_t width;
    uint32_t height;
    uint32_t depth_clip_space;
} AgcGfx1013ViewportState;

#define AGC_GFX1013_CLIP_SPACE_NEGATIVE_ONE_TO_ONE 0u
#define AGC_GFX1013_CLIP_SPACE_ZERO_TO_ONE         1u

typedef struct AgcGfx1013ScissorState {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
} AgcGfx1013ScissorState;

typedef enum AgcGfx1013PolygonMode {
    AGC_GFX1013_POLYGON_MODE_FILL = 0,
    AGC_GFX1013_POLYGON_MODE_LINE = 1,
    AGC_GFX1013_POLYGON_MODE_POINT = 2,
    AGC_GFX1013_POLYGON_MODE_COUNT
} AgcGfx1013PolygonMode;

typedef enum AgcGfx1013PrimitiveTopology {
    AGC_GFX1013_TOPOLOGY_POINT_LIST = 0,
    AGC_GFX1013_TOPOLOGY_LINE_LIST,
    AGC_GFX1013_TOPOLOGY_LINE_STRIP,
    AGC_GFX1013_TOPOLOGY_TRIANGLE_LIST,
    AGC_GFX1013_TOPOLOGY_TRIANGLE_STRIP,
    AGC_GFX1013_TOPOLOGY_PATCH_LIST,
    AGC_GFX1013_TOPOLOGY_COUNT
} AgcGfx1013PrimitiveTopology;

typedef struct AgcGfx1013PrimitiveSizeState {
    float point_size;
    float point_size_min;
    float point_size_max;
    float line_width;
} AgcGfx1013PrimitiveSizeState;

typedef struct AgcGfx1013FrameState {
    AgcGfx1013ColorTargetState color_target;
    AgcGfx1013ColorTargetState additional_color_targets[
        AGC_GFX1013_MAX_COLOR_TARGETS - 1u];
    uint32_t color_target_count;
    AgcGfx1013ViewportState viewport;
    AgcGfx1013ScissorState scissor;
    uint32_t target_mask;
    uint32_t context_load_control;
    uint32_t context_shadow_control;
    uint32_t clear_state_flags;
    uint32_t min_vertex_index;
    uint32_t vertex_index_offset;
    uint32_t max_vertex_index;
    uint32_t ngg_mode_control;
    uint32_t vertex_reuse_block_control;
    uint32_t instance_step_rate;
    uint32_t clip_control;
    uint32_t raster_mode_control;
} AgcGfx1013FrameState;

typedef struct AgcGfx1013GraphicsDefaultStats {
    uint32_t sh_register_count;
    uint32_t cx_register_count;
    uint32_t uc_register_count;
} AgcGfx1013GraphicsDefaultStats;

typedef struct AgcGfx1013ComputeDefaultStats {
    uint32_t sh_register_count;
    uint32_t packet_count;
} AgcGfx1013ComputeDefaultStats;

typedef struct AgcGfx1013ResourceTableBinding {
    uint32_t placeholder;
    uint64_t address;
} AgcGfx1013ResourceTableBinding;

typedef struct AgcGfx1013ComputeState {
    const AgcShaderRecord *record;
    const AgcRegisterValue *sh_registers;
    uint32_t num_sh_registers;
    uint64_t code_address;
    const uint32_t *user_data;
    uint32_t num_user_data;
    uint32_t local_size_x;
    uint32_t local_size_y;
    uint32_t local_size_z;
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
    uint32_t modifier;
    const AgcGfx1013ResourceTableBinding *resource_tables;
    uint32_t num_resource_tables;
} AgcGfx1013ComputeState;

typedef struct AgcGfx1013HtileRmwState {
    const AgcShaderRecord *record;
    const AgcRegisterValue *sh_registers;
    uint32_t num_sh_registers;
    uint64_t code_address;
    uint64_t htile_address;
    uint64_t htile_allocation_size;
    const AgcGfx1013HtileSubresourceLayout *subresource;
    const AgcGfx1013HtileExpclearPlan *plan;
} AgcGfx1013HtileRmwState;

_Static_assert(sizeof(AgcGfx1013HtileRmwState) == 64,
    "gfx1013 HTILE RMW state must be 64 bytes");

typedef struct AgcGfx1013EopFenceState {
    uint64_t address;
    uint32_t value;
} AgcGfx1013EopFenceState;

typedef enum AgcGfx1013ResourceUsage {
    AGC_GFX1013_RESOURCE_USAGE_UNDEFINED = 0,
    AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET,
    AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE,
    AGC_GFX1013_RESOURCE_USAGE_COPY_SOURCE,
    AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION,
    AGC_GFX1013_RESOURCE_USAGE_SHADER_READ,
    AGC_GFX1013_RESOURCE_USAGE_PRESENT,
    AGC_GFX1013_RESOURCE_USAGE_HOST_READ,
    AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE,
    AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ,
    AGC_GFX1013_RESOURCE_USAGE_COUNT
} AgcGfx1013ResourceUsage;

typedef struct AgcGfx1013ResourceTransition {
    AgcGfx1013ResourceUsage before;
    AgcGfx1013ResourceUsage after;
    uint64_t completion_address;
    uint32_t completion_value;
} AgcGfx1013ResourceTransition;

#define AGC_GFX1013_ADDRESS32_HIGH 0x00000002u

typedef struct AgcGfx1013TessellationRingTable {
    uint32_t words[AGC_GFX1013_TESS_RING_TABLE_DWORDS];
} AgcGfx1013TessellationRingTable;

typedef struct AgcGfx1013TessellationState {
    uint64_t offchip_ring_address;
    uint64_t factor_ring_address;
    uint32_t offchip_ring_size;
    uint32_t factor_ring_size;
    uint32_t offchip_param;
    uint32_t max_tess_level;
    uint32_t min_tess_level;
    uint32_t esgs_ring_itemsize;
    uint32_t distribution;
    uint32_t tf_param;
} AgcGfx1013TessellationState;

typedef struct AgcGfx1013TessellationLayoutState {
    uint32_t patch_count;
    uint32_t input_control_points;
    uint32_t output_control_points;
    uint32_t vertex_output_count;
    uint32_t control_output_count;
    uint32_t primitive_mode;
    uint32_t tes_reads_tess_factors;
} AgcGfx1013TessellationLayoutState;

_Static_assert(sizeof(AgcGfx1013TessellationRingTable) == 128,
    "gfx1013 tessellation ring table must be 128 bytes");

typedef struct AgcGfx1013BaselineDrawState {
    AgcGfx1013Wave32VsPsState shaders;
    const AgcGfx1013FrameState *frame;
    const AgcGfx1013DepthSurfaceState *depth_surface_state;
    const AgcGfx1013DepthStencilState *depth_stencil_state;
    const AgcGfx1013ResourceTableBinding *primitive_resource_tables;
    uint32_t num_primitive_resource_tables;
    const AgcGfx1013ResourceTableBinding *pixel_resource_tables;
    uint32_t num_pixel_resource_tables;
    const AgcRegisterValue *post_bind_sh_registers;
    uint32_t num_post_bind_sh_registers;
    const AgcRegisterValue *post_bind_cx_registers;
    uint32_t num_post_bind_cx_registers;
    const AgcRegisterValue *post_bind_uc_registers;
    uint32_t num_post_bind_uc_registers;
    uint32_t index_type;
    uint32_t index_swap;
    uint32_t instance_count;
    uint32_t vertex_count;
    uint64_t draw_modifier;
} AgcGfx1013BaselineDrawState;

typedef struct AgcGfx1013IndexedDrawState {
    AgcGfx1013BaselineDrawState draw;
    uint64_t index_buffer_address;
    uint32_t index_buffer_count;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t draw_initiator;
} AgcGfx1013IndexedDrawState;

typedef struct AgcGfx1013IndirectDrawState {
    AgcGfx1013BaselineDrawState draw;
    uint64_t argument_buffer_address;
    uint64_t index_buffer_address;
    uint32_t argument_offset;
    uint32_t index_buffer_count;
    uint32_t draw_count;
    uint32_t stride;
    uint32_t base_vertex_location;
    uint32_t start_instance_location;
    /* Reserved until a PS5 DrawIndex packet encoding is hardware-qualified. */
    uint32_t draw_index_location;
    uint32_t draw_index_enable;
    uint32_t draw_initiator;
    uint32_t indexed;
} AgcGfx1013IndirectDrawState;

/* Shader-driven resolve. The draw must bind a sampler2DMS pixel shader and
 * its descriptor table; OpenAGC supplies the cache transition, restores 1x
 * raster state, and executes the caller's fullscreen draw. */
typedef struct AgcGfx1013ColorResolveState {
    const AgcGfx1013ColorTargetState *source;
    const AgcGfx1013BaselineDrawState *draw;
} AgcGfx1013ColorResolveState;

typedef struct AgcGfx1013TessDrawState {
    AgcGfx1013Wave32TessVsPsState shaders;
    const AgcGfx1013FrameState *frame;
    const AgcGfx1013TessellationState *tessellation;
    const AgcGfx1013DepthSurfaceState *depth_surface_state;
    const AgcGfx1013DepthStencilState *depth_stencil_state;
    const AgcGfx1013ResourceTableBinding *hull_resource_tables;
    uint32_t num_hull_resource_tables;
    const AgcGfx1013ResourceTableBinding *primitive_resource_tables;
    uint32_t num_primitive_resource_tables;
    const AgcGfx1013ResourceTableBinding *pixel_resource_tables;
    uint32_t num_pixel_resource_tables;
    const AgcRegisterValue *post_bind_sh_registers;
    uint32_t num_post_bind_sh_registers;
    const AgcRegisterValue *post_bind_cx_registers;
    uint32_t num_post_bind_cx_registers;
    const AgcRegisterValue *post_bind_uc_registers;
    uint32_t num_post_bind_uc_registers;
    uint32_t instance_count;
    uint32_t vertex_count;
    uint64_t draw_modifier;
} AgcGfx1013TessDrawState;

int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32VsPs(
    const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindWave32VsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013ValidateVsPs(
    const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindVsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32VsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndexAuto(
    SceAgcCb *cb, const AgcGfx1013BaselineDrawState *state);
int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndexed(
    SceAgcCb *cb, const AgcGfx1013IndexedDrawState *state);
int32_t PS5_SYSV_ABI agcGfx1013DrawBaselineIndirect(
    SceAgcCb *cb, const AgcGfx1013IndirectDrawState *state);
/* Records one or more raw Ariel DMA_DATA packets for a GPU buffer copy.
 * Vulkan-compatible source, destination, and size alignment is four bytes. */
int32_t PS5_SYSV_ABI agcGfx1013CopyBuffer(
    SceAgcCb *cb, uint64_t source_address, uint64_t destination_address,
    uint64_t byte_count);
int32_t PS5_SYSV_ABI agcGfx1013DrawTessIndexAuto(
    SceAgcCb *cb, const AgcGfx1013TessDrawState *state);
int32_t PS5_SYSV_ABI agcGfx1013SignalEopFence(
    SceAgcCb *cb, const AgcGfx1013EopFenceState *state);
int32_t PS5_SYSV_ABI agcGfx1013WriteOcclusionSnapshot(
    SceAgcCb *cb, uint64_t address);
int32_t PS5_SYSV_ABI agcGfx1013BeginOcclusionQuery(
    SceAgcCb *cb, uint64_t address, uint32_t precise);
int32_t PS5_SYSV_ABI agcGfx1013EndOcclusionQuery(
    SceAgcCb *cb, uint64_t address);
int32_t PS5_SYSV_ABI agcGfx1013GetResourceTransitionDwords(
    const AgcGfx1013ResourceTransition *transition, uint32_t *dword_count);
int32_t PS5_SYSV_ABI agcGfx1013TransitionResource(
    SceAgcCb *cb, const AgcGfx1013ResourceTransition *transition);
int32_t PS5_SYSV_ABI agcGfx1013ValidateWave32TessVsPs(
    const AgcGfx1013Wave32TessVsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindWave32TessVsPs(
    SceAgcCb *cb, const AgcGfx1013Wave32TessVsPsState *state);
int32_t PS5_SYSV_ABI agcGfx1013GetColorTargetFormatInfo(
    AgcGfx1013ColorTargetFormat format,
    AgcGfx1013ColorTargetFormatInfo *info);
int32_t PS5_SYSV_ABI agcGfx1013InitColorTarget(
    AgcGfx1013ColorTargetState *state, uint64_t address, uint32_t width,
    uint32_t height, AgcGfx1013ColorTargetFormat format);
int32_t PS5_SYSV_ABI agcGfx1013SetColorTarget(
    SceAgcCb *cb, const AgcGfx1013ColorTargetState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetColorTargetSlot(
    SceAgcCb *cb, uint32_t slot,
    const AgcGfx1013ColorTargetState *state);
int32_t PS5_SYSV_ABI agcGfx1013GetColorSurfaceLayout(
    const AgcGfx1013ColorSurfaceLayoutInput *input,
    AgcGfx1013ColorSurfaceLayout *layout);
int32_t PS5_SYSV_ABI agcGfx1013SetSampleState(
    SceAgcCb *cb, const AgcGfx1013SampleState *state);
int32_t PS5_SYSV_ABI agcGfx1013ResolveColor4x(
    SceAgcCb *cb, const AgcGfx1013ColorResolveState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetDepthSurface(
    SceAgcCb *cb, const AgcGfx1013DepthSurfaceState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetHtileOperation(
    SceAgcCb *cb, AgcGfx1013HtileOperation operation);
int32_t PS5_SYSV_ABI agcGfx1013SetDepthExpclear(
    SceAgcCb *cb, const AgcGfx1013DepthExpclearState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetDepthStencilExpclear(
    SceAgcCb *cb, const AgcGfx1013DepthStencilExpclearState *state);
int32_t PS5_SYSV_ABI agcGfx1013BuildHtileExpclearPlan(
    const AgcGfx1013HtileExpclearPlanState *state,
    AgcGfx1013HtileExpclearPlan *plan);
int32_t PS5_SYSV_ABI agcGfx1013GetDepthSurfaceLayout(
    const AgcGfx1013DepthSurfaceLayoutInput *input,
    AgcGfx1013DepthSurfaceLayout *layout);
int32_t PS5_SYSV_ABI agcGfx1013GetHtileLayout(
    const AgcGfx1013HtileLayoutInput *input,
    AgcGfx1013HtileLayout *layout);
int32_t PS5_SYSV_ABI agcGfx1013GetHtileSubresourceLayout(
    const AgcGfx1013HtileLayoutInput *input, uint32_t mip_level,
    uint32_t layer, AgcGfx1013HtileSubresourceLayout *layout);
int32_t PS5_SYSV_ABI agcGfx1013SetViewport(
    SceAgcCb *cb, const AgcGfx1013ViewportState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetScissor(
    SceAgcCb *cb, const AgcGfx1013ScissorState *state);
int32_t PS5_SYSV_ABI agcGfx1013ApplyPolygonMode(
    AgcGfx1013FrameState *state, AgcGfx1013PolygonMode mode);
int32_t PS5_SYSV_ABI agcGfx1013GetPrimitiveType(
    AgcGfx1013PrimitiveTopology topology, uint32_t *primitive_type);
int32_t PS5_SYSV_ABI agcGfx1013SetPrimitiveSizeState(
    SceAgcCb *cb, const AgcGfx1013PrimitiveSizeState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetTargetMask(
    SceAgcCb *cb, uint32_t mask);
int32_t PS5_SYSV_ABI agcGfx1013SetColorBlendState(
    SceAgcCb *cb, const AgcGfx1013ColorBlendState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetDepthBiasState(
    SceAgcCb *cb, const AgcGfx1013DepthBiasState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetDepthStencilState(
    SceAgcCb *cb, const AgcGfx1013DepthStencilState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetDepthDisabled(SceAgcCb *cb);
int32_t PS5_SYSV_ABI agcGfx1013BuildFramePrologue(
    SceAgcCb *cb, const AgcGfx1013FrameState *state,
    AgcGfx1013GraphicsDefaultStats *stats);
int32_t PS5_SYSV_ABI agcGfx1013ApplyFramePostBind(
    SceAgcCb *cb, const AgcGfx1013FrameState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetContextControl(
    SceAgcCb *cb, uint32_t load_control, uint32_t shadow_control);
int32_t PS5_SYSV_ABI agcGfx1013ValidateCompute(
    const AgcGfx1013ComputeState *state);
int32_t PS5_SYSV_ABI agcGfx1013ApplyComputeDefaultsV8(
    SceAgcCb *cb, AgcGfx1013ComputeDefaultStats *stats);
int32_t PS5_SYSV_ABI agcGfx1013DispatchCompute(
    SceAgcCb *cb, const AgcGfx1013ComputeState *state);
int32_t PS5_SYSV_ABI agcGfx1013RmwHtile(
    SceAgcCb *cb, const AgcGfx1013HtileRmwState *state);
int32_t PS5_SYSV_ABI agcGfx1013BindResourceTables(
    SceAgcCb *cb, const AgcGfx1013ShaderBinding *shader,
    const AgcGfx1013ResourceTableBinding *tables, uint32_t table_count);
int32_t PS5_SYSV_ABI agcGfx1013ValidateResourceTables(
    const AgcGfx1013ShaderBinding *shader,
    const AgcGfx1013ResourceTableBinding *tables, uint32_t table_count,
    uint32_t *binding_count);
int32_t PS5_SYSV_ABI agcGfx1013BuildTessellationRingTable(
    AgcGfx1013TessellationRingTable *table,
    const AgcGfx1013TessellationState *state);
int32_t PS5_SYSV_ABI agcGfx1013BuildTessellationOffchipLayouts(
    const AgcGfx1013TessellationLayoutState *state,
    uint32_t *tcs_offchip_layout, uint32_t *tes_offchip_layout);
int32_t PS5_SYSV_ABI agcGfx1013SetTessellationRings(
    SceAgcCb *cb, const AgcGfx1013TessellationState *state);
int32_t PS5_SYSV_ABI agcGfx1013SetTessellationContext(
    SceAgcCb *cb, const AgcGfx1013TessellationState *state);
int32_t PS5_SYSV_ABI agcGfx1013ApplyGraphicsDefaultsV8(
    SceAgcCb *cb, AgcGfx1013GraphicsDefaultStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_GRAPHICS_H_ */
