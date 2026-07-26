#include "test.h"
#include "agc_registers.h"
#include "agc_shader.h"
#include "agc_error.h"
#include "agcdriver.h"

#include <stddef.h>
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
    uint32_t specials_buffer[12] = {0};
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

static void test_shader_specials_struct_layout(void) {
    TEST_ASSERT_EQ(sizeof(AgcShaderSpecialRegister), 0x08u,
        "Special register pair = 8 bytes");
    TEST_ASSERT_EQ(offsetof(AgcShaderSpecialRegister, value), 0x04u,
        "Special register value offset");
    TEST_ASSERT_EQ(sizeof(AgcShaderSpecials), 0x30u,
        "Specials struct = 0x30 bytes");
    TEST_ASSERT_EQ(offsetof(AgcShaderSpecials, ge_cntl), 0x00u,
        "GE_CNTL pair offset");
    TEST_ASSERT_EQ(offsetof(AgcShaderSpecials, vgt_shader_stages_en), 0x08u,
        "VGT_SHADER_STAGES_EN pair offset");
    TEST_ASSERT_EQ(offsetof(AgcShaderSpecials, reserved_10), 0x10u,
        "Reserved pairs offset");
    TEST_ASSERT_EQ(offsetof(AgcShaderSpecials, vgt_gs_out_prim_type), 0x20u,
        "VGT_GS_OUT_PRIM_TYPE pair offset");
    TEST_ASSERT_EQ(offsetof(AgcShaderSpecials, ge_user_vgpr_en), 0x28u,
        "GE_USER_VGPR_EN pair offset");
}

static void test_shader_userdata_struct_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcShaderUserData), 40u, "UserData struct = 40 bytes");
}

