#include "driver_registry.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "agc_error.h"

static const uint32_t g_legacy_v1_aliases[] = {
    0x01000000u
};

static uint32_t g_runtime_firmware_version;

static const uint32_t g_legacy_v2_aliases[] = {
    0x02000000u, 0x02500000u
};

static const uint32_t g_legacy_v3_aliases[] = {
    0x03000000u, 0x03200000u
};

/*
 * Exact standard-PS5 builds whose libSceAgcDriver direct-submit ABI was
 * inspected. Keep this fail-closed: never replace the aliases with a range.
 * PS5 Pro has a different CWSR profile and is outside this backend contract.
 */
static const uint32_t g_standard_direct_aliases[] = {
    0x04000000u, 0x04030000u, 0x04500000u, 0x04510000u,
    0x05020000u, 0x05100000u, 0x05500000u,
    0x06000000u, 0x06020000u, 0x06500000u,
    0x07010000u, 0x07200000u, 0x07400000u, 0x07600000u, 0x07610000u,
    0x08000000u, 0x08200000u, 0x08400000u, 0x08600000u,
    0x09000000u, 0x09050000u, 0x09200000u, 0x09400000u, 0x09600000u,
    0x10010000u, 0x10200000u, 0x10400000u, 0x10600000u,
    0x11000000u, 0x11200000u, 0x11400000u, 0x11600000u,
    0x12000000u, 0x12020000u, 0x12200000u, 0x12400000u,
    0x12600000u, 0x12700000u
};

static uint16_t agcBcdByte(uint32_t value)
{
    return (uint16_t)(((value >> 4) & 0xfu) * 10u + (value & 0xfu));
}

static uint32_t agcFirmwareAbiKey(uint32_t raw_version)
{
    return raw_version & 0xffff0000u;
}

AgcFirmwareVersion agcFirmwareNormalize(uint32_t raw_version)
{
    AgcFirmwareVersion version;

    version.raw = raw_version;
    version.major = agcBcdByte(raw_version >> 24);
    version.minor = agcBcdByte(raw_version >> 16);
    version.patch = agcBcdByte(raw_version >> 8);
    return version;
}

bool agcProsperoStandardDirectAbiSupportsFirmware(uint32_t raw_version)
{
    size_t i;
    uint32_t abi_key = agcFirmwareAbiKey(raw_version);

    for (i = 0; i < sizeof(g_standard_direct_aliases) /
                    sizeof(g_standard_direct_aliases[0]); ++i) {
        if (g_standard_direct_aliases[i] == abi_key)
            return true;
    }
    return false;
}

static bool agcFirmwareAliasContains(const uint32_t *aliases,
    size_t alias_count, uint32_t raw_version)
{
    size_t i;

    for (i = 0; i < alias_count; ++i) {
        if (aliases[i] == raw_version)
            return true;
    }
    return false;
}

bool agcProsperoFirmwareSupported(uint32_t raw_version)
{
    uint32_t abi_key = agcFirmwareAbiKey(raw_version);

    return agcFirmwareAliasContains(g_legacy_v1_aliases,
               sizeof(g_legacy_v1_aliases) / sizeof(g_legacy_v1_aliases[0]),
               abi_key) ||
           agcFirmwareAliasContains(g_legacy_v2_aliases,
               sizeof(g_legacy_v2_aliases) / sizeof(g_legacy_v2_aliases[0]),
               abi_key) ||
           agcFirmwareAliasContains(g_legacy_v3_aliases,
               sizeof(g_legacy_v3_aliases) / sizeof(g_legacy_v3_aliases[0]),
               abi_key) ||
           agcProsperoStandardDirectAbiSupportsFirmware(raw_version);
}

