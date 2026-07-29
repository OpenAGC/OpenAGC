#include "test.h"
#include "agc_context.h"
#include "agc_error.h"
#include "agcdriver.h"

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
    static AgcRegisterDefaultValue regs[257];
    static AgcRegisterDefaultsGroup group = {
        kAgcRegisterDefaultSpaceCx, 0, 0x12345678, 257, regs
    };
    size_t size = agcRegisterDefaultsComputeSize(1, 1, 1, 1);
    uint8_t *blob = (uint8_t *)malloc(size);
    TEST_ASSERT(blob != NULL, "Allocated blob");
    int32_t ret = agcRegisterDefaultsBuild(blob, size, (uint64_t)(uintptr_t)blob, &group, 1, 1, 1, 1);
    TEST_ASSERT(ret != AGC_OK, "Group with >256 registers rejected");
    free(blob);
}

static void test_register_defaults_fw550_tables(void) {
    uint32_t primary_count = 0;
    uint32_t internal_count = 0;
    const AgcRegisterDefaultsGroup *primary = agcRegisterDefaultsGetPrimaryGroups(&primary_count);
    const AgcRegisterDefaultsGroup *internal = agcRegisterDefaultsGetInternalGroups(&internal_count);

    /* agcRegisterDefaultsGetPrimaryGroups now returns the complete v8 data
     * (127 groups, 703 registers) instead of the old incomplete data. */
    TEST_ASSERT(primary != NULL, "Primary table exists");
    TEST_ASSERT(internal != NULL, "Internal table exists");
    TEST_ASSERT_EQ(primary_count, 127u, "Primary group count (v8)");
    TEST_ASSERT_EQ(internal_count, 22u, "Internal group count (v8)");
    TEST_ASSERT_EQ(internal[0].type_hash, 0x8FB4EDB5u, "Internal group 0 hash");

    /* Verify blob sizes can be computed for the v8 data. */
    size_t primary_size = agcRegisterDefaultsComputeSize(
        primary_count, AGC_PRIMARY_CX_LENGTH, AGC_PRIMARY_SH_LENGTH, AGC_PRIMARY_UC_LENGTH);
    size_t internal_size = agcRegisterDefaultsComputeSize(
        internal_count, AGC_INTERNAL_CX_LENGTH, AGC_INTERNAL_SH_LENGTH, AGC_INTERNAL_UC_LENGTH);
    TEST_ASSERT(primary_size > 0, "Primary blob size > 0");
    TEST_ASSERT(internal_size > 0, "Internal blob size > 0");
}

static void test_register_defaults_v8(void) {
    uint32_t primary_count = 0;
    uint32_t internal_count = 0;
    const AgcRegisterDefaultsGroup *primary = agcRegisterDefaultsV8GetPrimaryGroups(&primary_count);
    const AgcRegisterDefaultsGroup *internal = agcRegisterDefaultsV8GetInternalGroups(&internal_count);

    TEST_ASSERT(primary != NULL, "the reference v8 primary table exists");
    TEST_ASSERT(internal != NULL, "the reference v8 internal table exists");
    TEST_ASSERT_EQ(primary_count, 127u, "the reference v8 primary group count");
    TEST_ASSERT_EQ(internal_count, 22u, "the reference v8 internal group count");

    /* Count total registers across all primary groups */
    uint32_t total_primary_regs = 0;
    for (uint32_t i = 0; i < primary_count; i++)
        total_primary_regs += primary[i].register_count;
    TEST_ASSERT_EQ(total_primary_regs, 703u, "the reference v8 primary total registers");

    /* Count total registers across all internal groups */
    uint32_t total_internal_regs = 0;
    for (uint32_t i = 0; i < internal_count; i++)
        total_internal_regs += internal[i].register_count;
    TEST_ASSERT_EQ(total_internal_regs, 25u, "the reference v8 internal total registers");

    /* Verify some known non-zero values from the reference */
    /* Internal CX group 0: offset 0x00E, value 0x00000002 */
    TEST_ASSERT_EQ(internal[0].space, kAgcRegisterDefaultSpaceCx, "v8 internal[0] space");
    TEST_ASSERT_EQ(internal[0].registers[0].offset, 0x00Eu, "v8 internal[0] reg offset");
    TEST_ASSERT_EQ(internal[0].registers[0].value, 0x00000002u, "v8 internal[0] reg value");

    /* Internal SH group 0: offset 0x216, value 0xFFFFFFFF */
    TEST_ASSERT_EQ(internal[4].space, kAgcRegisterDefaultSpaceSh, "v8 internal[4] space");
    TEST_ASSERT_EQ(internal[4].registers[0].offset, 0x216u, "v8 internal[4] reg offset");
    TEST_ASSERT_EQ(internal[4].registers[0].value, 0xFFFFFFFFu, "v8 internal[4] reg value");
}

