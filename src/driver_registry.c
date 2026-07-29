#include "driver_registry.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "agc_context.h"
#include "agc_error.h"
#include "agc_ioctl.h"

static const uint32_t g_legacy_v1_aliases[] = {
    0x0100u
};

static uint32_t g_runtime_firmware_version;

static const uint32_t g_legacy_v2_aliases[] = {
    0x0200u, 0x0250u
};

static const uint32_t g_legacy_v3_aliases[] = {
    0x0300u, 0x0320u
};

/*
 * Exact standard-PS5 builds whose direct /dev/gc submit ABI was
 * inspected. Keep this fail-closed: never replace the aliases with a range.
 * PS5 Pro has a different CWSR profile and is outside this backend contract.
 */
static const uint32_t g_standard_direct_aliases[] = {
    0x0400u, 0x0403u, 0x0450u, 0x0451u,
    0x0502u, 0x0510u, 0x0550u,
    0x0600u, 0x0602u, 0x0650u,
    0x0701u, 0x0720u, 0x0740u, 0x0760u, 0x0761u,
    0x0800u, 0x0820u, 0x0840u, 0x0860u,
    0x0900u, 0x0905u, 0x0920u, 0x0940u, 0x0960u,
    0x1001u, 0x1020u, 0x1040u, 0x1060u,
    0x1100u, 0x1120u, 0x1140u, 0x1160u,
    0x1200u, 0x1202u, 0x1220u, 0x1240u,
    0x1260u, 0x1270u
};

static const uint32_t g_trinity_profile_aliases[] = {
    0x0900u, 0x0905u, 0x0920u, 0x0940u, 0x0960u,
    0x1001u, 0x1020u, 0x1040u, 0x1060u,
    0x1100u, 0x1120u, 0x1140u, 0x1160u,
    0x1200u, 0x1202u, 0x1220u, 0x1240u, 0x1260u, 0x1270u
};

typedef struct AgcDirectDefaultsSelection {
    uint16_t firmware_abi_key;
    uint32_t selected_version;
} AgcDirectDefaultsSelection;

/* A dispatcher upper bound does not select a defaults version.  This table
 * contains only runtime selections proven for the exact firmware/GPU pair.
 * FW 5.50 selected V8 in the hardware-qualified default-state sample. */
static const AgcDirectDefaultsSelection g_direct_defaults_selections[] = {
    {0x0550u, AGC_REGISTER_DEFAULTS_VERSION_8},
    {0x1160u, AGC_REGISTER_DEFAULTS_VERSION_12}
};

static uint16_t agcBcdByte(uint32_t value)
{
    return (uint16_t)(((value >> 4) & 0xfu) * 10u + (value & 0xfu));
}

