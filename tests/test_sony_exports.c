#include "test.h"
#include "agc_context.h"
#include "agc_graphics.h"
#include "driver_sony_exports.h"
#include "openagc/runtime.h"

int32_t test_runtime_create_tessellation_fixture(AgcDevice device,
    AgcGraphicsPipeline *pipeline_out, AgcShader *hull_out,
    AgcShader *evaluation_out, AgcShader *pixel_out);

typedef struct FakeSonyLoader {
    bool module_absent;
    const char *missing_module;
    const char *missing_symbol;
    const char *recursive_symbol;
    uint32_t open_count;
    uint32_t close_count;
} FakeSonyLoader;

static uint32_t g_fake_multi_count;
static uint32_t g_fake_multi_sizes[2];
static uint32_t g_fake_submit_dcb_calls;
static int32_t g_fake_submit_dcb_result;
static uint32_t g_fake_submit_acb_calls;
static uint32_t g_fake_submit_acb_owner;
static uint32_t g_fake_async_calls;
static uint32_t g_fake_async_pipe;
static int32_t g_fake_async_result;
static uint32_t g_fake_pa_debug_calls;
static uintptr_t g_fake_tf_ring_address;
static uint32_t g_fake_tf_ring_size;
static uint32_t g_fake_tf_ring_calls;
static uint32_t g_fake_defaults_calls;
static uint32_t g_fake_defaults_counts[3];
static AgcRegisterDefaultValue g_fake_defaults_first[3];
static uint32_t g_fake_runtime_init_calls;
static uint32_t g_fake_runtime_init_version;
static int32_t g_fake_runtime_init_result;
static bool g_fake_runtime_init_calls_notify;
static uint32_t g_fake_reserved_dmem_calls;

