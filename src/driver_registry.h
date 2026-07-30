#ifndef OPENAGC_DRIVER_REGISTRY_H
#define OPENAGC_DRIVER_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "agc_ioctl.h"
#include "agc_runtime_diag.h"
#include "driver_ops.h"

typedef struct AgcFirmwareVersion {
    uint32_t raw;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} AgcFirmwareVersion;

typedef int32_t (*AgcFirmwareQueryFn)(void *context, uint32_t *raw_version);

typedef struct AgcFirmwareDetector {
    void *context;
    AgcFirmwareQueryFn query;
} AgcFirmwareDetector;

enum {
    AGC_BACKEND_CAP_NATIVE_SUBMIT = 1u << 0,
    AGC_BACKEND_CAP_COMPUTE = 1u << 1,
    AGC_BACKEND_CAP_GRAPHICS = 1u << 2
};

/* Private direct-/dev/gc ABI capabilities.  These are deliberately finer
 * grained than the public backend capabilities: an exact firmware can share
 * submit16 while differing in queue, suspend, ring, or defaults wrappers. */
enum {
    AGC_DIRECT_CAP_SUBMIT          = 1u << 0,
    AGC_DIRECT_CAP_MEMORY          = 1u << 1,
    AGC_DIRECT_CAP_QUEUE           = 1u << 2,
    AGC_DIRECT_CAP_SUSPEND_PRIMARY = 1u << 3,
    AGC_DIRECT_CAP_SUSPEND_FINAL   = 1u << 4,
    /* Reserved: public query is a permission stub; retain the bit position. */
    AGC_DIRECT_CAP_SUSPEND_QUERY   = 1u << 5,
    AGC_DIRECT_CAP_WORKLOAD        = 1u << 6,
    AGC_DIRECT_CAP_TF_RING         = 1u << 7,
    AGC_DIRECT_CAP_HS_OFFCHIP      = 1u << 8,
    AGC_DIRECT_CAP_DEFAULT_STATES  = 1u << 9,
    AGC_DIRECT_CAP_ASYNC_GRAPHICS  = 1u << 10,
    AGC_DIRECT_CAP_EOP_FLIP        = 1u << 11
};

enum {
    AGC_DIRECT_SHADOW_PROPERTY_GN2 = 1u << 0,
    AGC_DIRECT_SHADOW_PROPERTY_GN3 = 1u << 1,
    AGC_DIRECT_SHADOW_PROPERTY_GN4 = 1u << 2
};

#define AGC_DIRECT_DEFAULTS_VERSION_UNKNOWN UINT32_MAX

typedef struct AgcProsperoDirectProfile {
    AgcProsperoRuntimeProfile runtime;
    uint32_t capabilities;
    /* Highest caller-supplied sceAgcInit version accepted by this firmware. */
    uint32_t defaults_max_version;
    bool submit_uses_frame_close_trailer;
    bool workload_has_sony_stream_table;
    bool workload_uses_sony_stream_packet;
    uint32_t shadow_process_properties;
    uint32_t submit_ioctl;
    uint32_t queue_create_ioctl;
    uint32_t queue_destroy_ioctl;
    uint32_t suspend_primary_ioctl;
    uint32_t suspend_final_ioctl;
    uint32_t suspend_query_ioctl;
    uint32_t tf_ring_ioctl;
    uint32_t hs_offchip_ioctl;
    uint32_t async_graphics_ioctl;
} AgcProsperoDirectProfile;

typedef struct AgcProsperoDefaultsLayout {
    uint32_t primary_cx_length;
    uint32_t primary_sh_length;
    uint32_t primary_uc_length;
    uint32_t internal_cx_length;
    uint32_t internal_sh_length;
    uint32_t internal_uc_length;
    size_t primary_blob_size;
    size_t internal_blob_size;
} AgcProsperoDefaultsLayout;

typedef struct AgcDriverRegistryEntry {
    const char *name;
    const uint32_t *firmware_aliases;
    size_t firmware_alias_count;
    uint32_t capabilities;
    const AgcDriverOps *ops;
} AgcDriverRegistryEntry;

AgcFirmwareVersion agcFirmwareNormalize(uint32_t raw_version);
uint16_t agcDriverRuntimeFirmwareAbiKey(void);
bool agcProsperoFirmwareSupported(uint32_t raw_version);
bool agcProsperoStandardDirectAbiSupportsFirmware(uint32_t raw_version);
bool agcProsperoFirmwareUsesTrinityPredicate(uint32_t raw_version);
bool agcProsperoBuildRuntimeProfile(uint32_t raw_version, bool is_trinity,
    AgcProsperoRuntimeProfile *profile_out);
bool agcProsperoBuildDirectProfile(uint32_t raw_version, bool is_trinity,
    AgcProsperoDirectProfile *profile_out);
bool agcProsperoDirectProfileAcceptsDefaultsVersion(
    const AgcProsperoDirectProfile *profile, uint32_t version);
bool agcProsperoDefaultsLayoutForVersion(uint32_t version,
    AgcProsperoDefaultsLayout *layout_out);
bool agcProsperoBuildStandardRegisterShadowDescriptors(uint64_t driver_base,
    AgcGcRegisterShadowDescriptor descriptors_out[2]);
const AgcDriverRegistryEntry *agcDriverRegistryLookup(
    const AgcDriverRegistryEntry *entries, size_t entry_count,
    uint32_t raw_version, uint32_t required_capabilities);
int32_t agcDriverSelectFromRegistry(const AgcFirmwareDetector *detector,
    const AgcDriverRegistryEntry *entries, size_t entry_count,
    uint32_t required_capabilities, AgcFirmwareVersion *version_out,
    const AgcDriverOps **ops_out);
int32_t agcDriverSelectRuntime(AgcFirmwareVersion *version_out,
    const AgcDriverOps **ops_out);

#endif
