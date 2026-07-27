#include "test.h"
#include "agc_error.h"
#include "agc_texture.h"

#include <stddef.h>  /* offsetof */

static void test_texture_descriptor_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcTextureDescriptor), 32, "Texture descriptor = 32 bytes");
}

static void test_buffer_descriptor_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcBufferDescriptor), 16, "Buffer descriptor = 16 bytes");
}

static void test_sampler_descriptor_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcSamplerDescriptor), 16, "Sampler descriptor = 16 bytes");
}

static void test_texture_init(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    TEST_ASSERT_EQ(desc.dst_sel_x, 4, "Default X sel = SQ_SEL_X");
    TEST_ASSERT_EQ(desc.img_type, 1, "Default type = 2D");
}

static void test_texture_dimensions(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetDimensions(&desc, 1920, 1080, 1);
    TEST_ASSERT_EQ(desc.width_minus1, 1919, "width-1 = 1919");
    TEST_ASSERT_EQ(desc.height_minus1, 1079, "height-1 = 1079");
}

static void test_texture_format(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetFormat(&desc, kAgcDataFormat8_8_8_8, kAgcNumberSrgb);
    /* format = data_format(6 bits) | (number_type(4 bits) << 6) */
    TEST_ASSERT_EQ(desc.format & 0x3Fu, (uint32_t)kAgcDataFormat8_8_8_8,
        "Format data = 8_8_8_8");
    TEST_ASSERT_EQ((desc.format >> 6) & 0xFu, (uint32_t)kAgcNumberSrgb,
        "Format num type = Srgb");
}

static void test_buffer_init(void) {
    AgcBufferDescriptor desc;
    agcBufferDescriptorInit(&desc);
    TEST_ASSERT_EQ(desc.stride, 0, "Default stride = 0");
    agcBufferDescriptorSetStride(&desc, 16);
    TEST_ASSERT_EQ(desc.stride, 16, "Stride set to 16");
}

static void test_sampler_init(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    TEST_ASSERT_EQ(desc.words[0], 0u, "Sampler word0 init = 0");
    TEST_ASSERT_EQ(desc.words[1], 0u, "Sampler word1 init = 0");
    TEST_ASSERT_EQ(desc.words[2], 0u, "Sampler word2 init = 0");
    TEST_ASSERT_EQ(desc.words[3], 0u, "Sampler word3 init = 0");
}

static void test_sampler_filters(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetFilters(&desc, 1, 1, 1);
    /* XY_MIN_FILTER at word2 [23:22], XY_MAG_FILTER at [21:20],
     * MIP_FILTER at [27:26]. Each 2 bits. */
    TEST_ASSERT_EQ((desc.words[2] >> 22) & 0x3u, 1u, "Min filter = bilinear");
    TEST_ASSERT_EQ((desc.words[2] >> 20) & 0x3u, 1u, "Mag filter = bilinear");
    TEST_ASSERT_EQ((desc.words[2] >> 26) & 0x3u, 1u, "Mip filter = point");
}

static void test_sampler_filters_nearest(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetFilters(&desc, 0, 0, 0);
    TEST_ASSERT_EQ((desc.words[2] >> 22) & 0x3u, 0u, "Min filter = point");
    TEST_ASSERT_EQ((desc.words[2] >> 20) & 0x3u, 0u, "Mag filter = point");
}

static void test_sampler_wrap_modes(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    /* Hardware GnmTexClamp values: 0=WRAP, 1=MIRROR, 2=CLAMP_LAST_TEXEL */
    agcSamplerDescriptorSetWrapModes(&desc, 0, 1, 2);
    /* CLAMP_X at [2:0], CLAMP_Y at [5:3], CLAMP_Z at [8:6] — 3 bits each */
    TEST_ASSERT_EQ(desc.words[0] & 0x7u, 0u, "Wrap S = repeat (HW 0)");
    TEST_ASSERT_EQ((desc.words[0] >> 3) & 0x7u, 1u, "Wrap T = mirror (HW 1)");
    TEST_ASSERT_EQ((desc.words[0] >> 6) & 0x7u, 2u, "Wrap R = clamp (HW 2)");
}

static void test_sampler_lod(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetLod(&desc, 0.0f, 15.0f, 0.0f);
    /* MIN_LOD at word1 [11:0], MAX_LOD at word1 [23:12] — 12 bits each, 4.8 fp.
     * 15.0 * 256 = 3840 = 0xF00, fits in 12 bits. */
    TEST_ASSERT_EQ(desc.words[1] & 0xFFFu, 0u, "Min LOD = 0");
    TEST_ASSERT_EQ((desc.words[1] >> 12) & 0xFFFu, 3840u, "Max LOD = 15.0");
}

static void test_sampler_lod_bias(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetLod(&desc, 0.0f, 8.0f, -1.0f);
    /* LOD_BIAS at word2 [13:0], 14-bit 4.8 fixed point signed.
     * -1.0 * 256 = -256 = 0x3F00 in 14-bit two's complement. */
    uint32_t bias = desc.words[2] & 0x3FFFu;
    TEST_ASSERT_EQ(bias, 0x3F00u, "LOD bias = -1.0 (14-bit signed)");
}

static void test_sampler_compare_func(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetCompareFunc(&desc, 1); /* LESS */
    /* DEPTH_COMPARE_FUNC at word0 [14:12], 3 bits */
    TEST_ASSERT_EQ((desc.words[0] >> 12) & 0x7u, 1u, "Compare func = LESS");
    agcSamplerDescriptorSetCompareFunc(&desc, 7); /* ALWAYS */
    TEST_ASSERT_EQ((desc.words[0] >> 12) & 0x7u, 7u, "Compare func = ALWAYS");
}

static void test_sampler_anisotropy(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetAnisotropy(&desc, 8);
    /* MAX_ANISO_RATIO at word0 [11:9], 3 bits. 8x → log2(8) = 3. */
    TEST_ASSERT_EQ((desc.words[0] >> 9) & 0x7u, 3u, "Aniso ratio = 3 (8x)");
}

static void test_sampler_anisotropy_disabled(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetAnisotropy(&desc, 0);
    /* 0x → ratio = 0 (no aniso) */
    TEST_ASSERT_EQ((desc.words[0] >> 9) & 0x7u, 0u, "Aniso ratio = 0 (disabled)");
}

