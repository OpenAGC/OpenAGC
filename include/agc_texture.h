/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _AGC_TEXTURE_H_
#define _AGC_TEXTURE_H_

#include <stdint.h>
#include <stddef.h>  /* offsetof, for _Static_assert offset checks */

#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AgcSwizzleMode {
    kAgcSwModeLinear       = 0,
    kAgcSwMode256B_S       = 1,
    kAgcSwMode4KB_S        = 2,
    kAgcSwMode64KB_S       = 3,
    kAgcSwMode256KB_S      = 4,
} AgcSwizzleMode;

/*
 * Tile modes — matches shadPS4/RPCSX TileMode enum (32 values).
 * Used in T# (image) and render target descriptors.
 */
typedef enum AgcTileMode {
    kAgcTileDepth_2DThin_64       = 0,
    kAgcTileDepth_2DThin_128      = 1,
    kAgcTileDepth_2DThin_256      = 2,
    kAgcTileDepth_2DThin_512      = 3,
    kAgcTileDepth_2DThin_1K       = 4,
    kAgcTileDepth_1DThin          = 5,
    kAgcTileDepth_2DThin_64_Standard = 6,
    kAgcTileDepth_2DThin_128_Standard = 7,
    kAgcTileDisplay_LinearAligned = 8,
    kAgcTileDisplay_1DThin        = 9,
    kAgcTileDisplay_2DThin        = 10,
    kAgcTileDisplay_2DThin_Standard = 11,
    kAgcTileThin_2DThin           = 14,
    kAgcTileThin_2DThin_Standard  = 15,
    kAgcTileThick_2DThick         = 20,
    kAgcTileThick_2DThick_Standard = 21,
    kAgcTileDisplay_LinearGeneral = 31,
} AgcTileMode;

/*
 * Image types — matches shadPS4/RPCSX ImageType enum.
 */
typedef enum AgcImageType {
    kAgcImgType_1D        = 0,
    kAgcImgType_2D        = 1,
    kAgcImgType_3D        = 2,
    kAgcImgType_Cube      = 3,
    kAgcImgType_1DArray   = 4,
    kAgcImgType_2DArray   = 5,
    kAgcImgType_2DMSAA    = 6,
    kAgcImgType_2DMSAAArray = 7,
} AgcImageType;

typedef enum AgcDataFormat {
    kAgcDataFormatInvalid      = 0,
    kAgcDataFormat8            = 1,
    kAgcDataFormat8_8          = 2,
    kAgcDataFormat8_8_8_8      = 3,
    kAgcDataFormat16           = 4,
    kAgcDataFormat16_16        = 5,
    kAgcDataFormat16_16_16_16  = 6,
    kAgcDataFormat32           = 7,
    kAgcDataFormat32_32        = 8,
    kAgcDataFormat32_32_32_32  = 9,
    kAgcDataFormatBc1          = 10,
    kAgcDataFormatBc3          = 11,
    kAgcDataFormatBc7          = 12,
    /* Extended formats — cross-referenced with shadPS4/RPCSX */
    kAgcDataFormatBc2          = 13,
    kAgcDataFormatBc4          = 14,
    kAgcDataFormatBc5          = 15,
    kAgcDataFormatBc6          = 16,
    kAgcDataFormat8_24         = 17,  /* depth: 8-bit stencil + 24-bit depth */
    kAgcDataFormat24_8         = 18,  /* depth: 24-bit depth + 8-bit stencil */
    kAgcDataFormatX24_8_32     = 19,  /* depth: 32-bit float with 8-bit stencil */
    kAgcDataFormatFmask8_S     = 20,  /* Fmask formats */
    kAgcDataFormatFmask8_8_S   = 21,
    kAgcDataFormatFmask32      = 22,
    kAgcDataFormatFmask64      = 23,
    kAgcDataFormatGbGr         = 24,  /* planar/subsampled formats */
    kAgcDataFormatBgRg         = 25,
    kAgcDataFormat5_9_9_9      = 26,  /* shared exponent */
    kAgcDataFormat10_11_11     = 27,  /* shared exponent */
} AgcDataFormat;