static int32_t PS5_SYSV_ABI fakeSonyGetReservedDmem(
    uintptr_t *address, uint32_t *size)
{
    if (!address || !size)
        return AGC_ERROR_INVALID_ARGUMENT;
    ++g_fake_reserved_dmem_calls;
    *address = UINT64_C(0xfe0040000);
    *size = 0x10000u;
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI fakeSonySubmitMultiDcbs(
    void *const dcb_gpu_addrs[], const uint32_t *dcb_sizes_in_dwords,
    uint32_t count)
{
    if (!dcb_gpu_addrs || !dcb_sizes_in_dwords || count == 0 || count > 2)
        return AGC_ERROR_INVALID_ARGUMENT;
    g_fake_multi_count = count;
    memcpy(g_fake_multi_sizes, dcb_sizes_in_dwords,
        count * sizeof(dcb_sizes_in_dwords[0]));
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI fakeSonySubmitDcb(
    const AgcCommandBufferSubmit *packet)
{
    if (!packet || !packet->command_address || packet->dword_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    ++g_fake_submit_dcb_calls;
    return g_fake_submit_dcb_result;
}

static int32_t PS5_SYSV_ABI fakeSonySubmitAcb(
    uint32_t owner_handle, const AgcCommandBufferSubmit *packet)
{
    if (!packet || !packet->command_address || packet->dword_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    ++g_fake_submit_acb_calls;
    g_fake_submit_acb_owner = owner_handle;
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI fakeSonySetupAsyncGraphics(uint32_t pipe_id)
{
    ++g_fake_async_calls;
    g_fake_async_pipe = pipe_id;
    return g_fake_async_result;
}

static uint32_t PS5_SYSV_ABI fakeSonyGetPaDebugInterfaceVersion(void)
{
    ++g_fake_pa_debug_calls;
    return UINT32_C(0x8a6d0001);
}

static int32_t PS5_SYSV_ABI fakeSonySetTFRing(
    uintptr_t ring_address, uint32_t ring_size)
{
    g_fake_tf_ring_address = ring_address;
    g_fake_tf_ring_size = ring_size;
    ++g_fake_tf_ring_calls;
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI fakeSonyNotifyDefaultStates(
    const AgcRegisterDefaultValue *cx, const AgcRegisterDefaultValue *sh,
    const AgcRegisterDefaultValue *uc, uint32_t cx_count, uint32_t sh_count,
    uint32_t uc_count)
{
    const AgcRegisterDefaultValue *pairs[3] = {cx, sh, uc};
    uint32_t counts[3] = {cx_count, sh_count, uc_count};

    for (uint32_t i = 0; i < 3u; ++i) {
        if (!pairs[i] || counts[i] == 0)
            return AGC_ERROR_INVALID_ARGUMENT;
        g_fake_defaults_counts[i] = counts[i];
        g_fake_defaults_first[i] = pairs[i][0];
    }
    ++g_fake_defaults_calls;
    return AGC_OK;
}

static int32_t PS5_SYSV_ABI fakeSonyRuntimeInit(uint32_t version)
{
    ++g_fake_runtime_init_calls;
    g_fake_runtime_init_version = version;
    if (g_fake_runtime_init_result != AGC_OK)
        return g_fake_runtime_init_result;
    if (g_fake_runtime_init_calls_notify)
        return sceAgcDriverNotifyDefaultStates(0u);
    return AGC_OK;
}

static void *fakeCallbackAddress(const void *callback_storage)
{
    void *address = NULL;
    memcpy(&address, callback_storage, sizeof(address));
    return address;
}

static void *fakeSonyOpen(void *context, const char *name)
{
    FakeSonyLoader *fake = context;
    fake->open_count++;
    if (fake->module_absent ||
        (fake->missing_module && strcmp(name, fake->missing_module) == 0))
        return NULL;
    if (strcmp(name, "libSceAgcDriver.sprx") != 0 &&
        strcmp(name, "libSceAgc.sprx") != 0)
        return NULL;
    return fake;
}

#define FAKE_SYMBOL(name, member) \
    if (strcmp(symbol, name) == 0) \
        return fakeCallbackAddress(&agcGenericDriverOps.member)

static void *fakeSonyResolve(void *context, void *module, const char *symbol)
{
    FakeSonyLoader *fake = context;
    (void)module;

    if (fake->missing_symbol && strcmp(symbol, fake->missing_symbol) == 0)
        return NULL;
    if (fake->recursive_symbol && strcmp(symbol, fake->recursive_symbol) == 0) {
        if (strcmp(symbol, "sceAgcDriverSubmitDcb") == 0) {
            int32_t (PS5_SYSV_ABI *callback)(const AgcCommandBufferSubmit *) =
                sceAgcDriverSubmitDcb;
            return fakeCallbackAddress(&callback);
        }
        if (strcmp(symbol, "sceAgcDriverSetTFRing") == 0) {
            int32_t (PS5_SYSV_ABI *callback)(uintptr_t, uint32_t) =
                sceAgcDriverSetTFRing;
            return fakeCallbackAddress(&callback);
        }
        if (strcmp(symbol, "sceAgcDriverNotifyDefaultStates") == 0) {
            int32_t (PS5_SYSV_ABI *callback)(uint32_t) =
                sceAgcDriverNotifyDefaultStates;
            return fakeCallbackAddress(&callback);
        }
        if (strcmp(symbol, "sceAgcInit") == 0) {
            int32_t (PS5_SYSV_ABI *callback)(uint32_t) = sceAgcInit;
            return fakeCallbackAddress(&callback);
        }
    }

    if (strcmp(symbol, "sceAgcDriverSubmitMultiDcbs") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(
            void *const[], const uint32_t *, uint32_t) = fakeSonySubmitMultiDcbs;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcDriverSetTFRing") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(uintptr_t, uint32_t) =
            fakeSonySetTFRing;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcDriverSubmitDcb") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(
            const AgcCommandBufferSubmit *) = fakeSonySubmitDcb;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcDriverSubmitAcb") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(
            uint32_t, const AgcCommandBufferSubmit *) = fakeSonySubmitAcb;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcDriverSetupAsyncGraphics") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(uint32_t) =
            fakeSonySetupAsyncGraphics;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcDriverGetPaDebugInterfaceVersion") == 0) {
        uint32_t (PS5_SYSV_ABI *callback)(void) =
            fakeSonyGetPaDebugInterfaceVersion;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcDriverNotifyDefaultStates") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(
            const AgcRegisterDefaultValue *, const AgcRegisterDefaultValue *,
            const AgcRegisterDefaultValue *, uint32_t, uint32_t, uint32_t) =
            fakeSonyNotifyDefaultStates;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcInit") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(uint32_t) = fakeSonyRuntimeInit;
        return fakeCallbackAddress(&callback);
    }
    if (strcmp(symbol, "sceAgcDriverGetReservedDmemForAgc") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(uintptr_t *, uint32_t *) =
            fakeSonyGetReservedDmem;
        return fakeCallbackAddress(&callback);
    }

    FAKE_SYMBOL("sceAgcDriverSubmitMultiCommandBuffersDirect",
        submit_multi_command_buffers_direct);
    FAKE_SYMBOL("sceAgcDriverSubmitDcb", submit_dcb);
    FAKE_SYMBOL("sceAgcDriverSubmitAcb", submit_acb);
    FAKE_SYMBOL("sceAgcDriverSetupAsyncGraphics", setup_async_graphics);
    FAKE_SYMBOL("sceAgcDriverSetWorkloadsActive", set_workloads_active);
    FAKE_SYMBOL("sceAgcDriverSetWorkloadComplete", set_workload_complete);
    FAKE_SYMBOL("_sceAgcDriverCreateUserSpecialQueue",
        create_user_special_queue);
    FAKE_SYMBOL("_sceAgcDriverDestroyUserSpecialQueue",
        destroy_user_special_queue);
    FAKE_SYMBOL("sceAgcDriverGetPaDebugInterfaceVersion",
        get_pa_debug_interface_version);
    FAKE_SYMBOL("sceAgcDriverSetTargetRingForDiag", set_target_ring_for_diag);
    FAKE_SYMBOL("sceAgcDriverSdmaCopyLinearBlocking",
        sdma_copy_linear_blocking);
    FAKE_SYMBOL("sceAgcDriverRegisterCaptureInterface",
        register_capture_interface);
    FAKE_SYMBOL("sceAgcDriverDeregisterCaptureInterface",
        deregister_capture_interface);
    FAKE_SYMBOL("sceAgcDriverAcquireRazorACQ", acquire_razor_acq);
    FAKE_SYMBOL("sceAgcDriverReleaseRazorACQ", release_razor_acq);
    FAKE_SYMBOL("sceAgcDriverSubmitToRazorACQ", submit_to_razor_acq);
    return NULL;
}

static void fakeSonyClose(void *context, void *module)
{
    FakeSonyLoader *fake = context;
    (void)module;
    fake->close_count++;
}

static AgcSonyLoader fakeLoader(FakeSonyLoader *fake)
{
    AgcSonyLoader loader = {
        .context = fake,
        .open_module = fakeSonyOpen,
        .resolve_symbol = fakeSonyResolve,
        .close_module = fakeSonyClose,
    };
    return loader;
}

static const AgcSonyDriverProfile *fw550Profile(void)
{
    const AgcSonyDriverProfile *profile =
        agcSonyDriverProfileForFirmware(0x05500008u);

    TEST_ASSERT(profile != NULL, "FW 5.50 installed profile exists");
    return profile;
}

static void test_sony_firmware_profiles(void)
{
    static const uint16_t keys[] = {
        0x0320u, 0x0400u, 0x0403u, 0x0450u, 0x0451u,
        0x0502u, 0x0510u, 0x0550u, 0x0600u, 0x0602u, 0x0650u,
        0x0700u, 0x0701u, 0x0720u, 0x0740u, 0x0760u, 0x0761u,
        0x0800u, 0x0820u, 0x0840u, 0x0860u,
        0x0900u, 0x0905u, 0x0920u, 0x0940u, 0x0960u,
        0x1001u, 0x1020u, 0x1040u, 0x1060u,
        0x1100u, 0x1120u, 0x1140u, 0x1160u,
        0x1200u, 0x1202u, 0x1220u, 0x1240u, 0x1260u, 0x1270u,
    };
    const AgcSonyDriverProfile *profile = fw550Profile();
    size_t required = 0;

    TEST_ASSERT_EQ(profile->firmware_abi_key, 0x0550u,
        "FW 5.50 profile uses the exact ABI key");
    TEST_ASSERT(strcmp(profile->name, "fw550-installed") == 0,
        "FW 5.50 profile name is exact");
    TEST_ASSERT(strcmp(profile->runtime_module_name, "libSceAgc.sprx") == 0,
        "FW 5.50 profile names its matching installed runtime");
    TEST_ASSERT(strcmp(profile->runtime_init_name, "sceAgcInit") == 0,
        "FW 5.50 profile names the installed lifecycle export");
    TEST_ASSERT_EQ(profile->export_count, AGC_SONY_EXPORT_COUNT,
        "profile manifest covers every resolver slot");
    for (size_t i = 0; i < profile->export_count; ++i)
        required += profile->exports[i].required ? 1u : 0u;
    TEST_ASSERT_EQ(required, 7u, "profile has seven Vulkan-required exports");
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const AgcSonyDriverProfile *candidate =
            agcSonyDriverProfileForFirmware((uint32_t)keys[i] << 16);
        TEST_ASSERT(candidate != NULL, "every inspected firmware is profiled");
        TEST_ASSERT_EQ(candidate->firmware_abi_key, keys[i],
            "profile lookup retains its exact key");
        TEST_ASSERT(candidate->credential_layout != NULL,
            "every installed profile has an exact credential layout");
        TEST_ASSERT_EQ(candidate->credential_layout->process_threads_offset,
            0x10u, "installed profile has the PS5 process thread-list offset");
        TEST_ASSERT_EQ(candidate->credential_layout->process_ucred_offset,
            0x40u, "installed profile has the PS5 process ucred offset");
        TEST_ASSERT_EQ(candidate->credential_layout->thread_next_offset,
            0x10u, "installed profile has the PS5 thread-list link offset");
        TEST_ASSERT_EQ(candidate->credential_layout->thread_ucred_offset,
            0x140u, "installed profile has the PS5 thread ucred offset");
        TEST_ASSERT_EQ(candidate->credential_layout->ucred_authid_offset,
            0x58u, "installed profile has the PS5 AuthId offset");
    }
    TEST_ASSERT(agcSonyDriverProfileForFirmware(0x05510000u) == NULL,
        "nearby uninspected firmware is rejected");
    {
        const AgcSonyDriverProfile *fw700 =
            agcSonyDriverProfileForFirmware(0x07000000u);
        TEST_ASSERT(fw700 != NULL, "FW 7.00 installed profile exists");
        TEST_ASSERT(strcmp(fw700->name, "fw700-installed") == 0,
            "FW 7.00 installed profile is exact");
    }
    TEST_ASSERT(agcSonyDriverProfileForFirmware(0x03000000u) == NULL,
        "archival firmware is not promoted");
}

static void test_sony_export_resolution(void)
{
    FakeSonyLoader fake = {0};
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module), AGC_OK,
        "complete Sony export set resolves");
    TEST_ASSERT(module == &fake, "resolved module handle retained");
    TEST_ASSERT(strcmp(ops.name, "sony-installed") == 0,
        "Sony operations table is named");
    TEST_ASSERT(ops.submit_dcb == fakeSonySubmitDcb,
        "Sony submit callback populated from module export");
    TEST_ASSERT(ops.compute_uses_dcb,
        "Sony native compute uses the DCB carrier");
    TEST_ASSERT(ops.submit_acb == fakeSonySubmitAcb,
        "Sony ACB callback populated from module export");
    TEST_ASSERT(ops.setup_async_graphics == fakeSonySetupAsyncGraphics,
        "Sony async-graphics callback populated from module export");
    TEST_ASSERT(ops.get_pa_debug_interface_version ==
            fakeSonyGetPaDebugInterfaceVersion,
        "Sony diagnostic callback populated from module export");
    TEST_ASSERT(ops.set_tf_ring == fakeSonySetTFRing,
        "Sony TF-ring callback populated from functional public export");
    g_fake_tf_ring_address = 0;
    g_fake_tf_ring_size = 0;
    TEST_ASSERT_EQ(ops.set_tf_ring((uintptr_t)0x12345000u, 0x4000u), AGC_OK,
        "Sony TF-ring callback is callable");
    TEST_ASSERT_EQ(g_fake_tf_ring_address, (uintptr_t)0x12345000u,
        "Sony TF-ring callback preserves the GPU address");
    TEST_ASSERT_EQ(g_fake_tf_ring_size, 0x4000u,
        "Sony TF-ring callback preserves the byte size");
    TEST_ASSERT(ops.internal_suspend_point_submit_primary == NULL,
        "installed driver does not expose its private primary carrier");
    TEST_ASSERT(ops.internal_suspend_point_submit_final == NULL,
        "installed driver does not expose its private final carrier");
    TEST_ASSERT_EQ(ops.initialize(), AGC_OK,
        "module-backed initialization adapter succeeds");
    TEST_ASSERT_EQ(ops.shutdown(), AGC_OK,
        "loader-owned shutdown adapter succeeds");
    {
        uint32_t dcb0[2] = {0};
        uint32_t dcb1[4] = {0};
        void *addresses[2] = {dcb0, dcb1};
        uint32_t byte_sizes[2] = {sizeof(dcb0), sizeof(dcb1)};
        g_fake_multi_count = 0;
        TEST_ASSERT_EQ(ops.submit_multi_command_buffers_direct(
            2, addresses, byte_sizes, NULL, NULL), AGC_OK,
            "Sony multi-DCB adapter converts byte sizes");
        TEST_ASSERT_EQ(g_fake_multi_count, 2u,
            "Sony multi-DCB adapter retains count");
        TEST_ASSERT_EQ(g_fake_multi_sizes[0], 2u,
            "first byte size converts to dwords");
        TEST_ASSERT_EQ(g_fake_multi_sizes[1], 4u,
            "second byte size converts to dwords");
    }
    TEST_ASSERT(ops.set_workloads_active == NULL,
        "ABI-incompatible workload builder is not forwarded");
    TEST_ASSERT(ops.create_user_special_queue == NULL,
        "private queue helper is not part of the installed profile");
    TEST_ASSERT(ops.submit_eop_flip == NULL,
        "nonexistent EOP helper is not forwarded");
    TEST_ASSERT_EQ(fake.open_count, 2u,
        "Sony driver and matching runtime modules are discovered");
    TEST_ASSERT_EQ(fake.close_count, 0u, "selected Sony module remains loaded");

    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&ops), AGC_OK,
        "install resolved Sony operations table");
    {
        uint32_t dcb[2] = {0};
        AgcCommandBufferSubmit submit = {
            .command_address = (uintptr_t)dcb,
            .dword_count = 2u,
        };

        g_fake_submit_dcb_calls = 0u;
        g_fake_submit_dcb_result = AGC_ERROR_DEVICE_LOST;
        g_fake_submit_acb_calls = 0u;
        g_fake_submit_acb_owner = 0u;
        g_fake_async_calls = 0u;
        g_fake_async_pipe = 0u;
        g_fake_async_result = 23;
        g_fake_pa_debug_calls = 0u;
        TEST_ASSERT_EQ(sceAgcDriverSubmitAcb(17u, &submit), AGC_OK,
            "public ACB submission forwards to the Sony export");
        TEST_ASSERT_EQ(g_fake_submit_acb_calls, 1u,
            "Sony ACB export is called once");
        TEST_ASSERT_EQ(g_fake_submit_acb_owner, 17u,
            "Sony ACB export receives the owner handle");
        TEST_ASSERT_EQ(sceAgcDriverSetupAsyncGraphics(2u), 23,
            "public async setup preserves Sony's queue handle");
        TEST_ASSERT_EQ(g_fake_async_calls, 1u,
            "Sony async-graphics export is called once");
        TEST_ASSERT_EQ(g_fake_async_pipe, 2u,
            "Sony async-graphics export receives the pipe id");
        TEST_ASSERT_EQ(sceAgcDriverGetPaDebugInterfaceVersion(),
            UINT32_C(0x8a6d0001),
            "public diagnostic query preserves Sony's result");
        TEST_ASSERT_EQ(g_fake_pa_debug_calls, 1u,
            "Sony diagnostic export is called once");
        TEST_ASSERT_EQ(sceAgcDriverSubmitDcb(&submit),
            AGC_ERROR_DEVICE_LOST,
            "Sony submission failure is returned without carrier fallback");
        TEST_ASSERT(agcDriverGetOps() == &ops,
            "Sony operations remain selected after submission failure");
        TEST_ASSERT_EQ(sceAgcDriverSubmitDcb(&submit),
            AGC_ERROR_DEVICE_LOST,
            "later submission still dispatches to the selected Sony export");
        TEST_ASSERT_EQ(g_fake_submit_dcb_calls, 2u,
            "both failed submissions reached Sony and no fallback table");
        g_fake_submit_dcb_result = AGC_OK;
    }
    g_fake_runtime_init_calls = 0u;
    g_fake_runtime_init_version = UINT32_MAX;
    g_fake_runtime_init_result = AGC_OK;
    g_fake_runtime_init_calls_notify = true;
    g_fake_reserved_dmem_calls = 0u;
    g_fake_defaults_calls = 0u;
    memset(g_fake_defaults_counts, 0, sizeof(g_fake_defaults_counts));
    TEST_ASSERT_EQ(sceAgcInit(8u), AGC_OK,
        "public init selects Sony register defaults");
    TEST_ASSERT_EQ(g_fake_runtime_init_calls, 1u,
        "public init enters the installed libSceAgc lifecycle once");
    TEST_ASSERT_EQ(g_fake_runtime_init_version, 8u,
        "installed lifecycle receives the selected defaults version");
    TEST_ASSERT_EQ(g_fake_reserved_dmem_calls, 1u,
        "installed lifecycle diagnoses the driver-reserved AGC memory once");
    TEST_ASSERT_EQ(sceAgcDriverNotifyDefaultStates(0), AGC_OK,
        "explicit defaults notification is idempotent after installed init");
    TEST_ASSERT_EQ(g_fake_defaults_calls, 1u,
        "Sony default-state export is called once");
    TEST_ASSERT(g_fake_defaults_counts[0] > 0 &&
            g_fake_defaults_counts[1] > 0 && g_fake_defaults_counts[2] > 0,
        "Sony default-state adapter supplies all three register spaces");
    TEST_ASSERT_EQ(g_fake_defaults_first[0].offset, 0x0202u,
        "Sony CX defaults preserve the first selected register");
    TEST_ASSERT_EQ(sceAgcDriverNotifyDefaultStates(0), AGC_OK,
        "repeated Sony default-state notification is idempotent");
    TEST_ASSERT_EQ(g_fake_defaults_calls, 1u,
        "idempotent notification does not call Sony twice");
    TEST_ASSERT_EQ(agcDriverShutdown(), AGC_OK,
        "public shutdown forwards through Sony operations table");
    agcDriverClearOpsForTesting();
    TEST_ASSERT(agcDriverGetOps() == NULL,
        "production shutdown dispatch reset is emulated on the host");
    TEST_ASSERT_EQ(sce_agc_initialize(), AGC_ERROR_NOT_SUPPORTED,
        "unselected interval fails closed without a direct fallback");
    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&ops), AGC_OK,
        "retained loader-owned Sony operations are selected again");
    TEST_ASSERT_EQ(sceAgcInit(8u), AGC_OK,
        "installed operations can select defaults again after shutdown");
    TEST_ASSERT_EQ(sceAgcDriverNotifyDefaultStates(0), AGC_OK,
        "Sony defaults can be notified again after shutdown");
    TEST_ASSERT_EQ(g_fake_defaults_calls, 2u,
        "reinitialization calls the installed defaults export again");
    TEST_ASSERT_EQ(g_fake_runtime_init_calls, 2u,
        "reinitialization re-enters the retained installed runtime");
    agcDriverResetOpsForTesting();
}

