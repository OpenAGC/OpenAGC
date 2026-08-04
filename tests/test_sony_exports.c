#include "test.h"
#include "driver_sony_exports.h"

typedef struct FakeSonyLoader {
    bool module_absent;
    const char *missing_symbol;
    const char *recursive_symbol;
    uint32_t open_count;
    uint32_t close_count;
} FakeSonyLoader;

static uint32_t g_fake_multi_count;
static uint32_t g_fake_multi_sizes[2];

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
    if (fake->module_absent)
        return NULL;
    if (strcmp(name, "libSceAgcDriver.sprx") != 0)
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
    }

    if (strcmp(symbol, "sceAgcDriverSubmitMultiDcbs") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(
            void *const[], const uint32_t *, uint32_t) = fakeSonySubmitMultiDcbs;
        return fakeCallbackAddress(&callback);
    }

    FAKE_SYMBOL("sceAgcDriverSubmitMultiCommandBuffersDirect",
        submit_multi_command_buffers_direct);
    FAKE_SYMBOL("sceAgcDriverSubmitDcb", submit_dcb);
    FAKE_SYMBOL("sceAgcDriverSubmitAcb", submit_acb);
    FAKE_SYMBOL("sceAgcDriverSetupAsyncGraphics", setup_async_graphics);
    FAKE_SYMBOL("sceAgcDriverNotifyDefaultStates", notify_default_states);
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
    TEST_ASSERT_EQ(profile->export_count, AGC_SONY_EXPORT_COUNT,
        "profile manifest covers every resolver slot");
    for (size_t i = 0; i < profile->export_count; ++i)
        required += profile->exports[i].required ? 1u : 0u;
    TEST_ASSERT_EQ(required, 5u, "profile has five mandatory exports");
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const AgcSonyDriverProfile *candidate =
            agcSonyDriverProfileForFirmware((uint32_t)keys[i] << 16);
        TEST_ASSERT(candidate != NULL, "every inspected firmware is profiled");
        TEST_ASSERT_EQ(candidate->firmware_abi_key, keys[i],
            "profile lookup retains its exact key");
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
    TEST_ASSERT(ops.submit_dcb == agcGenericDriverOps.submit_dcb,
        "Sony submit callback populated from module export");
    TEST_ASSERT(ops.internal_suspend_point_submit_primary == NULL,
        "installed driver does not expose its private primary carrier");
    TEST_ASSERT(ops.internal_suspend_point_submit_final == NULL,
        "installed driver does not expose its private final carrier");
    TEST_ASSERT_EQ(ops.initialize(), AGC_OK,
        "module-backed initialization adapter succeeds");
    TEST_ASSERT_EQ(ops.shutdown(), AGC_OK,
        "loader-owned shutdown adapter succeeds");
    TEST_ASSERT_EQ(ops.notify_default_states(0), AGC_OK,
        "module-owned default states use a safe adapter");
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
    TEST_ASSERT_EQ(fake.open_count, 1u, "Sony module opened once");
    TEST_ASSERT_EQ(fake.close_count, 0u, "selected Sony module remains loaded");

    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&ops), AGC_OK,
        "install resolved Sony operations table");
    TEST_ASSERT_EQ(sce_agc_initialize(), AGC_OK,
        "public initialize forwards through Sony operations table");
    TEST_ASSERT_EQ(agcDriverShutdown(), AGC_OK,
        "public shutdown forwards through Sony operations table");
    TEST_ASSERT_EQ(sce_agc_initialize(), AGC_OK,
        "installed operations can initialize again after shutdown");
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
    TEST_RUN(test_sony_missing_mandatory_export);
    TEST_RUN(test_sony_missing_optional_export);
    TEST_RUN(test_sony_recursion_rejected);
    TEST_RUN(test_sony_loader_validation);
}
