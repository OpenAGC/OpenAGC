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

/*
 * openagc — texture.c
 *
 * Texture, buffer, and sampler descriptor helpers for RDNA2.
 */

#include "agc_texture.h"
#include "agc_types.h"
#include "agc_error.h"

#include <string.h>

void agcTextureDescriptorInit(AgcTextureDescriptor* desc) {
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    /* Default channel mapping: XYZW */
    desc->dst_sel_x = 4; /* SQ_SEL_X */
    desc->dst_sel_y = 5; /* SQ_SEL_Y */
    desc->dst_sel_z = 6; /* SQ_SEL_Z */
    desc->dst_sel_w = 7; /* SQ_SEL_W */
    desc->img_type  = 1; /* 2D */
    desc->sw_mode   = kAgcSwMode64KB_S;
}

void agcTextureDescriptorSetDimensions(AgcTextureDescriptor* desc,
    uint32_t width, uint32_t height, uint32_t depth)
{
    if (!desc) return;
    desc->width_minus1  = (width > 0)  ? (width - 1)  : 0;
    desc->height_minus1 = (height > 0) ? (height - 1) : 0;
    desc->depth_minus1  = (depth > 0)  ? (depth - 1)  : 0;
}

void agcTextureDescriptorSetFormat(AgcTextureDescriptor* desc,
    AgcDataFormat fmt, AgcNumberType ntype)
{
    if (!desc) return;
    /* Pack data format (6 bits, [5:0]) and number type (4 bits, [9:6])
     * into the format field, matching SQ_IMG_RSRC_WORD1 layout where
     * DATA_FORMAT is at [25:20] and NUM_FORMAT is at [29:26]. */
    desc->format = ((uint32_t)fmt & 0x3Fu) | (((uint32_t)ntype & 0xFu) << 6);
}

void agcTextureDescriptorSetSwizzleMode(AgcTextureDescriptor* desc,
    AgcSwizzleMode mode)
{
    if (!desc) return;
    desc->sw_mode = mode;
}

void agcTextureDescriptorSetBaseAddress(AgcTextureDescriptor* desc,
    uint64_t gpu_addr)
{
    if (!desc) return;
    desc->base_address = gpu_addr;
}

void agcTextureDescriptorSetImageType(AgcTextureDescriptor* desc,
    AgcImageType type)
{
    if (!desc) return;
    /* img_type is a 4-bit field (0-15); AgcImageType values fit in 3 bits. */
    desc->img_type = (uint32_t)type & 0xFu;
}

void agcTextureDescriptorSetTileMode(AgcTextureDescriptor* desc,
    AgcTileMode mode)
{
    if (!desc) return;
    /* sw_mode is a 5-bit field (0-31); AgcTileMode values use the full range,
     * e.g. kAgcTileDisplay_LinearGeneral == 31. */
    desc->sw_mode = (uint32_t)mode & 0x1Fu;
}

void agcTextureDescriptorSetMipLevels(AgcTextureDescriptor* desc,
    uint32_t num_levels)
{
    if (!desc) return;
    /* mip_levels is a 5-bit field (0-31). Store the count directly; values
     * above 31 are clamped to the maximum representable level count. */
    desc->mip_levels = (num_levels > 31u) ? 31u : num_levels;
}

void agcTextureDescriptorSetArraySize(AgcTextureDescriptor* desc,
    uint32_t base_array, uint32_t last_array)
{
    if (!desc) return;
    /* base_array/last_array are 3-bit fields (0-7). The slice range is
     * clamped to the representable window. */
    desc->base_array = (base_array > 7u) ? 7u : base_array;
    desc->last_array = (last_array > 7u) ? 7u : last_array;
}

void agcTextureDescriptorSetDepth(AgcTextureDescriptor* desc, uint32_t depth)
{
    if (!desc) return;
    desc->depth_minus1 = (depth > 0) ? (depth - 1) : 0;
}

void agcTextureDescriptorSetPitch(AgcTextureDescriptor* desc, uint32_t pitch)
{
    if (!desc) return;
    desc->pitch_minus1 = (pitch > 0) ? (pitch - 1) : 0;
}

