/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Installed libSceAgcDriver export forwarding. Symbols are resolved from a
 * module-specific handle so OpenAGC's identically named wrappers cannot be
 * selected through a process-global lookup.
 */

#include "driver_sony_exports.h"

#include <string.h>

#ifdef OPENAGC_PROSPERO
#include <ps5/kernel.h>
#endif

#define AGC_SONY_MAX_SUBMIT_DCBS 0xfffu
#define AGC_SONY_REQUIRED(symbol) {symbol, true}
#define AGC_SONY_OPTIONAL(symbol) {symbol, false}
#define AGC_SONY_UNAVAILABLE {NULL, false}

static const AgcSonyDriverExport g_legacy_v3_exports[] = {
    [AGC_SONY_EXPORT_SUBMIT_MULTI_DCBS] =
        AGC_SONY_REQUIRED("sceAgcDriverSubmitMultiDcbs"),
    [AGC_SONY_EXPORT_SUBMIT_DCB] =
        AGC_SONY_REQUIRED("sceAgcDriverSubmitDcb"),
    [AGC_SONY_EXPORT_SUBMIT_ACB] =
        AGC_SONY_REQUIRED("sceAgcDriverSubmitAcb"),
    [AGC_SONY_EXPORT_SETUP_ASYNC_GRAPHICS] =
        AGC_SONY_REQUIRED("sceAgcDriverSetupAsyncGraphics"),
    [AGC_SONY_EXPORT_GET_PA_DEBUG_INTERFACE_VERSION] =
        AGC_SONY_REQUIRED("sceAgcDriverGetPaDebugInterfaceVersion"),
    [AGC_SONY_EXPORT_SET_TARGET_RING_FOR_DIAG] =
        AGC_SONY_OPTIONAL("sceAgcDriverSetTargetRingForDiag"),
    [AGC_SONY_EXPORT_SDMA_COPY_LINEAR_BLOCKING] = AGC_SONY_UNAVAILABLE,
    [AGC_SONY_EXPORT_REGISTER_CAPTURE_INTERFACE] =
        AGC_SONY_OPTIONAL("sceAgcDriverRegisterCaptureInterface"),
    [AGC_SONY_EXPORT_DEREGISTER_CAPTURE_INTERFACE] =
        AGC_SONY_OPTIONAL("sceAgcDriverDeregisterCaptureInterface"),
    [AGC_SONY_EXPORT_ACQUIRE_RAZOR_ACQ] =
        AGC_SONY_OPTIONAL("sceAgcDriverAcquireRazorACQ"),
    [AGC_SONY_EXPORT_RELEASE_RAZOR_ACQ] =
        AGC_SONY_OPTIONAL("sceAgcDriverReleaseRazorACQ"),
    [AGC_SONY_EXPORT_SUBMIT_TO_RAZOR_ACQ] =
        AGC_SONY_OPTIONAL("sceAgcDriverSubmitToRazorACQ"),
};

static const AgcSonyDriverExport g_standard_exports[] = {
    [AGC_SONY_EXPORT_SUBMIT_MULTI_DCBS] =
        AGC_SONY_REQUIRED("sceAgcDriverSubmitMultiDcbs"),
    [AGC_SONY_EXPORT_SUBMIT_DCB] =
        AGC_SONY_REQUIRED("sceAgcDriverSubmitDcb"),
    [AGC_SONY_EXPORT_SUBMIT_ACB] =
        AGC_SONY_REQUIRED("sceAgcDriverSubmitAcb"),
    [AGC_SONY_EXPORT_SETUP_ASYNC_GRAPHICS] =
        AGC_SONY_REQUIRED("sceAgcDriverSetupAsyncGraphics"),
    [AGC_SONY_EXPORT_GET_PA_DEBUG_INTERFACE_VERSION] =
        AGC_SONY_REQUIRED("sceAgcDriverGetPaDebugInterfaceVersion"),
    [AGC_SONY_EXPORT_SET_TARGET_RING_FOR_DIAG] =
        AGC_SONY_OPTIONAL("sceAgcDriverSetTargetRingForDiag"),
    [AGC_SONY_EXPORT_SDMA_COPY_LINEAR_BLOCKING] =
        AGC_SONY_OPTIONAL("sceAgcDriverSdmaCopyLinearBlocking"),
    [AGC_SONY_EXPORT_REGISTER_CAPTURE_INTERFACE] =
        AGC_SONY_OPTIONAL("sceAgcDriverRegisterCaptureInterface"),
    [AGC_SONY_EXPORT_DEREGISTER_CAPTURE_INTERFACE] =
        AGC_SONY_OPTIONAL("sceAgcDriverDeregisterCaptureInterface"),
    [AGC_SONY_EXPORT_ACQUIRE_RAZOR_ACQ] =
        AGC_SONY_OPTIONAL("sceAgcDriverAcquireRazorACQ"),
    [AGC_SONY_EXPORT_RELEASE_RAZOR_ACQ] =
        AGC_SONY_OPTIONAL("sceAgcDriverReleaseRazorACQ"),
    [AGC_SONY_EXPORT_SUBMIT_TO_RAZOR_ACQ] =
        AGC_SONY_OPTIONAL("sceAgcDriverSubmitToRazorACQ"),
};

