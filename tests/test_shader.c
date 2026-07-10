#include "test.h"
#include "agc_shader.h"
#include "agc_error.h"

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

static void test_shader_specials_struct_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcShaderSpecials), 16u, "Specials struct = 16 bytes");
}

static void test_shader_userdata_struct_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcShaderUserData), 40u, "UserData struct = 40 bytes");
}

static void test_shader_specials_typed_accessor(void) {
    AgcShaderSpecials specials = {0};
    specials.ge_cntl = 0x12345678;
    specials.vgt_shader_stages_en = 0xAABBCCDD;
    specials.vgt_gs_out_prim_type = 0x11223344;
    specials.ge_user_vgpr_en = 0x55667788;

    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.shader_type = kAgcShaderTypeVs;
    rec.specials = (uint64_t)(uintptr_t)&specials;

    const AgcShaderSpecials *sp = agcShaderRecordGetSpecialsTyped(&rec);
    TEST_ASSERT(sp != NULL, "Specials typed pointer non-NULL");
    TEST_ASSERT_EQ(sp->ge_cntl, 0x12345678u, "GE_CNTL value");
    TEST_ASSERT_EQ(sp->vgt_shader_stages_en, 0xAABBCCDDu, "VGT_SHADER_STAGES_EN value");
    TEST_ASSERT_EQ(sp->vgt_gs_out_prim_type, 0x11223344u, "VGT_GS_OUT_PRIM_TYPE value");
    TEST_ASSERT_EQ(sp->ge_user_vgpr_en, 0x55667788u, "GE_USER_VGPR_EN value");
}

static void test_shader_userdata_typed_accessor(void) {
    AgcShaderUserData ud = {0};
    ud.entries[0] = 0x1111111111111111ULL;
    ud.entries[1] = 0x2222222222222222ULL;
    ud.entries[2] = 0x3333333333333333ULL;
    ud.entries[3] = 0x4444444444444444ULL;
    ud.entries[4] = 0x5555555555555555ULL;

    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.shader_type = kAgcShaderTypeCs;
    rec.user_data = (uint64_t)(uintptr_t)&ud;

    const AgcShaderUserData *pud = agcShaderRecordGetUserDataTyped(&rec);
    TEST_ASSERT(pud != NULL, "UserData typed pointer non-NULL");
    TEST_ASSERT_EQ(pud->entries[0], 0x1111111111111111ULL, "UserData entry 0");
    TEST_ASSERT_EQ(pud->entries[1], 0x2222222222222222ULL, "UserData entry 1");
    TEST_ASSERT_EQ(pud->entries[4], 0x5555555555555555ULL, "UserData entry 4");
}

static void test_shader_register_value_accessors(void) {
    uint32_t sh_regs[4] = {0xDEADBEEF, 0xCAFEBABE, 0, 0};
    uint32_t cx_regs[4] = {0x12345678, 0x9ABCDEF0, 0, 0};

    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.shader_type = kAgcShaderTypePs;
    rec.sh_registers = (uint64_t)(uintptr_t)sh_regs;
    rec.cx_registers = (uint64_t)(uintptr_t)cx_regs;

    const uint32_t *sh = agcShaderRecordGetShRegisterValues(&rec);
    TEST_ASSERT(sh != NULL, "SH register values non-NULL");
    TEST_ASSERT_EQ(sh[0], 0xDEADBEEFu, "SH reg[0]");
    TEST_ASSERT_EQ(sh[1], 0xCAFEBABEu, "SH reg[1]");

    const uint32_t *cx = agcShaderRecordGetCxRegisterValues(&rec);
    TEST_ASSERT(cx != NULL, "CX register values non-NULL");
    TEST_ASSERT_EQ(cx[0], 0x12345678u, "CX reg[0]");
    TEST_ASSERT_EQ(cx[1], 0x9ABCDEF0u, "CX reg[1]");
}

static void test_shader_null_sub_blocks(void) {
    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.shader_type = kAgcShaderTypeCs;
    /* All pointers left as 0 */

    TEST_ASSERT(agcShaderRecordGetSpecialsTyped(&rec) == NULL, "NULL specials");
    TEST_ASSERT(agcShaderRecordGetUserDataTyped(&rec) == NULL, "NULL user_data");
    TEST_ASSERT(agcShaderRecordGetShRegisterValues(&rec) == NULL, "NULL SH regs");
    TEST_ASSERT(agcShaderRecordGetCxRegisterValues(&rec) == NULL, "NULL CX regs");
}

/* ==================== Shader linking (sceAgcShaderLinkHsGs) tests ==================== */

/* Build a synthetic shader record with the given type and a distinctive
 * fill pattern so the 0x60-byte copy can be verified byte-for-byte. */