static void test_sampler_combined(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetFilters(&desc, 1, 1, 1);
    agcSamplerDescriptorSetWrapModes(&desc, 3, 3, 0);
    agcSamplerDescriptorSetLod(&desc, 0.0f, 12.0f, 0.5f);
    agcSamplerDescriptorSetCompareFunc(&desc, 3); /* LEQUAL */
    agcSamplerDescriptorSetAnisotropy(&desc, 4);

    /* Filters in word 2 */
    TEST_ASSERT_EQ((desc.words[2] >> 22) & 0x3u, 1u, "Combined: min filter = bilinear");
    TEST_ASSERT_EQ((desc.words[2] >> 20) & 0x3u, 1u, "Combined: mag filter = bilinear");
    /* Wrap modes in word 0 (3-bit each) */
    TEST_ASSERT_EQ(desc.words[0] & 0x7u, 3u, "Combined: wrap S = mirror_once (HW 3)");
    /* LOD in word 1 (12-bit each) */
    TEST_ASSERT_EQ((desc.words[1] >> 12) & 0xFFFu, 3072u, "Combined: max LOD = 12.0");
    /* Compare func in word 0 [14:12] */
    TEST_ASSERT_EQ((desc.words[0] >> 12) & 0x7u, 3u, "Combined: compare = LEQUAL");
    /* Aniso ratio in word 0 [11:9]: 4x → log2(4) = 2 */
    TEST_ASSERT_EQ((desc.words[0] >> 9) & 0x7u, 2u, "Combined: aniso ratio = 2 (4x)");
}

static void test_tile_mode_enum_values(void) {
    /* Verify key tile mode values match shadPS4/RPCSX */
    TEST_ASSERT_EQ((uint32_t)kAgcTileDepth_2DThin_64, 0u, "Depth 2DThin 64 = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcTileDisplay_LinearAligned, 8u, "Display LinearAligned = 8");
    TEST_ASSERT_EQ((uint32_t)kAgcTileDisplay_2DThin, 10u, "Display 2DThin = 10");
    TEST_ASSERT_EQ((uint32_t)kAgcTileThin_2DThin, 14u, "Thin 2DThin = 14");
    TEST_ASSERT_EQ((uint32_t)kAgcTileThick_2DThick, 20u, "Thick 2DThick = 20");
    TEST_ASSERT_EQ((uint32_t)kAgcTileDisplay_LinearGeneral, 31u, "Display LinearGeneral = 31");
}

static void test_image_type_enum_values(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcImgType_1D, 0u, "1D = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcImgType_2D, 1u, "2D = 1");
    TEST_ASSERT_EQ((uint32_t)kAgcImgType_3D, 2u, "3D = 2");
    TEST_ASSERT_EQ((uint32_t)kAgcImgType_Cube, 3u, "Cube = 3");
    TEST_ASSERT_EQ((uint32_t)kAgcImgType_2DArray, 5u, "2DArray = 5");
    TEST_ASSERT_EQ((uint32_t)kAgcImgType_2DMSAA, 6u, "2DMSAA = 6");
}

static void test_extended_data_formats(void) {
    /* Verify extended format enum values */
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormatBc1, 10u, "BC1 = 10");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormatBc2, 13u, "BC2 = 13");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormatBc4, 14u, "BC4 = 14");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormatBc5, 15u, "BC5 = 15");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormatBc6, 16u, "BC6 = 16");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormat8_24, 17u, "8_24 = 17");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormat24_8, 18u, "24_8 = 18");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormatFmask8_S, 20u, "Fmask8_S = 20");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormatGbGr, 24u, "GbGr = 24");
    TEST_ASSERT_EQ((uint32_t)kAgcDataFormat5_9_9_9, 26u, "5_9_9_9 = 26");
}

static void test_clamp_mode_enum(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcClampRepeat, 0u, "Repeat = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcClampMirror, 1u, "Mirror = 1");
    TEST_ASSERT_EQ((uint32_t)kAgcClampClamp, 2u, "Clamp = 2");
    TEST_ASSERT_EQ((uint32_t)kAgcClampBorder, 3u, "Border = 3");
    TEST_ASSERT_EQ((uint32_t)kAgcClampMirrorOnce, 4u, "MirrorOnce = 4");
}

static void test_filter_mode_enum(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcFilterPoint, 0u, "Point = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcFilterBilinear, 1u, "Bilinear = 1");
    TEST_ASSERT_EQ((uint32_t)kAgcFilterAnisoPoint, 2u, "AnisoPoint = 2");
    TEST_ASSERT_EQ((uint32_t)kAgcFilterAnisoLinear, 3u, "AnisoLinear = 3");
}

static void test_mip_filter_enum(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcMipFilterNone, 0u, "MipNone = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcMipFilterPoint, 1u, "MipPoint = 1");
    TEST_ASSERT_EQ((uint32_t)kAgcMipFilterLinear, 2u, "MipLinear = 2");
}

static void test_border_color_enum(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcBorderTransparentBlack, 0u, "TransparentBlack = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcBorderOpaqueBlack, 1u, "OpaqueBlack = 1");
    TEST_ASSERT_EQ((uint32_t)kAgcBorderWhite, 2u, "White = 2");
    TEST_ASSERT_EQ((uint32_t)kAgcBorderCustom, 3u, "Custom = 3");
}

static void test_extended_number_types(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcNumberUnorm, 0u, "Unorm = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcNumberFloat, 4u, "Float = 4");
    TEST_ASSERT_EQ((uint32_t)kAgcNumberSrgb, 5u, "Srgb = 5");
}

/* ==================== Render Target tests ==================== */

static void test_render_target_size(void) {
    /* Must be exactly 64 bytes (16 dwords) to match CB_COLOR register layout. */
    TEST_ASSERT_EQ(sizeof(AgcRenderTarget), 64u, "Render target = 64 bytes");
}

static void test_render_target_offsets(void) {
    /* Verify key dword offsets match the GPU register mapping. */
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, base_base256b), 0u,
        "base at offset 0 (dword 0)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, pitch), 4u,
        "pitch at offset 4 (dword 1)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, slice), 8u,
        "slice at offset 8 (dword 2)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, view), 12u,
        "view at offset 12 (dword 3)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, info), 16u,
        "info at offset 16 (dword 4)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, attrib), 20u,
        "attrib at offset 20 (dword 5)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, dcc_control), 24u,
        "dcc_control at offset 24 (dword 6)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, cmask_base256b), 28u,
        "cmask_base at offset 28 (dword 7)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, cmask_slice), 32u,
        "cmask_slice at offset 32 (dword 8)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, fmask_base256b), 36u,
        "fmask_base at offset 36 (dword 9)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, fmask_slice), 40u,
        "fmask_slice at offset 40 (dword 10)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, clear_word0), 44u,
        "clear_word0 at offset 44 (dword 11)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, clear_word1), 48u,
        "clear_word1 at offset 48 (dword 12)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, dcc_base256b), 52u,
        "dcc_base at offset 52 (dword 13)");
    TEST_ASSERT_EQ(offsetof(AgcRenderTarget, size), 60u,
        "size at offset 60 (dword 15)");
}