static void test_shader_specials_typed_accessor(void) {
    AgcShaderSpecials specials = {0};
    specials.ge_cntl.register_offset = 0x01;
    specials.ge_cntl.value = 0x12345678;
    specials.vgt_shader_stages_en.register_offset = 0x02;
    specials.vgt_shader_stages_en.value = 0xAABBCCDD;
    specials.vgt_gs_out_prim_type.register_offset = 0x03;
    specials.vgt_gs_out_prim_type.value = 0x11223344;
    specials.ge_user_vgpr_en.register_offset = 0x04;
    specials.ge_user_vgpr_en.value = 0x55667788;

    AgcShaderRecord rec = {0};
    rec.magic = AGC_SHADER_RECORD_MAGIC;
    rec.version = AGC_SHADER_RECORD_VERSION_GEN5;
    rec.shader_type = kAgcShaderTypeVs;
    rec.specials = (uint64_t)(uintptr_t)&specials;

    const AgcShaderSpecials *sp = agcShaderRecordGetSpecialsTyped(&rec);
    TEST_ASSERT(sp != NULL, "Specials typed pointer non-NULL");
    TEST_ASSERT_EQ(sp->ge_cntl.register_offset, 0x01u, "GE_CNTL register");
    TEST_ASSERT_EQ(sp->ge_cntl.value, 0x12345678u, "GE_CNTL value");
    TEST_ASSERT_EQ(sp->vgt_shader_stages_en.register_offset, 0x02u,
        "VGT_SHADER_STAGES_EN register");
    TEST_ASSERT_EQ(sp->vgt_shader_stages_en.value, 0xAABBCCDDu,
        "VGT_SHADER_STAGES_EN value");
    TEST_ASSERT_EQ(sp->vgt_gs_out_prim_type.register_offset, 0x03u,
        "VGT_GS_OUT_PRIM_TYPE register");
    TEST_ASSERT_EQ(sp->vgt_gs_out_prim_type.value, 0x11223344u,
        "VGT_GS_OUT_PRIM_TYPE value");
    TEST_ASSERT_EQ(sp->ge_user_vgpr_en.register_offset, 0x04u,
        "GE_USER_VGPR_EN register");
    TEST_ASSERT_EQ(sp->ge_user_vgpr_en.value, 0x55667788u,
        "GE_USER_VGPR_EN value");
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

static void test_get_gs_prim_payload(void) {
    AgcShaderRegister cx_registers[3] = {
        {0x100u, 0xFFFFFFFFu},
        {0x1C2u, 0x12u},
        {0x1C2u, 0x02u},
    };
    AgcShaderRecord rec = {0};
    rec.cx_registers = (uint64_t)(uintptr_t)cx_registers;
    rec.num_cx_registers = 3u;

    uint32_t payload = 0xFFFFFFFFu;
    TEST_ASSERT_EQ(sceAgcGetGsPrimPayload(&payload, &rec), AGC_OK,
        "GetGsPrimPayload returns OK");
    TEST_ASSERT_EQ(payload, 8u,
        "GetGsPrimPayload reports eight bytes for mode two");

    cx_registers[1].value = 0x13u;
    payload = 0xFFFFFFFFu;
    TEST_ASSERT_EQ(sceAgcGetGsPrimPayload(&payload, &rec), AGC_OK,
        "GetGsPrimPayload nonmatching mode returns OK");
    TEST_ASSERT_EQ(payload, 0u,
        "GetGsPrimPayload clears output for nonmatching mode");

    rec.num_cx_registers = 1u;
    payload = 0xFFFFFFFFu;
    TEST_ASSERT_EQ(sceAgcGetGsPrimPayload(&payload, &rec), AGC_OK,
        "GetGsPrimPayload respects CX register count");
    TEST_ASSERT_EQ(payload, 0u,
        "GetGsPrimPayload ignores entries beyond CX register count");
    TEST_ASSERT_EQ(sceAgcGetGsPrimPayload(NULL, &rec),
        AGC_ERROR_INVALID_ARGUMENT, "GetGsPrimPayload rejects null output");
    TEST_ASSERT_EQ(sceAgcGetGsPrimPayload(&payload, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "GetGsPrimPayload rejects null shader");
}

static void test_get_gs_oversubscription(void) {
    AgcShaderRegister output[2] = {{0xFFFFFFFFu, 0xFFFFFFFFu},
                                  {0xFFFFFFFFu, 0xFFFFFFFFu}};
    TEST_ASSERT_EQ(sceAgcGetGsOversubscription(output, NULL, 0u, 0.5f),
        AGC_OK, "GetGsOversubscription returns defaults for zero limit");
    TEST_ASSERT_EQ(output[0].offset, AGC_REG_GE_PC_ALLOC,
        "GetGsOversubscription GE_PC_ALLOC offset");
    TEST_ASSERT_EQ(output[0].value, 0u,
        "GetGsOversubscription GE_PC_ALLOC default");
    TEST_ASSERT_EQ(output[1].offset, AGC_REG_SPI_SHADER_PGM_RSRC4_GS,
        "GetGsOversubscription RSRC4_GS offset");
    TEST_ASSERT_EQ(output[1].value, 0u,
        "GetGsOversubscription RSRC4_GS default");

    TEST_ASSERT_EQ(sceAgcGetGsOversubscription(
        output, NULL, UINT32_MAX, 0.5f), AGC_OK,
        "GetGsOversubscription accepts forced maximum");
    TEST_ASSERT_EQ(output[0].value, 0x7FFu,
        "GetGsOversubscription forces all PC lines");
    TEST_ASSERT_EQ(output[1].value, 0x007F0000u,
        "GetGsOversubscription forces maximum late allocation");

    AgcShaderRegister cx_registers[5] = {
        {AGC_REG_VGT_GS_ONCHIP_CNTL, 128u << 11u},
        {AGC_REG_GE_NGG_SUBGRP_CNTL, 4u},
        {AGC_REG_SPI_VS_OUT_CONFIG, 3u << 2u},
        {AGC_REG_PA_CL_VS_OUT_CNTL, 0u},
        {AGC_REG_GE_MAX_OUTPUT_PER_SUBGROUP, 256u},
    };
    AgcShaderSpecials specials = {0};
    AgcShaderRecord rec = {0};
    rec.cx_registers = (uint64_t)(uintptr_t)cx_registers;
    rec.num_cx_registers = 5u;
    rec.specials = (uint64_t)(uintptr_t)&specials;
    TEST_ASSERT_EQ(sceAgcGetGsOversubscription(
        output, &rec, 65536u, 0.5f), AGC_OK,
        "GetGsOversubscription computes interpolated occupancy");
    TEST_ASSERT_EQ(output[0].value, 0x3FFu,
        "GetGsOversubscription interpolates PC allocation");
    TEST_ASSERT_EQ(output[1].value, 0x007F0000u,
        "GetGsOversubscription preserves maximum late allocation");

    rec.num_cx_registers = 4u;
    TEST_ASSERT_EQ(sceAgcGetGsOversubscription(
        output, &rec, 65536u, 0.5f), AGC_ERROR_INVALID_ARGUMENT,
        "GetGsOversubscription rejects missing shader state");
    TEST_ASSERT_EQ(sceAgcGetGsOversubscription(
        NULL, &rec, 0u, 0.5f), AGC_ERROR_INVALID_ARGUMENT,
        "GetGsOversubscription rejects null output");
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
    /* The AGC_SHADER_TYPE_* macros must match the AgcShaderType enum.
     * Encoding confirmed by sharpemu: CS=0, PS=1, ES=2, VS=3, GS=4, HS=5,
     * ES-alt=6 (DS for compat), LS=7. */
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_CS, (uint32_t)kAgcShaderTypeCs,
        "AGC_SHADER_TYPE_CS == kAgcShaderTypeCs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_PS, (uint32_t)kAgcShaderTypePs,
        "AGC_SHADER_TYPE_PS == kAgcShaderTypePs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_ES, (uint32_t)kAgcShaderTypeEs,
        "AGC_SHADER_TYPE_ES == kAgcShaderTypeEs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_VS, (uint32_t)kAgcShaderTypeVs,
        "AGC_SHADER_TYPE_VS == kAgcShaderTypeVs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_GS, (uint32_t)kAgcShaderTypeGs,
        "AGC_SHADER_TYPE_GS == kAgcShaderTypeGs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_HS, (uint32_t)kAgcShaderTypeHs,
        "AGC_SHADER_TYPE_HS == kAgcShaderTypeHs");
    TEST_ASSERT_EQ((uint32_t)AGC_SHADER_TYPE_LS, (uint32_t)kAgcShaderTypeLs,
        "AGC_SHADER_TYPE_LS == kAgcShaderTypeLs");
}

/* ===================================================================== */
/* Fused shader tests (reference-confirmed)                                */
/* ===================================================================== */

static void test_fused_shader_get_size_gs(void) {
    AgcShaderRecord front = {0};
    front.magic = AGC_SHADER_RECORD_MAGIC;
    front.version = AGC_SHADER_RECORD_VERSION_GEN5;
    front.shader_type = kAgcShaderBinaryTypeGsFront;
    front.num_sh_registers = 10;

    AgcShaderRecord back = {0};
    back.magic = AGC_SHADER_RECORD_MAGIC;
    back.version = AGC_SHADER_RECORD_VERSION_GEN5;
    back.shader_type = kAgcShaderBinaryTypeGsBack;
    back.num_sh_registers = 10;

    AgcSizeAlign sa = {0};
    int32_t ret = sceAgcGetFusedShaderSize(&sa, &front, &back);
    TEST_ASSERT_EQ(ret, AGC_OK, "GetFusedShaderSize GS returns OK");
    TEST_ASSERT_EQ(sa.size, (uint64_t)(10 * sizeof(AgcShaderRegister)),
        "GetFusedShaderSize GS size");
    TEST_ASSERT_EQ(sa.align, 4u, "GetFusedShaderSize GS align");

    AgcSizeAlign legacy_sa = {0};
    ret = sceAgcGetFusedShaderSize_0080(&legacy_sa, &front, &back);
    TEST_ASSERT_EQ(ret, AGC_OK, "GetFusedShaderSize_0080 forwards");
    TEST_ASSERT_EQ(legacy_sa.size, sa.size,
        "GetFusedShaderSize_0080 preserves size ABI");
    TEST_ASSERT_EQ(legacy_sa.align, sa.align,
        "GetFusedShaderSize_0080 preserves alignment ABI");
}

static void test_fused_shader_get_size_hs(void) {
    AgcShaderRecord front = {0};
    front.magic = AGC_SHADER_RECORD_MAGIC;
    front.version = AGC_SHADER_RECORD_VERSION_GEN5;
    front.shader_type = kAgcShaderBinaryTypeHsFront;
    front.num_sh_registers = 5;

    AgcShaderRecord back = {0};
    back.magic = AGC_SHADER_RECORD_MAGIC;
    back.version = AGC_SHADER_RECORD_VERSION_GEN5;
    back.shader_type = kAgcShaderBinaryTypeHsBack;
    back.num_sh_registers = 5;

    AgcSizeAlign sa = {0};
    int32_t ret = sceAgcGetFusedShaderSize(&sa, &front, &back);
    TEST_ASSERT_EQ(ret, AGC_OK, "GetFusedShaderSize HS returns OK");
    TEST_ASSERT_EQ(sa.size, (uint64_t)(5 * sizeof(AgcShaderRegister)),
        "GetFusedShaderSize HS size");
    TEST_ASSERT_EQ(sa.align, 4u, "GetFusedShaderSize HS align");
}

static void test_fused_shader_get_size_invalid_types(void) {
    AgcShaderRecord front = {0};
    front.shader_type = kAgcShaderTypeCs;  /* Wrong: CS, not GsFront/HsFront */
    AgcShaderRecord back = {0};
    back.shader_type = kAgcShaderBinaryTypeGsBack;

    AgcSizeAlign sa = {0};
    int32_t ret = sceAgcGetFusedShaderSize(&sa, &front, &back);
    TEST_ASSERT_EQ(ret, AGC_ERROR_SHADER_INVALID_HALVES,
        "GetFusedShaderSize invalid front type returns INVALID_HALVES");

    /* Also test mismatched front/back */
    front.shader_type = kAgcShaderBinaryTypeGsFront;
    back.shader_type = kAgcShaderBinaryTypeHsBack;  /* Wrong: HsBack with GsFront */
    ret = sceAgcGetFusedShaderSize(&sa, &front, &back);
    TEST_ASSERT_EQ(ret, AGC_ERROR_SHADER_INVALID_HALVES,
        "GetFusedShaderSize mismatched pair returns INVALID_HALVES");
}

static void test_fused_shader_get_size_null(void) {
    AgcShaderRecord rec = {0};
    rec.shader_type = kAgcShaderBinaryTypeGsFront;
    AgcSizeAlign sa = {0};
    TEST_ASSERT_EQ(sceAgcGetFusedShaderSize(NULL, &rec, &rec),
        AGC_ERROR_INVALID_ARGUMENT, "GetFusedShaderSize null dst");
    TEST_ASSERT_EQ(sceAgcGetFusedShaderSize(&sa, NULL, &rec),
        AGC_ERROR_INVALID_ARGUMENT, "GetFusedShaderSize null front");
    TEST_ASSERT_EQ(sceAgcGetFusedShaderSize(&sa, &rec, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "GetFusedShaderSize null back");
}

static void build_fused_shader_pair(
    AgcShaderRecord *front, AgcShaderRecord *back,
    AgcShaderSpecials *front_specials, AgcShaderSpecials *back_specials,
    AgcShaderRegister front_regs[4], AgcShaderRegister back_regs[6],
    bool is_gs)
{
    uint32_t checksum = is_gs ? AGC_SPI_SHADER_PGM_CHKSUM_GS
                              : AGC_SPI_SHADER_PGM_CHKSUM_HS;
    uint32_t rsrc1 = is_gs ? AGC_SPI_SHADER_PGM_RSRC1_GS
                           : AGC_SPI_SHADER_PGM_RSRC1_HS;
    uint32_t rsrc2 = is_gs ? AGC_SPI_SHADER_PGM_RSRC2_GS
                           : AGC_SPI_SHADER_PGM_RSRC2_HS;
    uint32_t program_lo = is_gs ? AGC_SPI_SHADER_PGM_LO_ES
                                : AGC_SPI_SHADER_PGM_LO_LS;
    uint32_t rsrc1_shift = is_gs ? 29u : 28u;

    memset(front, 0, sizeof(*front));
    memset(back, 0, sizeof(*back));
    memset(front_specials, 0, sizeof(*front_specials));
    memset(back_specials, 0, sizeof(*back_specials));

    front_regs[0] = (AgcShaderRegister){checksum, 0x11111111u};
    front_regs[1] = (AgcShaderRegister){checksum, 0x22222222u};
    front_regs[2] = (AgcShaderRegister){rsrc1,
        1u | ((is_gs ? 2u : 3u) << rsrc1_shift)};
    front_regs[3] = (AgcShaderRegister){rsrc2,
        (15u << 28u) | (3u << 16u) | 0x0800002Au | 0x00040000u};

    back_regs[0] = (AgcShaderRegister){checksum, 0xAAAAAAAAu};
    back_regs[1] = (AgcShaderRegister){checksum, 0xBBBBBBBBu};
    back_regs[2] = (AgcShaderRegister){rsrc1, 3u | (1u << rsrc1_shift)};
    back_regs[3] = (AgcShaderRegister){rsrc2, (1u << 16u) | 0x14u};
    back_regs[4] = (AgcShaderRegister){program_lo, 0u};
    back_regs[5] = (AgcShaderRegister){program_lo + 1u, 0xA5A5A500u};

    front->magic = AGC_SHADER_RECORD_MAGIC;
    front->version = AGC_SHADER_RECORD_VERSION_GEN5;
    front->shader_type = is_gs ? kAgcShaderBinaryTypeGsFront
                               : kAgcShaderBinaryTypeHsFront;
    front->code = 0x0000AB1234567800ULL;
    front->user_data = 0x1122334455667788ULL;
    front->specials = (uint64_t)(uintptr_t)front_specials;
    front->sh_registers = (uint64_t)(uintptr_t)front_regs;
    front->num_sh_registers = 4;

    back->magic = AGC_SHADER_RECORD_MAGIC;
    back->version = AGC_SHADER_RECORD_VERSION_GEN5;
    back->shader_type = is_gs ? kAgcShaderBinaryTypeGsBack
                              : kAgcShaderBinaryTypeHsBack;
    back->user_data = 0x8877665544332211ULL;
    back->specials = (uint64_t)(uintptr_t)back_specials;
    back->sh_registers = (uint64_t)(uintptr_t)back_regs;
    back->num_sh_registers = 6;
}

static void run_fused_shader_success(bool is_gs, bool revision_0200) {
    AgcShaderRecord front, back, fused;
    AgcShaderSpecials front_specials, back_specials;
    AgcShaderRegister front_regs[4], back_regs[6], scratch[6] = {0};
    build_fused_shader_pair(&front, &back, &front_specials, &back_specials,
        front_regs, back_regs, is_gs);

    int32_t ret = revision_0200
        ? sceAgcFuseShaderHalves_0200(&fused, &front, &back, scratch)
        : sceAgcFuseShaderHalves(&fused, &front, &back, scratch);
    TEST_ASSERT_EQ(ret, AGC_OK, "FuseShaderHalves valid pair returns OK");
    TEST_ASSERT_EQ((uint32_t)fused.shader_type,
        (uint32_t)(is_gs ? kAgcShaderTypeEs : kAgcShaderTypeVs),
        "FuseShaderHalves sets firmware fused type 2/3");
    TEST_ASSERT_EQ(fused.sh_registers, (uint64_t)(uintptr_t)scratch,
        "FuseShaderHalves points at scratch SH copy");
    TEST_ASSERT_EQ(scratch[0].value, 0x11111111u,
        "FuseShaderHalves copies first checksum");
    TEST_ASSERT_EQ(scratch[1].value, 0x22222222u,
        "FuseShaderHalves copies second checksum");
    TEST_ASSERT_EQ(back_regs[0].value, 0xAAAAAAAAu,
        "FuseShaderHalves leaves back SH source unchanged with scratch");

    TEST_ASSERT_EQ(scratch[2].value & 0x3Fu, 3u,
        "FuseShaderHalves merges RSRC1 low field maximum");
    TEST_ASSERT_EQ((scratch[2].value >> (is_gs ? 29u : 28u)) & 0x3u,
        is_gs ? 2u : 3u, "FuseShaderHalves merges RSRC1 stage field");
    TEST_ASSERT_EQ((scratch[3].value >> 28u) & 0xFu,
        revision_0200 ? 1u : 15u,
        "FuseShaderHalves merges RSRC2 high field for export version");
    if (is_gs) {
        TEST_ASSERT_EQ((scratch[3].value >> 16u) & 0x3u, 3u,
            "FuseShaderHalves merges GS RSRC2 bits 16:17");
        TEST_ASSERT_EQ(scratch[3].value & 0x00040000u, 0x00040000u,
            "FuseShaderHalves copies GS RSRC2 bit 18");
    }
    TEST_ASSERT_EQ(scratch[3].value & 0x0800003Eu,
        front_regs[3].value & 0x0800003Eu,
        "FuseShaderHalves copies selected RSRC2 flags");
    TEST_ASSERT_EQ(scratch[4].value, 0x12345678u,
        "FuseShaderHalves patches front program low address");
    TEST_ASSERT_EQ(scratch[5].value, 0xA5A5A5ABu,
        "FuseShaderHalves patches front program high address byte");
    TEST_ASSERT_EQ(fused.user_data,
        revision_0200 ? 0u : front.user_data,
        "FuseShaderHalves applies export-specific user data behavior");
}

static void test_fused_shader_fuse_legacy_gs(void) {
    run_fused_shader_success(true, false);
}

static void test_fused_shader_fuse_legacy_hs(void) {
    run_fused_shader_success(false, false);
}

static void test_fused_shader_fuse_0200_gs(void) {
    run_fused_shader_success(true, true);
}

static void test_fused_shader_fuse_0200_hs(void) {
    run_fused_shader_success(false, true);
}

static void test_fused_shader_stage_mismatch(void) {
    AgcShaderRecord front, back, fused;
    AgcShaderSpecials front_specials, back_specials;
    AgcShaderRegister front_regs[4], back_regs[6], scratch[6] = {0};

    build_fused_shader_pair(&front, &back, &front_specials, &back_specials,
        front_regs, back_regs, true);
    front_specials.vgt_shader_stages_en.value = 1u << 22u;
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves(
        &fused, &front, &back, scratch), AGC_ERROR_SHADER_INVALID_HALVES,
        "Legacy GS rejects stage bit 22 mismatch");
    TEST_ASSERT_EQ((uint32_t)fused.shader_type, (uint32_t)kAgcShaderTypeEs,
        "Firmware copies back record before reporting GS stage mismatch");

    build_fused_shader_pair(&front, &back, &front_specials, &back_specials,
        front_regs, back_regs, false);
    front_specials.vgt_shader_stages_en.value = 1u << 21u;
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves_0200(
        &fused, &front, &back, scratch), AGC_ERROR_SHADER_INVALID_HALVES,
        "0200 HS rejects stage bit 21 mismatch");
}