static void build_link_shader(AgcShaderRecord *rec, AgcShaderType type,
    uint8_t fill_seed)
{
    memset(rec, fill_seed, sizeof(*rec));
    rec->magic = AGC_SHADER_RECORD_MAGIC;
    rec->version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec->shader_type = (uint8_t)type;
    set_u32(rec->num_input_semantics, 7);
    set_u32(rec->num_output_semantics, 9);
    rec->num_sh_registers = 42;
}

static void test_shader_link_hs_cs_success(void) {
    AgcShaderRecord hs, cs, dst;
    build_link_shader(&hs, kAgcShaderTypeHs, 0x11);
    build_link_shader(&cs, kAgcShaderTypeCs, 0x22);
    memset(&dst, 0, sizeof(dst));

    int32_t ret = agcShaderLinkHsGs(&dst, &hs, &cs);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "LinkHsGs HS+CS returns AGC_OK");
    /* Output type must be GS(2). */
    TEST_ASSERT_EQ((uint32_t)dst.shader_type, (uint32_t)kAgcShaderTypeGs,
        "LinkHsGs output type = GS");
    /* The 0x60-byte copy must match the CS record exactly except for the
     * shader_type byte which is overwritten to GS(2). */
    AgcShaderRecord expected = cs;
    expected.shader_type = kAgcShaderTypeGs;
    TEST_ASSERT_EQ(memcmp(&dst, &expected, sizeof(dst)), 0,
        "LinkHsGs dst matches CS record with GS type");
}

static void test_shader_link_ls_cs_success(void) {
    AgcShaderRecord ls, cs, dst;
    build_link_shader(&ls, kAgcShaderTypeLs, 0x33);
    build_link_shader(&cs, kAgcShaderTypeCs, 0x44);
    memset(&dst, 0, sizeof(dst));

    int32_t ret = agcShaderLinkHsGs(&dst, &ls, &cs);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "LinkHsGs LS+CS returns AGC_OK");
    TEST_ASSERT_EQ((uint32_t)dst.shader_type, (uint32_t)kAgcShaderTypeGs,
        "LinkHsGs LS output type = GS");
    AgcShaderRecord expected = cs;
    expected.shader_type = kAgcShaderTypeGs;
    TEST_ASSERT_EQ(memcmp(&dst, &expected, sizeof(dst)), 0,
        "LinkHsGs LS dst matches CS record with GS type");
}

static void test_shader_link_invalid_source_type(void) {
    /* Source shader is VS(1), not HS(4) or LS(5). */
    AgcShaderRecord vs, cs, dst;
    build_link_shader(&vs, kAgcShaderTypeVs, 0x55);
    build_link_shader(&cs, kAgcShaderTypeCs, 0x66);
    memset(&dst, 0, sizeof(dst));

    int32_t ret = agcShaderLinkHsGs(&dst, &vs, &cs);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_ERROR_SHADER_INVALID_TYPE,
        "LinkHsGs VS source returns INVALID_TYPE");
    /* dst must not have been modified (still zeroed). */
    TEST_ASSERT_EQ(dst.shader_type, 0u, "LinkHsGs bad source: dst untouched");
}

static void test_shader_link_invalid_cs_type(void) {
    /* CS shader is GS(2), not CS(6). */
    AgcShaderRecord hs, gs, dst;
    build_link_shader(&hs, kAgcShaderTypeHs, 0x77);
    build_link_shader(&gs, kAgcShaderTypeGs, 0x88);
    memset(&dst, 0, sizeof(dst));

    int32_t ret = agcShaderLinkHsGs(&dst, &hs, &gs);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_ERROR_SHADER_INVALID_TYPE,
        "LinkHsGs GS-as-CS returns INVALID_TYPE");
    TEST_ASSERT_EQ(dst.shader_type, 0u, "LinkHsGs bad CS: dst untouched");
}

static void test_shader_link_null_pointers(void) {
    AgcShaderRecord hs, cs, dst;
    build_link_shader(&hs, kAgcShaderTypeHs, 0x99);
    build_link_shader(&cs, kAgcShaderTypeCs, 0xAA);

    TEST_ASSERT_EQ(agcShaderLinkHsGs(NULL, &hs, &cs),
        (int32_t)AGC_ERROR_INVALID_ARGUMENT, "LinkHsGs NULL dst");
    TEST_ASSERT_EQ(agcShaderLinkHsGs(&dst, NULL, &cs),
        (int32_t)AGC_ERROR_INVALID_ARGUMENT, "LinkHsGs NULL source");
    TEST_ASSERT_EQ(agcShaderLinkHsGs(&dst, &hs, NULL),
        (int32_t)AGC_ERROR_INVALID_ARGUMENT, "LinkHsGs NULL cs");
}

