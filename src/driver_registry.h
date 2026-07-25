#ifndef OPENAGC_DRIVER_REGISTRY_H
#define OPENAGC_DRIVER_REGISTRY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

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

typedef struct AgcDriverRegistryEntry {
    const char *name;
    const uint32_t *firmware_aliases;
    size_t firmware_alias_count;
    uint32_t capabilities;
    const AgcDriverOps *ops;
} AgcDriverRegistryEntry;

AgcFirmwareVersion agcFirmwareNormalize(uint32_t raw_version);
bool agcProsperoStandardDirectAbiSupportsFirmware(uint32_t raw_version);
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