static void test_fused_shader_missing_metadata(void) {
    AgcShaderRecord front, back, fused;
    AgcShaderSpecials front_specials, back_specials;
    AgcShaderRegister front_regs[4], back_regs[6], scratch[6] = {0};

    build_fused_shader_pair(&front, &back, &front_specials, &back_specials,
        front_regs, back_regs, true);
    front.specials = 0;
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves_0200(
        &fused, &front, &back, scratch), AGC_ERROR_SHADER_INVALID_HALVES,
        "FuseShaderHalves rejects missing Specials block safely");

    build_fused_shader_pair(&front, &back, &front_specials, &back_specials,
        front_regs, back_regs, true);
    front.num_sh_registers = 0;
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves_0200(
        &fused, &front, &back, scratch), AGC_ERROR_SHADER_INVALID_HALVES,
        "FuseShaderHalves rejects missing required SH registers safely");
}

static void test_fused_shader_fuse_invalid_types(void) {
    AgcShaderRecord front = {0};
    front.shader_type = kAgcShaderTypePs;  /* Wrong: PS, not GsFront/HsFront */
    AgcShaderRecord back = {0};
    back.shader_type = kAgcShaderBinaryTypeGsBack;

    AgcShaderRecord fused = {0};
    int32_t ret = sceAgcFuseShaderHalves(&fused, &front, &back, NULL);
    TEST_ASSERT_EQ(ret, AGC_ERROR_SHADER_INVALID_HALVES,
        "FuseShaderHalves invalid front type returns INVALID_HALVES");
}