static void test_register_defaults_v10_blob_layout(void) {
    uint32_t primary_count = 0;
    uint32_t internal_count = 0;
    const AgcRegisterDefaultsGroup *primary =
        agcRegisterDefaultsGetPrimaryGroupsForVersion(
            AGC_REGISTER_DEFAULTS_VERSION_12, &primary_count);
    const AgcRegisterDefaultsGroup *internal =
        agcRegisterDefaultsGetInternalGroupsForVersion(
            AGC_REGISTER_DEFAULTS_VERSION_12, &internal_count);

    TEST_ASSERT(primary != NULL, "version 12 maps to the v10 primary table");
    TEST_ASSERT(internal != NULL, "version 12 maps to the v10 internal table");
    TEST_ASSERT_EQ(primary_count, 128u, "v10 primary group count");
    TEST_ASSERT_EQ(internal_count, 28u, "v10 internal group count");

    size_t primary_size = agcRegisterDefaultsComputeSize(
        primary_count,
        AGC_REGISTER_DEFAULTS_V10_PRIMARY_CX_LENGTH,
        AGC_REGISTER_DEFAULTS_V10_PRIMARY_SH_LENGTH,
        AGC_REGISTER_DEFAULTS_V10_PRIMARY_UC_LENGTH);
    size_t internal_size = agcRegisterDefaultsComputeSize(
        internal_count,
        AGC_REGISTER_DEFAULTS_V10_INTERNAL_CX_LENGTH,
        AGC_REGISTER_DEFAULTS_V10_INTERNAL_SH_LENGTH,
        AGC_REGISTER_DEFAULTS_V10_INTERNAL_UC_LENGTH);

    TEST_ASSERT(primary_size <= 0x41000u, "v10 primary blob fits the DDID slot");
    TEST_ASSERT(internal_size > 0xC000u, "v10 internal blob exceeds the v8 slot");
    TEST_ASSERT(internal_size <= 0xF000u, "v10 internal blob fits its DDID slot");

    uint8_t *primary_blob = (uint8_t *)malloc(primary_size);
    uint8_t *internal_blob = (uint8_t *)malloc(internal_size);
    TEST_ASSERT(primary_blob != NULL, "allocated v10 primary blob");
    TEST_ASSERT(internal_blob != NULL, "allocated v10 internal blob");
    if (primary_blob != NULL && internal_blob != NULL) {
        TEST_ASSERT_EQ(agcRegisterDefaultsBuild(
            primary_blob, primary_size, (uint64_t)(uintptr_t)primary_blob,
            primary, primary_count,
            AGC_REGISTER_DEFAULTS_V10_PRIMARY_CX_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_PRIMARY_SH_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_PRIMARY_UC_LENGTH),
            AGC_OK, "v10 primary blob builds with exact dimensions");
        TEST_ASSERT_EQ(agcRegisterDefaultsBuild(
            internal_blob, internal_size, (uint64_t)(uintptr_t)internal_blob,
            internal, internal_count,
            AGC_REGISTER_DEFAULTS_V10_INTERNAL_CX_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_INTERNAL_SH_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_INTERNAL_UC_LENGTH),
            AGC_OK, "v10 internal blob builds with exact dimensions");
    }
    free(internal_blob);
    free(primary_blob);
}