typedef enum AgcNumberType {
    kAgcNumberUnorm = 0,
    kAgcNumberSnorm = 1,
    kAgcNumberUint  = 2,
    kAgcNumberSint  = 3,
    kAgcNumberFloat = 4,
    /* Extended number types */
    kAgcNumberSrgb  = 5,   /* gamma-corrected unorm */
    kAgcNumberUnormSnorm = 6, /* mixed: unorm for some channels, snorm for others */
} AgcNumberType;

/*
 * Sampler clamp/wrap modes — matches shadPS4 ClampMode enum.
 */
typedef enum AgcClampMode {
    kAgcClampRepeat      = 0,
    kAgcClampMirror      = 1,
    kAgcClampClamp       = 2,
    kAgcClampBorder      = 3,
    kAgcClampMirrorOnce  = 4,
} AgcClampMode;

/*
 * Sampler filter modes — matches shadPS4 Filter enum.
 */
typedef enum AgcFilterMode {
    kAgcFilterPoint      = 0,
    kAgcFilterBilinear   = 1,
    kAgcFilterAnisoPoint = 2,
    kAgcFilterAnisoLinear = 3,
} AgcFilterMode;

/*
 * Sampler mip filter modes.
 */
typedef enum AgcMipFilterMode {
    kAgcMipFilterNone    = 0,
    kAgcMipFilterPoint   = 1,
    kAgcMipFilterLinear  = 2,
} AgcMipFilterMode;

/*
 * Border color types.
 */
typedef enum AgcBorderColor {
    kAgcBorderTransparentBlack = 0,
    kAgcBorderOpaqueBlack      = 1,
    kAgcBorderWhite            = 2,
    kAgcBorderCustom           = 3,
} AgcBorderColor;

typedef struct AgcTextureDescriptor {
    uint64_t base_address;
    uint32_t width_minus1;
    uint32_t height_minus1;
    uint32_t depth_minus1;
    uint32_t pitch_minus1;
    uint32_t format;
    /* Bitfield word (32 bits) — packed T# control bits:
     *   [2:0]   dst_sel_x  (SQ_SEL_* channel mapping, 0-7)
     *   [5:3]   dst_sel_y
     *   [8:6]   dst_sel_z
     *   [11:9]  dst_sel_w
     *   [15:12] img_type   (AgcImageType, 0-15)
     *   [20:16] sw_mode    (AgcTileMode/AgcSwizzleMode, 0-31)
     *   [25:21] mip_levels (mip level count, 0-31)
     *   [28:26] base_array (first array slice, 0-7)
     *   [31:29] last_array (last array slice, 0-7)
     */
    uint32_t dst_sel_x : 3;
    uint32_t dst_sel_y : 3;
    uint32_t dst_sel_z : 3;
    uint32_t dst_sel_w : 3;
    uint32_t img_type  : 4;
    uint32_t sw_mode   : 5;
    uint32_t mip_levels: 5;
    uint32_t base_array: 3;
    uint32_t last_array: 3;
} AgcTextureDescriptor;

typedef struct AgcBufferDescriptor {
    uint64_t base_address;
    uint32_t stride      : 14;
    uint32_t dst_sel_x   : 3;
    uint32_t dst_sel_y   : 3;
    uint32_t dst_sel_z   : 3;
    uint32_t dst_sel_w   : 3;
    uint32_t reserved    : 6;
    uint32_t num_records : 16;
    uint32_t format      : 16;
} AgcBufferDescriptor;

typedef struct AgcSamplerDescriptor {
    uint32_t words[4];
} AgcSamplerDescriptor;

/*
 * Surface number type — CB_COLOR_INFO channel type field (3 bits, [10:8]).
 * Matches shadPS4 NumberFormat / freegnm GnmSurfaceNumber.
 *
 * Note: The CB register only has 3 bits, so values are limited to 0-7.
 * SRGB (which is 9 in the full texture NumberFormat enum) is encoded as
 * SnormNz (6) in the CB register, per shadPS4's GetFixedNumberFormat().
 * The driver remaps SnormNz→Srgb when interpreting the render target.
 */
