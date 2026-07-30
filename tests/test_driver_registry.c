#include "test.h"

#include <string.h>

#include "agc_error.h"
#include "agc_context.h"
#include "agc_graphics.h"
#include "agc_ioctl.h"
#include "agc_pm4.h"
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

static void test_archival_and_fw320_firmware_profiles(void)
{
    AgcProsperoRuntimeProfile profile;

    TEST_ASSERT(!agcProsperoFirmwareSupported(0x01000000u),
        "archival FW 1.00 is not advertised as supported");
    TEST_ASSERT(!agcProsperoFirmwareSupported(0x02500000u),
        "archival FW 2.50 is not advertised as supported");
    TEST_ASSERT(!agcProsperoFirmwareSupported(0x03000000u),
        "archival FW 3.00 is not advertised as supported");
    TEST_ASSERT(agcProsperoFirmwareSupported(0x03200000u),
        "FW 3.20 exact legacy-v3 runtime profile is supported");
    TEST_ASSERT(agcProsperoFirmwareSupported(0x05500008u),
        "FW 5.50 raw patch suffix uses the supported ABI key");
    TEST_ASSERT(agcProsperoFirmwareSupported(0x11600005u),
        "FW 11.60 raw patch suffix uses the supported ABI key");
    TEST_ASSERT(!agcProsperoFirmwareSupported(0x05510000u),
        "uninspected key inside the numeric interval fails closed");
    TEST_ASSERT(agcProsperoFirmwareSupported(0x05020000u),
        "inspected standard key below FW 5.50 is RE-qualified");
    TEST_ASSERT(agcProsperoFirmwareSupported(0x12000000u),
        "inspected standard key above FW 11.60 is RE-qualified");
    TEST_ASSERT(!agcProsperoFirmwareSupported(0x03100000u),
        "uninspected legacy firmware fails closed");
    {
        AgcProsperoDirectProfile direct;
        TEST_ASSERT(!agcProsperoBuildDirectProfile(
            0x03100000u, false, &direct),
            "uninspected legacy direct profile fails closed");
        TEST_ASSERT(agcProsperoBuildDirectProfile(
            0x03200000u, false, &direct),
            "FW 3.20 direct profile builds");
        TEST_ASSERT_EQ(direct.shadow_process_properties,
            AGC_DIRECT_SHADOW_PROPERTY_GN2 | AGC_DIRECT_SHADOW_PROPERTY_GN3,
            "FW 3.20 standard constructor publishes Gn2/Gn3");
    }

    TEST_ASSERT(!agcProsperoBuildRuntimeProfile(0x01000000u, false, &profile),
        "archival FW 1.00 runtime profile fails closed");
    TEST_ASSERT(!agcProsperoBuildRuntimeProfile(0x02500000u, false, &profile),
        "archival FW 2.50 runtime profile fails closed");
    TEST_ASSERT(!agcProsperoBuildRuntimeProfile(0x03000000u, false, &profile),
        "archival FW 3.00 runtime profile fails closed");

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

    TEST_ASSERT_EQ(AGC_GC_MMIO_PROT, 0x22u,
        "GC aperture preserves Sony CPU/GPU-write protection");
    TEST_ASSERT_EQ(AGC_GC_GPU_INFO_ADDRESS_HINT, 0xfe0300000ULL,
        "GPU-info allocation preserves Sony address hint");
    TEST_ASSERT_EQ(AGC_GC_INTERNAL_ADDRESS_HINT, 0xf00000000ULL,
        "internal allocations preserve Sony address hint");

    TEST_ASSERT(agcProsperoBuildDirectProfile(
        0x05500008u, false, &profile), "FW 5.50 direct profile builds");
    TEST_ASSERT_EQ(profile.capabilities,
        AGC_DIRECT_CAP_SUBMIT | AGC_DIRECT_CAP_MEMORY | AGC_DIRECT_CAP_QUEUE |
        AGC_DIRECT_CAP_SUSPEND_PRIMARY | AGC_DIRECT_CAP_SUSPEND_FINAL |
        AGC_DIRECT_CAP_WORKLOAD | AGC_DIRECT_CAP_TF_RING |
        AGC_DIRECT_CAP_HS_OFFCHIP | AGC_DIRECT_CAP_DEFAULT_STATES |
        AGC_DIRECT_CAP_ASYNC_GRAPHICS | AGC_DIRECT_CAP_EOP_FLIP,
        "FW 5.50 exposes only its qualified direct operations");
    TEST_ASSERT_EQ(profile.defaults_max_version, 9u,
        "FW 5.50 accepts register-defaults versions through 9");
    TEST_ASSERT(profile.submit_uses_frame_close_trailer,
        "FW 5.50 retains its hardware-proven close/trailer workaround");
    TEST_ASSERT_EQ(profile.tf_ring_ioctl, AGC_GC_IOCTL_SET_TF_RING,
        "FW 5.50 TF-ring uses the public 0x28 wrapper ioctl");
    TEST_ASSERT_EQ(profile.hs_offchip_ioctl, AGC_GC_IOCTL_SET_HS_OFFCHIP,
        "FW 5.50 HS-offchip uses the 0x2c wrapper ioctl");
    TEST_ASSERT(profile.workload_has_sony_stream_table,
        "FW 5.50 exposes its recovered Sony workload table");
    TEST_ASSERT_EQ(profile.shadow_process_properties,
        AGC_DIRECT_SHADOW_PROPERTY_GN2 | AGC_DIRECT_SHADOW_PROPERTY_GN3,
        "FW 5.50 standard constructor publishes Gn2/Gn3 without Gn4");

    TEST_ASSERT(agcProsperoBuildDirectProfile(
        0x11600000u, true, &profile), "FW 11.60 direct profile builds");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_SUBMIT) != 0,
        "FW 11.60 submit16 enabled");
    TEST_ASSERT(profile.submit_uses_frame_close_trailer,
        "FW 11.60 shares the standard submit completion policy");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_MEMORY) != 0,
        "FW 11.60 standard/Trinity memory profile enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_QUEUE) != 0,
        "FW 11.60 queue wrappers enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_TF_RING) != 0,
        "FW 11.60 public TF-ring wrapper enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_HS_OFFCHIP) != 0,
        "FW 11.60 HS-offchip wrapper enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_WORKLOAD) == 0,
        "FW 11.60 Trinity remains outside the standard workload-table gate");
    TEST_ASSERT_EQ(profile.shadow_process_properties,
        AGC_DIRECT_SHADOW_PROPERTY_GN2,
        "FW 11.60 Trinity selects the recovered reduced Gn2 branch");
    TEST_ASSERT_EQ(agcPm4Header3Sub(
        AGC_PM4_OP_SET_WORKLOAD, AGC_PM4_SUB_WORKLOAD_BEGIN, 3u),
        0xC0011E80u,
        "historical OpenAGC workload extension header remains documented");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_SUSPEND_QUERY) == 0,
        "FW 11.60 unknown suspend-query semantics fail closed");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_DEFAULT_STATES) != 0,
        "FW 11.60 exact version-12 defaults dispatcher is enabled");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_EOP_FLIP) == 0,
        "FW 11.60 cannot inherit FW 5.50-only EOP flip evidence");
    TEST_ASSERT_EQ(profile.defaults_max_version, 12u,
        "FW 11.60 accepts register-defaults versions through 12");
    TEST_ASSERT_EQ(profile.tf_ring_ioctl, AGC_GC_IOCTL_SET_TF_RING,
        "FW 11.60 TF-ring uses 0x80108128, not final suspend");
    TEST_ASSERT_EQ(profile.hs_offchip_ioctl, AGC_GC_IOCTL_SET_HS_OFFCHIP,
        "FW 11.60 HS-offchip uses 0xc010812c, not operation 0x2d");

    TEST_ASSERT(agcProsperoBuildDirectProfile(
        0x11600000u, false, &profile),
        "standard-PS5 FW 11.60 direct profile builds");
    TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_WORKLOAD) == 0,
        "standard-PS5 FW 11.60 workload remains fail-closed after stage 11 stall");
    TEST_ASSERT(!profile.workload_uses_sony_stream_packet,
        "FW 11.60 does not select the stalled stream adapter");
    TEST_ASSERT(profile.workload_has_sony_stream_table,
        "standard FW 11.60 exposes its recovered Sony workload table");
    TEST_ASSERT_EQ(profile.shadow_process_properties,
        AGC_DIRECT_SHADOW_PROPERTY_GN2 | AGC_DIRECT_SHADOW_PROPERTY_GN3 |
            AGC_DIRECT_SHADOW_PROPERTY_GN4,
        "standard FW 11.60 constructor publishes Gn2/Gn3/Gn4");

    TEST_ASSERT(agcProsperoBuildDirectProfile(
        0x12200000u, false, &profile), "FW 12.20 submit profile builds");
    TEST_ASSERT_EQ(profile.capabilities,
        AGC_DIRECT_CAP_SUBMIT | AGC_DIRECT_CAP_MEMORY | AGC_DIRECT_CAP_QUEUE |
        AGC_DIRECT_CAP_SUSPEND_PRIMARY |
        AGC_DIRECT_CAP_TF_RING |
        AGC_DIRECT_CAP_HS_OFFCHIP | AGC_DIRECT_CAP_DEFAULT_STATES |
        AGC_DIRECT_CAP_ASYNC_GRAPHICS,
        "FW 12.20 exposes only its exact carrier-qualified subset");
    TEST_ASSERT_EQ(profile.defaults_max_version, 12u,
        "FW 12.20 accepts caller versions through 12");
    TEST_ASSERT(profile.runtime.supports_tf_ring,
        "FW 12.20 explicit profile enables its public TF carrier");
    TEST_ASSERT_EQ(profile.tf_ring_ioctl, AGC_GC_IOCTL_SET_TF_RING,
        "FW 12.20 uses the public TF ioctl with reserved dword zeroed");
    TEST_ASSERT_EQ(profile.hs_offchip_ioctl, AGC_GC_IOCTL_SET_HS_OFFCHIP,
        "FW 12.20 uses the typed HS-offchip ioctl payload");
    TEST_ASSERT_EQ(profile.async_graphics_ioctl, AGC_GC_IOCTL_QUEUE_STATUS,
        "FW 12.20 uses the carrier-proven async setup ioctl");
    TEST_ASSERT_EQ(profile.shadow_process_properties,
        AGC_DIRECT_SHADOW_PROPERTY_GN2 | AGC_DIRECT_SHADOW_PROPERTY_GN3 |
            AGC_DIRECT_SHADOW_PROPERTY_GN4,
        "standard FW 12.20 shares the corpus-proven Gn2/Gn3/Gn4 state");
    TEST_ASSERT(!agcProsperoBuildDirectProfile(
        0x11600000u, false, NULL), "NULL direct profile rejected");
}