static void test_fused_shader_fuse_null(void) {
    AgcShaderRecord rec = {0};
    rec.shader_type = kAgcShaderBinaryTypeGsFront;
    AgcShaderRecord fused = {0};
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves(NULL, &rec, &rec, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "FuseShaderHalves null result");
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves(&fused, NULL, &rec, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "FuseShaderHalves null front");
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves(&fused, &rec, NULL, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "FuseShaderHalves null back");
    TEST_ASSERT_EQ(sceAgcFuseShaderHalves_0200(NULL, &rec, &rec, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "FuseShaderHalves_0200 null result");
}

static void build_prim_shader(
    AgcShaderRecord *shader, AgcShaderSpecials *specials,
    uint32_t stages_value)
{
    memset(shader, 0, sizeof(*shader));
    memset(specials, 0, sizeof(*specials));
    shader->magic = AGC_SHADER_RECORD_MAGIC;
    shader->version = AGC_SHADER_RECORD_VERSION_GEN5;
    shader->specials = (uint64_t)(uintptr_t)specials;
    specials->ge_cntl = (AgcShaderSpecialRegister){AGC_REG_GE_CNTL, 0x11111111u};
    specials->vgt_shader_stages_en = (AgcShaderSpecialRegister){
        AGC_REG_VGT_SHADER_STAGES_EN, stages_value};
    specials->vgt_gs_out_prim_type = (AgcShaderSpecialRegister){
        AGC_REG_VGT_GS_OUT_PRIM_TYPE, 0x22222222u};
    specials->ge_user_vgpr_en = (AgcShaderSpecialRegister){
        AGC_REG_GE_USER_VGPR_EN, 0x33333333u};
}

static void test_create_prim_state_gs_enabled(void) {
    AgcShaderRecord geometry;
    AgcShaderSpecials specials;
    AgcShaderRegister cx[2] = {0};
    AgcShaderRegister uc[3] = {0};
    build_prim_shader(&geometry, &specials, 0x10000020u);

    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, NULL, &geometry, 4u),
        AGC_OK, "CreatePrimState GS-enabled returns OK");
    TEST_ASSERT_EQ(cx[0].offset, AGC_REG_VGT_SHADER_STAGES_EN,
        "CreatePrimState copies stage register");
    TEST_ASSERT_EQ(cx[0].value, 0x10000020u,
        "CreatePrimState copies stage value");
    TEST_ASSERT_EQ(cx[1].offset, AGC_REG_VGT_GS_OUT_PRIM_TYPE,
        "CreatePrimState copies GS output register");
    TEST_ASSERT_EQ(cx[1].value, 0x22222222u,
        "CreatePrimState copies GS output value");
    TEST_ASSERT_EQ(uc[0].offset, AGC_REG_GE_CNTL,
        "CreatePrimState copies GE_CNTL register");
    TEST_ASSERT_EQ(uc[0].value, 0x11111111u,
        "CreatePrimState copies GE_CNTL value");
    TEST_ASSERT_EQ(uc[1].offset, AGC_REG_GE_USER_VGPR_EN,
        "CreatePrimState copies GE_USER_VGPR_EN register");
    TEST_ASSERT_EQ(uc[1].value, 0x33333333u,
        "CreatePrimState copies GE_USER_VGPR_EN value");
    TEST_ASSERT_EQ(uc[2].offset, AGC_REG_VGT_PRIMITIVE_TYPE,
        "CreatePrimState emits VGT_PRIMITIVE_TYPE register");
    TEST_ASSERT_EQ(uc[2].value, 4u,
        "CreatePrimState emits input primitive value");
}

