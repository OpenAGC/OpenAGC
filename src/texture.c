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
    /* In RDNA2, format is set via the unified format field.
     * The exact mapping depends on the hw tables.
     * TODO: implement full format table. */
    (void)fmt;
    (void)ntype;
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

void agcSamplerDescriptorInit(AgcSamplerDescriptor *desc) {
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    /* Word 0: clamp modes + filter modes
     * bits [3:0]   clamp_x (wrap_s)
     * bits [7:4]   clamp_y (wrap_t)
     * bits [11:10] clamp_z (wrap_r)
     * bits [15:12] filter_min (min_filter + mip_filter combined)
     * bits [19:16] filter_mag (mag_filter)
     */
    desc->words[0] = 0;
    /* Word 1: LOD bias + min/max LOD
     * bits [15:0]  min_lod (fixed point 4.8)
     * bits [31:16] max_lod (fixed point 4.8)
     */
    desc->words[1] = 0;
    /* Word 2: anisotropy + compare function
     * bits [2:0]   compare_func
     * bits [6:4]   anisotropic_filter_enable + max_aniso
     */
    desc->words[2] = 0;
    /* Word 3: reserved */
    desc->words[3] = 0;
}

void agcSamplerDescriptorSetFilters(AgcSamplerDescriptor *desc,
    uint32_t min_filter, uint32_t mag_filter, uint32_t mip_filter)
{
    if (!desc) return;
    /* Clear filter fields */
    desc->words[0] &= ~0x000FF000u;
    /* Min filter: bit 12 = min_filter, bit 13 = mip_filter */
    desc->words[0] |= (min_filter & 1u) << 12;
    desc->words[0] |= (mip_filter & 1u) << 13;
    /* Mag filter: bit 16 */
    desc->words[0] |= (mag_filter & 1u) << 16;
}

void agcSamplerDescriptorSetWrapModes(AgcSamplerDescriptor *desc,
    uint32_t wrap_s, uint32_t wrap_t, uint32_t wrap_r)
{
    if (!desc) return;
    desc->words[0] &= ~0x00000FFFu;
    desc->words[0] |= (wrap_s & 0xFu);
    desc->words[0] |= (wrap_t & 0xFu) << 4;
    desc->words[0] |= (wrap_r & 0x3u) << 8;
}

void agcSamplerDescriptorSetLod(AgcSamplerDescriptor *desc,
    float min_lod, float max_lod, float lod_bias)
{
    if (!desc) return;
    /* LOD values in 4.8 fixed point */
    uint32_t min_lod_fp = (uint32_t)(min_lod * 256.0f) & 0xFFFFu;
    uint32_t max_lod_fp = (uint32_t)(max_lod * 256.0f) & 0xFFFFu;
    desc->words[1] = min_lod_fp | (max_lod_fp << 16);
    /* LOD bias in word 2 bits [31:16], 4.8 fixed point */
    int32_t bias_fp = (int32_t)(lod_bias * 256.0f) & 0xFFFFu;
    desc->words[2] &= ~0xFFFF0000u;
    desc->words[2] |= (uint32_t)bias_fp << 16;
}

void agcSamplerDescriptorSetCompareFunc(AgcSamplerDescriptor *desc,
    uint32_t compare_func)
{
    if (!desc) return;
    desc->words[2] &= ~0x7u;
    desc->words[2] |= (compare_func & 0x7u);
}

void agcSamplerDescriptorSetAnisotropy(AgcSamplerDescriptor *desc,
    uint32_t max_anisotropy)
{
    if (!desc) return;
    if (max_anisotropy > 0) {
        /* Enable anisotropic filtering + set max aniso level */
        desc->words[2] |= (1u << 4); /* aniso enable */
        uint32_t aniso_level = 0;
        if (max_anisotropy >= 16) aniso_level = 7;
        else if (max_anisotropy >= 8) aniso_level = 6;
        else if (max_anisotropy >= 4) aniso_level = 5;
        else if (max_anisotropy >= 2) aniso_level = 4;
        else aniso_level = 0;
        desc->words[2] &= ~(0x7u << 5);
        desc->words[2] |= (aniso_level << 5);
    }
}
