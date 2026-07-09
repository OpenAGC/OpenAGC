#include "test.h"
#include "agc_context.h"
#include "agc_error.h"

#include <stdint.h>

static const AgcRegisterDefaultValue s_cx_regs[] = {
    {0x100, 0xAABBCCDD},
    {0x101, 0x11223344},
};

static const AgcRegisterDefaultValue s_sh_regs[] = {
    {0x200, 0xDEADBEEF},
};

static const AgcRegisterDefaultValue s_uc_regs[] = {
    {0x300, 0xCAFEBABE},
    {0x301, 0x00000000},
    {0x302, 0xFFFFFFFF},
};

static const AgcRegisterDefaultsGroup s_groups[] = {
    {kAgcRegisterDefaultSpaceCx, 0, 0x11111111, 2, s_cx_regs},
    {kAgcRegisterDefaultSpaceSh, 1, 0x22222222, 1, s_sh_regs},
    {kAgcRegisterDefaultSpaceUc, 2, 0x33333333, 3, s_uc_regs},
};

static void test_register_defaults_compute_size(void) {
    size_t size = agcRegisterDefaultsComputeSize(3, 4, 4, 4);
    TEST_ASSERT(size >= 0x40 + 4 * 8 * 3 + 3 * 12 + 3 * 128, "Size covers tables + blocks");
}

static void test_register_defaults_build(void) {
    size_t size = agcRegisterDefaultsComputeSize(3, 4, 4, 4);
    uint8_t *blob = (uint8_t *)malloc(size);
    TEST_ASSERT(blob != NULL, "Allocated blob");

    uint64_t base = (uint64_t)(uintptr_t)blob;
    int32_t ret = agcRegisterDefaultsBuild(blob, size, base, s_groups, 3, 4, 4, 4);
    TEST_ASSERT_EQ(ret, AGC_OK, "Build succeeded");

    const AgcRegisterDefaultsHeader *hdr = agcRegisterDefaultsGetHeader(blob);
    TEST_ASSERT(hdr != NULL, "Got header");
    TEST_ASSERT_EQ(hdr->group_count, 3u, "Group count");
    TEST_ASSERT(hdr->cx_table == base + 0x40, "CX table pointer");
    TEST_ASSERT(hdr->sh_table == base + 0x40 + 4 * 8, "SH table pointer");
    TEST_ASSERT(hdr->uc_table == base + 0x40 + 4 * 8 * 2, "UC table pointer");
    TEST_ASSERT(hdr->type_table == base + 0x40 + 4 * 8 * 3, "Type table pointer");

    const uint64_t *cx_table = agcRegisterDefaultsGetCxTable(blob);
    TEST_ASSERT(cx_table != NULL, "Got CX table");
    TEST_ASSERT(cx_table[0] != 0, "CX[0] points to block");

    const AgcRegisterDefaultsTypeEntry *types = agcRegisterDefaultsGetTypeTable(blob);
    TEST_ASSERT(types != NULL, "Got type table");
    TEST_ASSERT_EQ(types[0].type_hash, 0x11111111u, "Group 0 type hash");
    TEST_ASSERT_EQ(types[0].packed_index, 0u, "Group 0 packed index (space 0, index 0)");
    TEST_ASSERT_EQ(types[1].type_hash, 0x22222222u, "Group 1 type hash");
    TEST_ASSERT_EQ(types[1].packed_index, 5u, "Group 1 packed index (space 1, index 1)");
    TEST_ASSERT_EQ(types[2].type_hash, 0x33333333u, "Group 2 type hash");
    TEST_ASSERT_EQ(types[2].packed_index, 10u, "Group 2 packed index (space 2, index 2)");

    const AgcRegisterDefaultValue *block0 = agcRegisterDefaultsGetBlock(blob, 0);
    TEST_ASSERT(block0 != NULL, "Got block 0");
    TEST_ASSERT_EQ(block0[0].offset, 0x100u, "Block 0 reg 0 offset");
    TEST_ASSERT_EQ(block0[0].value, 0xAABBCCDDu, "Block 0 reg 0 value");
    TEST_ASSERT_EQ(block0[1].offset, 0x101u, "Block 0 reg 1 offset");
    TEST_ASSERT_EQ(block0[1].value, 0x11223344u, "Block 0 reg 1 value");

    const AgcRegisterDefaultValue *block1 = agcRegisterDefaultsGetBlock(blob, 1);
    TEST_ASSERT(block1 != NULL, "Got block 1");
    TEST_ASSERT_EQ(block1[0].offset, 0x200u, "Block 1 reg 0 offset");
    TEST_ASSERT_EQ(block1[0].value, 0xDEADBEEFu, "Block 1 reg 0 value");

    const AgcRegisterDefaultValue *block2 = agcRegisterDefaultsGetBlock(blob, 2);
    TEST_ASSERT(block2 != NULL, "Got block 2");
    TEST_ASSERT_EQ(block2[2].offset, 0x302u, "Block 2 reg 2 offset");
    TEST_ASSERT_EQ(block2[2].value, 0xFFFFFFFFu, "Block 2 reg 2 value");

    free(blob);
}

