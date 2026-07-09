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