/* Version selection tests — verify all versions return valid data */
static void test_register_defaults_version_selection(void) {
    /* Version 0 (also used for 1, 2, 3) */
    uint32_t pub_count = 0, int_count = 0;
    const AgcRegisterDefaultsGroup *pub0 = agcRegisterDefaultsGetPrimaryGroupsForVersion(0, &pub_count);
    const AgcRegisterDefaultsGroup *int0 = agcRegisterDefaultsGetInternalGroupsForVersion(0, &int_count);
    TEST_ASSERT(pub0 != 0, "v0 primary groups returned");
    TEST_ASSERT(int0 != 0, "v0 internal groups returned");
    TEST_ASSERT(pub_count == 150, "v0 primary group count = 150");
    TEST_ASSERT(int_count == 15, "v0 internal group count = 15");

    /* Version 1, 2, 3 should map to v0 */
    uint32_t pub1_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(3, &pub1_count);
    TEST_ASSERT(pub1_count == 150, "v3 maps to v0 (150 groups)");

    /* Version 4 */
    uint32_t pub4_count = 0, int4_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(4, &pub4_count);
    agcRegisterDefaultsGetInternalGroupsForVersion(4, &int4_count);
    TEST_ASSERT(pub4_count == 150, "v4 primary group count = 150");
    TEST_ASSERT(int4_count == 19, "v4 internal group count = 19");

    /* Version 5 (also used for 6) */
    uint32_t pub5_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(5, &pub5_count);
    TEST_ASSERT(pub5_count == 156, "v5 primary group count = 156");

    /* Version 6 maps to v5 */
    uint32_t pub6_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(6, &pub6_count);
    TEST_ASSERT(pub6_count == 156, "v6 maps to v5 (156 groups)");

    /* Version 7 */
    uint32_t pub7_count = 0, int7_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(7, &pub7_count);
    agcRegisterDefaultsGetInternalGroupsForVersion(7, &int7_count);
    TEST_ASSERT(pub7_count == 127, "v7 primary group count = 127");
    TEST_ASSERT(int7_count == 22, "v7 internal group count = 22");

    /* Version 8 */
    uint32_t pub8_count = 0, int8_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(8, &pub8_count);
    agcRegisterDefaultsGetInternalGroupsForVersion(8, &int8_count);
    TEST_ASSERT(pub8_count == 127, "v8 primary group count = 127");
    TEST_ASSERT(int8_count == 22, "v8 internal group count = 22");

    /* Version 9 */
    uint32_t pub9_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(9, &pub9_count);
    TEST_ASSERT(pub9_count == 127, "v9 primary group count = 127");

    /* Version 10 */
    uint32_t pub10_count = 0, int10_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(10, &pub10_count);
    agcRegisterDefaultsGetInternalGroupsForVersion(10, &int10_count);
    TEST_ASSERT(pub10_count == 128, "v10 primary group count = 128");
    TEST_ASSERT(int10_count == 28, "v10 internal group count = 28");

    /* Version 11 */
    uint32_t pub11_count = 0, int11_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(11, &pub11_count);
    agcRegisterDefaultsGetInternalGroupsForVersion(11, &int11_count);
    TEST_ASSERT(pub11_count == 137, "v11 primary group count = 137");
    TEST_ASSERT(int11_count == 24, "v11 internal group count = 24");

    /* Version 12 maps to v10 */
    uint32_t pub12_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(12, &pub12_count);
    TEST_ASSERT(pub12_count == 128, "v12 maps to v10 (128 groups)");

    /* Version > 12 falls back to v11 */
    uint32_t pub99_count = 0;
    agcRegisterDefaultsGetPrimaryGroupsForVersion(99, &pub99_count);
    TEST_ASSERT(pub99_count == 137, "v99 falls back to v11 (137 groups)");
}

/* sceAgcGetRegisterDefaults2 returns a pointer to an AgcRegisterDefaults structure */
static void test_register_defaults_get_defaults2(void) {
    /* Version 8 (FW 5.50) */
    AgcRegisterDefaults *defaults8 = (AgcRegisterDefaults *)sceAgcGetRegisterDefaults2(8);
    TEST_ASSERT(defaults8 != 0, "GetRegisterDefaults2(8) returns non-NULL");
    TEST_ASSERT(defaults8->count == 127, "GetRegisterDefaults2(8) count = 127");
    TEST_ASSERT(defaults8->types != 0, "GetRegisterDefaults2(8) types non-NULL");

    /* Internal version */
    AgcRegisterDefaults *int_defaults8 = (AgcRegisterDefaults *)sceAgcGetRegisterDefaults2Internal(8);
    TEST_ASSERT(int_defaults8 != 0, "GetRegisterDefaults2Internal(8) returns non-NULL");
    TEST_ASSERT(int_defaults8->count == 22, "GetRegisterDefaults2Internal(8) count = 22");

    /* Version 0 */
    AgcRegisterDefaults *defaults0 = (AgcRegisterDefaults *)sceAgcGetRegisterDefaults2(0);
    TEST_ASSERT(defaults0 != 0, "GetRegisterDefaults2(0) returns non-NULL");
    TEST_ASSERT(defaults0->count == 150, "GetRegisterDefaults2(0) count = 150");

    /* Version > 12 falls back to v11 */
    AgcRegisterDefaults *defaults99 = (AgcRegisterDefaults *)sceAgcGetRegisterDefaults2(99);
    TEST_ASSERT(defaults99 != 0, "GetRegisterDefaults2(99) returns non-NULL (fallback to v11)");
    TEST_ASSERT(defaults99->count == 137, "GetRegisterDefaults2(99) count = 137 (v11)");

    /* Caching: second call returns same pointer */
    AgcRegisterDefaults *defaults8_again = (AgcRegisterDefaults *)sceAgcGetRegisterDefaults2(8);
    TEST_ASSERT(defaults8 == defaults8_again, "GetRegisterDefaults2(8) cached on second call");

    /* Verify types array has correct format */
    TEST_ASSERT(defaults8->types[0] != 0, "GetRegisterDefaults2(8) types[0] hash non-zero");
    TEST_ASSERT(defaults8->types[2] == 0, "GetRegisterDefaults2(8) types[2] reserved = 0");
}

void test_suite_register_defaults(void) {
    TEST_SUITE("Register Defaults");
    TEST_RUN(test_register_defaults_compute_size);
    TEST_RUN(test_register_defaults_build);
    TEST_RUN(test_register_defaults_build_invalid);
    TEST_RUN(test_register_defaults_build_too_many_regs);
    TEST_RUN(test_register_defaults_fw550_tables);
    TEST_RUN(test_register_defaults_v8);
    TEST_RUN(test_register_defaults_v10_blob_layout);
    TEST_RUN(test_register_defaults_version_selection);
    TEST_RUN(test_register_defaults_get_defaults2);
}
