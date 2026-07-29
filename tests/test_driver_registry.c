#include "test.h"

#include <string.h>

#include "agc_error.h"
#include "agc_ioctl.h"
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
    TEST_ASSERT(agcProsperoStandardDirectAbiSupportsFirmware(0x05500008u),
        "hardware-reported FW 5.500.008 alias supported");
    TEST_ASSERT(agcProsperoStandardDirectAbiSupportsFirmware(0x0403ffffu),
        "unknown FW 4.03 build suffix uses the 0x0403 ABI key");
    TEST_ASSERT(!agcProsperoStandardDirectAbiSupportsFirmware(0x04040000u),
        "unregistered FW 4.04 ABI key remains unsupported");
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
        "FW 3.20 exact public TF-ring carrier is enabled");
}

static void test_direct_operation_profiles(void)
{
    AgcProsperoDirectProfile profile;

    TEST_ASSERT(agcProsperoBuildDirectProfile(
        0x05500008u, false, &profile), "FW 5.50 direct profile builds");
    TEST_ASSERT_EQ(profile.capabilities,
        AGC_DIRECT_CAP_SUBMIT | AGC_DIRECT_CAP_MEMORY | AGC_DIRECT_CAP_QUEUE |
        AGC_DIRECT_CAP_SUSPEND_PRIMARY | AGC_DIRECT_CAP_SUSPEND_FINAL |
        AGC_DIRECT_CAP_WORKLOAD | AGC_DIRECT_CAP_TF_RING |
        AGC_DIRECT_CAP_HS_OFFCHIP | AGC_DIRECT_CAP_DEFAULT_STATES |
        AGC_DIRECT_CAP_ASYNC_GRAPHICS | AGC_DIRECT_CAP_EOP_FLIP,
        "FW 5.50 exposes only its qualified direct operations");
    TEST_ASSERT_EQ(profile.defaults_version, 8u,
        "FW 5.50 selects register-defaults version 8");
    TEST_ASSERT_EQ(profile.tf_ring_ioctl, AGC_GC_IOCTL_SET_TF_RING,
        "FW 5.50 TF-ring uses the public 0x28 wrapper ioctl");
    TEST_ASSERT_EQ(profile.hs_offchip_ioctl, AGC_GC_IOCTL_SET_HS_OFFCHIP,
        "FW 5.50 HS-offchip uses the 0x2c wrapper ioctl");

    TEST_ASSERT(agcProsperoBuildDirectProfile(
        0x11600000u, true, &profile), "FW 11.60 direct profile builds");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_SUBMIT) != 0,
        "FW 11.60 submit16 enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_MEMORY) != 0,
        "FW 11.60 standard/Trinity memory profile enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_QUEUE) != 0,
        "FW 11.60 queue wrappers enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_TF_RING) != 0,
        "FW 11.60 public TF-ring wrapper enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_HS_OFFCHIP) != 0,
        "FW 11.60 HS-offchip wrapper enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_WORKLOAD) == 0,
        "FW 11.60 incompatible workload wrapper fails closed");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_SUSPEND_QUERY) == 0,
        "FW 11.60 unknown suspend-query semantics fail closed");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_DEFAULT_STATES) == 0,
        "FW 11.60 unknown defaults version fails closed");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_EOP_FLIP) == 0,
        "FW 11.60 cannot inherit FW 5.50-only EOP flip evidence");
    TEST_ASSERT_EQ(profile.defaults_version,
        AGC_DIRECT_DEFAULTS_VERSION_UNKNOWN,
        "FW 11.60 never inherits FW 5.50 defaults version");
    TEST_ASSERT_EQ(profile.tf_ring_ioctl, AGC_GC_IOCTL_SET_TF_RING,
        "FW 11.60 TF-ring uses 0x80108128, not final suspend");
    TEST_ASSERT_EQ(profile.hs_offchip_ioctl, AGC_GC_IOCTL_SET_HS_OFFCHIP,
        "FW 11.60 HS-offchip uses 0xc010812c, not operation 0x2d");

    TEST_ASSERT(agcProsperoBuildDirectProfile(
        0x12200000u, false, &profile), "FW 12.20 submit profile builds");
    TEST_ASSERT_EQ(profile.capabilities,
        AGC_DIRECT_CAP_SUBMIT | AGC_DIRECT_CAP_MEMORY | AGC_DIRECT_CAP_QUEUE |
        AGC_DIRECT_CAP_SUSPEND_PRIMARY |
        AGC_DIRECT_CAP_TF_RING |
        AGC_DIRECT_CAP_HS_OFFCHIP | AGC_DIRECT_CAP_ASYNC_GRAPHICS,
        "FW 12.20 exposes only its exact carrier-qualified subset");
    TEST_ASSERT(profile.runtime.supports_tf_ring,
        "FW 12.20 explicit profile enables its public TF carrier");
    TEST_ASSERT_EQ(profile.tf_ring_ioctl, AGC_GC_IOCTL_SET_TF_RING,
        "FW 12.20 uses the public TF ioctl with reserved dword zeroed");
    TEST_ASSERT_EQ(profile.hs_offchip_ioctl, AGC_GC_IOCTL_SET_HS_OFFCHIP,
        "FW 12.20 uses the typed HS-offchip ioctl payload");
    TEST_ASSERT_EQ(profile.async_graphics_ioctl, AGC_GC_IOCTL_QUEUE_STATUS,
        "FW 12.20 uses the carrier-proven async setup ioctl");
    TEST_ASSERT(!agcProsperoBuildDirectProfile(
        0x11600000u, false, NULL), "NULL direct profile rejected");
}

