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

int32_t agcSonyDriverResolve(
    const AgcSonyLoader *loader, AgcDriverOps *out_ops, void **out_module);

/* Retains the module handle for process lifetime on success. */
const AgcDriverOps *agcSonyDriverTryLoad(void);
const char *agcSonyDriverDebugFailure(void);

#endif