bool agcProsperoBuildRuntimeProfile(uint32_t raw_version, bool is_trinity,
    AgcProsperoRuntimeProfile *profile_out)
{
    AgcProsperoRuntimeProfile profile = {0};
    uint32_t abi_key = agcFirmwareAbiKey(raw_version);

    if (!profile_out)
        return false;

    if (agcFirmwareAliasContains(g_legacy_v1_aliases,
            sizeof(g_legacy_v1_aliases) / sizeof(g_legacy_v1_aliases[0]),
            abi_key)) {
        profile.family = AGC_PROSPERO_ABI_LEGACY_V1;
        profile.eop_ring_offset = 0x38000u;
    } else if (agcFirmwareAliasContains(g_legacy_v2_aliases,
            sizeof(g_legacy_v2_aliases) / sizeof(g_legacy_v2_aliases[0]),
            abi_key)) {
        profile.family = AGC_PROSPERO_ABI_LEGACY_V2;
        profile.authenticated_special_queue = true;
        profile.eop_ring_offset = 0x39000u;
    } else if (agcFirmwareAliasContains(g_legacy_v3_aliases,
            sizeof(g_legacy_v3_aliases) / sizeof(g_legacy_v3_aliases[0]),
            abi_key)) {
        profile.family = AGC_PROSPERO_ABI_LEGACY_V3;
        profile.authenticated_special_queue = true;
        profile.supports_tf_ring = true;
        profile.eop_ring_offset = 0x39000u;
    } else if (agcProsperoStandardDirectAbiSupportsFirmware(raw_version)) {
        profile.family = AGC_PROSPERO_ABI_STANDARD;
        profile.authenticated_special_queue = true;
        profile.supports_tf_ring = true;
        profile.eop_ring_offset = 0x39000u;
    } else {
        return false;
    }

    profile.is_trinity = is_trinity;
    profile.gpu_info_span = is_trinity ? 0x180000u : 0x100000u;
    profile.cwsr_work_offset = is_trinity ? 0x1000000u : 0xa00000u;
    profile.cwsr_size = is_trinity ? 0x1600000u : 0x1000000u;
    *profile_out = profile;
    return true;
}

const char *PS5_SYSV_ABI agcProsperoAbiFamilyName(
    AgcProsperoAbiFamily family)
{
    switch (family) {
    case AGC_PROSPERO_ABI_LEGACY_V1: return "legacy-v1";
    case AGC_PROSPERO_ABI_LEGACY_V2: return "legacy-v2";
    case AGC_PROSPERO_ABI_LEGACY_V3: return "legacy-v3";
    case AGC_PROSPERO_ABI_STANDARD:  return "standard";
    default:                         return "unsupported";
    }
}

int32_t PS5_SYSV_ABI agcDriverDebugRuntimeProfile(
    AgcDriverRuntimeDiagnostics *diagnostics)
{
    if (!diagnostics)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->firmware_version = g_runtime_firmware_version;
    diagnostics->backend_name = agcDriverDebugBackendName();
#ifdef OPENAGC_PROSPERO
    return agcProsperoGetRuntimeProfile(&diagnostics->profile);
#else
    return AGC_ERROR_NOT_SUPPORTED;
#endif
}

const AgcDriverRegistryEntry *agcDriverRegistryLookup(
    const AgcDriverRegistryEntry *entries, size_t entry_count,
    uint32_t raw_version, uint32_t required_capabilities)
{
    size_t i;

    if (!entries)
        return NULL;

    for (i = 0; i < entry_count; ++i) {
        const AgcDriverRegistryEntry *entry = &entries[i];
        size_t alias;

        if (!entry->ops || !entry->firmware_aliases ||
            (entry->capabilities & required_capabilities) !=
                required_capabilities)
            continue;
        for (alias = 0; alias < entry->firmware_alias_count; ++alias) {
            if (entry->firmware_aliases[alias] == raw_version)
                return entry;
        }
    }
    return NULL;
}

int32_t agcDriverSelectFromRegistry(const AgcFirmwareDetector *detector,
    const AgcDriverRegistryEntry *entries, size_t entry_count,
    uint32_t required_capabilities, AgcFirmwareVersion *version_out,
    const AgcDriverOps **ops_out)
{
    const AgcDriverRegistryEntry *entry;
    uint32_t raw_version = 0;
    int32_t result;

    if (!detector || !detector->query || !ops_out)
        return AGC_ERROR_INVALID_ARGUMENT;

    *ops_out = NULL;
    result = detector->query(detector->context, &raw_version);
    if (result != AGC_OK)
        return AGC_ERROR_NOT_SUPPORTED;

    if (version_out)
        *version_out = agcFirmwareNormalize(raw_version);
    entry = agcDriverRegistryLookup(entries, entry_count, raw_version,
        required_capabilities);
    if (!entry)
        return AGC_ERROR_NOT_SUPPORTED;

    *ops_out = entry->ops;
    return AGC_OK;
}

