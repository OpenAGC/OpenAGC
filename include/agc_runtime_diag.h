#ifndef OPENAGC_RUNTIME_DIAG_H
#define OPENAGC_RUNTIME_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "agc_types.h"

/* OpenAGC extension for runtime backend diagnostics; not a Sony SDK ABI. */
typedef enum AgcProsperoAbiFamily {
    AGC_PROSPERO_ABI_UNSUPPORTED = 0,
    AGC_PROSPERO_ABI_LEGACY_V1,
    AGC_PROSPERO_ABI_LEGACY_V2,
    AGC_PROSPERO_ABI_LEGACY_V3,
    AGC_PROSPERO_ABI_STANDARD
} AgcProsperoAbiFamily;

typedef struct AgcProsperoRuntimeProfile {
    AgcProsperoAbiFamily family;
    bool is_trinity;
    bool authenticated_special_queue;
    bool supports_tf_ring;
    uint32_t eop_ring_offset;
    uint32_t gpu_info_span;
    uint32_t cwsr_work_offset;
    uint32_t cwsr_size;
} AgcProsperoRuntimeProfile;

typedef struct AgcDriverRuntimeDiagnostics {
    uint32_t firmware_version;
    const char *backend_name;
    AgcProsperoRuntimeProfile profile;
} AgcDriverRuntimeDiagnostics;

const char *PS5_SYSV_ABI agcProsperoAbiFamilyName(
    AgcProsperoAbiFamily family);
int32_t PS5_SYSV_ABI agcDriverDebugRuntimeProfile(
    AgcDriverRuntimeDiagnostics *diagnostics);

#endif