typedef enum AgcSurfaceNumber {
    kAgcSurfNumUnorm     = 0,
    kAgcSurfNumSnorm     = 1,
    kAgcSurfNumUscaled   = 2,
    kAgcSurfNumSscaled   = 3,
    kAgcSurfNumUint      = 4,
    kAgcSurfNumSint      = 5,
    kAgcSurfNumSnormNz   = 6,  /* CB encoding for SRGB (SnormOgl in freegnm) */
    kAgcSurfNumFloat     = 7,
} AgcSurfaceNumber;

/*
 * Surface channel order — CB_COLOR_INFO comp_swap field (2 bits, [12:11]).
 * Matches shadPS4 SwapMode / freegnm GnmSurfaceSwap.
 */
typedef enum AgcSurfaceSwap {
    kAgcSurfSwapStandard         = 0,  /* RGBA */
    kAgcSurfSwapAlternate        = 1,  /* BGRA */
    kAgcSurfSwapStandardReverse  = 2,  /* ABGR */
    kAgcSurfSwapAlternateReverse = 3,  /* ARGB */
} AgcSurfaceSwap;

/*
 * AgcRenderTarget — 64-byte (16-dword) color-buffer descriptor.
 *
 * Each dword maps directly to a CB_COLOR register slot on the GPU.
 * The CB_COLOR0_BASE register is at offset 0xA318 and each slot is
 * 15 dwords (0xF) apart, so slot N starts at 0xA318 + N*0xF.
 *
 * Layout cross-referenced with:
 *   - freegnm  GnmRenderTarget   (freegnm/gnm/rendertarget.h)
 *   - shadPS4  ColorBuffer       (video_core/amdgpu/regs_color.h)
 *   - ps5-openagc agx_render_target_t (simplified; ps5-openagc is NOT proven
 *     working — used for field name cross-reference only)
 *
 * Dword  Register            Field
 *   0     CB_COLOR*_BASE      base_base256b (GPU addr >> 8)
 *   1     CB_COLOR*_PITCH     tile_max + fmask_tile_max
 *   2     CB_COLOR*_SLICE     tile_max (slice size in 64B tiles)
 *   3     CB_COLOR*_VIEW      slice_start + slice_max
 *   4     CB_COLOR*_INFO      format, number type, comp swap, dcc, cmask
 *   5     CB_COLOR*_ATTRIB    tile mode, fmask tile mode, samples, fragments
 *   6     CB_COLOR*_DCC_CNTL  DCC compression control
 *   7     CB_COLOR*_CMASK_BASE cmask base (addr >> 8)
 *   8     CB_COLOR*_CMASK_SLICE cmask tile_max
 *   9     CB_COLOR*_FMASK_BASE fmask base (addr >> 8)
 *  10     CB_COLOR*_FMASK_SLICE fmask tile_max
 *  11     CB_COLOR*_CLEAR_WORD0 fast-clear color word 0
 *  12     CB_COLOR*_CLEAR_WORD1 fast-clear color word 1
 *  13     CB_COLOR*_DCC_BASE  DCC base (addr >> 8)
 *  14     (reserved)
 *  15     (not a GPU register) width + height packed for host use
 */