#ifdef OPENAGC_PROSPERO
typedef struct AgcKernelSwVersion {
    uint64_t reserved0;
    char version_string[0x1c];
    uint32_t version;
    uint64_t reserved1;
} AgcKernelSwVersion;

_Static_assert(sizeof(AgcKernelSwVersion) == 0x30,
    "PS5 system software version ABI size");
_Static_assert(offsetof(AgcKernelSwVersion, version) == 0x24,
    "PS5 system software numeric version ABI offset");

extern int PS5_SYSV_ABI sceKernelGetProsperoSystemSwVersion(
    AgcKernelSwVersion *version);

static int32_t agcQueryProsperoFirmware(void *context, uint32_t *raw_version)
{
    AgcKernelSwVersion version;

    (void)context;
    if (!raw_version)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset(&version, 0, sizeof(version));
    if (sceKernelGetProsperoSystemSwVersion(&version) != 0)
        return AGC_ERROR_NOT_SUPPORTED;
    *raw_version = version.version;
    printf("[openagc] system software raw=0x%08X string=%s\n",
        version.version, version.version_string);
    return AGC_OK;
}
#endif

int32_t agcDriverSelectRuntime(AgcFirmwareVersion *version_out,
    const AgcDriverOps **ops_out)
{
#ifdef OPENAGC_PROSPERO
    static const AgcDriverRegistryEntry registry[] = {
        {
            "prospero-gcabi-v1-submit16",
            g_legacy_v1_aliases,
            sizeof(g_legacy_v1_aliases) / sizeof(g_legacy_v1_aliases[0]),
            AGC_BACKEND_CAP_NATIVE_SUBMIT | AGC_BACKEND_CAP_COMPUTE |
                AGC_BACKEND_CAP_GRAPHICS,
            &agcProsperoDriverOps
        },
        {
            "prospero-gcabi-v2-submit16",
            g_legacy_v2_aliases,
            sizeof(g_legacy_v2_aliases) / sizeof(g_legacy_v2_aliases[0]),
            AGC_BACKEND_CAP_NATIVE_SUBMIT | AGC_BACKEND_CAP_COMPUTE |
                AGC_BACKEND_CAP_GRAPHICS,
            &agcProsperoDriverOps
        },
        {
            "prospero-gcabi-v3-submit16",
            g_legacy_v3_aliases,
            sizeof(g_legacy_v3_aliases) / sizeof(g_legacy_v3_aliases[0]),
            AGC_BACKEND_CAP_NATIVE_SUBMIT | AGC_BACKEND_CAP_COMPUTE |
                AGC_BACKEND_CAP_GRAPHICS,
            &agcProsperoDriverOps
        },
        {
            "prospero-gcabi-standard-submit16",
            g_standard_direct_aliases,
            sizeof(g_standard_direct_aliases) /
                sizeof(g_standard_direct_aliases[0]),
            AGC_BACKEND_CAP_NATIVE_SUBMIT | AGC_BACKEND_CAP_COMPUTE |
                AGC_BACKEND_CAP_GRAPHICS,
            &agcProsperoDriverOps
        }
    };
    const AgcDriverRegistryEntry *entry;
    uint32_t raw_version;
    int32_t result;

    if (!ops_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *ops_out = NULL;
    result = agcQueryProsperoFirmware(NULL, &raw_version);
    if (result != AGC_OK)
        return AGC_ERROR_NOT_SUPPORTED;
    entry = agcDriverRegistryLookup(registry,
        sizeof(registry) / sizeof(registry[0]), agcFirmwareAbiKey(raw_version),
        AGC_BACKEND_CAP_NATIVE_SUBMIT);
    if (!entry)
        return AGC_ERROR_NOT_SUPPORTED;
    *ops_out = entry->ops;
    result = agcProsperoConfigureRuntimeProfile(raw_version);
    if (result != AGC_OK) {
        *ops_out = NULL;
        return result;
    }
    g_runtime_firmware_version = raw_version;
    if (version_out)
        *version_out = agcFirmwareNormalize(raw_version);
    return AGC_OK;
#else
    if (!ops_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (version_out)
        memset(version_out, 0, sizeof(*version_out));
    *ops_out = &agcGenericDriverOps;
    return AGC_OK;
#endif
}