static void test_render_target_init_defaults(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    /* Base addresses should be zeroed. */
    TEST_ASSERT_EQ(rt.base_base256b, 0u, "Init: base = 0");
    TEST_ASSERT_EQ(rt.cmask_base256b, 0u, "Init: cmask base = 0");
    TEST_ASSERT_EQ(rt.fmask_base256b, 0u, "Init: fmask base = 0");
    TEST_ASSERT_EQ(rt.dcc_base256b, 0u, "Init: dcc base = 0");
    TEST_ASSERT_EQ(rt.clear_word0, 0u, "Init: clear_word0 = 0");
    TEST_ASSERT_EQ(rt.clear_word1, 0u, "Init: clear_word1 = 0");
    /* Default tile mode = display 2D thin. */
    TEST_ASSERT_EQ((uint32_t)rt.attrib.tile_mode_index,
        (uint32_t)kAgcTileDisplay_2DThin, "Init: tile mode = display 2D thin");
    TEST_ASSERT_EQ((uint32_t)rt.attrib.fmask_tile_mode,
        (uint32_t)kAgcTileDisplay_2DThin, "Init: fmask tile mode = display 2D thin");
    /* Default 1 sample / 1 fragment (log2 = 0). */
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_samples_log2, 0u,
        "Init: num_samples_log2 = 0 (1 sample)");
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_fragments_log2, 0u,
        "Init: num_fragments_log2 = 0 (1 fragment)");
    /* DCC disabled by default. */
    TEST_ASSERT_EQ((uint32_t)rt.info.dcc_enable, 0u, "Init: dcc disabled");
}

static void test_render_target_base_address(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    /* 256-byte aligned address: 0x1000 >> 8 = 0x10 */
    agcRenderTargetSetBaseAddress(&rt, 0x1000);
    TEST_ASSERT_EQ(rt.base_base256b, 0x10u, "Base addr 0x1000 >> 8 = 0x10");
    /* Larger address */
    agcRenderTargetSetBaseAddress(&rt, 0x100000);
    TEST_ASSERT_EQ(rt.base_base256b, 0x1000u, "Base addr 0x100000 >> 8 = 0x1000");
}

static void test_render_target_format(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetFormat(&rt, kAgcDataFormat8_8_8_8, kAgcSurfNumSnormNz);
    TEST_ASSERT_EQ((uint32_t)rt.info.format, (uint32_t)kAgcDataFormat8_8_8_8,
        "Format = 8_8_8_8");
    TEST_ASSERT_EQ((uint32_t)rt.info.number_type, (uint32_t)kAgcSurfNumSnormNz,
        "Number type = SnormNz (CB encoding for SRGB)");
    /* Verify format is in bits [6:2] of info_u32 */
    TEST_ASSERT_EQ((rt.info.info_u32 >> 2) & 0x1Fu,
        (uint32_t)kAgcDataFormat8_8_8_8, "Format in info bits [6:2]");
}

static void test_render_target_comp_swap(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetCompSwap(&rt, kAgcSurfSwapAlternate);
    TEST_ASSERT_EQ((uint32_t)rt.info.comp_swap,
        (uint32_t)kAgcSurfSwapAlternate, "Comp swap = alternate (BGRA)");
    /* comp_swap is bits [12:11] */
    TEST_ASSERT_EQ((rt.info.info_u32 >> 11) & 0x3u, 1u,
        "Comp swap in info bits [12:11]");
}

static void test_render_target_dimensions(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    /* 1920x1080, pitch=1920 (multiple of 8) */
    agcRenderTargetSetDimensions(&rt, 1920, 1080, 1920);
    TEST_ASSERT_EQ((uint32_t)rt.size.width, 1920u, "Width = 1920");
    TEST_ASSERT_EQ((uint32_t)rt.size.height, 1080u, "Height = 1080");
    /* pitch tile_max = (1920/8) - 1 = 239 */
    TEST_ASSERT_EQ((uint32_t)rt.pitch.tile_max, 239u, "Pitch tile_max = 239");
    /* slice tile_max = (240 * 135) - 1 = 32399 */
    TEST_ASSERT_EQ((uint32_t)rt.slice.tile_max, 32399u, "Slice tile_max = 32399");
}

static void test_render_target_tile_mode(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetTileMode(&rt, kAgcTileDisplay_LinearAligned);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.tile_mode_index,
        (uint32_t)kAgcTileDisplay_LinearAligned, "Tile mode = linear aligned");
    /* tile_mode_index is bits [4:0] of attrib */
    TEST_ASSERT_EQ(rt.attrib.attrib_u32 & 0x1Fu,
        (uint32_t)kAgcTileDisplay_LinearAligned, "Tile mode in attrib bits [4:0]");
}

static void test_render_target_fmask_tile_mode(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetFmaskTileMode(&rt, kAgcTileDisplay_2DThin_Standard);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.fmask_tile_mode,
        (uint32_t)kAgcTileDisplay_2DThin_Standard,
        "Fmask tile mode = display 2D thin standard");
    /* fmask_tile_mode is bits [9:5] of attrib */
    TEST_ASSERT_EQ((rt.attrib.attrib_u32 >> 5) & 0x1Fu,
        (uint32_t)kAgcTileDisplay_2DThin_Standard,
        "Fmask tile mode in attrib bits [9:5]");
}

static void test_render_target_num_samples(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    /* 1 sample = log2 0 */
    agcRenderTargetSetNumSamples(&rt, 1);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_samples_log2, 0u, "1 sample = log2 0");
    /* 2 samples = log2 1 */
    agcRenderTargetSetNumSamples(&rt, 2);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_samples_log2, 1u, "2 samples = log2 1");
    /* 4 samples = log2 2 */
    agcRenderTargetSetNumSamples(&rt, 4);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_samples_log2, 2u, "4 samples = log2 2");
    /* 8 samples = log2 3 */
    agcRenderTargetSetNumSamples(&rt, 8);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_samples_log2, 3u, "8 samples = log2 3");
}

static void test_render_target_num_fragments(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetNumFragments(&rt, 4);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_fragments_log2, 2u,
        "4 fragments = log2 2");
    agcRenderTargetSetNumFragments(&rt, 2);
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_fragments_log2, 1u,
        "2 fragments = log2 1");
}

static void test_render_target_cmask_fmask_dcc(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    /* CMASK base */
    agcRenderTargetSetCmaskBaseAddress(&rt, 0x2000);
    TEST_ASSERT_EQ(rt.cmask_base256b, 0x20u, "CMASK base 0x2000 >> 8 = 0x20");
    /* FMASK base */
    agcRenderTargetSetFmaskBaseAddress(&rt, 0x3000);
    TEST_ASSERT_EQ(rt.fmask_base256b, 0x30u, "FMASK base 0x3000 >> 8 = 0x30");
    /* DCC base */
    agcRenderTargetSetDccBaseAddress(&rt, 0x4000);
    TEST_ASSERT_EQ(rt.dcc_base256b, 0x40u, "DCC base 0x4000 >> 8 = 0x40");
}