void agcTextureDescriptorSetDstSel(AgcTextureDescriptor* desc,
    uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    if (!desc) return;
    /* Each destination selector is a 3-bit field (0-7, SQ_SEL_*). */
    desc->dst_sel_x = x & 0x7u;
    desc->dst_sel_y = y & 0x7u;
    desc->dst_sel_z = z & 0x7u;
    desc->dst_sel_w = w & 0x7u;
}

uint64_t agcTextureDescriptorGetBaseAddress(const AgcTextureDescriptor* desc)
{
    if (!desc) return 0;
    return desc->base_address;
}

uint32_t agcTextureDescriptorGetWidth(const AgcTextureDescriptor* desc)
{
    if (!desc) return 0;
    return desc->width_minus1 + 1u;
}

uint32_t agcTextureDescriptorGetHeight(const AgcTextureDescriptor* desc)
{
    if (!desc) return 0;
    return desc->height_minus1 + 1u;
}

/*
 * Hardware format field encode/decode helpers.
 *
 * The format field packs data_format (6 bits, [5:0]) and number_type
 * (4 bits, [9:6]). This matches the packing in
 * agcTextureDescriptorSetFormat and the SQ_IMG_RSRC_WORD1 layout.
 */
uint32_t agcTextureFormatEncode(AgcDataFormat fmt, AgcNumberType ntype)
{
    return ((uint32_t)fmt & 0x3Fu) | (((uint32_t)ntype & 0xFu) << 6);
}

AgcDataFormat agcTextureFormatGetDataFormat(uint32_t format)
{
    return (AgcDataFormat)(format & 0x3Fu);
}

AgcNumberType agcTextureFormatGetNumberType(uint32_t format)
{
    return (AgcNumberType)((format >> 6) & 0xFu);
}

void agcBufferDescriptorInit(AgcBufferDescriptor* desc) {
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    desc->dst_sel_x = 4;
    desc->dst_sel_y = 5;
    desc->dst_sel_z = 6;
    desc->dst_sel_w = 7;
}

void agcBufferDescriptorSetAddress(AgcBufferDescriptor* desc,
    uint64_t gpu_addr)
{
    if (!desc) return;
    desc->base_address = gpu_addr;
}

void agcBufferDescriptorSetStride(AgcBufferDescriptor* desc,
    uint32_t stride_bytes)
{
    if (!desc) return;
    desc->stride = stride_bytes & 0x3FFF;
}

void agcBufferDescriptorSetNumRecords(AgcBufferDescriptor* desc,
    uint32_t num_records)
{
    if (!desc) return;
    desc->num_records = num_records & 0xFFFF;
}

/*
 * Sampler descriptor bit layout — matches AMD SQ_IMG_SAMP_WORD0-3 registers
 * (R_008F30 - R_008F3C) and freegnm GnmSampler struct.
 *
 * Word 0 (SQ_IMG_SAMP_WORD0):
 *   [2:0]   CLAMP_X           (GnmTexClamp, 3 bits)
 *   [5:3]   CLAMP_Y           (GnmTexClamp, 3 bits)
 *   [8:6]   CLAMP_Z           (GnmTexClamp, 3 bits)
 *   [11:9]  MAX_ANISO_RATIO   (log2(aniso level), 3 bits)
 *   [14:12] DEPTH_COMPARE_FUNC (GnmDepthCompare, 3 bits)
 *   [15]    FORCE_UNNORMALIZED
 *   [18:16] ANISO_THRESHOLD
 *   [19]    MC_COORD_TRUNC
 *   [20]    FORCE_DEGAMMA
 *   [26:21] ANISO_BIAS        (6 bits)
 *   [27]    TRUNC_COORD
 *   [28]    DISABLE_CUBE_WRAP
 *   [30:29] FILTER_MODE       (0=Blend, 1=Min, 2=Max)
 *   [31]    COMPAT_MODE
 *
 * Word 1 (SQ_IMG_SAMP_WORD1):
 *   [11:0]  MIN_LOD           (4.8 fixed point, 12 bits)
 *   [23:12] MAX_LOD           (4.8 fixed point, 12 bits)
 *   [27:24] PERF_MIP
 *   [31:28] PERF_Z
 *
 * Word 2 (SQ_IMG_SAMP_WORD2):
 *   [13:0]  LOD_BIAS          (4.8 fixed point signed, 14 bits)
 *   [19:14] LOD_BIAS_SEC      (6 bits)
 *   [21:20] XY_MAG_FILTER     (GnmFilter: 0=Point,1=Bilinear,2=AnisoPoint,3=AnisoBilinear)
 *   [23:22] XY_MIN_FILTER     (GnmFilter)
 *   [25:24] Z_FILTER          (GnmZFilter: 0=None,1=Point,2=Linear)
 *   [27:26] MIP_FILTER        (GnmMipFilter: 0=None,1=Point,2=Linear)
 *   [28]    MIP_POINT_PRECLAMP
 *   [29]    DISABLE_LSB_CEIL
 *   [31:30] (unused)
 *
 * Word 3 (SQ_IMG_SAMP_WORD3):
 *   [11:0]  BORDER_COLOR_PTR  (index into border color table)
 *   [29:12] (unused)
 *   [31:30] BORDER_COLOR_TYPE (GnmBorderColor: 0=TransBlack,1=OpaqueBlack,2=OpaqueWhite,3=FromTable)
 */