static void test_shader_link_copy_exact(void) {
    /* Verify the 0x60-byte copy is byte-exact by filling every field of
     * the CS record with a unique pattern and comparing the output. */
    AgcShaderRecord hs, cs, dst;
    build_link_shader(&hs, kAgcShaderTypeHs, 0x00);
    memset(&cs, 0, sizeof(cs));
    cs.magic = AGC_SHADER_RECORD_MAGIC;
    cs.version = AGC_SHADER_RECORD_VERSION_GEN5;
    cs.user_data = 0x1111111111111111ULL;
    cs.code = 0x2222222222222222ULL;
    cs.cx_registers = 0x3333333333333333ULL;
    cs.sh_registers = 0x4444444444444444ULL;
    cs.specials = 0x5555555555555555ULL;
    cs.input_semantics = 0x6666666666666666ULL;
    cs.output_semantics = 0x7777777777777777ULL;
    /* Fill the padding region with a pattern. */
    memset(cs._pad1, 0xAB, sizeof(cs._pad1));
    set_u32(cs.num_input_semantics, 0x12345678u);
    set_u32(cs.num_output_semantics, 0x9ABCDEF0u);
    cs.shader_type = kAgcShaderTypeCs;
    cs.num_sh_registers = 0xCD;

    int32_t ret = agcShaderLinkHsGs(&dst, &hs, &cs);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "LinkHsGs exact-copy: success");

    /* All fields except shader_type must match the CS record. */
    AgcShaderRecord expected = cs;
    expected.shader_type = kAgcShaderTypeGs;
    TEST_ASSERT_EQ(memcmp(&dst, &expected, sizeof(dst)), 0,
        "LinkHsGs exact-copy: dst matches CS with GS type");
    /* Spot-check individual fields. */
    TEST_ASSERT_EQ(dst.user_data, cs.user_data, "LinkHsGs copy: user_data");
    TEST_ASSERT_EQ(dst.code, cs.code, "LinkHsGs copy: code");
    TEST_ASSERT_EQ(dst.specials, cs.specials, "LinkHsGs copy: specials");
    TEST_ASSERT_EQ(agcShaderRecordGetNumInputSemantics(&dst), 0x12345678u,
        "LinkHsGs copy: num_input_semantics");
    TEST_ASSERT_EQ(agcShaderRecordGetNumOutputSemantics(&dst), 0x9ABCDEF0u,
        "LinkHsGs copy: num_output_semantics");
    TEST_ASSERT_EQ(dst.num_sh_registers, cs.num_sh_registers,
        "LinkHsGs copy: num_sh_registers");
}

static void test_shader_type_macros_match_enum(void) {
    /* The AGC_SHADER_TYPE_* macros must match the AgcShaderType enum. */
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_PS, (uint32_t)kAgcShaderTypePs,
        "AGC_SHADER_TYPE_PS == kAgcShaderTypePs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_VS, (uint32_t)kAgcShaderTypeVs,
        "AGC_SHADER_TYPE_VS == kAgcShaderTypeVs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_GS, (uint32_t)kAgcShaderTypeGs,
        "AGC_SHADER_TYPE_GS == kAgcShaderTypeGs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_DS, (uint32_t)kAgcShaderTypeEs,
        "AGC_SHADER_TYPE_DS == kAgcShaderTypeEs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_HS, (uint32_t)kAgcShaderTypeHs,
        "AGC_SHADER_TYPE_HS == kAgcShaderTypeHs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_LS, (uint32_t)kAgcShaderTypeLs,
        "AGC_SHADER_TYPE_LS == kAgcShaderTypeLs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_CS, (uint32_t)kAgcShaderTypeCs,
        "AGC_SHADER_TYPE_CS == kAgcShaderTypeCs");
}

void test_suite_shader(void) {
    TEST_SUITE("Shader Record");
    TEST_RUN(test_shader_record_invalid_null);
    TEST_RUN(test_shader_record_invalid_magic);
    TEST_RUN(test_shader_record_invalid_version);
    TEST_RUN(test_shader_record_invalid_type);
    TEST_RUN(test_shader_record_layout);
    TEST_RUN(test_shader_specials_struct_size);
    TEST_RUN(test_shader_userdata_struct_size);
    TEST_RUN(test_shader_specials_typed_accessor);
    TEST_RUN(test_shader_userdata_typed_accessor);
    TEST_RUN(test_shader_register_value_accessors);
    TEST_RUN(test_shader_null_sub_blocks);
    /* Shader linking (sceAgcShaderLinkHsGs) */
    TEST_RUN(test_shader_link_hs_cs_success);
    TEST_RUN(test_shader_link_ls_cs_success);
    TEST_RUN(test_shader_link_invalid_source_type);
    TEST_RUN(test_shader_link_invalid_cs_type);
    TEST_RUN(test_shader_link_null_pointers);
    TEST_RUN(test_shader_link_copy_exact);
    TEST_RUN(test_shader_type_macros_match_enum);
}