static void test_render_target_dcc_enable(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetDccEnable(&rt, 1);
    TEST_ASSERT_EQ((uint32_t)rt.info.dcc_enable, 1u, "DCC enabled");
    /* dcc_enable is bit 28 of info */
    TEST_ASSERT_EQ((rt.info.info_u32 >> 28) & 1u, 1u, "DCC enable in info bit 28");
    agcRenderTargetSetDccEnable(&rt, 0);
    TEST_ASSERT_EQ((uint32_t)rt.info.dcc_enable, 0u, "DCC disabled");
}

static void test_render_target_clear_words(void) {
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetClearWords(&rt, 0xDEADBEEF, 0xCAFEBABE);
    TEST_ASSERT_EQ(rt.clear_word0, 0xDEADBEEFu, "Clear word 0 set");
    TEST_ASSERT_EQ(rt.clear_word1, 0xCAFEBABEu, "Clear word 1 set");
}

static void test_render_target_combined(void) {
    /* Configure a full 1080p SRGB render target and verify all fields. */
    AgcRenderTarget rt;
    agcRenderTargetInit(&rt);
    agcRenderTargetSetBaseAddress(&rt, 0x100000);
    agcRenderTargetSetFormat(&rt, kAgcDataFormat8_8_8_8, kAgcSurfNumSnormNz);
    agcRenderTargetSetCompSwap(&rt, kAgcSurfSwapAlternate);
    agcRenderTargetSetDimensions(&rt, 1920, 1080, 1920);
    agcRenderTargetSetTileMode(&rt, kAgcTileDisplay_LinearAligned);
    agcRenderTargetSetNumSamples(&rt, 4);
    agcRenderTargetSetDccEnable(&rt, 1);
    agcRenderTargetSetDccBaseAddress(&rt, 0x200000);
    agcRenderTargetSetClearWords(&rt, 0x00000000, 0xFFFFFFFF);

    TEST_ASSERT_EQ(rt.base_base256b, 0x1000u, "Combined: base = 0x1000");
    TEST_ASSERT_EQ((uint32_t)rt.info.format, (uint32_t)kAgcDataFormat8_8_8_8,
        "Combined: format = 8_8_8_8");
    TEST_ASSERT_EQ((uint32_t)rt.info.number_type, (uint32_t)kAgcSurfNumSnormNz,
        "Combined: number type = SnormNz (SRGB)");
    TEST_ASSERT_EQ((uint32_t)rt.info.comp_swap, 1u, "Combined: comp swap = alternate");
    TEST_ASSERT_EQ((uint32_t)rt.size.width, 1920u, "Combined: width = 1920");
    TEST_ASSERT_EQ((uint32_t)rt.size.height, 1080u, "Combined: height = 1080");
    TEST_ASSERT_EQ((uint32_t)rt.pitch.tile_max, 239u, "Combined: pitch tile_max = 239");
    TEST_ASSERT_EQ((uint32_t)rt.attrib.tile_mode_index,
        (uint32_t)kAgcTileDisplay_LinearAligned, "Combined: tile mode = linear aligned");
    TEST_ASSERT_EQ((uint32_t)rt.attrib.num_samples_log2, 2u,
        "Combined: 4 samples = log2 2");
    TEST_ASSERT_EQ((uint32_t)rt.info.dcc_enable, 1u, "Combined: DCC enabled");
    TEST_ASSERT_EQ(rt.dcc_base256b, 0x2000u, "Combined: DCC base = 0x2000");
    TEST_ASSERT_EQ(rt.clear_word0, 0u, "Combined: clear word 0 = 0");
    TEST_ASSERT_EQ(rt.clear_word1, 0xFFFFFFFFu, "Combined: clear word 1 = 0xFFFFFFFF");
}

static void test_surface_number_enum(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcSurfNumUnorm, 0u, "SurfNum Unorm = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcSurfNumSnorm, 1u, "SurfNum Snorm = 1");
    TEST_ASSERT_EQ((uint32_t)kAgcSurfNumSnormNz, 6u, "SurfNum SnormNz = 6 (SRGB)");
    TEST_ASSERT_EQ((uint32_t)kAgcSurfNumFloat, 7u, "SurfNum Float = 7");
}

static void test_surface_swap_enum(void) {
    TEST_ASSERT_EQ((uint32_t)kAgcSurfSwapStandard, 0u, "SurfSwap Standard = 0");
    TEST_ASSERT_EQ((uint32_t)kAgcSurfSwapAlternate, 1u, "SurfSwap Alternate = 1");
    TEST_ASSERT_EQ((uint32_t)kAgcSurfSwapStandardReverse, 2u,
        "SurfSwap StandardReverse = 2");
    TEST_ASSERT_EQ((uint32_t)kAgcSurfSwapAlternateReverse, 3u,
        "SurfSwap AlternateReverse = 3");
}

/* ==================== Typed descriptor helper tests ==================== */

static void test_texture_set_image_type(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetImageType(&desc, kAgcImgType_3D);
    TEST_ASSERT_EQ(desc.img_type, 2u, "SetImageType 3D = 2");
    agcTextureDescriptorSetImageType(&desc, kAgcImgType_Cube);
    TEST_ASSERT_EQ(desc.img_type, 3u, "SetImageType Cube = 3");
    agcTextureDescriptorSetImageType(&desc, kAgcImgType_2DArray);
    TEST_ASSERT_EQ(desc.img_type, 5u, "SetImageType 2DArray = 5");
}

static void test_texture_set_tile_mode(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    /* sw_mode is now 5 bits and must hold the full AgcTileMode range. */
    agcTextureDescriptorSetTileMode(&desc, kAgcTileDisplay_LinearAligned);
    TEST_ASSERT_EQ(desc.sw_mode, 8u, "SetTileMode LinearAligned = 8");
    agcTextureDescriptorSetTileMode(&desc, kAgcTileDisplay_LinearGeneral);
    TEST_ASSERT_EQ(desc.sw_mode, 31u, "SetTileMode LinearGeneral = 31 (5-bit max)");
}

static void test_texture_set_tile_mode_matches_swizzle(void) {
    /* SetTileMode and SetSwizzleMode share the sw_mode field and must produce
     * identical bit patterns for equivalent numeric values. */
    AgcTextureDescriptor a, b;
    agcTextureDescriptorInit(&a);
    agcTextureDescriptorInit(&b);
    agcTextureDescriptorSetTileMode(&a, (AgcTileMode)kAgcSwMode4KB_S);
    agcTextureDescriptorSetSwizzleMode(&b, kAgcSwMode4KB_S);
    TEST_ASSERT_EQ(a.sw_mode, b.sw_mode, "SetTileMode == SetSwizzleMode bit pattern");
}