void agcSamplerDescriptorInit(AgcSamplerDescriptor *desc) {
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
}

void agcSamplerDescriptorSetFilters(AgcSamplerDescriptor *desc,
    uint32_t min_filter, uint32_t mag_filter, uint32_t mip_filter)
{
    if (!desc) return;
    /* XY_MIN_FILTER at word2 [23:22], XY_MAG_FILTER at [21:20],
     * MIP_FILTER at [27:26]. Each is 2 bits. */
    desc->words[2] &= ~((0x3u << 22) | (0x3u << 20) | (0x3u << 26));
    desc->words[2] |= (min_filter & 0x3u) << 22;
    desc->words[2] |= (mag_filter & 0x3u) << 20;
    desc->words[2] |= (mip_filter & 0x3u) << 26;
}

void agcSamplerDescriptorSetWrapModes(AgcSamplerDescriptor *desc,
    uint32_t wrap_s, uint32_t wrap_t, uint32_t wrap_r)
{
    if (!desc) return;
    /* CLAMP_X at [2:0], CLAMP_Y at [5:3], CLAMP_Z at [8:6].
     * Each is 3 bits (hardware GnmTexClamp values 0-7). */
    desc->words[0] &= ~((0x7u << 0) | (0x7u << 3) | (0x7u << 6));
    desc->words[0] |= (wrap_s & 0x7u);
    desc->words[0] |= (wrap_t & 0x7u) << 3;
    desc->words[0] |= (wrap_r & 0x7u) << 6;
}

void agcSamplerDescriptorSetLod(AgcSamplerDescriptor *desc,
    float min_lod, float max_lod, float lod_bias)
{
    if (!desc) return;
    /* MIN_LOD at word1 [11:0], MAX_LOD at word1 [23:12].
     * Both are 12-bit 4.8 fixed point. */
    uint32_t min_lod_fp = (uint32_t)(min_lod * 256.0f) & 0xFFFu;
    uint32_t max_lod_fp = (uint32_t)(max_lod * 256.0f) & 0xFFFu;
    desc->words[1] &= ~0x00FFFFFFu;
    desc->words[1] |= min_lod_fp;
    desc->words[1] |= max_lod_fp << 12;

    /* LOD_BIAS at word2 [13:0], 14-bit 4.8 fixed point (signed). */
    int32_t bias_fp = (int32_t)(lod_bias * 256.0f);
    desc->words[2] &= ~0x3FFFu;
    desc->words[2] |= (uint32_t)bias_fp & 0x3FFFu;
}

void agcSamplerDescriptorSetCompareFunc(AgcSamplerDescriptor *desc,
    uint32_t compare_func)
{
    if (!desc) return;
    /* DEPTH_COMPARE_FUNC at word0 [14:12], 3 bits. */
    desc->words[0] &= ~(0x7u << 12);
    desc->words[0] |= (compare_func & 0x7u) << 12;
}