static void test_sony_native_graphics_lifecycle(void)
{
    FakeSonyLoader fake = {0};
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcQueueDesc queue_desc = AGC_QUEUE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcPresentChainDesc present_desc = AGC_PRESENT_CHAIN_DESC_INIT;
    AgcResourceTransition present_transition = AGC_RESOURCE_TRANSITION_INIT;
    AgcDevice device = NULL;
    AgcQueue queue = NULL;
    AgcQueue compute_queue = NULL;
    AgcCommandBuffer first = NULL;
    AgcCommandBuffer second = NULL;
    AgcCommandBuffer compute = NULL;
    AgcCommandBuffer commands[2];
    AgcFence fence = NULL;
    AgcImage present_images[2] = {NULL, NULL};
    AgcPresentChain present_chain = NULL;
    AgcGraphicsPipeline tessellation = NULL;
    AgcShader hull = NULL;
    AgcShader evaluation = NULL;
    AgcShader pixel = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module), AGC_OK,
        "native lifecycle resolves a complete Sony table");
    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&ops), AGC_OK,
        "native lifecycle selects the Sony table");
    device_desc.agc_version = 8u;
    g_fake_defaults_calls = 0u;
    g_fake_runtime_init_calls = 0u;
    g_fake_runtime_init_result = AGC_OK;
    g_fake_runtime_init_calls_notify = true;
    g_fake_submit_dcb_calls = 0u;
    g_fake_submit_dcb_result = AGC_OK;
    g_fake_multi_count = 0u;
    TEST_ASSERT_EQ(agcCreateDevice(&device_desc, &device), AGC_OK,
        "native device initializes through Sony operations");
    TEST_ASSERT_EQ(g_fake_defaults_calls, 1u,
        "native device initialization notifies Sony defaults once");
    TEST_ASSERT_EQ(g_fake_runtime_init_calls, 1u,
        "native device initialization enters installed libSceAgc once");
    TEST_ASSERT_EQ(agcDriverSelectRegisterDefaultsVersion(8u), AGC_OK,
        "Sony accepts reselection of the active defaults version");
    TEST_ASSERT_EQ(agcDriverSelectRegisterDefaultsVersion(9u),
        AGC_ERROR_INVALID_ARGUMENT,
        "Sony rejects a defaults-version change after notification");
    g_fake_tf_ring_address = 0u;
    g_fake_tf_ring_size = 0u;
    g_fake_tf_ring_calls = 0u;
    TEST_ASSERT_EQ(test_runtime_create_tessellation_fixture(device,
        &tessellation, &hull, &evaluation, &pixel), AGC_OK,
        "native tessellation pipeline initializes through Sony TF-ring setup");
    TEST_ASSERT_EQ(g_fake_tf_ring_calls, 1u,
        "first native tessellation pipeline configures Sony TF ring once");
    TEST_ASSERT(g_fake_tf_ring_address != 0u,
        "Sony TF-ring setup receives OpenAGC's factor-ring address");
    TEST_ASSERT_EQ(g_fake_tf_ring_size, AGC_GFX1013_TESS_FACTOR_RING_SIZE,
        "Sony TF-ring setup receives OpenAGC's full factor-ring size");
    TEST_ASSERT_EQ(agcCreateQueue(device, &queue_desc, &queue), AGC_OK,
        "native graphics queue remains OpenAGC-owned");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &first),
        AGC_OK, "first native Sony command buffer creates");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &second),
        AGC_OK, "second native Sony command buffer creates");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "native Sony fence remains OpenAGC-owned");
    commands[0] = first;
    commands[1] = second;
    for (uint32_t i = 0u; i < 2u; ++i) {
        TEST_ASSERT_EQ(agcBeginCommandBuffer(commands[i]), AGC_OK,
            "native Sony command buffer begins");
        TEST_ASSERT_EQ(agcCmdMemoryBarrier(commands[i]), AGC_OK,
            "OpenAGC emits a carrier-independent command");
        TEST_ASSERT_EQ(agcEndCommandBuffer(commands[i]), AGC_OK,
            "native Sony command buffer ends");
    }
    submit.command_buffer_count = 1u;
    submit.command_buffers = commands;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "native single submission reaches Sony DCB export");
    TEST_ASSERT_EQ(g_fake_submit_dcb_calls, 1u,
        "native single submission calls Sony exactly once");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1u), AGC_OK,
        "OpenAGC-owned native fence completes after Sony submit");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "OpenAGC-owned native fence resets for multi-submit");
    submit.command_buffer_count = 2u;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "native multi-submission reaches Sony multi-DCB export");
    TEST_ASSERT_EQ(g_fake_multi_count, 2u,
        "native multi-submission preserves both command buffers");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1u), AGC_OK,
        "OpenAGC-owned multi-submit fence completes");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "OpenAGC-owned fence resets for presentation transition");
    image_desc.width = 1920u;
    image_desc.height = 1080u;
    image_desc.format = AGC_FORMAT_BGRA8_SRGB;
    image_desc.usage = AGC_IMAGE_USAGE_SCANOUT_BIT |
        AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &present_images[0]),
        AGC_OK, "first Sony-selected presentation image creates");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &present_images[1]),
        AGC_OK, "second Sony-selected presentation image creates");
    present_desc.image_count = 2u;
    present_desc.images = present_images;
    TEST_ASSERT_EQ(agcCreatePresentChain(device, &present_desc,
        &present_chain), AGC_OK,
        "Sony-selected device creates an OpenAGC VideoOut chain");
    TEST_ASSERT_EQ(agcResetCommandBuffer(first), AGC_OK,
        "Sony presentation command buffer resets");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(first), AGC_OK,
        "Sony presentation transition begins");
    present_transition.resource_type = kAgcResourceTypeImage;
    present_transition.image = present_images[0];
    present_transition.image_range =
        (AgcImageSubresourceRange)AGC_IMAGE_SUBRESOURCE_RANGE_INIT;
    present_transition.before = kAgcResourceUsageUndefined;
    present_transition.after = kAgcResourceUsageVideoOutScanout;
    present_transition.before_owner = kAgcResourceOwnerHost;
    present_transition.after_owner = kAgcResourceOwnerGraphics;
    TEST_ASSERT_EQ(agcCmdTransitionResources(first, 1u,
        &present_transition), AGC_OK,
        "Sony presentation transition remains OpenAGC-emitted PM4");
    TEST_ASSERT_EQ(agcEndCommandBuffer(first), AGC_OK,
        "Sony presentation transition ends");
    commands[0] = first;
    submit.command_buffer_count = 1u;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "Sony presentation readiness submits through DCB");
    TEST_ASSERT_EQ(g_fake_submit_dcb_calls, 2u,
        "presentation readiness reaches Sony DCB exactly once");
    TEST_ASSERT_EQ(agcPresent(present_chain, 0u, 1u, fence,
        UINT64_C(1000000)), AGC_OK,
        "Sony-selected path presents through OpenAGC VideoOut ownership");
    TEST_ASSERT_EQ(agcDestroyPresentChain(present_chain), AGC_OK,
        "Sony-selected presentation chain destroys");
    TEST_ASSERT_EQ(agcResetCommandBuffer(first), AGC_OK,
        "presentation command releases its recorded image reference");
    TEST_ASSERT_EQ(agcDestroyImage(present_images[1]), AGC_OK,
        "second Sony-selected presentation image destroys");
    TEST_ASSERT_EQ(agcDestroyImageDeferred(present_images[0], fence), AGC_OK,
        "presented Sony-selected image enters fence-keyed retirement");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "presented Sony-selected image retires after chain release");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "OpenAGC-owned fence resets for compute submit");
    queue_desc.type = kAgcQueueCompute;
    g_fake_async_calls = 0u;
    g_fake_async_pipe = 0u;
    g_fake_async_result = AGC_OK;
    TEST_ASSERT_EQ(agcCreateQueue(device, &queue_desc, &compute_queue), AGC_OK,
        "native compute queue initializes through Sony async setup");
    TEST_ASSERT_EQ(g_fake_async_calls, 1u,
        "native compute queue calls Sony async setup once");
    TEST_ASSERT_EQ(g_fake_async_pipe, 1u,
        "Sony DCB compute selects the Prospero async pipe");
    command_desc.queue_type = kAgcQueueCompute;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc, &compute),
        AGC_OK, "native Sony compute command buffer creates");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(compute), AGC_OK,
        "native Sony compute command buffer begins");
    TEST_ASSERT_EQ(agcEndCommandBuffer(compute), AGC_OK,
        "native Sony compute command buffer ends for runtime-owned NOP");
    commands[0] = compute;
    submit.command_buffer_count = 1u;
    TEST_ASSERT_EQ(agcQueueSubmit(compute_queue, &submit, fence), AGC_OK,
        "native compute submission reaches Sony DCB export");
    TEST_ASSERT_EQ(g_fake_submit_dcb_calls, 3u,
        "graphics, presentation, and compute submissions use Sony DCB");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1u), AGC_OK,
        "OpenAGC-owned compute fence completes");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(compute), AGC_OK,
        "native Sony compute command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_OK,
        "native compute queue destroys without a private Sony helper");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK,
        "native Sony fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(second), AGC_OK,
        "second native Sony command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(first), AGC_OK,
        "first native Sony command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK,
        "native graphics queue destroys without a private Sony helper");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(tessellation), AGC_OK,
        "native Sony tessellation pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(pixel), AGC_OK,
        "native Sony tessellation pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(evaluation), AGC_OK,
        "native Sony tessellation evaluation shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(hull), AGC_OK,
        "native Sony tessellation control bundle destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "native device shuts down through the Sony adapter");
    TEST_ASSERT_EQ(fake.close_count, 0u,
        "native lifecycle never unloads the loader-owned module");
    TEST_ASSERT_EQ(agcDriverSelectRegisterDefaultsVersion(9u), AGC_OK,
        "Sony shutdown resets carrier-owned defaults-version state");
    agcDriverResetOpsForTesting();
}