typedef struct AgcRenderTarget {
    /* Dword 0 — CB_COLOR*_BASE: base address >> 8 (256-byte aligned) */
    uint32_t base_base256b;

    /* Dword 1 — CB_COLOR*_PITCH */
    union {
        struct {
            uint32_t tile_max      : 11;  /* (pitch_pixels / 8) - 1 */
            uint32_t _reserved0    : 9;
            uint32_t fmask_tile_max: 11;  /* fmask pitch in tiles - 1 */
            uint32_t _reserved1    : 1;
        };
        uint32_t pitch_u32;
    } pitch;

    /* Dword 2 — CB_COLOR*_SLICE */
    union {
        struct {
            uint32_t tile_max   : 22;  /* (pitch/8 * height/8) - 1 */
            uint32_t _reserved0 : 10;
        };
        uint32_t slice_u32;
    } slice;

    /* Dword 3 — CB_COLOR*_VIEW */
    union {
        struct {
            uint32_t slice_start : 11;
            uint32_t _reserved0  : 2;
            uint32_t slice_max   : 11;  /* num_slices - 1 */
            uint32_t _reserved1  : 8;
        };
        uint32_t view_u32;
    } view;

    /* Dword 4 — CB_COLOR*_INFO */
    union {
        struct {
            uint32_t endian_swap          : 2;   /* [1:0] */
            uint32_t format               : 5;   /* [6:2] AgcDataFormat */
            uint32_t linear_general       : 1;   /* [7] */
            uint32_t number_type          : 3;   /* [10:8] AgcSurfaceNumber */
            uint32_t comp_swap            : 2;   /* [12:11] AgcSurfaceSwap */
            uint32_t fast_clear           : 1;   /* [13] */
            uint32_t compression          : 1;   /* [14] */
            uint32_t blend_clamp          : 1;   /* [15] */
            uint32_t blend_bypass         : 1;   /* [16] */
            uint32_t simple_float         : 1;   /* [17] */
            uint32_t round_mode           : 1;   /* [18] */
            uint32_t cmask_is_linear      : 1;   /* [19] */
            uint32_t blend_opt_dont_rd_dst: 3;   /* [22:20] */
            uint32_t blend_opt_discard_px : 3;   /* [25:23] */
            uint32_t fmask_compress_dis   : 1;   /* [26] fmask_compression_mode */
            uint32_t fmask_compress_1frag : 1;   /* [27] */
            uint32_t dcc_enable           : 1;   /* [28] */
            uint32_t cmask_addr_type      : 2;   /* [30:29] */
            uint32_t alt_tile_mode        : 1;   /* [31] NEO vs base */
        };
        uint32_t info_u32;
    } info;

    /* Dword 5 — CB_COLOR*_ATTRIB */
    union {
        struct {
            uint32_t tile_mode_index    : 5;  /* [4:0] AgcTileMode */
            uint32_t fmask_tile_mode    : 5;  /* [9:5] AgcTileMode */
            uint32_t fmask_bank_height  : 2;  /* [11:10] */
            uint32_t num_samples_log2   : 3;  /* [14:12] log2(MSAA samples) */
            uint32_t num_fragments_log2 : 2;  /* [16:15] log2(fragments) */
            uint32_t force_dst_alpha_1  : 1;  /* [17] */
            uint32_t _reserved0         : 14;
        };
        uint32_t attrib_u32;
    } attrib;

    /* Dword 6 — CB_COLOR*_DCC_CNTL */
    union {
        struct {
            uint32_t overwrite_combine_disabler : 1;  /* [0] */
            uint32_t _reserved0                 : 1;  /* [1] */
            uint32_t max_uncompressed_blocksize : 2;  /* [3:2] */
            uint32_t min_compressed_blocksize   : 1;  /* [4] */
            uint32_t max_compressed_blocksize   : 2;  /* [6:5] */
            uint32_t color_transform            : 2;  /* [8:7] */
            uint32_t independent_64b_blocks     : 1;  /* [9] */
            uint32_t _reserved1                 : 22;
        };
        uint32_t dcc_control_u32;
    } dcc_control;

    /* Dword 7 — CB_COLOR*_CMASK_BASE: cmask address >> 8 */
    uint32_t cmask_base256b;

    /* Dword 8 — CB_COLOR*_CMASK_SLICE */
    union {
        struct {
            uint32_t tile_max   : 14;
            uint32_t _reserved0 : 18;
        };
        uint32_t cmask_slice_u32;
    } cmask_slice;

    /* Dword 9 — CB_COLOR*_FMASK_BASE: fmask address >> 8 */
    uint32_t fmask_base256b;

    /* Dword 10 — CB_COLOR*_FMASK_SLICE */
    union {
        struct {
            uint32_t tile_max   : 22;
            uint32_t _reserved0 : 10;
        };
        uint32_t fmask_slice_u32;
    } fmask_slice;

    /* Dword 11 — CB_COLOR*_CLEAR_WORD0 */
    uint32_t clear_word0;

    /* Dword 12 — CB_COLOR*_CLEAR_WORD1 */
    uint32_t clear_word1;

    /* Dword 13 — CB_COLOR*_DCC_BASE: DCC address >> 8 */
    uint32_t dcc_base256b;

    /* Dword 14 — reserved (GPU register space padding) */
    uint32_t _reserved_dword14;

    /* Dword 15 — not a GPU register; width/height packed for host use */
    union {
        struct {
            uint16_t width;
            uint16_t height;
        };
        uint32_t size_u32;
    } size;
} AgcRenderTarget;