static void test_texture_set_mip_levels(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetMipLevels(&desc, 10);
    TEST_ASSERT_EQ(desc.mip_levels, 10u, "SetMipLevels 10");
    /* Edge: zero mip levels. */
    agcTextureDescriptorSetMipLevels(&desc, 0);
    TEST_ASSERT_EQ(desc.mip_levels, 0u, "SetMipLevels 0");
    /* Edge: clamps to 31 (5-bit max). */
    agcTextureDescriptorSetMipLevels(&desc, 64);
    TEST_ASSERT_EQ(desc.mip_levels, 31u, "SetMipLevels clamps to 31");
}

static void test_texture_set_array_size(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetArraySize(&desc, 0, 5);
    TEST_ASSERT_EQ(desc.base_array, 0u, "SetArraySize base = 0");
    TEST_ASSERT_EQ(desc.last_array, 5u, "SetArraySize last = 5");
    /* Edge: clamp to 7 (3-bit max). */
    agcTextureDescriptorSetArraySize(&desc, 16, 32);
    TEST_ASSERT_EQ(desc.base_array, 7u, "SetArraySize base clamps to 7");
    TEST_ASSERT_EQ(desc.last_array, 7u, "SetArraySize last clamps to 7");
}

static void test_texture_set_depth(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetDepth(&desc, 16);
    TEST_ASSERT_EQ(desc.depth_minus1, 15u, "SetDepth 16 -> depth-1 = 15");
    /* Edge: zero depth stores 0 (no underflow). */
    agcTextureDescriptorSetDepth(&desc, 0);
    TEST_ASSERT_EQ(desc.depth_minus1, 0u, "SetDepth 0 -> depth-1 = 0");
}

static void test_texture_set_pitch(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetPitch(&desc, 256);
    TEST_ASSERT_EQ(desc.pitch_minus1, 255u, "SetPitch 256 -> pitch-1 = 255");
    /* Edge: zero pitch stores 0 (no underflow). */
    agcTextureDescriptorSetPitch(&desc, 0);
    TEST_ASSERT_EQ(desc.pitch_minus1, 0u, "SetPitch 0 -> pitch-1 = 0");
}

static void test_texture_set_dst_sel(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    /* Swap to WZYX (7,6,5,4). */
    agcTextureDescriptorSetDstSel(&desc, 7, 6, 5, 4);
    TEST_ASSERT_EQ(desc.dst_sel_x, 7u, "SetDstSel x = 7");
    TEST_ASSERT_EQ(desc.dst_sel_y, 6u, "SetDstSel y = 6");
    TEST_ASSERT_EQ(desc.dst_sel_z, 5u, "SetDstSel z = 5");
    TEST_ASSERT_EQ(desc.dst_sel_w, 4u, "SetDstSel w = 4");
    /* Edge: values clamp to 3-bit range (0-7); 9 & 7 == 1. */
    agcTextureDescriptorSetDstSel(&desc, 9, 9, 9, 9);
    TEST_ASSERT_EQ(desc.dst_sel_x, 1u, "SetDstSel clamps 9 -> 1 (3-bit)");
}

static void test_texture_getters(void) {
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetBaseAddress(&desc, 0x1000);
    agcTextureDescriptorSetDimensions(&desc, 1920, 1080, 1);
    TEST_ASSERT_EQ(agcTextureDescriptorGetBaseAddress(&desc), 0x1000ull,
        "GetBaseAddress returns base_address");
    TEST_ASSERT_EQ(agcTextureDescriptorGetWidth(&desc), 1920u, "GetWidth = 1920");
    TEST_ASSERT_EQ(agcTextureDescriptorGetHeight(&desc), 1080u, "GetHeight = 1080");
}

static void test_texture_getters_zero_dims(void) {
    /* Edge: zeroed descriptor — width/height getters return 1 (minus1+1). */
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    TEST_ASSERT_EQ(agcTextureDescriptorGetBaseAddress(&desc), 0x0ull,
        "GetBaseAddress default = 0");
    TEST_ASSERT_EQ(agcTextureDescriptorGetWidth(&desc), 1u, "GetWidth default = 1");
    TEST_ASSERT_EQ(agcTextureDescriptorGetHeight(&desc), 1u, "GetHeight default = 1");
}

static void test_sampler_set_clamp_mode(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetClampMode(&desc,
        kAgcClampRepeat, kAgcClampMirror, kAgcClampBorder);
    /* CLAMP_X at [2:0], CLAMP_Y at [5:3], CLAMP_Z at [8:6] — 3 bits each.
     * kAgcClampBorder(3) converts to HW CLAMP_BORDER(6). */
    TEST_ASSERT_EQ(desc.words[0] & 0x7u, 0u, "Typed clamp S = repeat (HW 0)");
    TEST_ASSERT_EQ((desc.words[0] >> 3) & 0x7u, 1u, "Typed clamp T = mirror (HW 1)");
    TEST_ASSERT_EQ((desc.words[0] >> 6) & 0x7u, 6u, "Typed clamp R = border (HW 6)");
}

static void test_sampler_clamp_matches_raw(void) {
    /* Typed SetClampMode converts enum values to hardware GnmTexClamp values,
     * then calls SetWrapModes. Verify the conversion is correct. */
    AgcSamplerDescriptor a, b;
    agcSamplerDescriptorInit(&a);
    agcSamplerDescriptorInit(&b);
    /* Mirror(1)→HW 1, Clamp(2)→HW 2, MirrorOnce(4)→HW 3 */
    agcSamplerDescriptorSetClampMode(&a, kAgcClampMirror, kAgcClampClamp, kAgcClampMirrorOnce);
    agcSamplerDescriptorSetWrapModes(&b, 1, 2, 3);
    TEST_ASSERT_EQ(a.words[0], b.words[0], "SetClampMode converts to HW values");
}

static void test_sampler_set_filter_mode(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetFilterMode(&desc,
        kAgcFilterBilinear, kAgcFilterBilinear, kAgcMipFilterPoint);
    /* XY_MIN_FILTER at word2 [23:22], XY_MAG_FILTER at [21:20],
     * MIP_FILTER at [27:26]. Enum values map 1:1 to HW values. */
    TEST_ASSERT_EQ((desc.words[2] >> 22) & 0x3u, 1u, "Typed min filter = bilinear");
    TEST_ASSERT_EQ((desc.words[2] >> 20) & 0x3u, 1u, "Typed mag filter = bilinear");
    TEST_ASSERT_EQ((desc.words[2] >> 26) & 0x3u, 1u, "Typed mip filter = point");
}

static void test_sampler_filter_matches_raw(void) {
    /* AgcFilterMode and AgcMipFilterMode values map 1:1 to hardware values,
     * so typed and raw setters produce identical bit patterns. */
    AgcSamplerDescriptor a, b;
    agcSamplerDescriptorInit(&a);
    agcSamplerDescriptorInit(&b);
    agcSamplerDescriptorSetFilterMode(&a,
        kAgcFilterPoint, kAgcFilterBilinear, kAgcMipFilterNone);
    agcSamplerDescriptorSetFilters(&b, 0, 1, 0);
    TEST_ASSERT_EQ(a.words[2], b.words[2], "SetFilterMode == SetFilters bit pattern");
}