static void test_sony_missing_mandatory_export(void)
{
    FakeSonyLoader fake = {
        .missing_symbol = "sceAgcDriverSubmitDcb",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = (void *)1;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED, "missing mandatory Sony export is rejected");
    TEST_ASSERT(module == NULL, "failed Sony module handle cleared");
    TEST_ASSERT_EQ(fake.close_count, 1u, "incomplete Sony module is closed");
}

static void test_sony_missing_optional_export(void)
{
    FakeSonyLoader fake = {
        .missing_symbol = "sceAgcDriverSdmaCopyLinearBlocking",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module), AGC_OK,
        "missing optional Sony export preserves eligibility");
    TEST_ASSERT(module == &fake,
        "module remains selected when an optional export is absent");
    TEST_ASSERT(ops.sdma_copy_linear_blocking == NULL,
        "missing optional callback remains unsupported");
    TEST_ASSERT_EQ(fake.close_count, 0u,
        "eligible module is not released for an optional absence");
}

static void test_sony_missing_tf_ring_export(void)
{
    FakeSonyLoader fake = {
        .missing_symbol = "sceAgcDriverSetTFRing",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = (void *)1;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED,
        "missing Vulkan-required Sony TF-ring export is rejected");
    TEST_ASSERT(module == NULL,
        "TF-ring-incomplete Sony module handle is cleared");
    TEST_ASSERT_EQ(fake.close_count, 1u,
        "TF-ring-incomplete Sony module is rejected");
}