static void test_standard_register_shadow_descriptors(void)
{
    static const uint32_t expected_words[20] = {
        0xe0008000u, 0x0000000fu, 0x00019000u,
        0x00000000u, 0x000003bfu, 0x00002000u, 0x00002281u,
        0x00002400u, 0x00002843u, 0x00000000u,
        0xe0021000u, 0x0000000fu, 0x00019000u,
        0x00000000u, 0x000003bfu, 0x00002000u, 0x00002281u,
        0x00002400u, 0x00002843u, 0x00000000u,
    };
    AgcGcRegisterShadowDescriptor descriptors[2];

    memset(descriptors, 0xa5, sizeof(descriptors));
    TEST_ASSERT(agcProsperoBuildStandardRegisterShadowDescriptors(
        AGC_GC_DRIVER_MEMORY_ADDRESS_HINT, descriptors),
        "standard-profile register-shadow descriptors build");
    TEST_ASSERT(memcmp(descriptors, expected_words, sizeof(expected_words)) == 0,
        "standard register-shadow descriptor bytes match SPRX evidence");
    TEST_ASSERT(!agcProsperoBuildStandardRegisterShadowDescriptors(
        AGC_GC_DRIVER_MEMORY_ADDRESS_HINT, NULL),
        "NULL standard register-shadow output rejected");
    TEST_ASSERT(!agcProsperoBuildStandardRegisterShadowDescriptors(
        UINT64_MAX, descriptors),
        "overflowing standard register-shadow base rejected");
}