_Static_assert(sizeof(AgcRenderTarget) == 0x40,
    "AgcRenderTarget must be 64 bytes (16 dwords)");
_Static_assert(offsetof(AgcRenderTarget, pitch) == 4,
    "AgcRenderTarget.pitch at dword 1 (offset 4)");
_Static_assert(offsetof(AgcRenderTarget, info) == 16,
    "AgcRenderTarget.info at dword 4 (offset 16)");
_Static_assert(offsetof(AgcRenderTarget, attrib) == 20,
    "AgcRenderTarget.attrib at dword 5 (offset 20)");
_Static_assert(offsetof(AgcRenderTarget, cmask_base256b) == 28,
    "AgcRenderTarget.cmask_base256b at dword 7 (offset 28)");
_Static_assert(offsetof(AgcRenderTarget, fmask_base256b) == 36,
    "AgcRenderTarget.fmask_base256b at dword 9 (offset 36)");
_Static_assert(offsetof(AgcRenderTarget, clear_word0) == 44,
    "AgcRenderTarget.clear_word0 at dword 11 (offset 44)");
_Static_assert(offsetof(AgcRenderTarget, dcc_base256b) == 52,
    "AgcRenderTarget.dcc_base256b at dword 13 (offset 52)");
_Static_assert(offsetof(AgcRenderTarget, size) == 60,
    "AgcRenderTarget.size at dword 15 (offset 60)");

void agcTextureDescriptorInit(AgcTextureDescriptor *desc);
void agcTextureDescriptorSetDimensions(
    AgcTextureDescriptor *desc, uint32_t width, uint32_t height, uint32_t depth);
void agcTextureDescriptorSetFormat(
    AgcTextureDescriptor *desc, AgcDataFormat fmt, AgcNumberType ntype);
void agcTextureDescriptorSetSwizzleMode(AgcTextureDescriptor *desc, AgcSwizzleMode mode);
void agcTextureDescriptorSetBaseAddress(AgcTextureDescriptor *desc, uint64_t gpu_addr);

/* Typed convenience setters using the expanded enums. */
void agcTextureDescriptorSetImageType(AgcTextureDescriptor *desc, AgcImageType type);
void agcTextureDescriptorSetTileMode(AgcTextureDescriptor *desc, AgcTileMode mode);
void agcTextureDescriptorSetMipLevels(AgcTextureDescriptor *desc, uint32_t num_levels);
void agcTextureDescriptorSetArraySize(AgcTextureDescriptor *desc,
    uint32_t base_array, uint32_t last_array);
void agcTextureDescriptorSetDepth(AgcTextureDescriptor *desc, uint32_t depth);
void agcTextureDescriptorSetPitch(AgcTextureDescriptor *desc, uint32_t pitch);
void agcTextureDescriptorSetDstSel(AgcTextureDescriptor *desc,
    uint32_t x, uint32_t y, uint32_t z, uint32_t w);

/* Accessors. */
uint64_t agcTextureDescriptorGetBaseAddress(const AgcTextureDescriptor *desc);
uint32_t agcTextureDescriptorGetWidth(const AgcTextureDescriptor *desc);
uint32_t agcTextureDescriptorGetHeight(const AgcTextureDescriptor *desc);

/*
 * Hardware format field encode/decode helpers.
 *
 * The texture descriptor format field packs the data format (6 bits,
 * [5:0]) and number type (4 bits, [9:6]) into a single uint32, matching
 * the SQ_IMG_RSRC_WORD1 layout used by agcTextureDescriptorSetFormat.
 * These helpers expose the packing directly so callers can compute or
 * inspect the format field without a descriptor.
 */

/* Get the hardware-encoded format value for a data format + number type
 * combo. Returns the packed format field value
 * (data_format | (num_type << 6)). */