static void test_sony_missing_default_states_export(void)
{
    FakeSonyLoader fake = {
        .missing_symbol = "sceAgcDriverNotifyDefaultStates",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = (void *)1;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED,
        "missing Vulkan-required Sony defaults export is rejected");
    TEST_ASSERT(module == NULL,
        "defaults-incomplete Sony module handle is cleared");
    TEST_ASSERT_EQ(fake.close_count, 1u,
        "defaults-incomplete Sony module is rejected");
}

static void test_sony_missing_runtime_module(void)
{
    FakeSonyLoader fake = {
        .missing_module = "libSceAgc.sprx",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = (void *)1;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED,
        "missing installed libSceAgc lifecycle module is rejected");
    TEST_ASSERT(module == NULL, "missing runtime clears the carrier handle");
    TEST_ASSERT_EQ(fake.open_count, 2u,
        "resolver checks driver before the matching runtime module");
    TEST_ASSERT_EQ(fake.close_count, 1u,
        "failed lifecycle discovery rejects the driver module");
}

static void test_sony_missing_runtime_init(void)
{
    FakeSonyLoader fake = {
        .missing_symbol = "sceAgcInit",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = (void *)1;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED,
        "missing installed lifecycle export is rejected");
    TEST_ASSERT(module == NULL, "incomplete lifecycle clears carrier handle");
    TEST_ASSERT_EQ(fake.close_count, 2u,
        "both incomplete runtime and driver modules are rejected");
}