static void test_create_prim_state_primitive_lookup(void) {
    static const uint32_t expected[18] = {
        0u, 1u, 1u, 2u, 2u, 2u, 3u, 2u, 2u,
        1u, 1u, 2u, 2u, 2u, 2u, 2u, 4u, 1u,
    };
    AgcShaderRecord geometry;
    AgcShaderSpecials specials;
    AgcShaderRegister cx[2];
    AgcShaderRegister uc[3];
    build_prim_shader(&geometry, &specials, 0u);

    for (uint32_t primitive = 1; primitive <= 18; primitive++) {
        TEST_ASSERT_EQ(sceAgcCreatePrimState(
            cx, uc, NULL, &geometry, primitive), AGC_OK,
            "CreatePrimState primitive lookup returns OK");
        TEST_ASSERT_EQ(cx[1].offset, AGC_REG_VGT_GS_OUT_PRIM_TYPE,
            "CreatePrimState fallback output register");
        TEST_ASSERT_EQ(cx[1].value, expected[primitive - 1u],
            "CreatePrimState firmware primitive lookup value");
        TEST_ASSERT_EQ(uc[2].value, primitive,
            "CreatePrimState preserves raw input primitive");
    }

    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, NULL, &geometry, 0u),
        AGC_OK, "CreatePrimState primitive zero returns OK");
    TEST_ASSERT_EQ(cx[1].value, 2u,
        "CreatePrimState primitive zero uses firmware fallback");
    TEST_ASSERT_EQ(uc[2].value, 0u,
        "CreatePrimState primitive zero remains raw in UCONFIG");
    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, NULL, &geometry, 19u),
        AGC_OK, "CreatePrimState primitive 19 returns OK");
    TEST_ASSERT_EQ(cx[1].value, 2u,
        "CreatePrimState primitive 19 uses firmware fallback");
    TEST_ASSERT_EQ(uc[2].value, 19u,
        "CreatePrimState primitive 19 remains raw in UCONFIG");
}

static void test_create_prim_state_hull_merge(void) {
    AgcShaderRecord geometry, hull;
    AgcShaderSpecials geometry_specials, hull_specials;
    AgcShaderRegister cx[2];
    AgcShaderRegister uc[3];
    build_prim_shader(&geometry, &geometry_specials, 0x100u);
    build_prim_shader(&hull, &hull_specials, 0x200u);
    hull_specials.vgt_gs_out_prim_type =
        (AgcShaderSpecialRegister){0x777u, 0x88888888u};
    hull_specials.ge_user_vgpr_en =
        (AgcShaderSpecialRegister){0x999u, 0xAAAAAAAAu};

    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, &hull, &geometry, 7u),
        AGC_OK, "CreatePrimState hull merge returns OK");
    TEST_ASSERT_EQ(cx[0].value, 0x300u,
        "CreatePrimState ORs hull and geometry stage values");
    TEST_ASSERT_EQ(cx[1].offset, 0x777u,
        "CreatePrimState uses hull output pair when GS stays disabled");
    TEST_ASSERT_EQ(cx[1].value, 0x88888888u,
        "CreatePrimState copies hull output value");
    TEST_ASSERT_EQ(uc[1].offset, 0x999u,
        "CreatePrimState hull overrides user VGPR register");
    TEST_ASSERT_EQ(uc[1].value, 0xAAAAAAAAu,
        "CreatePrimState hull overrides user VGPR value");

    hull_specials.vgt_shader_stages_en.value = 1u << 5u;
    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, &hull, &geometry, 1u),
        AGC_OK, "CreatePrimState hull GS-enable returns OK");
    TEST_ASSERT_EQ(cx[0].value, 0x120u,
        "CreatePrimState hull sets GS-enable bit");
    TEST_ASSERT_EQ(cx[1].offset, AGC_REG_VGT_GS_OUT_PRIM_TYPE,
        "CreatePrimState retains geometry fallback pair after hull enables GS");
    TEST_ASSERT_EQ(cx[1].value, 0u,
        "CreatePrimState retains primitive lookup value after hull enables GS");
}

static void test_create_prim_state_optional_outputs_and_invalid(void) {
    AgcShaderRecord geometry, hull = {0};
    AgcShaderSpecials specials;
    AgcShaderRegister cx[2] = {0};
    AgcShaderRegister uc[3] = {0};
    build_prim_shader(&geometry, &specials, 0u);

    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, NULL, NULL, &geometry, 4u),
        AGC_OK, "CreatePrimState supports CX-only output");
    TEST_ASSERT_EQ(sceAgcCreatePrimState(NULL, uc, NULL, &geometry, 4u),
        AGC_OK, "CreatePrimState supports UCONFIG-only output");
    TEST_ASSERT_EQ(sceAgcCreatePrimState(NULL, NULL, NULL, NULL, 4u),
        AGC_OK, "CreatePrimState no outputs matches firmware no-op");
    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, NULL, NULL, 4u),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreatePrimState rejects missing geometry safely");
    geometry.specials = 0;
    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, NULL, &geometry, 4u),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreatePrimState rejects missing geometry Specials safely");
    build_prim_shader(&geometry, &specials, 0u);
    TEST_ASSERT_EQ(sceAgcCreatePrimState(cx, uc, &hull, &geometry, 4u),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreatePrimState rejects missing hull Specials safely");
}

