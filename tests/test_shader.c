#include "test.h"
#include "agc_shader.h"

#include <stdint.h>
#include <string.h>

static void set_u32(uint8_t dst[4], uint32_t value) {
    memcpy(dst, &value, sizeof(value));
}

static void test_shader_record_invalid_null(void) {
    TEST_ASSERT(!agcShaderRecordIsValid(NULL), "NULL should be invalid");
    TEST_ASSERT(agcShaderRecordGetCode(NULL) == NULL, "NULL code pointer");
    TEST_ASSERT(agcShaderRecordGetNumShRegisters(NULL) == 0, "NULL SH count");
}

static void test_shader_record_invalid_magic(void) {
    AgcShaderRecord rec = {0};
    rec.magic = 0xDEADBEEF;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.shader_type = kAgcShaderTypeCs;
    TEST_ASSERT(!agcShaderRecordIsValid(&rec), "Bad magic should be invalid");
}

static void test_shader_record_invalid_version(void) {
    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = 0x01;
    rec.shader_type = kAgcShaderTypeCs;
    TEST_ASSERT(!agcShaderRecordIsValid(&rec), "Bad version should be invalid");
}

static void test_shader_record_invalid_type(void) {
    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.shader_type = 0xFF;
    TEST_ASSERT(!agcShaderRecordIsValid(&rec), "Bad shader type should be invalid");
}

static void test_shader_record_layout(void) {
    /* Synthetic record with synthetic sub-buffers. */
    uint32_t code_buffer[4] = {0x12345678, 0x9ABCDEF0, 0, 0};
    uint32_t user_data_buffer[8] = {0};
    uint32_t cx_buffer[4] = {0};
    uint32_t sh_buffer[4] = {0};
    uint32_t specials_buffer[4] = {0};
    uint32_t in_sem_buffer[4] = {0};
    uint32_t out_sem_buffer[4] = {0};

    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.user_data = (uint64_t)(uintptr_t)user_data_buffer;
    rec.code = (uint64_t)(uintptr_t)code_buffer;
    rec.cx_registers = (uint64_t)(uintptr_t)cx_buffer;
    rec.sh_registers = (uint64_t)(uintptr_t)sh_buffer;
    rec.specials = (uint64_t)(uintptr_t)specials_buffer;
    rec.input_semantics = (uint64_t)(uintptr_t)in_sem_buffer;
    rec.output_semantics = (uint64_t)(uintptr_t)out_sem_buffer;
    set_u32(rec.num_input_semantics, 2);
    set_u32(rec.num_output_semantics, 3);
    rec.shader_type = kAgcShaderTypeVs;
    rec.num_sh_registers = 16;

    TEST_ASSERT(agcShaderRecordIsValid(&rec), "Valid record should pass");
    TEST_ASSERT(agcShaderRecordGetType(&rec) == kAgcShaderTypeVs, "Shader type");
    TEST_ASSERT(agcShaderRecordGetCode(&rec) == code_buffer, "Code pointer");
    TEST_ASSERT(agcShaderRecordGetUserData(&rec) == user_data_buffer, "User data pointer");
    TEST_ASSERT(agcShaderRecordGetCxRegisters(&rec) == cx_buffer, "CX registers pointer");
    TEST_ASSERT(agcShaderRecordGetShRegisters(&rec) == sh_buffer, "SH registers pointer");
    TEST_ASSERT(agcShaderRecordGetSpecials(&rec) == specials_buffer, "Specials pointer");
    TEST_ASSERT(agcShaderRecordGetInputSemantics(&rec) == in_sem_buffer, "Input semantics pointer");
    TEST_ASSERT(agcShaderRecordGetOutputSemantics(&rec) == out_sem_buffer, "Output semantics pointer");
    TEST_ASSERT_EQ(agcShaderRecordGetNumInputSemantics(&rec), 2, "Input semantics count");
    TEST_ASSERT_EQ(agcShaderRecordGetNumOutputSemantics(&rec), 3, "Output semantics count");
    TEST_ASSERT_EQ(agcShaderRecordGetNumShRegisters(&rec), 16, "SH register count");
}

void test_suite_shader(void) {
    TEST_SUITE("Shader Record");
    TEST_RUN(test_shader_record_invalid_null);
    TEST_RUN(test_shader_record_invalid_magic);
    TEST_RUN(test_shader_record_invalid_version);
    TEST_RUN(test_shader_record_invalid_type);
    TEST_RUN(test_shader_record_layout);
}