static void test_sony_runtime_init_failure_is_sticky(void)
{
    FakeSonyLoader fake = {0};
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module), AGC_OK,
        "runtime-failure test resolves both installed modules");
    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&ops), AGC_OK,
        "runtime-failure test selects Sony");
    g_fake_runtime_init_calls = 0u;
    g_fake_runtime_init_result = AGC_ERROR_DEVICE_LOST;
    g_fake_runtime_init_calls_notify = false;
    TEST_ASSERT_EQ(sceAgcInit(8u), AGC_ERROR_DEVICE_LOST,
        "installed lifecycle failure reaches the caller");
    TEST_ASSERT_EQ(sceAgcInit(8u), AGC_ERROR_DEVICE_LOST,
        "failed installed lifecycle remains fail-closed");
    TEST_ASSERT_EQ(g_fake_runtime_init_calls, 1u,
        "failed installed lifecycle is not retried in-process");
    TEST_ASSERT_EQ(agcDriverShutdown(), AGC_ERROR_DEVICE_LOST,
        "shutdown cannot erase a failed installed lifecycle");
    TEST_ASSERT(agcDriverGetOps() == &ops,
        "failed lifecycle never switches to the direct carrier");
    agcDriverResetOpsForTesting();
    g_fake_runtime_init_result = AGC_OK;
}