static void test_register_defaults_build_invalid(void) {
    uint8_t blob[8];
    int32_t ret = agcRegisterDefaultsBuild(blob, sizeof(blob), 0, s_groups, 3, 4, 4, 4);
    TEST_ASSERT(ret != AGC_OK, "Undersized buffer rejected");
}

static void test_register_defaults_build_too_many_regs(void) {
    static AgcRegisterDefaultValue regs[17];
    static AgcRegisterDefaultsGroup group = {
        kAgcRegisterDefaultSpaceCx, 0, 0x12345678, 17, regs
    };
    size_t size = agcRegisterDefaultsComputeSize(1, 1, 1, 1);
    uint8_t *blob = (uint8_t *)malloc(size);
    TEST_ASSERT(blob != NULL, "Allocated blob");
    int32_t ret = agcRegisterDefaultsBuild(blob, size, (uint64_t)(uintptr_t)blob, &group, 1, 1, 1, 1);
    TEST_ASSERT(ret != AGC_OK, "Group with >16 registers rejected");
    free(blob);
}

static void test_register_defaults_fw550_tables(void) {
    uint32_t primary_count = 0;
    uint32_t internal_count = 0;
    const AgcRegisterDefaultsGroup *primary = agcRegisterDefaultsGetPrimaryGroups(&primary_count);
    const AgcRegisterDefaultsGroup *internal = agcRegisterDefaultsGetInternalGroups(&internal_count);

    TEST_ASSERT(primary != NULL, "Primary table exists");
    TEST_ASSERT(internal != NULL, "Internal table exists");
    TEST_ASSERT_EQ(primary_count, 13u, "Primary group count");
    TEST_ASSERT_EQ(internal_count, 22u, "Internal group count");
    TEST_ASSERT_EQ(primary[0].type_hash, 0x0BC65DA4u, "Primary group 0 hash");
    TEST_ASSERT_EQ(internal[0].type_hash, 0x8FB4EDB5u, "Internal group 0 hash");

    size_t primary_size = agcRegisterDefaultsComputeSize(
        primary_count, AGC_PRIMARY_CX_LENGTH, AGC_PRIMARY_SH_LENGTH, AGC_PRIMARY_UC_LENGTH);
    size_t internal_size = agcRegisterDefaultsComputeSize(
        internal_count, AGC_INTERNAL_CX_LENGTH, AGC_INTERNAL_SH_LENGTH, AGC_INTERNAL_UC_LENGTH);
    TEST_ASSERT_EQ(primary_size, 0xB58u, "Primary blob size");
    TEST_ASSERT_EQ(internal_size, 0xCF8u, "Internal blob size");
}

void test_suite_register_defaults(void) {
    TEST_SUITE("Register Defaults");
    TEST_RUN(test_register_defaults_compute_size);
    TEST_RUN(test_register_defaults_build);
    TEST_RUN(test_register_defaults_build_invalid);
    TEST_RUN(test_register_defaults_build_too_many_regs);
    TEST_RUN(test_register_defaults_fw550_tables);
}