static void test_update_prim_state(void) {
    static const uint32_t expected[18] = {
        0x80000180u, 0x00000011u, 0x20000180u, 0x00000012u,
        0x00000000u, 0x00000013u, 0x00000000u, 0x00000014u,
        0x00000000u, 0x00000015u, 0x00000000u, 0x0000001Au,
        0x00000000u, 0x0000001Bu, 0x00000000u, 0x0000001Cu,
        0x00000000u, 0x0000001Du,
    };
    AgcShaderRegister cx[2] = {{0u, 0u}, {0u, 0xA5A5A5A0u}};
    AgcShaderRegister uc[3] = {{0u, 0u}, {0u, 0u}, {0u, 0x5A5A5A40u}};

    for (uint32_t primitive = 1u; primitive <= 18u; primitive++) {
        cx[0].value = 0u;
        cx[1].value = 0xA5A5A5A0u;
        uc[2].value = 0x5A5A5A40u;
        TEST_ASSERT_EQ(sceAgcUpdatePrimState(cx, uc, primitive), AGC_OK,
            "UpdatePrimState table entry returns OK");
        TEST_ASSERT_EQ(cx[1].value,
            (0xA5A5A5A0u & ~0x7u) | expected[primitive - 1u],
            "UpdatePrimState applies firmware GS output table");
        TEST_ASSERT_EQ(uc[2].value,
            (0x5A5A5A40u & ~0x1Fu) | primitive,
            "UpdatePrimState replaces UCONFIG primitive bits");
    }

    cx[1].value = 0x12345670u;
    TEST_ASSERT_EQ(sceAgcUpdatePrimState(cx, NULL, 0u), AGC_OK,
        "UpdatePrimState zero primitive returns OK");
    TEST_ASSERT_EQ(cx[1].value, 0x12345672u,
        "UpdatePrimState zero primitive uses firmware fallback");
    cx[1].value = 0x12345670u;
    TEST_ASSERT_EQ(sceAgcUpdatePrimState(cx, NULL, 19u), AGC_OK,
        "UpdatePrimState out-of-table primitive returns OK");
    TEST_ASSERT_EQ(cx[1].value, 0x12345672u,
        "UpdatePrimState out-of-table primitive uses firmware fallback");

    cx[0].value = 0x24u;
    cx[1].value = 0xCAFEBABEu;
    TEST_ASSERT_EQ(sceAgcUpdatePrimState(cx, NULL, 4u), AGC_OK,
        "UpdatePrimState guarded CX state returns OK");
    TEST_ASSERT_EQ(cx[1].value, 0xCAFEBABEu,
        "UpdatePrimState preserves guarded CX state");
    TEST_ASSERT_EQ(sceAgcUpdatePrimState(NULL, NULL, 4u), AGC_OK,
        "UpdatePrimState accepts null outputs");
}

static uint32_t semantic_word(
    uint32_t id, uint32_t mapping, uint32_t flags)
{
    return (id & 0xFFu) | ((mapping & 0x1Fu) << 8u) | flags;
}

static void build_interpolant_shader(
    AgcShaderRecord *shader,
    AgcShaderSemantic *inputs, uint32_t num_inputs,
    AgcShaderSemantic *outputs, uint32_t num_outputs)
{
    memset(shader, 0, sizeof(*shader));
    shader->magic = AGC_SHADER_RECORD_MAGIC;
    shader->version = AGC_SHADER_RECORD_VERSION_GEN5;
    shader->input_semantics = (uint64_t)(uintptr_t)inputs;
    shader->output_semantics = (uint64_t)(uintptr_t)outputs;
    set_u32(shader->num_input_semantics, num_inputs);
    set_u32(shader->num_output_semantics, num_outputs);
}

static void test_create_interpolant_mapping_identity(void) {
    AgcShaderRegister regs[32] = {0};
    AgcShaderRegister legacy_regs[32] = {0};
    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, NULL, NULL),
        AGC_OK, "CreateInterpolantMapping null PS uses identity state");
    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping_0100(
        legacy_regs, NULL, NULL), AGC_OK,
        "CreateInterpolantMapping_0100 forwards to current ABI");
    TEST_ASSERT(memcmp(regs, legacy_regs, sizeof(regs)) == 0,
        "CreateInterpolantMapping_0100 emits the proven identity mapping");
    for (uint32_t i = 0; i < 32u; i++) {
        TEST_ASSERT_EQ(regs[i].offset,
            AGC_INTERPOLANT_REGISTER_DESCRIPTOR_BASE + i,
            "CreateInterpolantMapping emits raw descriptor offset");
        TEST_ASSERT_EQ(regs[i].value, i,
            "CreateInterpolantMapping emits identity value");
    }

    AgcShaderRecord empty_ps;
    build_interpolant_shader(&empty_ps, NULL, 0u, NULL, 0u);
    memset(regs, 0xA5, sizeof(regs));
    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, NULL, &empty_ps),
        AGC_OK, "CreateInterpolantMapping empty PS uses identity state");
    TEST_ASSERT_EQ(regs[31].value, 31u,
        "CreateInterpolantMapping fills final identity entry");
}

static void test_create_interpolant_mapping_flags(void) {
    AgcShaderSemantic gs_semantics[3] = {
        {semantic_word(7u, 5u, 0u)},
        {semantic_word(9u, 12u, 0u)},
        {semantic_word(10u, 4u, 1u << 20u)},
    };
    AgcShaderSemantic ps_semantics[4] = {
        {semantic_word(7u, 0u, 0u)},
        {semantic_word(8u, 0u, 2u << 28u)},
        {semantic_word(9u, 0u,
            (1u << 22u) | (1u << 24u) | (1u << 28u))},
        {semantic_word(10u, 0u,
            (1u << 20u) | (3u << 28u) | (2u << 30u))},
    };
    AgcShaderRecord gs, ps;
    AgcShaderRegister regs[32] = {0};
    build_interpolant_shader(&gs, NULL, 0u, gs_semantics, 3u);
    build_interpolant_shader(&ps, ps_semantics, 4u, NULL, 0u);

    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, &gs, &ps),
        AGC_OK, "CreateInterpolantMapping flag mapping returns OK");
    TEST_ASSERT_EQ(regs[0].value, 0x00000005u,
        "CreateInterpolantMapping uses matched GS hardware mapping");
    TEST_ASSERT_EQ(regs[1].value, 0x00000220u,
        "CreateInterpolantMapping uses missing-semantic default");
    TEST_ASSERT_EQ(regs[2].value, 0x0000052Cu,
        "CreateInterpolantMapping transforms flat/custom flags");
    TEST_ASSERT_EQ(regs[3].value, 0x01580304u,
        "CreateInterpolantMapping transforms F16/default-high flags");
    TEST_ASSERT_EQ(regs[4].offset,
        AGC_INTERPOLANT_REGISTER_DESCRIPTOR_BASE + 4u,
        "CreateInterpolantMapping starts identity tail at PS input count");
    TEST_ASSERT_EQ(regs[4].value, 4u,
        "CreateInterpolantMapping fills identity tail");
}