static void test_all_register_defaults_layouts(void)
{
    for (uint32_t version = 0u; version <= 12u; ++version) {
        AgcProsperoDefaultsLayout layout;
        uint32_t primary_count = 0u;
        uint32_t internal_count = 0u;

        TEST_ASSERT(agcProsperoDefaultsLayoutForVersion(version, &layout),
            "every caller-selectable defaults version has a blob layout");
        (void)agcRegisterDefaultsGetPrimaryGroupsForVersion(
            version, &primary_count);
        (void)agcRegisterDefaultsGetInternalGroupsForVersion(
            version, &internal_count);
        TEST_ASSERT(agcRegisterDefaultsComputeSize(primary_count,
            layout.primary_cx_length, layout.primary_sh_length,
            layout.primary_uc_length) <= layout.primary_blob_size,
            "primary defaults allocation fits the selected version");
        TEST_ASSERT(agcRegisterDefaultsComputeSize(internal_count,
            layout.internal_cx_length, layout.internal_sh_length,
            layout.internal_uc_length) <= layout.internal_blob_size,
            "internal defaults allocation fits the selected version");
        TEST_ASSERT(layout.primary_blob_size + layout.internal_blob_size +
            64u <= 0xfc000u,
            "defaults blobs and deferred trailer fit the DDID region");
    }

    TEST_ASSERT(!agcProsperoDefaultsLayoutForVersion(13u,
        &(AgcProsperoDefaultsLayout){0}),
        "unsupported defaults version has no layout");
    TEST_ASSERT(!agcProsperoDefaultsLayoutForVersion(8u, NULL),
        "NULL defaults layout output rejected");
}

