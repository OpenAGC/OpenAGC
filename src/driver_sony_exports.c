/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Installed libSceAgcDriver export forwarding. Symbols are resolved from a
 * module-specific handle so OpenAGC's identically named wrappers cannot be
 * selected through RTLD_DEFAULT.
 */

#include "driver_sony_exports.h"

#include <string.h>

#ifdef OPENAGC_PROSPERO
#include <dlfcn.h>
#endif

#define AGC_SONY_DRIVER_MODULE "libSceAgcDriver.sprx"
#define AGC_SONY_MAX_SUBMIT_DCBS 0xfffu

static const char *g_sony_failure;
typedef int32_t (PS5_SYSV_ABI *AgcSonySubmitMultiDcbsFn)(
    void *const[], const uint32_t *, uint32_t);
static AgcSonySubmitMultiDcbsFn g_sony_submit_multi_dcbs;

static int32_t PS5_SYSV_ABI agcSonyInitialize(void)
{
    /* dlopen runs libSceAgcDriver's module_start initialization. */
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI agcSonyInitializeInternalMemory(void)
{
    /* Internal regions are owned and initialized by the installed module. */
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI agcSonyNotifyDefaultStates(uint32_t flags)
{
    (void)flags;
    /* The installed module performed its six-argument defaults handshake
     * during module_start; OpenAGC's one-argument helper is not ABI-identical. */
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI agcSonySubmitMultiCommandBuffersDirect(
    uint32_t count, void *const dcb_gpu_addrs[], uint32_t *dcb_sizes_in_bytes,
    void *const acb_gpu_addrs[], uint32_t *acb_sizes_in_bytes)
{
    uint32_t dword_sizes[AGC_SONY_MAX_SUBMIT_DCBS];

    if (!g_sony_submit_multi_dcbs)
        return AGC_ERROR_NOT_SUPPORTED;
    if (count == 0 || count > AGC_SONY_MAX_SUBMIT_DCBS ||
        !dcb_gpu_addrs || !dcb_sizes_in_bytes)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (acb_gpu_addrs || acb_sizes_in_bytes)
        return AGC_ERROR_NOT_SUPPORTED;
    for (uint32_t i = 0; i < count; ++i) {
        if (!dcb_gpu_addrs[i] || dcb_sizes_in_bytes[i] == 0 ||
            (dcb_sizes_in_bytes[i] & 3u) != 0)
            return AGC_ERROR_INVALID_ARGUMENT;
        dword_sizes[i] = dcb_sizes_in_bytes[i] / 4u;
    }
    return g_sony_submit_multi_dcbs(
        dcb_gpu_addrs, dword_sizes, count);
}

static int32_t agcSonyReject(
    const AgcSonyLoader *loader, void *module, AgcDriverOps *out_ops,
    void **out_module)
{
    if (module && loader->close_module)
        loader->close_module(loader->context, module);
    memset(out_ops, 0, sizeof(*out_ops));
    *out_module = NULL;
    return AGC_ERROR_NOT_SUPPORTED;
}

#define AGC_SONY_RESOLVE(field, symbol_name, public_symbol, required) do { \
    void *address = loader->resolve_symbol( \
        loader->context, module, symbol_name); \
    _Static_assert(sizeof(out_ops->field) == sizeof(address), \
        "Sony export pointer size"); \
    if (address) { \
        memcpy(&out_ops->field, &address, sizeof(address)); \
        if (out_ops->field == public_symbol) { \
            g_sony_failure = symbol_name; \
            return agcSonyReject(loader, module, out_ops, out_module); \
        } \
    } else if (required) { \
        g_sony_failure = symbol_name; \
        return agcSonyReject(loader, module, out_ops, out_module); \
    } \
} while (0)

int32_t agcSonyDriverResolve(
    const AgcSonyLoader *loader, AgcDriverOps *out_ops, void **out_module)
{
    void *module;

    if (!loader || !loader->open_module || !loader->resolve_symbol ||
        !out_ops || !out_module)
        return AGC_ERROR_INVALID_ARGUMENT;

    memset(out_ops, 0, sizeof(*out_ops));
    *out_module = NULL;
    g_sony_failure = NULL;
    g_sony_submit_multi_dcbs = NULL;
    module = loader->open_module(loader->context, AGC_SONY_DRIVER_MODULE);
    if (!module)
        return AGC_ERROR_NOT_SUPPORTED;

    out_ops->name = "sony-installed";
    out_ops->initialize = agcSonyInitialize;
    out_ops->initialize_internal_memory = agcSonyInitializeInternalMemory;
    out_ops->notify_default_states = agcSonyNotifyDefaultStates;

    /* Mandatory firmware-sensitive surface. A partial set is not selected. */
    {
        void *address = loader->resolve_symbol(loader->context, module,
            "sceAgcDriverSubmitMultiDcbs");
        if (address)
            memcpy(&g_sony_submit_multi_dcbs, &address, sizeof(address));
        if (!g_sony_submit_multi_dcbs ||
            g_sony_submit_multi_dcbs == sceAgcDriverSubmitMultiDcbs) {
            g_sony_failure = "sceAgcDriverSubmitMultiDcbs";
            return agcSonyReject(loader, module, out_ops, out_module);
        }
        out_ops->submit_multi_command_buffers_direct =
            agcSonySubmitMultiCommandBuffersDirect;
    }
    AGC_SONY_RESOLVE(submit_dcb, "sceAgcDriverSubmitDcb",
        sceAgcDriverSubmitDcb, true);
    AGC_SONY_RESOLVE(submit_acb, "sceAgcDriverSubmitAcb",
        sceAgcDriverSubmitAcb, true);
    AGC_SONY_RESOLVE(setup_async_graphics, "sceAgcDriverSetupAsyncGraphics",
        sceAgcDriverSetupAsyncGraphics, true);
    AGC_SONY_RESOLVE(create_user_special_queue,
        "_sceAgcDriverCreateUserSpecialQueue",
        _sceAgcDriverCreateUserSpecialQueue, false);
    AGC_SONY_RESOLVE(destroy_user_special_queue,
        "_sceAgcDriverDestroyUserSpecialQueue",
        _sceAgcDriverDestroyUserSpecialQueue, false);
    AGC_SONY_RESOLVE(get_pa_debug_interface_version,
        "sceAgcDriverGetPaDebugInterfaceVersion",
        sceAgcDriverGetPaDebugInterfaceVersion, true);

    /* The public Direct suspend exports are permission stubs. The installed
     * driver's private primary/final carriers are not exported, so both
     * OpenAGC internal operations remain NULL and fail closed. */

    /* Optional operations remain NULL when absent and dispatch safely as
     * AGC_ERROR_NOT_SUPPORTED. */
    AGC_SONY_RESOLVE(set_tf_ring_direct, "sceAgcDriverSetTFRingDirect",
        sceAgcDriverSetTFRingDirect, false);
    AGC_SONY_RESOLVE(set_hs_offchip_param_direct,
        "sceAgcDriverSetHsOffchipParamDirect",
        sceAgcDriverSetHsOffchipParamDirect, false);
    AGC_SONY_RESOLVE(set_target_ring_for_diag,
        "sceAgcDriverSetTargetRingForDiag",
        sceAgcDriverSetTargetRingForDiag, false);
    AGC_SONY_RESOLVE(sdma_copy_linear_blocking,
        "sceAgcDriverSdmaCopyLinearBlocking",
        sceAgcDriverSdmaCopyLinearBlocking, false);
    AGC_SONY_RESOLVE(submit_eop_flip, "sceAgcDriverSubmitEopFlip",
        sceAgcDriverSubmitEopFlip, false);
    AGC_SONY_RESOLVE(register_capture_interface,
        "sceAgcDriverRegisterCaptureInterface",
        sceAgcDriverRegisterCaptureInterface, false);
    AGC_SONY_RESOLVE(deregister_capture_interface,
        "sceAgcDriverDeregisterCaptureInterface",
        sceAgcDriverDeregisterCaptureInterface, false);
    AGC_SONY_RESOLVE(acquire_razor_acq, "sceAgcDriverAcquireRazorACQ",
        sceAgcDriverAcquireRazorACQ, false);
    AGC_SONY_RESOLVE(release_razor_acq, "sceAgcDriverReleaseRazorACQ",
        sceAgcDriverReleaseRazorACQ, false);
    AGC_SONY_RESOLVE(submit_to_razor_acq, "sceAgcDriverSubmitToRazorACQ",
        sceAgcDriverSubmitToRazorACQ, false);
    AGC_SONY_RESOLVE(submit_to_hdr_scopes_acq,
        "sceAgcDriverSubmitToHDRScopesACQ",
        sceAgcDriverSubmitToHDRScopesACQ, false);

    *out_module = module;
    return AGC_OK;
}

const char *agcSonyDriverDebugFailure(void)
{
    return g_sony_failure;
}

#ifdef OPENAGC_PROSPERO
static bool g_sony_attempted;
static void *g_sony_module;
static AgcDriverOps g_sony_ops;

static void *agcSonyDlopen(void *context, const char *name)
{
    (void)context;
    return dlopen(name, RTLD_LAZY);
}

static void *agcSonyDlsym(void *context, void *module, const char *name)
{
    (void)context;
    return dlsym(module, name);
}

static void agcSonyDlclose(void *context, void *module)
{
    (void)context;
    (void)dlclose(module);
}

const AgcDriverOps *agcSonyDriverTryLoad(void)
{
    const AgcSonyLoader loader = {
        .open_module = agcSonyDlopen,
        .resolve_symbol = agcSonyDlsym,
        .close_module = agcSonyDlclose,
    };

    if (!g_sony_attempted) {
        g_sony_attempted = true;
        if (agcSonyDriverResolve(
                &loader, &g_sony_ops, &g_sony_module) != AGC_OK)
            g_sony_module = NULL;
    }
    return g_sony_module ? &g_sony_ops : NULL;
}
#else
const AgcDriverOps *agcSonyDriverTryLoad(void)
{
    return NULL;
}
#endif