static void test_sony_recursion_rejected(void)
{
    FakeSonyLoader fake = {
        .recursive_symbol = "sceAgcDriverSubmitDcb",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED, "OpenAGC wrapper self-resolution is rejected");
    TEST_ASSERT_EQ(fake.close_count, 1u, "recursive module is closed");
}

static void test_sony_tf_ring_recursion_rejected(void)
{
    FakeSonyLoader fake = {
        .recursive_symbol = "sceAgcDriverSetTFRing",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED,
        "OpenAGC TF-ring wrapper self-resolution is rejected");
    TEST_ASSERT_EQ(fake.close_count, 1u,
        "recursive TF-ring module is rejected");
}

static void test_sony_default_states_recursion_rejected(void)
{
    FakeSonyLoader fake = {
        .recursive_symbol = "sceAgcDriverNotifyDefaultStates",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED,
        "OpenAGC defaults wrapper self-resolution is rejected");
    TEST_ASSERT_EQ(fake.close_count, 1u,
        "recursive defaults module is rejected");
}

static void test_sony_runtime_init_recursion_rejected(void)
{
    FakeSonyLoader fake = {
        .recursive_symbol = "sceAgcInit",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_NOT_SUPPORTED,
        "OpenAGC init wrapper self-resolution is rejected");
    TEST_ASSERT_EQ(fake.close_count, 2u,
        "recursive installed runtime and driver modules are rejected");
}