uint32_t agcTextureFormatEncode(AgcDataFormat fmt, AgcNumberType ntype);

/* Decode the hardware format field back into the data format (low 6 bits). */
AgcDataFormat agcTextureFormatGetDataFormat(uint32_t format);

/* Decode the hardware format field back into the number type (bits [9:6]). */
AgcNumberType agcTextureFormatGetNumberType(uint32_t format);

void agcBufferDescriptorInit(AgcBufferDescriptor *desc);
void agcBufferDescriptorSetAddress(AgcBufferDescriptor *desc, uint64_t gpu_addr);
void agcBufferDescriptorSetStride(AgcBufferDescriptor *desc, uint32_t stride_bytes);
void agcBufferDescriptorSetNumRecords(AgcBufferDescriptor *desc, uint32_t num_records);

void agcSamplerDescriptorInit(AgcSamplerDescriptor *desc);
void agcSamplerDescriptorSetFilters(AgcSamplerDescriptor *desc,
    uint32_t min_filter, uint32_t mag_filter, uint32_t mip_filter);
void agcSamplerDescriptorSetWrapModes(AgcSamplerDescriptor *desc,
    uint32_t wrap_s, uint32_t wrap_t, uint32_t wrap_r);
void agcSamplerDescriptorSetLod(AgcSamplerDescriptor *desc,
    float min_lod, float max_lod, float lod_bias);
void agcSamplerDescriptorSetCompareFunc(AgcSamplerDescriptor *desc,
    uint32_t compare_func);
void agcSamplerDescriptorSetAnisotropy(AgcSamplerDescriptor *desc,
    uint32_t max_anisotropy);

/* Hardware-ready gfx1013 resource descriptors. These types are byte-exact
 * SQ descriptor layouts, unlike the portable abstract descriptors above. */
#define AGC_GFX1013_BUFFER_WORD3_STRUCTURED 0x11014FACu
#define AGC_GFX1013_BUFFER_WORD3_RAW 0x31014FACu
#define AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM 56u
/* GFX10 SQ image-resource formats use a 9-bit field. These BC encodings match
 * the gfx10 resource-format table and are sampled-image formats, not render
 * target formats. */
#define AGC_GFX1013_IMAGE_FORMAT_BC1_UNORM 169u
#define AGC_GFX1013_IMAGE_FORMAT_BC1_SRGB 170u
#define AGC_GFX1013_IMAGE_FORMAT_BC2_UNORM 171u
#define AGC_GFX1013_IMAGE_FORMAT_BC2_SRGB 172u
#define AGC_GFX1013_IMAGE_FORMAT_BC3_UNORM 173u
#define AGC_GFX1013_IMAGE_FORMAT_BC3_SRGB 174u
#define AGC_GFX1013_IMAGE_FORMAT_BC4_UNORM 175u
#define AGC_GFX1013_IMAGE_FORMAT_BC4_SNORM 176u
#define AGC_GFX1013_IMAGE_FORMAT_BC5_UNORM 177u
#define AGC_GFX1013_IMAGE_FORMAT_BC5_SNORM 178u
#define AGC_GFX1013_IMAGE_FORMAT_BC6_UFLOAT 179u
#define AGC_GFX1013_IMAGE_FORMAT_BC6_SFLOAT 180u
#define AGC_GFX1013_IMAGE_FORMAT_BC7_UNORM 181u
#define AGC_GFX1013_IMAGE_FORMAT_BC7_SRGB 182u
#define AGC_GFX1013_IMAGE_TYPE_2D 9u
#define AGC_GFX1013_IMAGE_TYPE_CUBE 11u
#define AGC_GFX1013_IMAGE_TYPE_2D_ARRAY 13u
#define AGC_GFX1013_IMAGE_TYPE_2D_MSAA 14u
#define AGC_GFX1013_IMAGE_SWIZZLE_64KB_R_X 27u
#define AGC_GFX1013_IMAGE_64KB_ALIGNMENT 0x10000u

typedef struct AgcGfx1013BufferDescriptor {
    uint32_t words[4];
} AgcGfx1013BufferDescriptor;

typedef struct AgcGfx1013ImageDescriptor {
    uint32_t words[8];
} AgcGfx1013ImageDescriptor;

