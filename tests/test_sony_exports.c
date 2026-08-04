#include "test.h"
#include "agc_context.h"
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
static uintptr_t g_fake_tf_ring_address;
static uint32_t g_fake_tf_ring_size;
static uint32_t g_fake_defaults_calls;
static uint32_t g_fake_defaults_counts[3];
static AgcRegisterDefaultValue g_fake_defaults_first[3];

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

static int32_t PS5_SYSV_ABI fakeSonySetTFRing(
    uintptr_t ring_address, uint32_t ring_size)
{
    g_fake_tf_ring_address = ring_address;
    g_fake_tf_ring_size = ring_size;
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
    if (strcmp(symbol, "sceAgcDriverNotifyDefaultStates") == 0) {
        int32_t (PS5_SYSV_ABI *callback)(
            const AgcRegisterDefaultValue *, const AgcRegisterDefaultValue *,
            const AgcRegisterDefaultValue *, uint32_t, uint32_t, uint32_t) =
            fakeSonyNotifyDefaultStates;
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
    TEST_ASSERT_EQ(fake.open_count, 1u, "Sony module opened once");
    TEST_ASSERT_EQ(fake.close_count, 0u, "selected Sony module remains loaded");

    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&ops), AGC_OK,
        "install resolved Sony operations table");
    TEST_ASSERT_EQ(sceAgcInit(8u), AGC_OK,
        "public init selects Sony register defaults");
    g_fake_defaults_calls = 0;
    memset(g_fake_defaults_counts, 0, sizeof(g_fake_defaults_counts));
    TEST_ASSERT_EQ(sceAgcDriverNotifyDefaultStates(0), AGC_OK,
        "Sony default-state adapter forwards the selected defaults");
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
    TEST_ASSERT_EQ(sceAgcInit(8u), AGC_OK,
        "installed operations can select defaults again after shutdown");
    TEST_ASSERT_EQ(sceAgcDriverNotifyDefaultStates(0), AGC_OK,
        "Sony defaults can be notified again after shutdown");
    TEST_ASSERT_EQ(g_fake_defaults_calls, 2u,
        "reinitialization calls the installed defaults export again");
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
    TEST_RUN(test_sony_missing_tf_ring_export);
    TEST_RUN(test_sony_missing_default_states_export);
    TEST_RUN(test_sony_recursion_rejected);
    TEST_RUN(test_sony_tf_ring_recursion_rejected);
    TEST_RUN(test_sony_default_states_recursion_rejected);
    TEST_RUN(test_sony_loader_validation);
}