static void test_sony_loader_validation(void)
{
    FakeSonyLoader fake = {0};
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(
        NULL, fw550Profile(), &ops, &module),
        AGC_ERROR_INVALID_ARGUMENT, "NULL Sony loader rejected");
    loader.resolve_symbol = NULL;
    TEST_ASSERT_EQ(agcSonyDriverResolve(
        &loader, fw550Profile(), &ops, &module),
        AGC_ERROR_INVALID_ARGUMENT, "incomplete Sony loader rejected");
}

void test_suite_sony_exports(void)
{
    TEST_SUITE("Sony Driver Export Forwarding");
    TEST_RUN(test_sony_firmware_profiles);
    TEST_RUN(test_sony_export_resolution);
    TEST_RUN(test_sony_native_graphics_lifecycle);
    TEST_RUN(test_sony_missing_mandatory_export);
    TEST_RUN(test_sony_missing_optional_export);
    TEST_RUN(test_sony_missing_tf_ring_export);
    TEST_RUN(test_sony_missing_default_states_export);
    TEST_RUN(test_sony_missing_runtime_module);
    TEST_RUN(test_sony_missing_runtime_init);
    TEST_RUN(test_sony_runtime_init_failure_is_sticky);
    TEST_RUN(test_sony_recursion_rejected);
    TEST_RUN(test_sony_tf_ring_recursion_rejected);
    TEST_RUN(test_sony_default_states_recursion_rejected);
    TEST_RUN(test_sony_runtime_init_recursion_rejected);
    TEST_RUN(test_sony_loader_validation);
}