static void test_create_interpolant_mapping_all_entries(void) {
    AgcShaderSemantic inputs[32];
    AgcShaderSemantic outputs[32];
    AgcShaderRegister regs[32] = {0};
    AgcShaderRecord gs, ps;

    for (uint32_t i = 0; i < 32u; i++) {
        inputs[i].value = semantic_word(i, 0u, 0u);
        outputs[31u - i].value = semantic_word(i, 31u - i, 0u);
    }
    build_interpolant_shader(&gs, NULL, 0u, outputs, 32u);
    build_interpolant_shader(&ps, inputs, 32u, NULL, 0u);

    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, &gs, &ps),
        AGC_OK, "CreateInterpolantMapping accepts all 32 entries");
    for (uint32_t i = 0; i < 32u; i++) {
        TEST_ASSERT_EQ(regs[i].offset,
            AGC_INTERPOLANT_REGISTER_DESCRIPTOR_BASE + i,
            "CreateInterpolantMapping all-entry descriptor offset");
        TEST_ASSERT_EQ(regs[i].value, 31u - i,
            "CreateInterpolantMapping searches full output table");
    }
}

static void test_create_interpolant_mapping_invalid(void) {
    AgcShaderRecord gs, ps;
    AgcShaderSemantic input = {semantic_word(1u, 0u, 0u)};
    AgcShaderRegister regs[32] = {0};
    build_interpolant_shader(&ps, &input, 1u, NULL, 0u);
    build_interpolant_shader(&gs, NULL, 0u, NULL, 0u);

    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(NULL, &gs, &ps),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreateInterpolantMapping rejects null output safely");
    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, NULL, &ps),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreateInterpolantMapping rejects missing GS safely");
    ps.input_semantics = 0;
    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, &gs, &ps),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreateInterpolantMapping rejects missing PS semantics safely");
    build_interpolant_shader(&ps, &input, 33u, NULL, 0u);
    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, &gs, &ps),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreateInterpolantMapping rejects more than 32 PS inputs safely");
    build_interpolant_shader(&ps, &input, 1u, NULL, 0u);
    set_u32(gs.num_output_semantics, 1u);
    TEST_ASSERT_EQ(sceAgcCreateInterpolantMapping(regs, &gs, &ps),
        AGC_ERROR_INVALID_ARGUMENT,
        "CreateInterpolantMapping rejects missing GS semantics safely");
}

static void test_enhanced_interpolant_mapping_variants(void) {
    AgcShaderSemantic inputs[4] = {
        {semantic_word(1u, 0u, 0u) | AGC_SHADER_SEMANTIC_CUSTOM_MASK |
            (2u << 28u)},
        {semantic_word(2u, 0u, 0u) | (1u << 20u) | (1u << 30u)},
        {semantic_word(3u, 0u, 0u) | (2u << 20u) | (2u << 30u)},
        {semantic_word(4u, 0u, 0u) | (3u << 20u) | (3u << 30u) |
            AGC_SHADER_SEMANTIC_CUSTOM_MASK},
    };
    AgcShaderSemantic outputs[3] = {
        {semantic_word(1u, 5u, 0u)},
        {semantic_word(2u, 7u, 0u) | (1u << 20u)},
        {semantic_word(4u, 9u, 0u) | (3u << 20u)},
    };
    AgcShaderRegister create_regs[32];
    AgcShaderRegister update_regs[32];
    AgcShaderRecord gs, ps;

    memset(create_regs, 0xcc, sizeof(create_regs));
    memset(update_regs, 0xcc, sizeof(update_regs));
    build_interpolant_shader(&gs, NULL, 0u, outputs, 3u);
    build_interpolant_shader(&ps, inputs, 4u, NULL, 0u);

    TEST_ASSERT_EQ(sceAgcUnknownDbOlWdppb4o(create_regs, &gs, &ps),
        AGC_OK, "enhanced create interpolant mapping succeeds");
    TEST_ASSERT_EQ(create_regs[0].value, 0x00000625u,
        "enhanced mode zero descriptor");
    TEST_ASSERT_EQ(create_regs[1].value, 0x01380107u,
        "enhanced mode one descriptor");
    TEST_ASSERT_EQ(create_regs[2].value, 0x02480220u,
        "enhanced unmatched mode two descriptor");
    TEST_ASSERT_EQ(create_regs[3].value, 0x03680709u,
        "enhanced mode three descriptor");
    TEST_ASSERT_EQ(create_regs[4].offset,
        AGC_INTERPOLANT_REGISTER_DESCRIPTOR_BASE + 4u,
        "enhanced create starts identity tail at input count");
    TEST_ASSERT_EQ(create_regs[4].value, 4u,
        "enhanced create fills identity tail");

    TEST_ASSERT_EQ(sceAgcUnknownVieBRwlh1Lw(update_regs, &gs, &ps),
        AGC_OK, "enhanced update interpolant mapping succeeds");
    for (uint32_t i = 0u; i < 4u; i++) {
        TEST_ASSERT_EQ(update_regs[i].offset, create_regs[i].offset,
            "enhanced update descriptor offset matches create");
        TEST_ASSERT_EQ(update_regs[i].value, create_regs[i].value,
            "enhanced update descriptor value matches create");
    }
    TEST_ASSERT_EQ(update_regs[4].offset, 0xCCCCCCCCu,
        "enhanced update preserves tail offset");
    TEST_ASSERT_EQ(update_regs[4].value, 0xCCCCCCCCu,
        "enhanced update preserves tail value");

    build_interpolant_shader(&ps, NULL, 0u, NULL, 0u);
    update_regs[0].offset = 0x12345678u;
    update_regs[0].value = 0x87654321u;
    TEST_ASSERT_EQ(sceAgcUnknownVieBRwlh1Lw(update_regs, &gs, &ps),
        AGC_OK, "enhanced empty update succeeds");
    TEST_ASSERT_EQ(update_regs[0].offset, 0x12345678u,
        "enhanced empty update preserves offset");
    TEST_ASSERT_EQ(update_regs[0].value, 0x87654321u,
        "enhanced empty update preserves value");
}