typedef struct AgcGfx1013Image2DState {
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t image_type;
    uint32_t dst_sel_x;
    uint32_t dst_sel_y;
    uint32_t dst_sel_z;
    uint32_t dst_sel_w;
    uint32_t sample_count;
    uint32_t swizzle_mode;
    uint32_t base_array_layer;
    uint32_t last_array_layer;
    uint32_t mip_level_count;
} AgcGfx1013Image2DState;

typedef struct AgcGfx1013BcFormatInfo {
    uint32_t resource_format;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t bytes_per_block;
} AgcGfx1013BcFormatInfo;

typedef struct AgcGfx1013LinearBcSurfaceLayoutInput {
    uint32_t width;
    uint32_t height;
    uint32_t layer_count;
    uint32_t mip_level_count;
    uint32_t resource_format;
} AgcGfx1013LinearBcSurfaceLayoutInput;

typedef struct AgcGfx1013LinearBcSurfaceLayout {
    uint64_t allocation_size;
    uint64_t slice_size;
    uint32_t pitch_blocks;
    uint32_t padded_height_blocks;
    uint32_t alignment;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t bytes_per_block;
} AgcGfx1013LinearBcSurfaceLayout;

typedef struct AgcGfx1013LinearBcSubresourceLayout {
    uint64_t offset;
    uint64_t size;
    uint32_t row_pitch;
    uint32_t pitch_blocks;
    uint32_t width_blocks;
    uint32_t height_blocks;
    uint32_t width;
    uint32_t height;
} AgcGfx1013LinearBcSubresourceLayout;

typedef struct AgcGfx1013CombinedImageSamplerDescriptor {
    AgcGfx1013ImageDescriptor image;
    AgcSamplerDescriptor sampler;
    uint32_t reserved[4];
} AgcGfx1013CombinedImageSamplerDescriptor;

_Static_assert(sizeof(AgcGfx1013BufferDescriptor) == 16,
    "gfx1013 buffer descriptor must be 16 bytes");
_Static_assert(sizeof(AgcGfx1013ImageDescriptor) == 32,
    "gfx1013 image descriptor must be 32 bytes");
_Static_assert(sizeof(AgcGfx1013Image2DState) == 64,
    "gfx1013 image state must retain its append-only 64-byte layout");
_Static_assert(sizeof(AgcGfx1013BcFormatInfo) == 16,
    "gfx1013 BC format info must be 16 bytes");
_Static_assert(sizeof(AgcGfx1013LinearBcSurfaceLayout) == 40,
    "gfx1013 linear BC surface layout must be 40 bytes");
_Static_assert(sizeof(AgcGfx1013LinearBcSubresourceLayout) == 40,
    "gfx1013 linear BC subresource layout must be 40 bytes");
_Static_assert(sizeof(AgcGfx1013CombinedImageSamplerDescriptor) == 64,
    "gfx1013 combined image/sampler descriptor must be 64 bytes");
_Static_assert(offsetof(AgcGfx1013CombinedImageSamplerDescriptor, sampler) == 32,
    "gfx1013 combined descriptor sampler must start at dword 8");

int32_t PS5_SYSV_ABI agcGfx1013BufferDescriptorEncode(
    AgcGfx1013BufferDescriptor *descriptor, uint64_t address,
    uint32_t stride, uint32_t element_count);
int32_t PS5_SYSV_ABI agcGfx1013RawBufferDescriptorEncode(
    AgcGfx1013BufferDescriptor *descriptor, uint64_t address,
    uint32_t byte_size);
int32_t PS5_SYSV_ABI agcGfx1013Image2DDescriptorEncode(
    AgcGfx1013ImageDescriptor *descriptor,
    const AgcGfx1013Image2DState *state);
int32_t PS5_SYSV_ABI agcGfx1013GetBcFormatInfo(
    uint32_t resource_format, AgcGfx1013BcFormatInfo *info);
int32_t PS5_SYSV_ABI agcGfx1013GetLinearBcSurfaceLayout(
    const AgcGfx1013LinearBcSurfaceLayoutInput *input,
    AgcGfx1013LinearBcSurfaceLayout *layout);
