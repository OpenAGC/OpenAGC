#include "driver_registry.h"

#include <stddef.h>
#include <string.h>

#include "agc_error.h"

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

    for (i = 0; i < sizeof(g_standard_direct_aliases) /
                    sizeof(g_standard_direct_aliases[0]); ++i) {
        if (g_standard_direct_aliases[i] == raw_version)
            return true;
    }
    return false;
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
    return AGC_OK;
}
#endif

int32_t agcDriverSelectRuntime(AgcFirmwareVersion *version_out,
    const AgcDriverOps **ops_out)
{
#ifdef OPENAGC_PROSPERO
    static const AgcDriverRegistryEntry registry[] = {
        {
            "prospero-gcabi-v4-standard-direct",
            g_standard_direct_aliases,
            sizeof(g_standard_direct_aliases) /
                sizeof(g_standard_direct_aliases[0]),
            AGC_BACKEND_CAP_NATIVE_SUBMIT | AGC_BACKEND_CAP_COMPUTE |
                AGC_BACKEND_CAP_GRAPHICS,
            &agcProsperoDriverOps
        }
    };
    const AgcFirmwareDetector detector = {NULL, agcQueryProsperoFirmware};

    return agcDriverSelectFromRegistry(&detector, registry,
        sizeof(registry) / sizeof(registry[0]),
        AGC_BACKEND_CAP_NATIVE_SUBMIT, version_out, ops_out);
#else
    if (!ops_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (version_out)
        memset(version_out, 0, sizeof(*version_out));
    *ops_out = &agcGenericDriverOps;
    return AGC_OK;
#endif
}