static void test_sampler_filter_aniso(void) {
    /* AnisoPoint(2) and AnisoLinear(3) must be preserved as 2-bit values. */
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetFilterMode(&desc,
        kAgcFilterAnisoPoint, kAgcFilterAnisoLinear, kAgcMipFilterLinear);
    TEST_ASSERT_EQ((desc.words[2] >> 22) & 0x3u, 2u, "AnisoPoint min filter = 2");
    TEST_ASSERT_EQ((desc.words[2] >> 20) & 0x3u, 3u, "AnisoLinear mag filter = 3");
    TEST_ASSERT_EQ((desc.words[2] >> 26) & 0x3u, 2u, "Linear mip filter = 2");
}

static void test_sampler_set_border_color(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    /* BORDER_COLOR_TYPE at word3 [31:30], 2 bits. */
    agcSamplerDescriptorSetBorderColor(&desc, kAgcBorderOpaqueBlack);
    TEST_ASSERT_EQ((desc.words[3] >> 30) & 0x3u, 1u, "Border color = opaque black");
    agcSamplerDescriptorSetBorderColor(&desc, kAgcBorderWhite);
    TEST_ASSERT_EQ((desc.words[3] >> 30) & 0x3u, 2u, "Border color = white");
    agcSamplerDescriptorSetBorderColor(&desc, kAgcBorderCustom);
    TEST_ASSERT_EQ((desc.words[3] >> 30) & 0x3u, 3u, "Border color = custom (max enum)");
    /* Edge: transparent black (0). */
    agcSamplerDescriptorSetBorderColor(&desc, kAgcBorderTransparentBlack);
    TEST_ASSERT_EQ((desc.words[3] >> 30) & 0x3u, 0u, "Border color = transparent black");
}

static void test_sampler_set_max_anisotropy(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    /* MAX_ANISO_RATIO at word0 [11:9], stored as log2. */
    agcSamplerDescriptorSetMaxAnisotropy(&desc, 16);
    TEST_ASSERT_EQ((desc.words[0] >> 9) & 0x7u, 4u, "MaxAniso 16 = ratio 4 (log2)");
    agcSamplerDescriptorSetMaxAnisotropy(&desc, 1);
    TEST_ASSERT_EQ((desc.words[0] >> 9) & 0x7u, 0u, "MaxAniso 1 = ratio 0 (no aniso)");
}

static void test_sampler_max_aniso_matches_raw(void) {
    /* SetMaxAnisotropy delegates to SetAnisotropy; same bit pattern. */
    AgcSamplerDescriptor a, b;
    agcSamplerDescriptorInit(&a);
    agcSamplerDescriptorInit(&b);
    agcSamplerDescriptorSetMaxAnisotropy(&a, 8);
    agcSamplerDescriptorSetAnisotropy(&b, 8);
    TEST_ASSERT_EQ(a.words[0], b.words[0], "SetMaxAnisotropy == SetAnisotropy");
}

static void test_sampler_typed_combined(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetClampMode(&desc,
        kAgcClampBorder, kAgcClampBorder, kAgcClampRepeat);
    agcSamplerDescriptorSetFilterMode(&desc,
        kAgcFilterBilinear, kAgcFilterBilinear, kAgcMipFilterPoint);
    agcSamplerDescriptorSetBorderColor(&desc, kAgcBorderOpaqueBlack);
    agcSamplerDescriptorSetMaxAnisotropy(&desc, 4);

    /* kAgcClampBorder(3) → HW CLAMP_BORDER(6) */
    TEST_ASSERT_EQ(desc.words[0] & 0x7u, 6u, "Combined typed: wrap S = border (HW 6)");
    TEST_ASSERT_EQ((desc.words[0] >> 3) & 0x7u, 6u, "Combined typed: wrap T = border (HW 6)");
    /* Filters in word 2 */
    TEST_ASSERT_EQ((desc.words[2] >> 22) & 0x3u, 1u, "Combined typed: min = bilinear");
    TEST_ASSERT_EQ((desc.words[2] >> 20) & 0x3u, 1u, "Combined typed: mag = bilinear");
    /* Border color in word 3 [31:30] */
    TEST_ASSERT_EQ((desc.words[3] >> 30) & 0x3u, 1u, "Combined typed: border = opaque black");
    /* Aniso ratio in word 0 [11:9]: 4x → log2(4) = 2 */
    TEST_ASSERT_EQ((desc.words[0] >> 9) & 0x7u, 2u, "Combined typed: aniso ratio = 2 (4x)");
}

/* ==================== Format encode/decode helper tests ==================== */

static void test_texture_format_encode_basic(void) {
    /* data_format (6 bits, [5:0]) | number_type (4 bits, [9:6]). */
    uint32_t fmt = agcTextureFormatEncode(kAgcDataFormat8_8_8_8, kAgcNumberUnorm);
    TEST_ASSERT_EQ(fmt & 0x3Fu, (uint32_t)kAgcDataFormat8_8_8_8,
        "Encode: data format = 8_8_8_8");
    TEST_ASSERT_EQ((fmt >> 6) & 0xFu, (uint32_t)kAgcNumberUnorm,
        "Encode: number type = Unorm");
}

static void test_texture_format_encode_srgb(void) {
    uint32_t fmt = agcTextureFormatEncode(kAgcDataFormatBc7, kAgcNumberSrgb);
    TEST_ASSERT_EQ(fmt & 0x3Fu, (uint32_t)kAgcDataFormatBc7,
        "Encode: data format = BC7");
    TEST_ASSERT_EQ((fmt >> 6) & 0xFu, (uint32_t)kAgcNumberSrgb,
        "Encode: number type = Srgb");
}

static void test_texture_format_encode_float(void) {
    uint32_t fmt = agcTextureFormatEncode(kAgcDataFormat32_32_32_32, kAgcNumberFloat);
    TEST_ASSERT_EQ(fmt & 0x3Fu, (uint32_t)kAgcDataFormat32_32_32_32,
        "Encode: data format = 32_32_32_32");
    TEST_ASSERT_EQ((fmt >> 6) & 0xFu, (uint32_t)kAgcNumberFloat,
        "Encode: number type = Float");
}

static void test_texture_format_encode_matches_setformat(void) {
    /* agcTextureFormatEncode must produce the same value that
     * agcTextureDescriptorSetFormat stores in desc.format. */
    AgcTextureDescriptor desc;
    agcTextureDescriptorInit(&desc);
    agcTextureDescriptorSetFormat(&desc, kAgcDataFormat16_16_16_16, kAgcNumberSnorm);
    uint32_t encoded = agcTextureFormatEncode(
        kAgcDataFormat16_16_16_16, kAgcNumberSnorm);
    TEST_ASSERT_EQ(desc.format, encoded,
        "FormatEncode matches SetFormat bit pattern");
}