static void test_common_operation_carrier_profiles(void)
{
    static const uint16_t active_keys[] = {
        0x0320u, 0x0400u, 0x0403u, 0x0450u, 0x0451u,
        0x0502u, 0x0510u, 0x0550u, 0x0600u, 0x0602u, 0x0650u,
        0x0701u, 0x0720u, 0x0740u, 0x0760u, 0x0761u,
        0x0800u, 0x0820u, 0x0840u, 0x0860u,
        0x0900u, 0x0905u, 0x0920u, 0x0940u, 0x0960u,
        0x1001u, 0x1020u, 0x1040u, 0x1060u,
        0x1100u, 0x1120u, 0x1140u, 0x1160u,
        0x1200u, 0x1202u, 0x1220u, 0x1240u, 0x1260u, 0x1270u,
    };

    for (size_t i = 0; i < sizeof(active_keys) / sizeof(active_keys[0]); ++i) {
        AgcProsperoDirectProfile profile;
        uint32_t raw = (uint32_t)active_keys[i] << 16;

        TEST_ASSERT(agcProsperoBuildDirectProfile(raw, false, &profile),
            "active carrier-qualified profile builds");
        TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_TF_RING) != 0,
            "active profile exposes exact public TF-ring carrier");
        TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_MEMORY) != 0,
            "active profile exposes exact internal-memory carrier");
        TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_QUEUE) != 0,
            "active profile exposes exact authenticated-queue carrier");
        TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_SUSPEND_PRIMARY) != 0,
            "active profile exposes exact primary-suspend carrier");
        TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_HS_OFFCHIP) != 0,
            "active profile exposes exact HS-offchip carrier");
        TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_ASYNC_GRAPHICS) != 0,
            "active profile exposes exact async carrier");
        TEST_ASSERT_EQ(profile.tf_ring_ioctl, AGC_GC_IOCTL_SET_TF_RING,
            "active profile retains public TF command");
        TEST_ASSERT_EQ(profile.hs_offchip_ioctl, AGC_GC_IOCTL_SET_HS_OFFCHIP,
            "active profile retains HS-offchip command");
        TEST_ASSERT_EQ(profile.async_graphics_ioctl, AGC_GC_IOCTL_QUEUE_STATUS,
            "active profile retains async setup command");
        if (active_keys[i] == 0x0550u) {
            TEST_ASSERT((profile.capabilities &
                AGC_DIRECT_CAP_DEFAULT_STATES) != 0,
                "FW 5.50 exact runtime defaults selection is enabled");
            TEST_ASSERT_EQ(profile.defaults_version, 8u,
                "FW 5.50 exact runtime defaults selection remains V8");
            TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_EOP_FLIP) != 0,
                "FW 5.50 exact EOP flip path remains enabled");
        } else {
            TEST_ASSERT((profile.capabilities &
                AGC_DIRECT_CAP_DEFAULT_STATES) == 0,
                "unobserved runtime defaults selection fails closed");
            TEST_ASSERT_EQ(profile.defaults_version,
                AGC_DIRECT_DEFAULTS_VERSION_UNKNOWN,
                "dispatcher upper bound cannot select a defaults version");
            TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_EOP_FLIP) == 0,
                "unverified EOP flip path fails closed");
        }
    }

    {
        static const uint16_t archival_keys[] = {0x0100u, 0x0200u, 0x0250u,
                                                 0x0300u};
        for (size_t i = 0; i < sizeof(archival_keys) /
                sizeof(archival_keys[0]); ++i) {
            AgcProsperoDirectProfile profile;
            uint32_t raw = (uint32_t)archival_keys[i] << 16;

            TEST_ASSERT(agcProsperoBuildDirectProfile(raw, false, &profile),
                "archival submit-only profile still builds");
            TEST_ASSERT((profile.capabilities & (AGC_DIRECT_CAP_TF_RING |
                AGC_DIRECT_CAP_HS_OFFCHIP |
                AGC_DIRECT_CAP_ASYNC_GRAPHICS |
                AGC_DIRECT_CAP_MEMORY | AGC_DIRECT_CAP_QUEUE |
                AGC_DIRECT_CAP_SUSPEND_PRIMARY)) == 0,
                "archival profile cannot inherit active carrier facts");
        }
    }
}