void agcSamplerDescriptorSetAnisotropy(AgcSamplerDescriptor *desc,
    uint32_t max_anisotropy)
{
    if (!desc) return;
    /* MAX_ANISO_RATIO at word0 [11:9], 3 bits.
     * Stored as log2 of the aniso level: 1→0, 2→1, 4→2, 8→3, 16→4.
     * A ratio of 0 means no anisotropic filtering. */
    uint32_t ratio = 0;
    if (max_anisotropy >= 16) ratio = 4;
    else if (max_anisotropy >= 8) ratio = 3;
    else if (max_anisotropy >= 4) ratio = 2;
    else if (max_anisotropy >= 2) ratio = 1;
    /* max_anisotropy <= 1 → ratio = 0 (no aniso) */
    desc->words[0] &= ~(0x7u << 9);
    desc->words[0] |= ratio << 9;
}

void agcSamplerDescriptorSetClampMode(AgcSamplerDescriptor *desc,
    AgcClampMode s, AgcClampMode t, AgcClampMode r)
{
    if (!desc) return;
    /* Convert AgcClampMode enum values to hardware GnmTexClamp values.
     * The enum values 0-2 map 1:1, but Border(3)→CLAMP_BORDER(6) and
     * MirrorOnce(4)→MIRROR_ONCE_LAST_TEXEL(3). */
    static const uint32_t hw_clamp[] = {
        0,  /* kAgcClampRepeat     → WRAP                  (0) */
        1,  /* kAgcClampMirror     → MIRROR                (1) */
        2,  /* kAgcClampClamp      → CLAMP_LAST_TEXEL      (2) */
        6,  /* kAgcClampBorder     → CLAMP_BORDER          (6) */
        3,  /* kAgcClampMirrorOnce → MIRROR_ONCE_LAST_TEXEL (3) */
    };
    uint32_t hs = (s < 5) ? hw_clamp[s] : 0;
    uint32_t ht = (t < 5) ? hw_clamp[t] : 0;
    uint32_t hr = (r < 5) ? hw_clamp[r] : 0;
    agcSamplerDescriptorSetWrapModes(desc, hs, ht, hr);
}

void agcSamplerDescriptorSetFilterMode(AgcSamplerDescriptor *desc,
    AgcFilterMode min_filter, AgcFilterMode mag_filter, AgcMipFilterMode mip_filter)
{
    if (!desc) return;
    /* AgcFilterMode values (0-3) map 1:1 to hardware GnmFilter values.
     * AgcMipFilterMode values (0-2) map 1:1 to hardware GnmMipFilter values.
     * Both are 2-bit fields, so we can pass them directly to the raw setter. */
    agcSamplerDescriptorSetFilters(desc,
        (uint32_t)min_filter & 0x3u,
        (uint32_t)mag_filter & 0x3u,
        (uint32_t)mip_filter & 0x3u);
}

void agcSamplerDescriptorSetBorderColor(AgcSamplerDescriptor *desc,
    AgcBorderColor border_color)
{
    if (!desc) return;
    /* BORDER_COLOR_TYPE at word3 [31:30], 2 bits. */
    desc->words[3] &= ~(0x3u << 30);
    desc->words[3] |= (uint32_t)border_color << 30;
}

void agcSamplerDescriptorSetMaxAnisotropy(AgcSamplerDescriptor *desc,
    uint32_t max_aniso)
{
    if (!desc) return;
    /* Thin wrapper over the raw anisotropy setter; accepts the standard
     * power-of-two levels (1, 2, 4, 8, 16) and maps them to log2 ratio. */
    agcSamplerDescriptorSetAnisotropy(desc, max_aniso);
}