static void test_ngg_compiler_record_pipeline_fixture(void)
{
    AgcShaderRecord front, back, fused, ps;
    AgcShaderSpecials front_specials, back_specials;
    AgcShaderRegister front_regs[4], back_regs[6], fused_regs[6] = {0};
    AgcShaderRegister prim_cx[2] = {0};
    AgcShaderRegister prim_uc[3] = {0};
    AgcShaderRegister interpolants[32] = {0};
    AgcShaderSemantic gs_outputs[1] = {
        {semantic_word(7u, 0u, 0u)},
    };
    AgcShaderSemantic ps_inputs[1] = {
        {semantic_word(7u, 0u, 0u)},
    };
    const uint32_t stages_en = (1u << 13u) | (1u << 25u);

    build_fused_shader_pair(
        &front, &back, &front_specials, &back_specials,
        front_regs, back_regs, true);

    front_specials.vgt_shader_stages_en =
        (AgcShaderSpecialRegister){AGC_REG_VGT_SHADER_STAGES_EN, stages_en};
    back_specials.ge_cntl =
        (AgcShaderSpecialRegister){AGC_REG_GE_CNTL, 0x00FE0080u};
    back_specials.vgt_shader_stages_en =
        (AgcShaderSpecialRegister){AGC_REG_VGT_SHADER_STAGES_EN, stages_en};
    back_specials.vgt_gs_out_prim_type =
        (AgcShaderSpecialRegister){AGC_REG_VGT_GS_OUT_PRIM_TYPE, 2u};
    back_specials.ge_user_vgpr_en =
        (AgcShaderSpecialRegister){AGC_REG_GE_USER_VGPR_EN, 0u};

    back.code = 0x0000CD1234000000ULL;
    back.output_semantics = (uint64_t)(uintptr_t)gs_outputs;
    set_u32(back.num_output_semantics, 1u);
    build_interpolant_shader(&ps, ps_inputs, 1u, NULL, 0u);

    TEST_ASSERT_EQ(sceAgcCreateShader(&front, front.shader_type), AGC_OK,
        "compiler fixture accepts GS-front record");
    TEST_ASSERT_EQ(sceAgcCreateShader(&back, back.shader_type), AGC_OK,
        "compiler fixture accepts GS-back record");
    TEST_ASSERT_EQ(
        sceAgcFuseShaderHalves_0200(
            &fused, &front, &back, fused_regs),
        AGC_OK,
        "compiler fixture fuses NGG shader records");
    TEST_ASSERT_EQ((uint32_t)fused.shader_type, (uint32_t)kAgcShaderTypeEs,
        "compiler fixture produces fused ES shader type");
    TEST_ASSERT_EQ(fused.code, back.code,
        "compiler fixture preserves NGG back-half program");
    TEST_ASSERT_EQ(fused.specials, (uint64_t)(uintptr_t)&back_specials,
        "compiler fixture preserves compiler-generated specials");
    TEST_ASSERT_EQ(fused.output_semantics,
        (uint64_t)(uintptr_t)gs_outputs,
        "compiler fixture preserves GS output semantics");

    TEST_ASSERT_EQ(
        sceAgcCreatePrimState(prim_cx, prim_uc, NULL, &fused, 4u),
        AGC_OK,
        "compiler fixture derives primitive state");
    TEST_ASSERT_EQ(prim_cx[0].offset, AGC_REG_VGT_SHADER_STAGES_EN,
        "compiler fixture emits NGG stage register");
    TEST_ASSERT_EQ(prim_cx[0].value, stages_en,
        "compiler fixture emits NGG passthrough stage value");
    TEST_ASSERT_EQ(prim_uc[0].offset, AGC_REG_GE_CNTL,
        "compiler fixture emits GE_CNTL");
    TEST_ASSERT_EQ(prim_uc[0].value, 0x00FE0080u,
        "compiler fixture preserves compiler GE_CNTL value");

    TEST_ASSERT_EQ(
        sceAgcCreateInterpolantMapping(interpolants, &fused, &ps),
        AGC_OK,
        "compiler fixture links GS outputs to PS inputs");
    TEST_ASSERT_EQ(interpolants[0].offset,
        AGC_INTERPOLANT_REGISTER_DESCRIPTOR_BASE,
        "compiler fixture emits first interpolant descriptor");
    TEST_ASSERT_EQ(interpolants[0].value, 0u,
        "compiler fixture maps PS input zero to GS parameter zero");
}

void test_suite_shader(void) {
    TEST_SUITE("Shader Record");
    TEST_RUN(test_shader_record_invalid_null);
    TEST_RUN(test_shader_record_invalid_magic);
    TEST_RUN(test_shader_record_invalid_version);
    TEST_RUN(test_shader_record_invalid_type);
    TEST_RUN(test_shader_record_layout);
    TEST_RUN(test_shader_specials_struct_layout);
    TEST_RUN(test_shader_userdata_struct_size);
    TEST_RUN(test_shader_specials_typed_accessor);
    TEST_RUN(test_shader_userdata_typed_accessor);
    TEST_RUN(test_shader_register_value_accessors);
    TEST_RUN(test_get_gs_prim_payload);
    TEST_RUN(test_get_gs_oversubscription);
    TEST_RUN(test_shader_null_sub_blocks);
    /* Shader linking (sceAgcShaderLinkHsGs) */
    TEST_RUN(test_shader_link_hs_cs_success);
    TEST_RUN(test_shader_link_ls_cs_success);
    TEST_RUN(test_shader_link_invalid_source_type);
    TEST_RUN(test_shader_link_invalid_cs_type);
    TEST_RUN(test_shader_link_null_pointers);
    TEST_RUN(test_shader_link_copy_exact);
    TEST_RUN(test_shader_type_macros_match_enum);
    /* Fused shader support (sceAgcGetFusedShaderSize / sceAgcFuseShaderHalves) */
    TEST_RUN(test_fused_shader_get_size_gs);
    TEST_RUN(test_fused_shader_get_size_hs);
    TEST_RUN(test_fused_shader_get_size_invalid_types);
    TEST_RUN(test_fused_shader_get_size_null);
    TEST_RUN(test_fused_shader_fuse_legacy_gs);
    TEST_RUN(test_fused_shader_fuse_legacy_hs);
    TEST_RUN(test_fused_shader_fuse_0200_gs);
    TEST_RUN(test_fused_shader_fuse_0200_hs);
    TEST_RUN(test_fused_shader_stage_mismatch);
    TEST_RUN(test_fused_shader_missing_metadata);
    TEST_RUN(test_fused_shader_fuse_invalid_types);
    TEST_RUN(test_fused_shader_fuse_null);
    /* Primitive state builder (FW 5.50 D9sr1xGUriE). */
    TEST_RUN(test_create_prim_state_gs_enabled);
    TEST_RUN(test_create_prim_state_primitive_lookup);
    TEST_RUN(test_create_prim_state_hull_merge);
    TEST_RUN(test_create_prim_state_optional_outputs_and_invalid);
    TEST_RUN(test_update_prim_state);
    /* Interpolant mapping builder (FW 5.50 pdEV7bI6COI). */
    TEST_RUN(test_create_interpolant_mapping_identity);
    TEST_RUN(test_create_interpolant_mapping_flags);
    TEST_RUN(test_create_interpolant_mapping_all_entries);
    TEST_RUN(test_create_interpolant_mapping_invalid);
    TEST_RUN(test_enhanced_interpolant_mapping_variants);
    /* Synthetic compiler ES+GS/NGG record pipeline contract. */
    TEST_RUN(test_ngg_compiler_record_pipeline_fixture);
}