_Static_assert(sizeof(g_legacy_v3_exports) /
    sizeof(g_legacy_v3_exports[0]) == AGC_SONY_EXPORT_COUNT,
    "legacy v3 Sony export manifest");
_Static_assert(sizeof(g_standard_exports) /
    sizeof(g_standard_exports[0]) == AGC_SONY_EXPORT_COUNT,
    "standard Sony export manifest");

#define AGC_SONY_PROFILE(key, label, manifest) { \
    .firmware_abi_key = key, \
    .name = label, \
    .module_name = "libSceAgcDriver.sprx", \
    .exports = manifest, \
    .export_count = AGC_SONY_EXPORT_COUNT, \
}

static const AgcSonyDriverProfile g_sony_driver_profiles[] = {
    AGC_SONY_PROFILE(0x0320u, "fw320-installed", g_legacy_v3_exports),
    AGC_SONY_PROFILE(0x0400u, "fw400-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0403u, "fw403-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0450u, "fw450-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0451u, "fw451-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0502u, "fw502-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0510u, "fw510-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0550u, "fw550-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0600u, "fw600-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0602u, "fw602-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0650u, "fw650-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0701u, "fw701-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0720u, "fw720-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0740u, "fw740-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0760u, "fw760-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0761u, "fw761-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0800u, "fw800-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0820u, "fw820-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0840u, "fw840-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0860u, "fw860-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0900u, "fw900-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0905u, "fw905-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0920u, "fw920-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0940u, "fw940-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x0960u, "fw960-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1001u, "fw1001-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1020u, "fw1020-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1040u, "fw1040-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1060u, "fw1060-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1100u, "fw1100-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1120u, "fw1120-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1140u, "fw1140-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1160u, "fw1160-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1200u, "fw1200-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1202u, "fw1202-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1220u, "fw1220-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1240u, "fw1240-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1260u, "fw1260-installed", g_standard_exports),
    AGC_SONY_PROFILE(0x1270u, "fw1270-installed", g_standard_exports),
};

static const char *g_sony_failure;
typedef int32_t (PS5_SYSV_ABI *AgcSonySubmitMultiDcbsFn)(
    void *const[], const uint32_t *, uint32_t);
static AgcSonySubmitMultiDcbsFn g_sony_submit_multi_dcbs;

const AgcSonyDriverProfile *agcSonyDriverProfileForFirmware(
    uint32_t raw_version)
{
    uint16_t abi_key = (uint16_t)(raw_version >> 16);

    for (size_t i = 0; i < sizeof(g_sony_driver_profiles) /
            sizeof(g_sony_driver_profiles[0]); ++i) {
        if (g_sony_driver_profiles[i].firmware_abi_key == abi_key)
            return &g_sony_driver_profiles[i];
    }
    return NULL;
}

static int32_t PS5_SYSV_ABI agcSonyInitialize(void)
{
#ifdef OPENAGC_PROSPERO
    return agcProsperoPrepareGpuCredentials();
#else
    return AGC_OK;
#endif
}

