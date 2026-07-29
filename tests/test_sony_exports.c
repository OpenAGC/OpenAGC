#include "test.h"
#include "driver_sony_exports.h"

typedef struct FakeSonyLoader {
    const char *missing_symbol;
    const char *recursive_symbol;
    uint32_t open_count;
    uint32_t close_count;
} FakeSonyLoader;

static int32_t PS5_SYSV_ABI fakeSonySubmitMultiDcbs(
    void *const dcb_gpu_addrs[], const uint32_t *dcb_sizes_in_dwords,
    uint32_t count)
{
    return dcb_gpu_addrs && dcb_sizes_in_dwords && count ? AGC_OK :
        AGC_ERROR_INVALID_ARGUMENT;
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

static void test_sony_export_resolution(void)
{
    FakeSonyLoader fake = {0};
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(&loader, &ops, &module), AGC_OK,
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
    TEST_ASSERT_EQ(ops.notify_default_states(0), AGC_OK,
        "module-owned default states use a safe adapter");
    {
        uint32_t dcb[2] = {0};
        void *addresses[1] = {dcb};
        uint32_t byte_sizes[1] = {sizeof(dcb)};
        TEST_ASSERT_EQ(ops.submit_multi_command_buffers_direct(
            1, addresses, byte_sizes, NULL, NULL), AGC_OK,
            "Sony multi-DCB adapter converts byte sizes");
    }
    TEST_ASSERT(ops.set_workloads_active == NULL,
        "ABI-incompatible workload builder is not forwarded");
    TEST_ASSERT(ops.create_user_special_queue ==
        agcGenericDriverOps.create_user_special_queue,
        "optional queue export is used when a loader provides it");
    TEST_ASSERT_EQ(fake.open_count, 1u, "Sony module opened once");
    TEST_ASSERT_EQ(fake.close_count, 0u, "selected Sony module remains loaded");

    TEST_ASSERT_EQ(agcDriverInstallOpsForTesting(&ops), AGC_OK,
        "install resolved Sony operations table");
    TEST_ASSERT_EQ(sce_agc_initialize(), AGC_OK,
        "public initialize forwards through Sony operations table");
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

    TEST_ASSERT_EQ(agcSonyDriverResolve(&loader, &ops, &module),
        AGC_ERROR_NOT_SUPPORTED, "missing mandatory Sony export is rejected");
    TEST_ASSERT(module == NULL, "failed Sony module handle cleared");
    TEST_ASSERT_EQ(fake.close_count, 1u, "incomplete Sony module is closed");
}

static void test_sony_recursion_rejected(void)
{
    FakeSonyLoader fake = {
        .recursive_symbol = "sceAgcDriverSubmitDcb",
    };
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(&loader, &ops, &module),
        AGC_ERROR_NOT_SUPPORTED, "OpenAGC wrapper self-resolution is rejected");
    TEST_ASSERT_EQ(fake.close_count, 1u, "recursive module is closed");
}

static void test_sony_loader_validation(void)
{
    FakeSonyLoader fake = {0};
    AgcSonyLoader loader = fakeLoader(&fake);
    AgcDriverOps ops;
    void *module = NULL;

    TEST_ASSERT_EQ(agcSonyDriverResolve(NULL, &ops, &module),
        AGC_ERROR_INVALID_ARGUMENT, "NULL Sony loader rejected");
    loader.resolve_symbol = NULL;
    TEST_ASSERT_EQ(agcSonyDriverResolve(&loader, &ops, &module),
        AGC_ERROR_INVALID_ARGUMENT, "incomplete Sony loader rejected");
}

void test_suite_sony_exports(void)
{
    TEST_SUITE("Sony Driver Export Forwarding");
    TEST_RUN(test_sony_export_resolution);
    TEST_RUN(test_sony_missing_mandatory_export);
    TEST_RUN(test_sony_recursion_rejected);
    TEST_RUN(test_sony_loader_validation);
}
