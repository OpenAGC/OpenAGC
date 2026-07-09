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

void test_suite_texture(void) {
    TEST_SUITE("Texture Descriptors");
    TEST_RUN(test_texture_descriptor_size);
    TEST_RUN(test_buffer_descriptor_size);
    TEST_RUN(test_sampler_descriptor_size);
    TEST_RUN(test_texture_init);
    TEST_RUN(test_texture_dimensions);
    TEST_RUN(test_buffer_init);
}