static int32_t PS5_SYSV_ABI agcSonyInitializeInternalMemory(void)
{
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI agcSonyShutdown(void)
{
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI agcSonyNotifyDefaultStates(uint32_t flags)
{
    (void)flags;
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
    return g_sony_submit_multi_dcbs(dcb_gpu_addrs, dword_sizes, count);
}

static bool agcSonyProfileIsValid(const AgcSonyDriverProfile *profile)
{
    static const AgcSonyDriverExportSlot required_slots[] = {
        AGC_SONY_EXPORT_SUBMIT_MULTI_DCBS,
        AGC_SONY_EXPORT_SUBMIT_DCB,
        AGC_SONY_EXPORT_SUBMIT_ACB,
        AGC_SONY_EXPORT_SETUP_ASYNC_GRAPHICS,
        AGC_SONY_EXPORT_GET_PA_DEBUG_INTERFACE_VERSION,
    };

    if (!profile || !profile->name || !profile->module_name ||
        !profile->exports || profile->export_count != AGC_SONY_EXPORT_COUNT)
        return false;
    for (size_t i = 0; i < sizeof(required_slots) /
            sizeof(required_slots[0]); ++i) {
        const AgcSonyDriverExport *entry = &profile->exports[required_slots[i]];
        if (!entry->required || !entry->name)
            return false;
    }
    return true;
}

static int32_t agcSonyReject(const AgcSonyLoader *loader, void *module,
    AgcDriverOps *out_ops, void **out_module)
{
    if (module && loader->close_module)
        loader->close_module(loader->context, module);
    memset(out_ops, 0, sizeof(*out_ops));
    *out_module = NULL;
    return AGC_ERROR_NOT_SUPPORTED;
}

#define AGC_SONY_RESOLVE(field, slot, public_symbol) do { \
    const AgcSonyDriverExport *entry = &profile->exports[slot]; \
    void *address = entry->name ? loader->resolve_symbol( \
        loader->context, module, entry->name) : NULL; \
    _Static_assert(sizeof(out_ops->field) == sizeof(address), \
        "Sony export pointer size"); \
    if (address) { \
        memcpy(&out_ops->field, &address, sizeof(address)); \
        if (out_ops->field == public_symbol) { \
            g_sony_failure = entry->name; \
            return agcSonyReject(loader, module, out_ops, out_module); \
        } \
    } else if (entry->required) { \
        g_sony_failure = entry->name; \
        return agcSonyReject(loader, module, out_ops, out_module); \
    } \
} while (0)

int32_t agcSonyDriverResolve(const AgcSonyLoader *loader,
    const AgcSonyDriverProfile *profile, AgcDriverOps *out_ops,
    void **out_module)
{
    void *module;

    if (!loader || !loader->open_module || !loader->resolve_symbol ||
        !agcSonyProfileIsValid(profile) || !out_ops || !out_module)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset(out_ops, 0, sizeof(*out_ops));
    *out_module = NULL;
    g_sony_failure = NULL;
    g_sony_submit_multi_dcbs = NULL;
    module = loader->open_module(loader->context, profile->module_name);
    if (!module)
        return AGC_ERROR_NOT_SUPPORTED;

    out_ops->name = "sony-installed";
    out_ops->initialize = agcSonyInitialize;
    out_ops->initialize_internal_memory = agcSonyInitializeInternalMemory;
    out_ops->shutdown = agcSonyShutdown;
    out_ops->notify_default_states = agcSonyNotifyDefaultStates;

    {
        const AgcSonyDriverExport *entry =
            &profile->exports[AGC_SONY_EXPORT_SUBMIT_MULTI_DCBS];
        void *address = loader->resolve_symbol(
            loader->context, module, entry->name);
        if (address)
            memcpy(&g_sony_submit_multi_dcbs, &address, sizeof(address));
        if (!g_sony_submit_multi_dcbs ||
            g_sony_submit_multi_dcbs == sceAgcDriverSubmitMultiDcbs) {
            g_sony_failure = entry->name;
            return agcSonyReject(loader, module, out_ops, out_module);
        }
        out_ops->submit_multi_command_buffers_direct =
            agcSonySubmitMultiCommandBuffersDirect;
    }
    AGC_SONY_RESOLVE(submit_dcb, AGC_SONY_EXPORT_SUBMIT_DCB,
        sceAgcDriverSubmitDcb);
    AGC_SONY_RESOLVE(submit_acb, AGC_SONY_EXPORT_SUBMIT_ACB,
        sceAgcDriverSubmitAcb);
    AGC_SONY_RESOLVE(setup_async_graphics,
        AGC_SONY_EXPORT_SETUP_ASYNC_GRAPHICS,
        sceAgcDriverSetupAsyncGraphics);
    AGC_SONY_RESOLVE(get_pa_debug_interface_version,
        AGC_SONY_EXPORT_GET_PA_DEBUG_INTERFACE_VERSION,
        sceAgcDriverGetPaDebugInterfaceVersion);
    AGC_SONY_RESOLVE(set_target_ring_for_diag,
        AGC_SONY_EXPORT_SET_TARGET_RING_FOR_DIAG,
        sceAgcDriverSetTargetRingForDiag);
    AGC_SONY_RESOLVE(sdma_copy_linear_blocking,
        AGC_SONY_EXPORT_SDMA_COPY_LINEAR_BLOCKING,
        sceAgcDriverSdmaCopyLinearBlocking);
    AGC_SONY_RESOLVE(register_capture_interface,
        AGC_SONY_EXPORT_REGISTER_CAPTURE_INTERFACE,
        sceAgcDriverRegisterCaptureInterface);
    AGC_SONY_RESOLVE(deregister_capture_interface,
        AGC_SONY_EXPORT_DEREGISTER_CAPTURE_INTERFACE,
        sceAgcDriverDeregisterCaptureInterface);
    AGC_SONY_RESOLVE(acquire_razor_acq,
        AGC_SONY_EXPORT_ACQUIRE_RAZOR_ACQ, sceAgcDriverAcquireRazorACQ);
    AGC_SONY_RESOLVE(release_razor_acq,
        AGC_SONY_EXPORT_RELEASE_RAZOR_ACQ, sceAgcDriverReleaseRazorACQ);
    AGC_SONY_RESOLVE(submit_to_razor_acq,
        AGC_SONY_EXPORT_SUBMIT_TO_RAZOR_ACQ,
        sceAgcDriverSubmitToRazorACQ);

    *out_module = module;
    return AGC_OK;
}

const char *agcSonyDriverDebugFailure(void)
{
    return g_sony_failure;
}

#ifdef OPENAGC_PROSPERO
static bool g_sony_attempted;
static AgcSonyDriverLoadStatus g_sony_status;
static const AgcSonyDriverProfile *g_sony_profile;
static void *g_sony_module;
static AgcDriverOps g_sony_ops;

static void *agcSonyFindLoadedModule(void *context, const char *name)
{
    uint32_t handle = 0;

    (void)context;
    if (kernel_dynlib_handle(-1, name, &handle) != 0 || handle == 0)
        return NULL;
    return (void *)(uintptr_t)handle;
}

static void *agcSonyResolveLoadedSymbol(
    void *context, void *module, const char *name)
{
    (void)context;
    return (void *)(uintptr_t)kernel_dynlib_dlsym(
        -1, (uint32_t)(uintptr_t)module, name);
}

static void agcSonyKeepLoadedModule(void *context, void *module)
{
    (void)context;
    (void)module;
}

const AgcDriverOps *agcSonyDriverTryLoad(
    const AgcSonyDriverProfile *profile,
    AgcSonyDriverLoadStatus *status_out)
{
    const AgcSonyLoader loader = {
        .open_module = agcSonyFindLoadedModule,
        .resolve_symbol = agcSonyResolveLoadedSymbol,
        .close_module = agcSonyKeepLoadedModule,
    };

    if (!profile) {
        if (status_out)
            *status_out = AGC_SONY_DRIVER_NOT_PRESENT;
        return NULL;
    }
    if (g_sony_attempted && g_sony_profile != profile) {
        if (status_out)
            *status_out = AGC_SONY_DRIVER_INCOMPATIBLE;
        return NULL;
    }
    if (!g_sony_attempted || g_sony_status == AGC_SONY_DRIVER_NOT_PRESENT) {
        g_sony_attempted = true;
        g_sony_profile = profile;
        if (agcSonyDriverResolve(
                &loader, profile, &g_sony_ops, &g_sony_module) == AGC_OK) {
            g_sony_status = AGC_SONY_DRIVER_READY;
        } else if (agcSonyDriverDebugFailure()) {
            g_sony_status = AGC_SONY_DRIVER_INCOMPATIBLE;
        } else {
            g_sony_status = AGC_SONY_DRIVER_NOT_PRESENT;
        }
    }
    if (status_out)
        *status_out = g_sony_status;
    return g_sony_module ? &g_sony_ops : NULL;
}
#else
const AgcDriverOps *agcSonyDriverTryLoad(
    const AgcSonyDriverProfile *profile,
    AgcSonyDriverLoadStatus *status_out)
{
    (void)profile;
    if (status_out)
        *status_out = AGC_SONY_DRIVER_NOT_PRESENT;
    return NULL;
}
#endif