static uint16_t agcFirmwareAbiKey(uint32_t raw_version)
{
    return (uint16_t)(raw_version >> 16);
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
    uint16_t abi_key = agcFirmwareAbiKey(raw_version);

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

static bool agcDirectDefaultsVersionForFirmware(uint16_t abi_key,
    uint32_t *version_out)
{
    size_t i;

    for (i = 0; i < sizeof(g_direct_defaults_selections) /
                    sizeof(g_direct_defaults_selections[0]); ++i) {
        if (g_direct_defaults_selections[i].firmware_abi_key == abi_key) {
            *version_out = g_direct_defaults_selections[i].selected_version;
            return true;
        }
    }
    return false;
}

bool agcProsperoFirmwareUsesTrinityPredicate(uint32_t raw_version)
{
    uint16_t abi_key = agcFirmwareAbiKey(raw_version);

    return agcFirmwareAliasContains(g_trinity_profile_aliases,
        sizeof(g_trinity_profile_aliases) /
            sizeof(g_trinity_profile_aliases[0]), abi_key);
}

static bool agcProsperoFirmwareIsArchival(uint32_t raw_version)
{
    uint16_t abi_key = agcFirmwareAbiKey(raw_version);

    return agcFirmwareAliasContains(g_legacy_v1_aliases,
               sizeof(g_legacy_v1_aliases) / sizeof(g_legacy_v1_aliases[0]),
               abi_key) ||
           agcFirmwareAliasContains(g_legacy_v2_aliases,
               sizeof(g_legacy_v2_aliases) / sizeof(g_legacy_v2_aliases[0]),
               abi_key) ||
           abi_key == 0x0300u;
}

bool agcProsperoFirmwareSupported(uint32_t raw_version)
{
    uint16_t abi_key = agcFirmwareAbiKey(raw_version);

    /* FW 5.50 and standard-PS5 FW 11.60 hardware-qualify the standard
     * submit16 path.  The external SPRX corpus independently verifies the
     * common submit, memory, queue, primary-suspend, TF-ring, HS-offchip, and
     * async carriers for every active key.  FW 3.20 retains its separately
     * recovered legacy-v3 completion policy.  Exact alias membership keeps
     * every uninspected key fail-closed. */
    return abi_key == 0x0320u ||
        agcProsperoStandardDirectAbiSupportsFirmware(raw_version);
}

bool agcProsperoBuildRuntimeProfile(uint32_t raw_version, bool is_trinity,
    AgcProsperoRuntimeProfile *profile_out)
{
    AgcProsperoDirectProfile direct_profile;

    if (!profile_out ||
        !agcProsperoBuildDirectProfile(raw_version, is_trinity,
            &direct_profile))
        return false;
    *profile_out = direct_profile.runtime;
    return true;
}

bool agcProsperoBuildDirectProfile(uint32_t raw_version, bool is_trinity,
    AgcProsperoDirectProfile *profile_out)
{
    AgcProsperoDirectProfile direct = {0};
    AgcProsperoRuntimeProfile profile = {0};
    uint16_t abi_key = agcFirmwareAbiKey(raw_version);

    if (!profile_out)
        return false;
    if (is_trinity && !agcProsperoFirmwareUsesTrinityPredicate(raw_version))
        return false;
    if (agcProsperoFirmwareIsArchival(raw_version))
        return false;

    if (agcFirmwareAliasContains(g_legacy_v3_aliases,
            sizeof(g_legacy_v3_aliases) / sizeof(g_legacy_v3_aliases[0]),
            abi_key)) {
        profile.family = AGC_PROSPERO_ABI_LEGACY_V3;
        profile.authenticated_special_queue = true;
        profile.eop_ring_offset = 0x39000u;
    } else if (agcProsperoStandardDirectAbiSupportsFirmware(raw_version)) {
        profile.family = AGC_PROSPERO_ABI_STANDARD;
        profile.authenticated_special_queue = true;
        profile.eop_ring_offset = 0x39000u;
    } else {
        return false;
    }

    profile.is_trinity = is_trinity;
    profile.gpu_info_span = is_trinity ? 0x180000u : 0x100000u;
    profile.cwsr_work_offset = is_trinity ? 0x1000000u : 0xa00000u;
    profile.cwsr_size = is_trinity ? 0x1600000u : 0x1000000u;
    direct.runtime = profile;
    direct.capabilities = AGC_DIRECT_CAP_SUBMIT;
    direct.defaults_version = AGC_DIRECT_DEFAULTS_VERSION_UNKNOWN;
    direct.submit_ioctl = AGC_GC_IOCTL_SUBMIT_16;

    /* Every inspected standard-firmware image shares the normalized submit16
     * and multi-DCB carrier group. In an exploited-payload context that group
     * uses the CLOSE transition plus a trailing NOP IB to prevent the final
     * descriptor from remaining deferred. FW 5.50 supplies the hardware proof;
     * the policy belongs to the compatibility group, not one firmware key. */
    direct.submit_uses_frame_close_trailer =
        profile.family == AGC_PROSPERO_ABI_STANDARD;

    /* Every active FW 3.20-12.70 image has a named public wrapper and a
     * fully fingerprinted private carrier for these three operations. Later
     * TF/HS groups only add explicit zeroing of the reserved fourth dword.
     * Archival FW 1.x, 2.x, and 3.00 remain outside this promotion. */
    if (abi_key == 0x0320u ||
        agcProsperoStandardDirectAbiSupportsFirmware(raw_version)) {
        direct.capabilities |= AGC_DIRECT_CAP_MEMORY | AGC_DIRECT_CAP_QUEUE |
            AGC_DIRECT_CAP_SUSPEND_PRIMARY | AGC_DIRECT_CAP_TF_RING |
            AGC_DIRECT_CAP_HS_OFFCHIP | AGC_DIRECT_CAP_ASYNC_GRAPHICS;
    }

    if (agcDirectDefaultsVersionForFirmware(abi_key,
            &direct.defaults_version))
        direct.capabilities |= AGC_DIRECT_CAP_DEFAULT_STATES;

    /* FW 5.50 and standard-PS5 FW 11.60 have hardware-qualified prerequisites
     * for the operations enabled here. AGC_DIRECT_CAP_WORKLOAD refers only to
     * OpenAGC's one-ID convenience submission, not Sony's incompatible
     * multi-argument export ABI. FW 11.60 maps that convenience call onto the
     * exact Sony stream packets after installing its GPU-info process property.
     * EOP flip and suspend query retain their narrower evidence boundaries. */
    if (abi_key == 0x0550u) {
        direct.capabilities |= AGC_DIRECT_CAP_SUSPEND_FINAL |
            AGC_DIRECT_CAP_WORKLOAD | AGC_DIRECT_CAP_EOP_FLIP;
    } else if (abi_key == 0x1160u) {
        direct.capabilities |= AGC_DIRECT_CAP_SUSPEND_FINAL;
        if (!is_trinity) {
            direct.capabilities |= AGC_DIRECT_CAP_WORKLOAD;
            direct.workload_uses_sony_stream_packet = true;
        }
    }

    if ((direct.capabilities & AGC_DIRECT_CAP_QUEUE) != 0) {
        direct.queue_create_ioctl = AGC_GC_IOCTL_QUEUE_CREATE;
        direct.queue_destroy_ioctl = AGC_GC_IOCTL_QUEUE_DESTROY;
    }
    if ((direct.capabilities & AGC_DIRECT_CAP_SUSPEND_PRIMARY) != 0)
        direct.suspend_primary_ioctl = AGC_GC_IOCTL_SUSPEND_16;
    if ((direct.capabilities & AGC_DIRECT_CAP_SUSPEND_FINAL) != 0)
        direct.suspend_final_ioctl = AGC_GC_IOCTL_SUSPEND_39;
    if ((direct.capabilities & AGC_DIRECT_CAP_TF_RING) != 0)
        direct.tf_ring_ioctl = AGC_GC_IOCTL_SET_TF_RING;
    if ((direct.capabilities & AGC_DIRECT_CAP_HS_OFFCHIP) != 0)
        direct.hs_offchip_ioctl = AGC_GC_IOCTL_SET_HS_OFFCHIP;
    if ((direct.capabilities & AGC_DIRECT_CAP_ASYNC_GRAPHICS) != 0)
        direct.async_graphics_ioctl = AGC_GC_IOCTL_QUEUE_STATUS;

    direct.runtime.supports_tf_ring =
        (direct.capabilities & AGC_DIRECT_CAP_TF_RING) != 0;
    *profile_out = direct;
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
    if (!agcProsperoFirmwareSupported(raw_version))
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
