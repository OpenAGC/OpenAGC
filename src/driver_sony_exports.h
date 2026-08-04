/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Private installed-Sony-driver loader contract.
 */

#ifndef OPENAGC_DRIVER_SONY_EXPORTS_H
#define OPENAGC_DRIVER_SONY_EXPORTS_H

#include "driver_ops.h"

typedef struct AgcSonyLoader {
    void *context;
    void *(*open_module)(void *context, const char *name);
    void *(*resolve_symbol)(void *context, void *module, const char *name);
    void (*close_module)(void *context, void *module);
} AgcSonyLoader;

typedef enum AgcSonyDriverLoadStatus {
    AGC_SONY_DRIVER_NOT_PRESENT = 0,
    AGC_SONY_DRIVER_READY,
    AGC_SONY_DRIVER_INCOMPATIBLE
} AgcSonyDriverLoadStatus;

typedef enum AgcSonyDriverExportSlot {
    AGC_SONY_EXPORT_SUBMIT_MULTI_DCBS = 0,
    AGC_SONY_EXPORT_SUBMIT_DCB,
    AGC_SONY_EXPORT_SUBMIT_ACB,
    AGC_SONY_EXPORT_SETUP_ASYNC_GRAPHICS,
    AGC_SONY_EXPORT_SET_TF_RING,
    AGC_SONY_EXPORT_GET_PA_DEBUG_INTERFACE_VERSION,
    AGC_SONY_EXPORT_SET_TARGET_RING_FOR_DIAG,
    AGC_SONY_EXPORT_SDMA_COPY_LINEAR_BLOCKING,
    AGC_SONY_EXPORT_REGISTER_CAPTURE_INTERFACE,
    AGC_SONY_EXPORT_DEREGISTER_CAPTURE_INTERFACE,
    AGC_SONY_EXPORT_ACQUIRE_RAZOR_ACQ,
    AGC_SONY_EXPORT_RELEASE_RAZOR_ACQ,
    AGC_SONY_EXPORT_SUBMIT_TO_RAZOR_ACQ,
    AGC_SONY_EXPORT_COUNT
} AgcSonyDriverExportSlot;

typedef struct AgcSonyDriverExport {
    const char *name;
    bool required;
} AgcSonyDriverExport;

typedef struct AgcSonyDriverProfile {
    uint16_t firmware_abi_key;
    const char *name;
    const char *module_name;
    const AgcSonyDriverExport *exports;
    size_t export_count;
} AgcSonyDriverProfile;

const AgcSonyDriverProfile *agcSonyDriverProfileForFirmware(
    uint32_t raw_version);

int32_t agcSonyDriverResolve(
    const AgcSonyLoader *loader, const AgcSonyDriverProfile *profile,
    AgcDriverOps *out_ops, void **out_module);

/* Resolves only an already-loaded module and never starts or unloads an SPRX. */
const AgcDriverOps *agcSonyDriverTryLoad(
    const AgcSonyDriverProfile *profile,
    AgcSonyDriverLoadStatus *status_out);
const char *agcSonyDriverDebugFailure(void);

#endif