int32_t PS5_SYSV_ABI agcGfx1013GetLinearBcSubresourceLayout(
    const AgcGfx1013LinearBcSurfaceLayoutInput *input, uint32_t mip_level,
    uint32_t layer, AgcGfx1013LinearBcSubresourceLayout *layout);
int32_t PS5_SYSV_ABI agcGfx1013CombinedImageSamplerDescriptorEncode(
    AgcGfx1013CombinedImageSamplerDescriptor *descriptor,
    const AgcGfx1013Image2DState *image,
    const AgcSamplerDescriptor *sampler);

/* Typed convenience setters using the expanded sampler enums. */
void agcSamplerDescriptorSetClampMode(AgcSamplerDescriptor *desc,
    AgcClampMode s, AgcClampMode t, AgcClampMode r);
void agcSamplerDescriptorSetFilterMode(AgcSamplerDescriptor *desc,
    AgcFilterMode min_filter, AgcFilterMode mag_filter, AgcMipFilterMode mip_filter);
void agcSamplerDescriptorSetBorderColor(AgcSamplerDescriptor *desc,
    AgcBorderColor border_color);
void agcSamplerDescriptorSetMaxAnisotropy(AgcSamplerDescriptor *desc,
    uint32_t max_aniso);

/*
 * AgcRenderTarget helpers — initialize and configure a 64-byte color-buffer
 * descriptor matching the CB_COLOR register layout.
 */

/* Zero the descriptor and set sane defaults (1 sample, 1 fragment, 2D thin). */
void agcRenderTargetInit(AgcRenderTarget *rt);

/* Set the color-buffer base GPU address (must be 256-byte aligned). */
void agcRenderTargetSetBaseAddress(AgcRenderTarget *rt, uint64_t gpu_addr);

/* Set the color-buffer format and number type in CB_COLOR_INFO. */
void agcRenderTargetSetFormat(AgcRenderTarget *rt,
    AgcDataFormat fmt, AgcSurfaceNumber ntype);

/* Set the component swap order in CB_COLOR_INFO. */
void agcRenderTargetSetCompSwap(AgcRenderTarget *rt, AgcSurfaceSwap swap);

/* Set width/height (stored in dword 15) and derive pitch/slice tile_max.
 * pitch must be a multiple of 8. */
void agcRenderTargetSetDimensions(AgcRenderTarget *rt,
    uint32_t width, uint32_t height, uint32_t pitch);

/* Set the tile mode (CB_COLOR_ATTRIB.tile_mode_index). */
void agcRenderTargetSetTileMode(AgcRenderTarget *rt, AgcTileMode mode);

/* Set the fmask tile mode (CB_COLOR_ATTRIB.fmask_tile_mode). */
void agcRenderTargetSetFmaskTileMode(AgcRenderTarget *rt, AgcTileMode mode);

/* Set MSAA sample/fragment counts (stored as log2 in CB_COLOR_ATTRIB). */
void agcRenderTargetSetNumSamples(AgcRenderTarget *rt, uint32_t num_samples);
void agcRenderTargetSetNumFragments(AgcRenderTarget *rt, uint32_t num_fragments);

/* Set the CMASK base GPU address (256-byte aligned). */
void agcRenderTargetSetCmaskBaseAddress(AgcRenderTarget *rt, uint64_t gpu_addr);

/* Set the FMASK base GPU address (256-byte aligned). */
void agcRenderTargetSetFmaskBaseAddress(AgcRenderTarget *rt, uint64_t gpu_addr);

/* Set the DCC base GPU address (256-byte aligned) and enable DCC. */
void agcRenderTargetSetDccBaseAddress(AgcRenderTarget *rt, uint64_t gpu_addr);

/* Enable or disable DCC compression (CB_COLOR_INFO.dcc_enable). */
void agcRenderTargetSetDccEnable(AgcRenderTarget *rt, uint32_t enable);

/* Set the fast-clear color words (CB_COLOR_CLEAR_WORD0/1). */
void agcRenderTargetSetClearWords(AgcRenderTarget *rt,
    uint32_t word0, uint32_t word1);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_TEXTURE_H_ */