static void test_trinity_runtime_profile(void)
{
    AgcProsperoRuntimeProfile profile;

    TEST_ASSERT(!agcProsperoFirmwareUsesTrinityPredicate(0x08600000u),
        "FW 8.60 has no Trinity predicate import");
    TEST_ASSERT(agcProsperoFirmwareUsesTrinityPredicate(0x09000000u),
        "FW 9.00 exact key introduces Trinity predicate");
    TEST_ASSERT(agcProsperoFirmwareUsesTrinityPredicate(0x12700000u),
        "FW 12.70 exact key retains Trinity predicate");
    TEST_ASSERT(!agcProsperoFirmwareUsesTrinityPredicate(0x09700000u),
        "unknown neighboring key cannot inherit Trinity behavior");
    TEST_ASSERT(!agcProsperoBuildRuntimeProfile(0x08600000u, true, &profile),
        "pre-Trinity firmware rejects an impossible Trinity model");

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

static void test_runtime_profile_diagnostic_labels(void)
{
    AgcDriverRuntimeDiagnostics diagnostics;

    TEST_ASSERT(strcmp(agcProsperoAbiFamilyName(AGC_PROSPERO_ABI_LEGACY_V1),
        "legacy-v1") == 0, "legacy v1 diagnostic label");
    TEST_ASSERT(strcmp(agcProsperoAbiFamilyName(AGC_PROSPERO_ABI_LEGACY_V2),
        "legacy-v2") == 0, "legacy v2 diagnostic label");
    TEST_ASSERT(strcmp(agcProsperoAbiFamilyName(AGC_PROSPERO_ABI_LEGACY_V3),
        "legacy-v3") == 0, "legacy v3 diagnostic label");
    TEST_ASSERT(strcmp(agcProsperoAbiFamilyName(AGC_PROSPERO_ABI_STANDARD),
        "standard") == 0, "standard diagnostic label");
    TEST_ASSERT(strcmp(agcProsperoAbiFamilyName(AGC_PROSPERO_ABI_UNSUPPORTED),
        "unsupported") == 0, "unsupported diagnostic label");
    TEST_ASSERT_EQ(agcDriverDebugRuntimeProfile(NULL),
        AGC_ERROR_INVALID_ARGUMENT, "NULL diagnostic output rejected");
    TEST_ASSERT_EQ(agcDriverDebugRuntimeProfile(&diagnostics),
        AGC_ERROR_NOT_SUPPORTED, "generic backend has no Prospero profile");
    TEST_ASSERT(diagnostics.backend_name != NULL,
        "generic diagnostic still reports selected backend name");
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
    TEST_RUN(test_direct_operation_profiles);
    TEST_RUN(test_common_operation_carrier_profiles);
    TEST_RUN(test_trinity_runtime_profile);
    TEST_RUN(test_runtime_profile_diagnostic_labels);
    TEST_RUN(test_exact_alias_and_capability_selection);
    TEST_RUN(test_unknown_and_detection_failure_fail_closed);
    TEST_RUN(test_invalid_registry_arguments);
}