static void test_common_operation_carrier_profiles(void)
{
    static const uint32_t active_raw_versions[] = {
        0x03200001u, 0x04000002u, 0x04030003u, 0x04500004u, 0x04510005u,
        0x05020006u, 0x05100007u, 0x05500008u, 0x06000009u, 0x0602000au,
        0x0650000bu, 0x0701000cu, 0x0720000du, 0x0740000eu, 0x0760000fu,
        0x07610010u, 0x08000011u, 0x08200012u, 0x08400013u, 0x08600014u,
        0x09000015u, 0x09050016u, 0x09200017u, 0x09400018u, 0x09600019u,
        0x1001001au, 0x1020001bu, 0x1040001cu, 0x1060001du, 0x1100001eu,
        0x1120001fu, 0x11400020u, 0x11600005u, 0x12000022u, 0x12020023u,
        0x12200024u, 0x12400025u, 0x12600026u, 0x12700027u,
    };
    static const uint8_t defaults_max_versions[] = {
        7u, 8u, 8u, 8u, 8u,
        9u, 9u, 9u, 9u, 9u, 9u,
        9u, 9u, 9u, 9u, 9u,
        9u, 9u, 9u, 9u,
        12u, 12u, 12u, 12u, 12u,
        12u, 12u, 12u, 12u,
        12u, 12u, 12u, 12u,
        12u, 12u, 12u, 12u, 12u, 12u,
    };

    _Static_assert(sizeof(active_raw_versions) /
        sizeof(active_raw_versions[0]) ==
        sizeof(defaults_max_versions) / sizeof(defaults_max_versions[0]),
        "every active profile needs one defaults bound");

    for (size_t i = 0; i < sizeof(active_raw_versions) /
            sizeof(active_raw_versions[0]); ++i) {
        AgcFirmwareVersion version =
            agcFirmwareNormalize(active_raw_versions[i]);
        AgcProsperoDirectProfile profile;
        AgcGfx1013ColorTargetFormatInfo r16_unorm_info;
        AgcGfx1013ColorTargetState r16_unorm_target;
        AgcGfx1013ColorTargetFormatInfo rg16_unorm_info;
        AgcGfx1013ColorTargetState rg16_unorm_target;
        AgcGfx1013ColorTargetFormatInfo rgba16_unorm_info;
        AgcGfx1013ColorTargetState rgba16_unorm_target;
        AgcGfx1013ColorTargetFormatInfo r16_snorm_info;
        AgcGfx1013ColorTargetState r16_snorm_target;
        AgcGfx1013ColorTargetFormatInfo rg16_snorm_info;
        AgcGfx1013ColorTargetState rg16_snorm_target;
        AgcGfx1013ColorTargetFormatInfo rgba16_snorm_info;
        AgcGfx1013ColorTargetState rgba16_snorm_target;
        AgcGfx1013ColorTargetFormatInfo r16_uint_info;
        AgcGfx1013ColorTargetState r16_uint_target;
        AgcGfx1013ColorTargetFormatInfo rg16_uint_info;
        AgcGfx1013ColorTargetState rg16_uint_target;
        AgcGfx1013ColorTargetFormatInfo rgba16_uint_info;
        AgcGfx1013ColorTargetState rgba16_uint_target;
        AgcGfx1013ColorTargetFormatInfo r16_sint_info;
        AgcGfx1013ColorTargetState r16_sint_target;
        AgcGfx1013ColorTargetFormatInfo rg16_sint_info;
        AgcGfx1013ColorTargetState rg16_sint_target;
        AgcGfx1013ColorTargetFormatInfo rgba16_sint_info;
        AgcGfx1013ColorTargetState rgba16_sint_target;
        uint32_t raw = active_raw_versions[i];
        uint16_t key = (uint16_t)(raw >> 16);
        uint8_t major_bcd = (uint8_t)(key >> 8);
        uint8_t minor_bcd = (uint8_t)key;

        TEST_ASSERT_EQ(version.raw, raw,
            "full firmware value survives normalization");
        TEST_ASSERT_EQ(version.major,
            ((major_bcd >> 4) & 0xfu) * 10u + (major_bcd & 0xfu),
            "full firmware value selects its BCD major version");
        TEST_ASSERT_EQ(version.minor,
            ((minor_bcd >> 4) & 0xfu) * 10u + (minor_bcd & 0xfu),
            "full firmware value selects its BCD minor version");

        TEST_ASSERT(agcProsperoBuildDirectProfile(raw, false, &profile),
            "full firmware value selects its exact direct profile");
        TEST_ASSERT(agcProsperoFirmwareSupported(raw),
            "every active exact profile is runtime-selectable");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_R16_UNORM, &r16_unorm_info), AGC_OK,
            "firmware-neutral R16 UNORM tuple resolves for active profile");
        TEST_ASSERT_EQ(r16_unorm_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16,
            "active profile shares the gfx1013 16-bit color encoding");
        TEST_ASSERT_EQ(r16_unorm_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UNORM,
            "active profile shares the gfx1013 UNORM number encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&r16_unorm_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_R16_UNORM), AGC_OK,
            "firmware-neutral R16 UNORM target initializes");
        TEST_ASSERT_EQ(r16_unorm_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UNORM,
            "active profile receives the same typed R16 UNORM state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RG16_UNORM, &rg16_unorm_info), AGC_OK,
            "firmware-neutral RG16 UNORM tuple resolves for active profile");
        TEST_ASSERT_EQ(rg16_unorm_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16,
            "active profile shares the gfx1013 16+16 color encoding");
        TEST_ASSERT_EQ(rg16_unorm_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UNORM,
            "active profile shares the two-channel UNORM number encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rg16_unorm_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RG16_UNORM), AGC_OK,
            "firmware-neutral RG16 UNORM target initializes");
        TEST_ASSERT_EQ(rg16_unorm_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UNORM,
            "active profile receives the same typed RG16 UNORM state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RGBA16_UNORM, &rgba16_unorm_info), AGC_OK,
            "firmware-neutral RGBA16 UNORM tuple resolves for active profile");
        TEST_ASSERT_EQ(rgba16_unorm_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
            "active profile shares the gfx1013 16x4 color encoding");
        TEST_ASSERT_EQ(rgba16_unorm_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UNORM,
            "active profile shares the four-channel UNORM number encoding");
        TEST_ASSERT_EQ(rgba16_unorm_info.bytes_per_pixel, 8u,
            "active profile shares the eight-byte RGBA16 element size");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rgba16_unorm_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RGBA16_UNORM), AGC_OK,
            "firmware-neutral RGBA16 UNORM target initializes");
        TEST_ASSERT_EQ(rgba16_unorm_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UNORM,
            "active profile receives the same typed RGBA16 UNORM state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_R16_SNORM, &r16_snorm_info), AGC_OK,
            "firmware-neutral R16 SNORM tuple resolves for active profile");
        TEST_ASSERT_EQ(r16_snorm_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16,
            "active profile shares the gfx1013 signed 16-bit encoding");
        TEST_ASSERT_EQ(r16_snorm_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SNORM,
            "active profile shares the gfx1013 SNORM number encoding");
        TEST_ASSERT_EQ(r16_snorm_info.bytes_per_pixel, 2u,
            "active profile shares the two-byte R16 SNORM element size");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&r16_snorm_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_R16_SNORM), AGC_OK,
            "firmware-neutral R16 SNORM target initializes");
        TEST_ASSERT_EQ(r16_snorm_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SNORM,
            "active profile receives the same typed R16 SNORM state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RG16_SNORM, &rg16_snorm_info), AGC_OK,
            "firmware-neutral RG16 SNORM tuple resolves for active profile");
        TEST_ASSERT_EQ(rg16_snorm_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16,
            "active profile shares the gfx1013 signed 16+16 encoding");
        TEST_ASSERT_EQ(rg16_snorm_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SNORM,
            "active profile shares the two-channel SNORM number encoding");
        TEST_ASSERT_EQ(rg16_snorm_info.bytes_per_pixel, 4u,
            "active profile shares the four-byte RG16 SNORM element size");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rg16_snorm_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RG16_SNORM), AGC_OK,
            "firmware-neutral RG16 SNORM target initializes");
        TEST_ASSERT_EQ(rg16_snorm_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SNORM,
            "active profile receives the same typed RG16 SNORM state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RGBA16_SNORM, &rgba16_snorm_info), AGC_OK,
            "firmware-neutral RGBA16 SNORM tuple resolves for active profile");
        TEST_ASSERT_EQ(rgba16_snorm_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
            "active profile shares the gfx1013 signed 16x4 encoding");
        TEST_ASSERT_EQ(rgba16_snorm_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SNORM,
            "active profile shares the four-channel SNORM number encoding");
        TEST_ASSERT_EQ(rgba16_snorm_info.bytes_per_pixel, 8u,
            "active profile shares the eight-byte RGBA16 SNORM element size");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rgba16_snorm_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RGBA16_SNORM), AGC_OK,
            "firmware-neutral RGBA16 SNORM target initializes");
        TEST_ASSERT_EQ(rgba16_snorm_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SNORM,
            "active profile receives the same typed RGBA16 SNORM state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_R16_UINT, &r16_uint_info), AGC_OK,
            "firmware-neutral R16 UINT tuple resolves for active profile");
        TEST_ASSERT_EQ(r16_uint_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16,
            "active profile shares the gfx1013 unsigned 16-bit encoding");
        TEST_ASSERT_EQ(r16_uint_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UINT,
            "active profile shares the gfx1013 UINT number encoding");
        TEST_ASSERT_EQ(r16_uint_info.bytes_per_pixel, 2u,
            "active profile shares the two-byte R16 UINT element size");
        TEST_ASSERT_EQ(r16_uint_info.spi_shader_export_format,
            AGC_GFX1013_SPI_EXPORT_UINT16_ABGR,
            "active profile shares the integer pixel export encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&r16_uint_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_R16_UINT), AGC_OK,
            "firmware-neutral R16 UINT target initializes");
        TEST_ASSERT_EQ(r16_uint_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UINT,
            "active profile receives the same typed R16 UINT state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RG16_UINT, &rg16_uint_info), AGC_OK,
            "firmware-neutral RG16 UINT tuple resolves for active profile");
        TEST_ASSERT_EQ(rg16_uint_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16,
            "active profile shares the gfx1013 unsigned 16+16 encoding");
        TEST_ASSERT_EQ(rg16_uint_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UINT,
            "active profile shares the two-channel UINT number encoding");
        TEST_ASSERT_EQ(rg16_uint_info.bytes_per_pixel, 4u,
            "active profile shares the four-byte RG16 UINT element size");
        TEST_ASSERT_EQ(rg16_uint_info.spi_shader_export_format,
            AGC_GFX1013_SPI_EXPORT_UINT16_ABGR,
            "active profile shares the packed RG16 UINT export encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rg16_uint_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RG16_UINT), AGC_OK,
            "firmware-neutral RG16 UINT target initializes");
        TEST_ASSERT_EQ(rg16_uint_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UINT,
            "active profile receives the same typed RG16 UINT state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RGBA16_UINT, &rgba16_uint_info), AGC_OK,
            "firmware-neutral RGBA16 UINT tuple resolves for active profile");
        TEST_ASSERT_EQ(rgba16_uint_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
            "active profile shares the gfx1013 unsigned 16x4 encoding");
        TEST_ASSERT_EQ(rgba16_uint_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UINT,
            "active profile shares the four-channel UINT number encoding");
        TEST_ASSERT_EQ(rgba16_uint_info.bytes_per_pixel, 8u,
            "active profile shares the eight-byte RGBA16 UINT element size");
        TEST_ASSERT_EQ(rgba16_uint_info.spi_shader_export_format,
            AGC_GFX1013_SPI_EXPORT_UINT16_ABGR,
            "active profile shares the packed RGBA16 UINT export encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rgba16_uint_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RGBA16_UINT), AGC_OK,
            "firmware-neutral RGBA16 UINT target initializes");
        TEST_ASSERT_EQ(rgba16_uint_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_UINT,
            "active profile receives the same typed RGBA16 UINT state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_R16_SINT, &r16_sint_info), AGC_OK,
            "firmware-neutral R16 SINT tuple resolves for active profile");
        TEST_ASSERT_EQ(r16_sint_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16,
            "active profile shares the gfx1013 signed 16-bit encoding");
        TEST_ASSERT_EQ(r16_sint_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SINT,
            "active profile shares the SINT number encoding");
        TEST_ASSERT_EQ(r16_sint_info.bytes_per_pixel, 2u,
            "active profile shares the two-byte R16 SINT element size");
        TEST_ASSERT_EQ(r16_sint_info.spi_shader_export_format,
            AGC_GFX1013_SPI_EXPORT_SINT16_ABGR,
            "active profile shares the packed R16 SINT export encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&r16_sint_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_R16_SINT), AGC_OK,
            "firmware-neutral R16 SINT target initializes");
        TEST_ASSERT_EQ(r16_sint_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SINT,
            "active profile receives the same typed R16 SINT state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RG16_SINT, &rg16_sint_info), AGC_OK,
            "firmware-neutral RG16 SINT tuple resolves for active profile");
        TEST_ASSERT_EQ(rg16_sint_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16,
            "active profile shares the gfx1013 signed 16+16 encoding");
        TEST_ASSERT_EQ(rg16_sint_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SINT,
            "active profile shares the two-channel SINT number encoding");
        TEST_ASSERT_EQ(rg16_sint_info.bytes_per_pixel, 4u,
            "active profile shares the four-byte RG16 SINT element size");
        TEST_ASSERT_EQ(rg16_sint_info.spi_shader_export_format,
            AGC_GFX1013_SPI_EXPORT_SINT16_ABGR,
            "active profile shares the packed RG16 SINT export encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rg16_sint_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RG16_SINT), AGC_OK,
            "firmware-neutral RG16 SINT target initializes");
        TEST_ASSERT_EQ(rg16_sint_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SINT,
            "active profile receives the same typed RG16 SINT state");
        TEST_ASSERT_EQ(agcGfx1013GetColorTargetFormatInfo(
            AGC_GFX1013_RT_FORMAT_RGBA16_SINT, &rgba16_sint_info), AGC_OK,
            "firmware-neutral RGBA16 SINT tuple resolves for active profile");
        TEST_ASSERT_EQ(rgba16_sint_info.color_format,
            AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
            "active profile shares the gfx1013 signed 16x4 encoding");
        TEST_ASSERT_EQ(rgba16_sint_info.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SINT,
            "active profile shares the four-channel SINT number encoding");
        TEST_ASSERT_EQ(rgba16_sint_info.bytes_per_pixel, 8u,
            "active profile shares the eight-byte RGBA16 SINT element size");
        TEST_ASSERT_EQ(rgba16_sint_info.spi_shader_export_format,
            AGC_GFX1013_SPI_EXPORT_SINT16_ABGR,
            "active profile shares the packed RGBA16 SINT export encoding");
        TEST_ASSERT_EQ(agcGfx1013InitColorTarget(&rgba16_sint_target,
            UINT64_C(0x0000000201000000), 1536u, 1536u,
            AGC_GFX1013_RT_FORMAT_RGBA16_SINT), AGC_OK,
            "firmware-neutral RGBA16 SINT target initializes");
        TEST_ASSERT_EQ(rgba16_sint_target.number_type,
            AGC_GFX1013_SURFACE_NUMBER_SINT,
            "active profile receives the same typed RGBA16 SINT state");
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
        if (key == 0x0320u) {
            TEST_ASSERT(!profile.submit_uses_frame_close_trailer,
                "legacy-v3 does not inherit the standard submit policy");
        } else {
            TEST_ASSERT(profile.submit_uses_frame_close_trailer,
                "standard compatibility group shares one submit policy");
        }
        TEST_ASSERT((profile.capabilities &
            AGC_DIRECT_CAP_DEFAULT_STATES) != 0,
            "every exact profile exposes caller-selectable defaults");
        TEST_ASSERT_EQ(profile.defaults_max_version, defaults_max_versions[i],
            "exact profile retains its SPRX-evidenced defaults bound");
        TEST_ASSERT(agcProsperoDirectProfileAcceptsDefaultsVersion(
            &profile, defaults_max_versions[i]),
            "profile accepts its exact maximum defaults version");
        TEST_ASSERT(agcProsperoDirectProfileAcceptsDefaultsVersion(
            &profile, 0u),
            "profile accepts a caller-selected backward-compatible version");
        TEST_ASSERT(agcProsperoDirectProfileAcceptsDefaultsVersion(
            &profile, AGC_REGISTER_DEFAULTS_VERSION_7),
            "every active profile accepts the portability ELF's common V7");
        TEST_ASSERT(!agcProsperoDirectProfileAcceptsDefaultsVersion(
            &profile, (uint32_t)defaults_max_versions[i] + 1u),
            "profile rejects a caller version above its exact SPRX bound");
        if (key == 0x0550u || key == 0x1160u) {
            TEST_ASSERT_EQ((profile.capabilities & AGC_DIRECT_CAP_EOP_FLIP) != 0,
                key == 0x0550u,
                "EOP flip remains independently qualified");
        } else {
            TEST_ASSERT((profile.capabilities & AGC_DIRECT_CAP_EOP_FLIP) == 0,
                "unverified EOP flip path fails closed");
        }
    }

    TEST_ASSERT(!agcProsperoDirectProfileAcceptsDefaultsVersion(NULL, 0u),
        "NULL profile rejects defaults selection");

    {
        static const uint16_t archival_keys[] = {0x0100u, 0x0200u, 0x0250u,
                                                 0x0300u};
        for (size_t i = 0; i < sizeof(archival_keys) /
                sizeof(archival_keys[0]); ++i) {
            AgcProsperoDirectProfile profile;
            uint32_t raw = (uint32_t)archival_keys[i] << 16;

            TEST_ASSERT(!agcProsperoFirmwareSupported(raw),
                "every archival alias is excluded from supported firmware");
            TEST_ASSERT(!agcProsperoBuildRuntimeProfile(raw, false,
                &profile.runtime),
                "every archival runtime profile fails closed");
            TEST_ASSERT(!agcProsperoBuildDirectProfile(raw, false, &profile),
                "archival direct profile fails closed");
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
    TEST_RUN(test_archival_and_fw320_firmware_profiles);
    TEST_RUN(test_direct_operation_profiles);
    TEST_RUN(test_standard_register_shadow_descriptors);
    TEST_RUN(test_all_register_defaults_layouts);
    TEST_RUN(test_common_operation_carrier_profiles);
    TEST_RUN(test_trinity_runtime_profile);
    TEST_RUN(test_runtime_profile_diagnostic_labels);
    TEST_RUN(test_exact_alias_and_capability_selection);
    TEST_RUN(test_unknown_and_detection_failure_fail_closed);
    TEST_RUN(test_invalid_registry_arguments);
}
