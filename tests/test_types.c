#include "test.h"
#include "agc_types.h"
#include "agc_error.h"
#include "agc_pm4.h"
#include "agc_nids.h"

static void test_draw_flags_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcDrawFlags), 4, "AgcDrawFlags should be 4 bytes");
}

static void test_context_state_size(void) {
    TEST_ASSERT_EQ(sizeof(AgcContextState), 2048, "AgcContextState should be 2048 bytes");
}

static void test_error_codes(void) {
    TEST_ASSERT_EQ(AGC_OK, 0, "AGC_OK should be 0");
    TEST_ASSERT(AGC_ERROR_INVALID_ARGUMENT < 0, "Error codes should be negative");
    TEST_ASSERT(AGC_ERROR_CB_OVERFLOW < 0, "CB errors should be negative");
}

static void test_error_string(void) {
    const char* s = agcErrorString(AGC_OK);
    TEST_ASSERT(s != NULL, "agcErrorString should return non-NULL");
    TEST_ASSERT(strcmp(s, "AGC_OK") == 0, "AGC_OK string should match");

    s = agcErrorString(AGC_ERROR_INVALID_ARGUMENT);
    TEST_ASSERT(strcmp(s, "AGC_ERROR_INVALID_ARGUMENT") == 0, "error string match");

    s = agcErrorString(0x12345678);
    TEST_ASSERT(strcmp(s, "AGC_ERROR_UNKNOWN") == 0, "unknown error string");
}

static void test_pm4_header(void) {
    uint32_t hdr = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_FLIP, 6);
    TEST_ASSERT_EQ(agcPm4Type(hdr), AGC_PM4_TYPE3, "PM4 type should be 3");
    TEST_ASSERT_EQ(agcPm4Length(hdr), 6, "PM4 length should decode from bits 29:16");
    TEST_ASSERT_EQ(agcPm4Opcode(hdr), AGC_PM4_OP_NOP, "opcode should be NOP");
    TEST_ASSERT_EQ(agcPm4Subcommand(hdr), AGC_PM4_SUB_FLIP, "subcommand should be flip");
    TEST_ASSERT_EQ(hdr, 0xC004105Cu, "observation/RPCSX PM4 header layout");
}

static void test_known_nids(void) {
    TEST_ASSERT(strcmp(AGC_NID_SCE_AGC_CB_NOP, "LtTouSCZjHM") == 0, "sceAgcCbNop NID");
    TEST_ASSERT(strcmp(AGC_NID_SCE_AGC_VSH_DCB_SET_FLIP, "YUeqkyT7mEQ") == 0, "sceAgcVshDcbSetFlip NID");
    TEST_ASSERT(strcmp(AGC_NID_SCE_AGC_DRIVER_SUBMIT_DCB, "UglJIZjGssM") == 0, "submit DCB NID");
}

void test_suite_types(void) {
    TEST_SUITE("Types & Constants");
    TEST_RUN(test_draw_flags_size);
    TEST_RUN(test_context_state_size);
    TEST_RUN(test_error_codes);
    TEST_RUN(test_error_string);
    TEST_RUN(test_pm4_header);
    TEST_RUN(test_known_nids);
}
