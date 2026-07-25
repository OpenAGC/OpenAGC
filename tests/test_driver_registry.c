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

static void test_standard_direct_firmware_aliases(void)
{
    TEST_ASSERT(agcProsperoStandardDirectAbiSupportsFirmware(0x04000000u),
        "oldest independently inspected direct-submit firmware supported");
    TEST_ASSERT(agcProsperoStandardDirectAbiSupportsFirmware(0x05500000u),
        "hardware-validated firmware supported");
    TEST_ASSERT(agcProsperoStandardDirectAbiSupportsFirmware(0x11600000u),
        "FW 11.60 independently inspected alias supported");
    TEST_ASSERT(agcProsperoStandardDirectAbiSupportsFirmware(0x12700000u),
        "newest independently inspected standard firmware supported");
    TEST_ASSERT(!agcProsperoStandardDirectAbiSupportsFirmware(0x03200000u),
        "pre-direct-submit ABI firmware rejected");
    TEST_ASSERT(!agcProsperoStandardDirectAbiSupportsFirmware(0x05510000u),
        "uninspected nearby firmware rejected");
}

static void test_legacy_submit16_firmware_profiles(void)
{
    AgcProsperoRuntimeProfile profile;

    TEST_ASSERT(agcProsperoFirmwareSupported(0x01000000u),
        "FW 1.00 submit16 profile supported");
    TEST_ASSERT(agcProsperoFirmwareSupported(0x02500000u),
        "FW 2.50 submit16 profile supported");
    TEST_ASSERT(agcProsperoFirmwareSupported(0x03200000u),
        "FW 3.20 submit16 profile supported");
    TEST_ASSERT(!agcProsperoFirmwareSupported(0x03100000u),
        "uninspected legacy firmware fails closed");

    TEST_ASSERT(agcProsperoBuildRuntimeProfile(0x01000000u, false, &profile),
        "FW 1.00 profile builds");
    TEST_ASSERT_EQ(profile.family, AGC_PROSPERO_ABI_LEGACY_V1,
        "FW 1.00 family selected");
    TEST_ASSERT_EQ(profile.eop_ring_offset, 0x38000u,
        "FW 1.00 EOP offset retained");
    TEST_ASSERT(!profile.authenticated_special_queue,
        "FW 1.00 does not use later queue authentication layout");
    TEST_ASSERT(!profile.supports_tf_ring,
        "FW 1.00 has no TF-ring ioctl");

    TEST_ASSERT(agcProsperoBuildRuntimeProfile(0x02500000u, false, &profile),
        "FW 2.50 profile builds");
    TEST_ASSERT_EQ(profile.family, AGC_PROSPERO_ABI_LEGACY_V2,
        "FW 2.50 family selected");
    TEST_ASSERT(profile.authenticated_special_queue,
        "FW 2.50 authenticated queue layout selected");
    TEST_ASSERT(!profile.supports_tf_ring,
        "FW 2.50 has no TF-ring ioctl");

    TEST_ASSERT(agcProsperoBuildRuntimeProfile(0x03200000u, false, &profile),
        "FW 3.20 profile builds");
    TEST_ASSERT_EQ(profile.family, AGC_PROSPERO_ABI_LEGACY_V3,
        "FW 3.20 family selected");
    TEST_ASSERT(profile.supports_tf_ring,
        "FW 3.20 TF-ring ioctl selected");
}

static void test_trinity_runtime_profile(void)
{
    AgcProsperoRuntimeProfile profile;

    TEST_ASSERT(agcProsperoBuildRuntimeProfile(0x11600000u, true, &profile),
        "Trinity profile builds for inspected later firmware");
    TEST_ASSERT(profile.is_trinity, "Trinity hardware recorded");
    TEST_ASSERT_EQ(profile.gpu_info_span, 0x180000u,
        "Trinity GPU-info span matches firmware predicate branch");
    TEST_ASSERT_EQ(profile.cwsr_work_offset, 0x1000000u,
        "Trinity CWSR working offset matches firmware predicate branch");
    TEST_ASSERT_EQ(profile.cwsr_size, 0x1600000u,
        "Trinity CWSR allocation is 22 MiB");

    TEST_ASSERT(agcProsperoBuildRuntimeProfile(0x11600000u, false, &profile),
        "standard profile builds for same firmware");
    TEST_ASSERT_EQ(profile.gpu_info_span, 0x100000u,
        "standard GPU-info span retained");
    TEST_ASSERT_EQ(profile.cwsr_work_offset, 0xa00000u,
        "standard CWSR working offset retained");
    TEST_ASSERT_EQ(profile.cwsr_size, 0x1000000u,
        "standard CWSR allocation remains 16 MiB");
    TEST_ASSERT(!agcProsperoBuildRuntimeProfile(0x03100000u, false, &profile),
        "unknown firmware profile fails closed");
    TEST_ASSERT(!agcProsperoBuildRuntimeProfile(0x11600000u, false, NULL),
        "NULL profile output rejected");
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
    TEST_RUN(test_standard_direct_firmware_aliases);
    TEST_RUN(test_legacy_submit16_firmware_profiles);
    TEST_RUN(test_trinity_runtime_profile);
    TEST_RUN(test_exact_alias_and_capability_selection);
    TEST_RUN(test_unknown_and_detection_failure_fail_closed);
    TEST_RUN(test_invalid_registry_arguments);
}
