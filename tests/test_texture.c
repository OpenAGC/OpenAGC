#include "test.h"
#include "agc_texture.h"

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
    /* min_filter=1 at bit 12, mip_filter=1 at bit 13, mag_filter=1 at bit 16 */
    TEST_ASSERT_EQ((desc.words[0] >> 12) & 1u, 1u, "Min filter = linear");
    TEST_ASSERT_EQ((desc.words[0] >> 13) & 1u, 1u, "Mip filter = linear");
    TEST_ASSERT_EQ((desc.words[0] >> 16) & 1u, 1u, "Mag filter = linear");
}

static void test_sampler_filters_nearest(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetFilters(&desc, 0, 0, 0);
    TEST_ASSERT_EQ((desc.words[0] >> 12) & 1u, 0u, "Min filter = nearest");
    TEST_ASSERT_EQ((desc.words[0] >> 16) & 1u, 0u, "Mag filter = nearest");
}

static void test_sampler_wrap_modes(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetWrapModes(&desc, 0, 1, 2);
    TEST_ASSERT_EQ(desc.words[0] & 0xFu, 0u, "Wrap S = repeat");
    TEST_ASSERT_EQ((desc.words[0] >> 4) & 0xFu, 1u, "Wrap T = clamp");
    TEST_ASSERT_EQ((desc.words[0] >> 8) & 0x3u, 2u, "Wrap R = clamp_to_edge");
}

static void test_sampler_lod(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetLod(&desc, 0.0f, 16.0f, 0.0f);
    /* min_lod=0 at bits [15:0], max_lod=16*256=4096 at bits [31:16] */
    TEST_ASSERT_EQ(desc.words[1] & 0xFFFFu, 0u, "Min LOD = 0");
    TEST_ASSERT_EQ((desc.words[1] >> 16) & 0xFFFFu, 4096u, "Max LOD = 16.0");
}

static void test_sampler_lod_bias(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetLod(&desc, 0.0f, 8.0f, -1.0f);
    /* lod_bias=-1.0 * 256 = -256 = 0xFF00 in 16-bit */
    uint32_t bias = (desc.words[2] >> 16) & 0xFFFFu;
    TEST_ASSERT_EQ(bias, 0xFF00u, "LOD bias = -1.0");
}

static void test_sampler_compare_func(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetCompareFunc(&desc, 1); /* LESS */
    TEST_ASSERT_EQ(desc.words[2] & 0x7u, 1u, "Compare func = LESS");
    agcSamplerDescriptorSetCompareFunc(&desc, 7); /* ALWAYS */
    TEST_ASSERT_EQ(desc.words[2] & 0x7u, 7u, "Compare func = ALWAYS");
}

static void test_sampler_anisotropy(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetAnisotropy(&desc, 8);
    /* aniso enable at bit 4, level 6 at bits [7:5] */
    TEST_ASSERT_EQ((desc.words[2] >> 4) & 1u, 1u, "Aniso enabled");
    TEST_ASSERT_EQ((desc.words[2] >> 5) & 0x7u, 6u, "Aniso level = 6 (8x)");
}

static void test_sampler_anisotropy_disabled(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetAnisotropy(&desc, 0);
    TEST_ASSERT_EQ((desc.words[2] >> 4) & 1u, 0u, "Aniso disabled");
}

static void test_sampler_combined(void) {
    AgcSamplerDescriptor desc;
    agcSamplerDescriptorInit(&desc);
    agcSamplerDescriptorSetFilters(&desc, 1, 1, 1);
    agcSamplerDescriptorSetWrapModes(&desc, 3, 3, 0);
    agcSamplerDescriptorSetLod(&desc, 0.0f, 12.0f, 0.5f);
    agcSamplerDescriptorSetCompareFunc(&desc, 3); /* LEQUAL */
    agcSamplerDescriptorSetAnisotropy(&desc, 4);

    TEST_ASSERT_EQ((desc.words[0] >> 12) & 1u, 1u, "Combined: min filter = linear");
    TEST_ASSERT_EQ((desc.words[0] >> 16) & 1u, 1u, "Combined: mag filter = linear");
    TEST_ASSERT_EQ(desc.words[0] & 0xFu, 3u, "Combined: wrap S = mirrored");
    TEST_ASSERT_EQ((desc.words[1] >> 16) & 0xFFFFu, 3072u, "Combined: max LOD = 12.0");
    TEST_ASSERT_EQ(desc.words[2] & 0x7u, 3u, "Combined: compare = LEQUAL");
    TEST_ASSERT_EQ((desc.words[2] >> 4) & 1u, 1u, "Combined: aniso enabled");
    TEST_ASSERT_EQ((desc.words[2] >> 5) & 0x7u, 5u, "Combined: aniso level = 5 (4x)");
}

void test_suite_texture(void) {
    TEST_SUITE("Texture Descriptors");
    TEST_RUN(test_texture_descriptor_size);
    TEST_RUN(test_buffer_descriptor_size);
    TEST_RUN(test_sampler_descriptor_size);
    TEST_RUN(test_texture_init);
    TEST_RUN(test_texture_dimensions);
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
}