static void test_texture_format_get_data_format(void) {
    uint32_t fmt = agcTextureFormatEncode(kAgcDataFormatBc1, kAgcNumberUnorm);
    TEST_ASSERT_EQ((uint32_t)agcTextureFormatGetDataFormat(fmt),
        (uint32_t)kAgcDataFormatBc1, "GetDataFormat = BC1");
}

static void test_texture_format_get_number_type(void) {
    uint32_t fmt = agcTextureFormatEncode(kAgcDataFormat8, kAgcNumberUint);
    TEST_ASSERT_EQ((uint32_t)agcTextureFormatGetNumberType(fmt),
        (uint32_t)kAgcNumberUint, "GetNumberType = Uint");
}

static void test_texture_format_roundtrip(void) {
    /* Encode then decode must return the original values. Test several
     * format + type combinations across the enum range. */
    const AgcDataFormat fmts[] = {
        kAgcDataFormatInvalid,
        kAgcDataFormat8,
        kAgcDataFormat8_8_8_8,
        kAgcDataFormat16_16_16_16,
        kAgcDataFormat32_32_32_32,
        kAgcDataFormatBc1,
        kAgcDataFormatBc7,
        kAgcDataFormat24_8,
        kAgcDataFormatGbGr,
    };
    const AgcNumberType types[] = {
        kAgcNumberUnorm,
        kAgcNumberSnorm,
        kAgcNumberUint,
        kAgcNumberSint,
        kAgcNumberFloat,
        kAgcNumberSrgb,
        kAgcNumberUnormSnorm,
    };
    for (size_t i = 0; i < sizeof(fmts)/sizeof(fmts[0]); i++) {
        for (size_t j = 0; j < sizeof(types)/sizeof(types[0]); j++) {
            uint32_t encoded = agcTextureFormatEncode(fmts[i], types[j]);
            AgcDataFormat df = agcTextureFormatGetDataFormat(encoded);
            AgcNumberType nt = agcTextureFormatGetNumberType(encoded);
            TEST_ASSERT_EQ((uint32_t)df, (uint32_t)fmts[i],
                "Roundtrip: data format preserved");
            TEST_ASSERT_EQ((uint32_t)nt, (uint32_t)types[j],
                "Roundtrip: number type preserved");
        }
    }
}

static void test_texture_format_roundtrip_all_data_formats(void) {
    /* Exhaustive round-trip over all 28 AgcDataFormat values with Unorm. */
    for (uint32_t i = 0; i <= (uint32_t)kAgcDataFormat10_11_11; i++) {
        uint32_t encoded = agcTextureFormatEncode((AgcDataFormat)i, kAgcNumberUnorm);
        AgcDataFormat df = agcTextureFormatGetDataFormat(encoded);
        TEST_ASSERT_EQ((uint32_t)df, i, "Roundtrip all data formats: preserved");
    }
}

static void test_texture_format_roundtrip_all_number_types(void) {
    /* Exhaustive round-trip over all 7 AgcNumberType values with 8_8_8_8. */
    for (uint32_t i = 0; i <= (uint32_t)kAgcNumberUnormSnorm; i++) {
        uint32_t encoded = agcTextureFormatEncode(
            kAgcDataFormat8_8_8_8, (AgcNumberType)i);
        AgcNumberType nt = agcTextureFormatGetNumberType(encoded);
        TEST_ASSERT_EQ((uint32_t)nt, i, "Roundtrip all number types: preserved");
    }
}

static void test_gfx1013_hardware_descriptors(void)
{
    AgcGfx1013BufferDescriptor buffer = {{0}};
    AgcGfx1013ImageDescriptor image = {{0}};
    AgcGfx1013CombinedImageSamplerDescriptor combined;
    AgcSamplerDescriptor sampler;
    const AgcGfx1013Image2DState image_state = {
        .address = 0x0000000202700000ull,
        .width = 2u,
        .height = 2u,
        .format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM,
        .image_type = AGC_GFX1013_IMAGE_TYPE_2D,
        .dst_sel_x = 4u,
        .dst_sel_y = 5u,
        .dst_sel_z = 6u,
        .dst_sel_w = 7u,
    };

    TEST_ASSERT_EQ(agcGfx1013BufferDescriptorEncode(
        &buffer, 0x0000000202600000ull, 32u, 8u), AGC_OK,
        "gfx1013 structured buffer descriptor encodes");
    TEST_ASSERT_EQ(buffer.words[0], 0x02600000u,
        "gfx1013 buffer address low");
    TEST_ASSERT_EQ(buffer.words[1], 0x00200002u,
        "gfx1013 buffer address high and stride");
    TEST_ASSERT_EQ(buffer.words[2], 8u,
        "gfx1013 buffer element count");
    TEST_ASSERT_EQ(buffer.words[3], 0x11014facu,
        "gfx1013 buffer hardware controls");

    TEST_ASSERT_EQ(agcGfx1013Image2DDescriptorEncode(
        &image, &image_state), AGC_OK,
        "gfx1013 2D image descriptor encodes");
    TEST_ASSERT_EQ(image.words[0], 0x02027000u,
        "gfx1013 image address low");
    TEST_ASSERT_EQ(image.words[1], 0x43800000u,
        "gfx1013 image format and width low");
    TEST_ASSERT_EQ(image.words[2], 0x80004000u,
        "gfx1013 image dimensions and resource level");
    TEST_ASSERT_EQ(image.words[3], 0x90000facu,
        "gfx1013 image type and destination selection");
    TEST_ASSERT_EQ(image.words[4], 0u,
        "gfx1013 unused image words are zero");

    agcSamplerDescriptorInit(&sampler);
    agcSamplerDescriptorSetClampMode(
        &sampler, kAgcClampClamp, kAgcClampClamp, kAgcClampClamp);
    agcSamplerDescriptorSetFilterMode(
        &sampler, kAgcFilterBilinear, kAgcFilterBilinear,
        kAgcMipFilterNone);
    TEST_ASSERT_EQ(agcGfx1013CombinedImageSamplerDescriptorEncode(
        &combined, &image_state, &sampler), AGC_OK,
        "gfx1013 combined image/sampler descriptor encodes");
    TEST_ASSERT_EQ(combined.image.words[3], image.words[3],
        "combined descriptor preserves image encoding");
    TEST_ASSERT_EQ(combined.sampler.words[0], sampler.words[0],
        "combined descriptor places sampler at dword 8");
    TEST_ASSERT_EQ(combined.reserved[3], 0u,
        "combined descriptor clears trailing stride padding");
}

