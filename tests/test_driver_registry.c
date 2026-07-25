#include "test.h"

#include "agc_error.h"
#include "driver_ops.h"
#include "driver_registry.h"

typedef struct FakeFirmwareQuery {
    int32_t result;
    uint32_t raw_version;
    uint32_t calls;
} FakeFirmwareQuery;

static int32_t fake_query(void *context, uint32_t *raw_version)
{
    FakeFirmwareQuery *query = (FakeFirmwareQuery *)context;

    ++query->calls;
    if (query->result == AGC_OK)
        *raw_version = query->raw_version;
    return query->result;
}

static void test_firmware_normalization(void)
{
    AgcFirmwareVersion version = agcFirmwareNormalize(0x05500100u);

    TEST_ASSERT_EQ(version.raw, 0x05500100u, "raw firmware retained");
    TEST_ASSERT_EQ(version.major, 5u, "firmware major normalized");
    TEST_ASSERT_EQ(version.minor, 50u, "firmware minor normalized");
    TEST_ASSERT_EQ(version.patch, 1u, "firmware patch normalized");
}

static void test_exact_alias_and_capability_selection(void)
{
    static const uint32_t aliases[] = {0x05500000u, 0x05500001u};
    static const AgcDriverRegistryEntry registry[] = {
        {
            "test-fw550",
            aliases,
            sizeof(aliases) / sizeof(aliases[0]),
            AGC_BACKEND_CAP_NATIVE_SUBMIT | AGC_BACKEND_CAP_COMPUTE,
            &agcGenericDriverOps
        }
    };
    FakeFirmwareQuery query = {AGC_OK, 0x05500001u, 0};
    AgcFirmwareDetector detector = {&query, fake_query};
    AgcFirmwareVersion version;
    const AgcDriverOps *ops = NULL;

    TEST_ASSERT_EQ(agcDriverSelectFromRegistry(&detector, registry, 1,
        AGC_BACKEND_CAP_COMPUTE, &version, &ops), AGC_OK,
        "explicit firmware alias selects backend");
    TEST_ASSERT(ops == &agcGenericDriverOps, "selected operations returned");
    TEST_ASSERT_EQ(version.raw, 0x05500001u, "detected raw version returned");
    TEST_ASSERT_EQ(query.calls, 1u, "firmware detector called exactly once");

    ops = NULL;
    TEST_ASSERT_EQ(agcDriverSelectFromRegistry(&detector, registry, 1,
        AGC_BACKEND_CAP_GRAPHICS, NULL, &ops), AGC_ERROR_NOT_SUPPORTED,
        "backend missing required capability is rejected");
    TEST_ASSERT(ops == NULL, "rejected capability leaves no backend");
}

static void test_unknown_and_detection_failure_fail_closed(void)
{
    static const uint32_t aliases[] = {0x05500000u};
    static const AgcDriverRegistryEntry registry[] = {
        {
            "test-fw550",
            aliases,
            1,
            AGC_BACKEND_CAP_NATIVE_SUBMIT,
            &agcGenericDriverOps
        }
    };
    FakeFirmwareQuery query = {AGC_OK, 0x05510000u, 0};
    AgcFirmwareDetector detector = {&query, fake_query};
    const AgcDriverOps *ops = NULL;

    TEST_ASSERT_EQ(agcDriverSelectFromRegistry(&detector, registry, 1,
        AGC_BACKEND_CAP_NATIVE_SUBMIT, NULL, &ops),
        AGC_ERROR_NOT_SUPPORTED, "nearby unknown firmware is not generalized");
    TEST_ASSERT(ops == NULL, "unknown firmware leaves backend unset");

    query.result = AGC_ERROR_INTERNAL;
    query.raw_version = 0x05500000u;
    TEST_ASSERT_EQ(agcDriverSelectFromRegistry(&detector, registry, 1,
        AGC_BACKEND_CAP_NATIVE_SUBMIT, NULL, &ops),
        AGC_ERROR_NOT_SUPPORTED, "detection failure fails closed");
    TEST_ASSERT(ops == NULL, "detection failure leaves backend unset");
}

static void test_invalid_registry_arguments(void)
{
    const AgcDriverOps *ops = NULL;

    TEST_ASSERT_EQ(agcDriverSelectFromRegistry(NULL, NULL, 0, 0, NULL, &ops),
        AGC_ERROR_INVALID_ARGUMENT, "NULL detector rejected");
    TEST_ASSERT_EQ(agcDriverSelectFromRegistry(
        &(AgcFirmwareDetector){NULL, fake_query}, NULL, 0, 0, NULL, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "NULL operations output rejected");
}

void test_suite_driver_registry(void)
{
    TEST_SUITE("Runtime Driver Registry");
    TEST_RUN(test_firmware_normalization);
    TEST_RUN(test_exact_alias_and_capability_selection);
    TEST_RUN(test_unknown_and_detection_failure_fail_closed);
    TEST_RUN(test_invalid_registry_arguments);
}