int32_t PS5_SYSV_ABI agcGfx1013BufferDescriptorEncode(
    AgcGfx1013BufferDescriptor *descriptor, uint64_t address,
    uint32_t stride, uint32_t element_count)
{
    AgcGfx1013BufferDescriptor encoded = {{0}};

    if (!descriptor || address == 0u || element_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((address >> 48u) != 0u || stride > 0x3fffu)
        return AGC_ERROR_VALIDATION_FAILED;
    encoded.words[0] = (uint32_t)address;
    encoded.words[1] = (uint32_t)(address >> 32u) | (stride << 16u);
    encoded.words[2] = element_count;
    encoded.words[3] = AGC_GFX1013_BUFFER_WORD3_STRUCTURED;
    *descriptor = encoded;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013Image2DDescriptorEncode(
    AgcGfx1013ImageDescriptor *descriptor,
    const AgcGfx1013Image2DState *state)
{
    AgcGfx1013ImageDescriptor encoded = {{0}};
    uint32_t width_minus_one;
    uint32_t sample_count;
    uint32_t sample_log2;

    if (!descriptor || !state || state->address == 0u ||
        state->width == 0u || state->height == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((state->address & 0xffu) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if ((state->address >> 48u) != 0u || state->width > 16384u ||
        state->height > 16384u || state->format > 0x3fu ||
        state->image_type > 0xfu || state->dst_sel_x > 7u ||
        state->dst_sel_y > 7u || state->dst_sel_z > 7u ||
        state->dst_sel_w > 7u)
        return AGC_ERROR_VALIDATION_FAILED;

    sample_count = state->sample_count == 0u ? 1u : state->sample_count;
    if (sample_count == 1u) {
        sample_log2 = 0u;
        if (state->image_type != AGC_GFX1013_IMAGE_TYPE_2D ||
            state->swizzle_mode != 0u)
            return AGC_ERROR_NOT_SUPPORTED;
    } else if (sample_count == 4u) {
        sample_log2 = 2u;
        if (state->image_type != AGC_GFX1013_IMAGE_TYPE_2D_MSAA ||
            state->swizzle_mode != AGC_GFX1013_IMAGE_SWIZZLE_64KB_R_X)
            return AGC_ERROR_NOT_SUPPORTED;
        if ((state->address &
             (AGC_GFX1013_IMAGE_64KB_ALIGNMENT - 1u)) != 0u)
            return AGC_ERROR_INVALID_ALIGNMENT;
    } else {
        return AGC_ERROR_NOT_SUPPORTED;
    }

    width_minus_one = state->width - 1u;
    encoded.words[0] = (uint32_t)(state->address >> 8u);
    encoded.words[1] = ((uint32_t)(state->address >> 40u) & 0xffu) |
        (state->format << 20u) | ((width_minus_one & 0x3u) << 30u);
    encoded.words[2] = ((width_minus_one >> 2u) & 0xfffu) |
        ((state->height - 1u) << 14u) | (1u << 31u);
    encoded.words[3] = state->dst_sel_x | (state->dst_sel_y << 3u) |
        (state->dst_sel_z << 6u) | (state->dst_sel_w << 9u) |
        (sample_log2 << 16u) | (state->swizzle_mode << 20u) |
        (state->image_type << 28u);
    encoded.words[5] = sample_log2 << 4u;
    *descriptor = encoded;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGfx1013CombinedImageSamplerDescriptorEncode(
    AgcGfx1013CombinedImageSamplerDescriptor *descriptor,
    const AgcGfx1013Image2DState *image,
    const AgcSamplerDescriptor *sampler)
{
    AgcGfx1013CombinedImageSamplerDescriptor encoded;
    int32_t result;

    if (!descriptor || !image || !sampler)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset(&encoded, 0, sizeof(encoded));
    result = agcGfx1013Image2DDescriptorEncode(&encoded.image, image);
    if (result != AGC_OK)
        return result;
    encoded.sampler = *sampler;
    *descriptor = encoded;
    return AGC_OK;
}

/* ==================== Render Target helpers ==================== */

void agcRenderTargetInit(AgcRenderTarget *rt) {
    if (!rt) return;
    memset(rt, 0, sizeof(*rt));
    /* Default to 1 sample / 1 fragment (log2 = 0, already zeroed).
     * Default tile mode is display 2D thin (common for color buffers). */
    rt->attrib.tile_mode_index = kAgcTileDisplay_2DThin;
    rt->attrib.fmask_tile_mode = kAgcTileDisplay_2DThin;
}

void agcRenderTargetSetBaseAddress(AgcRenderTarget *rt, uint64_t gpu_addr) {
    if (!rt) return;
    /* CB_COLOR_BASE stores address >> 8 (256-byte granularity). */
    rt->base_base256b = (uint32_t)(gpu_addr >> 8);
}

void agcRenderTargetSetFormat(AgcRenderTarget *rt,
    AgcDataFormat fmt, AgcSurfaceNumber ntype)
{
    if (!rt) return;
    /* CB_COLOR_INFO.format is bits [6:2], number_type is bits [10:8]. */
    rt->info.format = (uint32_t)fmt & 0x1Fu;
    rt->info.number_type = (uint32_t)ntype & 0x7u;
}

void agcRenderTargetSetCompSwap(AgcRenderTarget *rt, AgcSurfaceSwap swap) {
    if (!rt) return;
    rt->info.comp_swap = (uint32_t)swap & 0x3u;
}

void agcRenderTargetSetDimensions(AgcRenderTarget *rt,
    uint32_t width, uint32_t height, uint32_t pitch)
{
    if (!rt) return;
    /* Store width/height in dword 15 (not a GPU register, host-side). */
    rt->size.width  = (uint16_t)(width & 0xFFFFu);
    rt->size.height = (uint16_t)(height & 0xFFFFu);

    /* CB_COLOR_PITCH.tile_max = (pitch / 8) - 1, in 8-pixel tiles. */
    if (pitch >= 8)
        rt->pitch.tile_max = (pitch / 8u) - 1u;
    else
        rt->pitch.tile_max = 0;

    /* CB_COLOR_SLICE.tile_max = (pitch/8 * height/8) - 1, in 64B tiles. */
    uint32_t tiles_x = (pitch >= 8) ? (pitch / 8u) : 1u;
    uint32_t tiles_y = (height >= 8) ? (height / 8u) : 1u;
    uint32_t slice_tiles = tiles_x * tiles_y;
    rt->slice.tile_max = (slice_tiles > 0) ? (slice_tiles - 1u) : 0;
}

void agcRenderTargetSetTileMode(AgcRenderTarget *rt, AgcTileMode mode) {
    if (!rt) return;
    rt->attrib.tile_mode_index = (uint32_t)mode & 0x1Fu;
}

void agcRenderTargetSetFmaskTileMode(AgcRenderTarget *rt, AgcTileMode mode) {
    if (!rt) return;
    rt->attrib.fmask_tile_mode = (uint32_t)mode & 0x1Fu;
}

void agcRenderTargetSetNumSamples(AgcRenderTarget *rt, uint32_t num_samples) {
    if (!rt) return;
    /* Stored as log2; valid values are 1, 2, 4, 8. */
    uint32_t log2 = 0;
    if (num_samples >= 8) log2 = 3;
    else if (num_samples >= 4) log2 = 2;
    else if (num_samples >= 2) log2 = 1;
    rt->attrib.num_samples_log2 = log2;
}

void agcRenderTargetSetNumFragments(AgcRenderTarget *rt,
    uint32_t num_fragments)
{
    if (!rt) return;
    uint32_t log2 = 0;
    if (num_fragments >= 4) log2 = 2;
    else if (num_fragments >= 2) log2 = 1;
    rt->attrib.num_fragments_log2 = log2;
}

void agcRenderTargetSetCmaskBaseAddress(AgcRenderTarget *rt,
    uint64_t gpu_addr)
{
    if (!rt) return;
    rt->cmask_base256b = (uint32_t)(gpu_addr >> 8);
}

void agcRenderTargetSetFmaskBaseAddress(AgcRenderTarget *rt,
    uint64_t gpu_addr)
{
    if (!rt) return;
    rt->fmask_base256b = (uint32_t)(gpu_addr >> 8);
}

void agcRenderTargetSetDccBaseAddress(AgcRenderTarget *rt,
    uint64_t gpu_addr)
{
    if (!rt) return;
    rt->dcc_base256b = (uint32_t)(gpu_addr >> 8);
}

void agcRenderTargetSetDccEnable(AgcRenderTarget *rt, uint32_t enable) {
    if (!rt) return;
    rt->info.dcc_enable = enable ? 1u : 0u;
}

void agcRenderTargetSetClearWords(AgcRenderTarget *rt,
    uint32_t word0, uint32_t word1)
{
    if (!rt) return;
    rt->clear_word0 = word0;
    rt->clear_word1 = word1;
}