static void test_gfx1013_hardware_descriptor_validation(void)
{
    AgcGfx1013BufferDescriptor buffer = {{1u, 2u, 3u, 4u}};
    AgcGfx1013ImageDescriptor image = {{1u}};
    AgcGfx1013Image2DState state = {
        .address = 0x0000000202700001ull,
        .width = 2u,
        .height = 2u,
        .format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM,
        .image_type = AGC_GFX1013_IMAGE_TYPE_2D,
        .dst_sel_x = 4u,
        .dst_sel_y = 5u,
        .dst_sel_z = 6u,
        .dst_sel_w = 7u,
    };

    TEST_ASSERT_EQ(agcGfx1013BufferDescriptorEncode(
        &buffer, 0x0001000000000000ull, 32u, 8u),
        AGC_ERROR_VALIDATION_FAILED, "48-bit buffer address enforced");
    TEST_ASSERT_EQ(buffer.words[0], 1u,
        "invalid buffer encoding preserves destination");
    TEST_ASSERT_EQ(agcGfx1013Image2DDescriptorEncode(&image, &state),
        AGC_ERROR_INVALID_ALIGNMENT, "image alignment enforced");
    TEST_ASSERT_EQ(image.words[0], 1u,
        "invalid image encoding preserves destination");
}

static void test_gfx1013_msaa_image_descriptor(void)
{
    AgcGfx1013ImageDescriptor image = {{0}};
    AgcGfx1013Image2DState state = {
        .address = 0x0000000203000000ull,
        .width = 1920u,
        .height = 1080u,
        .format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM,
        .image_type = AGC_GFX1013_IMAGE_TYPE_2D_MSAA,
        .dst_sel_x = 4u, .dst_sel_y = 5u,
        .dst_sel_z = 6u, .dst_sel_w = 7u,
        .sample_count = 4u,
        .swizzle_mode = AGC_GFX1013_IMAGE_SWIZZLE_64KB_R_X,
    };

    TEST_ASSERT_EQ(agcGfx1013Image2DDescriptorEncode(&image, &state),
        AGC_OK, "gfx1013 4x image descriptor encodes");
    TEST_ASSERT_EQ(image.words[3], 0xE1B20FACu,
        "gfx1013 4x image type, R_X swizzle, and sample count");
    TEST_ASSERT_EQ(image.words[5], 0x20u,
        "gfx1013 4x image max mip mirrors log2 samples");

    state.address += 0x100u;
    image.words[0] = 0x11223344u;
    TEST_ASSERT_EQ(agcGfx1013Image2DDescriptorEncode(&image, &state),
        AGC_ERROR_INVALID_ALIGNMENT,
        "gfx1013 64KB_R_X image requires 64KB alignment");
    TEST_ASSERT_EQ(image.words[0], 0x11223344u,
        "misaligned gfx1013 MSAA image preserves destination");
}

void test_suite_texture(void) {
    TEST_SUITE("Texture Descriptors");
    TEST_RUN(test_texture_descriptor_size);
    TEST_RUN(test_buffer_descriptor_size);
    TEST_RUN(test_sampler_descriptor_size);
    TEST_RUN(test_texture_init);
    TEST_RUN(test_texture_dimensions);
    TEST_RUN(test_texture_format);
    TEST_RUN(test_buffer_init);
    TEST_RUN(test_sampler_init);
    TEST_RUN(test_sampler_filters);
    TEST_RUN(test_sampler_filters_nearest);
    TEST_RUN(test_sampler_wrap_modes);
    TEST_RUN(test_sampler_lod);
    TEST_RUN(test_sampler_lod_bias);
    TEST_RUN(test_sampler_compare_func);
    TEST_RUN(test_sampler_anisotropy);
    TEST_RUN(test_sampler_anisotropy_disabled);
    TEST_RUN(test_sampler_combined);
    TEST_RUN(test_tile_mode_enum_values);
    TEST_RUN(test_image_type_enum_values);
    TEST_RUN(test_extended_data_formats);
    TEST_RUN(test_clamp_mode_enum);
    TEST_RUN(test_filter_mode_enum);
    TEST_RUN(test_mip_filter_enum);
    TEST_RUN(test_border_color_enum);
    TEST_RUN(test_extended_number_types);
    /* Render target descriptor tests */
    TEST_RUN(test_render_target_size);
    TEST_RUN(test_render_target_offsets);
    TEST_RUN(test_render_target_init_defaults);
    TEST_RUN(test_render_target_base_address);
    TEST_RUN(test_render_target_format);
    TEST_RUN(test_render_target_comp_swap);
    TEST_RUN(test_render_target_dimensions);
    TEST_RUN(test_render_target_tile_mode);
    TEST_RUN(test_render_target_fmask_tile_mode);
    TEST_RUN(test_render_target_num_samples);
    TEST_RUN(test_render_target_num_fragments);
    TEST_RUN(test_render_target_cmask_fmask_dcc);
    TEST_RUN(test_render_target_dcc_enable);
    TEST_RUN(test_render_target_clear_words);
    TEST_RUN(test_render_target_combined);
    TEST_RUN(test_surface_number_enum);
    TEST_RUN(test_surface_swap_enum);
    /* Typed descriptor helper tests */
    TEST_RUN(test_texture_set_image_type);
    TEST_RUN(test_texture_set_tile_mode);
    TEST_RUN(test_texture_set_tile_mode_matches_swizzle);
    TEST_RUN(test_texture_set_mip_levels);
    TEST_RUN(test_texture_set_array_size);
    TEST_RUN(test_texture_set_depth);
    TEST_RUN(test_texture_set_pitch);
    TEST_RUN(test_texture_set_dst_sel);
    TEST_RUN(test_texture_getters);
    TEST_RUN(test_texture_getters_zero_dims);
    TEST_RUN(test_sampler_set_clamp_mode);
    TEST_RUN(test_sampler_clamp_matches_raw);
    TEST_RUN(test_sampler_set_filter_mode);
    TEST_RUN(test_sampler_filter_matches_raw);
    TEST_RUN(test_sampler_filter_aniso);
    TEST_RUN(test_sampler_set_border_color);
    TEST_RUN(test_sampler_set_max_anisotropy);
    TEST_RUN(test_sampler_max_aniso_matches_raw);
    TEST_RUN(test_sampler_typed_combined);
    /* Format encode/decode helper tests */
    TEST_RUN(test_texture_format_encode_basic);
    TEST_RUN(test_texture_format_encode_srgb);
    TEST_RUN(test_texture_format_encode_float);
    TEST_RUN(test_texture_format_encode_matches_setformat);
    TEST_RUN(test_texture_format_get_data_format);
    TEST_RUN(test_texture_format_get_number_type);
    TEST_RUN(test_texture_format_roundtrip);
    TEST_RUN(test_texture_format_roundtrip_all_data_formats);
    TEST_RUN(test_texture_format_roundtrip_all_number_types);
    TEST_RUN(test_gfx1013_hardware_descriptors);
    TEST_RUN(test_gfx1013_hardware_descriptor_validation);
    TEST_RUN(test_gfx1013_msaa_image_descriptor);
}
