#include "test.h"
#include "agc_ioctl.h"
#include "agcdriver.h"

/*
 * Ioctl command encoding tests — verify the IOC macro produces the exact
 * 32-bit command words recovered from the kernel dump. These values are
 * load-bearing ABI facts; if any change, the prospero backend would call the
 * wrong ioctl.
 */
static void test_ioctl_encoding(void) {
    /* FreeBSD IOC: (dir<<30)|(size<<16)|(type<<8)|nr, type=0x81 */
    TEST_ASSERT_EQ(AGC_GC_IOCTL_FRAME_OPEN,    0xC0088100u, "FRAME_OPEN cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_CLOSE,         0xC0088101u, "CLOSE cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SUBMIT_16,     0xC0108102u, "SUBMIT_16 cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SUBMIT_4,      0xC0048113u, "SUBMIT_4 cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SUBMIT_MFENCE, 0xC0048114u, "SUBMIT_MFENCE cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SUBMIT_40,     0xC0288130u, "SUBMIT_40 cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SUBMIT_PID,    0xC010813Bu, "SUBMIT_PID cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_MAKESYSMAP_8,  0xC0088109u, "MAKESYSMAP_8 cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_MAKESYSMAP_12, 0xC00C810Du, "MAKESYSMAP_12 cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_QUEUE_CREATE,  0xC0408121u, "QUEUE_CREATE cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_QUEUE_DESTROY, 0xC00C810Eu, "QUEUE_DESTROY cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_QUEUE_STATUS,  0x80048126u, "QUEUE_STATUS cmd (setup async)");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SET_TF_RING,   0xC0108120u, "SET_TF_RING cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SET_HS_OFFCHIP,0xC010812Cu, "SET_HS_OFFCHIP cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SETUP_ASYNC,   0xC004811Fu, "SETUP_ASYNC cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SUSPEND_16,    0xC010811Cu, "SUSPEND_16 cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_SUBMITDONE,    0xC0048125u, "SUBMITDONE cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_LARGE_132,     0xC0848119u, "LARGE_132 cmd");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_CWSR_INIT_8,   0x80088136u, "CWSR_INIT_8 cmd");
}

static void test_ioctl_nr_enum(void) {
    TEST_ASSERT_EQ(AGC_GC_NR_FRAME_OPEN,       0x00u, "NR FRAME_OPEN");
    TEST_ASSERT_EQ(AGC_GC_NR_SUBMIT_PID,       0x3Bu, "NR SUBMIT_PID");
    TEST_ASSERT_EQ(AGC_GC_NR_QUEUE_CREATE,     0x21u, "NR QUEUE_CREATE");
    TEST_ASSERT_EQ(AGC_GC_NR_QUEUE_DESTROY,    0x0Eu, "NR QUEUE_DESTROY");
    TEST_ASSERT_EQ(AGC_GC_NR_MAKESYSMAP_8,     0x09u, "NR MAKESYSMAP_8");
    TEST_ASSERT_EQ(AGC_GC_NR_SET_TF_RING,      0x20u, "NR SET_TF_RING");
    TEST_ASSERT_EQ(AGC_GC_NR_SETUP_ASYNC,      0x1Fu, "NR SETUP_ASYNC");
    TEST_ASSERT_EQ(AGC_GC_NR_SUSPEND_16,       0x1Cu, "NR SUSPEND_16");
    TEST_ASSERT_EQ(AGC_GC_IOCTL_COUNT,         76u,   "ioctl count");
}

static void test_submit_struct_layout(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcSubmitArgs),    0x18u, "SubmitArgs size");
    TEST_ASSERT_EQ(offsetof(AgcGcSubmitArgs, pid),      0x00u, "SubmitArgs pid");
    TEST_ASSERT_EQ(offsetof(AgcGcSubmitArgs, num_cbs),  0x08u, "SubmitArgs num_cbs");
    TEST_ASSERT_EQ(offsetof(AgcGcSubmitArgs, cb_array), 0x10u, "SubmitArgs cb_array");
}

static void test_command_buffer_struct_layout(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcCommandBuffer), 0x10u, "CommandBuffer size");
    TEST_ASSERT_EQ(offsetof(AgcGcCommandBuffer, header),  0x00u, "CommandBuffer header");
    TEST_ASSERT_EQ(offsetof(AgcGcCommandBuffer, ib_base), 0x08u, "CommandBuffer ib_base");
}

static void test_cb_header_opcodes(void) {
    TEST_ASSERT_EQ(AGC_GC_CB_HEADER_IB,        0xC0023F00u, "CB IB opcode (0x3F)");
    TEST_ASSERT_EQ(AGC_GC_CB_HEADER_IB_CNST,   0xC0023300u, "CB IB_CNST opcode (0x33)");
}

static void test_ib_vmid_layout(void) {
    /* VMID in bits [63:52], address in bits [51:0] */
    uint64_t ib = 0x0000001234567890ULL;
    uint64_t vmid = 5u;
    uint64_t packed = (ib & AGC_GC_IB_VMASK) | (vmid << AGC_GC_IB_VSHIFT);
    TEST_ASSERT_EQ((uint32_t)(packed >> AGC_GC_IB_VSHIFT), 5u, "VMID extracted");
    TEST_ASSERT_EQ((uint32_t)packed, 0x34567890u, "addr lo preserved");
}

static void test_vmid_and_cbs_ranges(void) {
    TEST_ASSERT_EQ(AGC_GC_VMIN, 2u, "VMID min");
    TEST_ASSERT_EQ(AGC_GC_VMAX, 15u, "VMID max");
    TEST_ASSERT_EQ(AGC_GC_NUM_CBS_MIN, 1u, "num_cbs min");
    TEST_ASSERT_EQ(AGC_GC_NUM_CBS_MAX, 0xFFFu, "num_cbs max");
}

static void test_makesysmap_structs(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcMakesysmapArg8),  0x08u, "MakesysmapArg8 size");
    TEST_ASSERT_EQ(sizeof(AgcGcMakesysmapArg12), 0x0Cu, "MakesysmapArg12 size");
    TEST_ASSERT_EQ(sizeof(AgcGcMakesysmapArg48), 0x30u, "MakesysmapArg48 size");
    TEST_ASSERT_EQ(offsetof(AgcGcMakesysmapArg48, cpu_addr), 0x00u, "MakesysmapArg48 cpu_addr");
    TEST_ASSERT_EQ(offsetof(AgcGcMakesysmapArg48, gpu_addr), 0x08u, "MakesysmapArg48 gpu_addr");
    TEST_ASSERT_EQ(offsetof(AgcGcMakesysmapArg48, size),    0x10u, "MakesysmapArg48 size");
    TEST_ASSERT_EQ(offsetof(AgcGcMakesysmapArg48, flags),   0x18u, "MakesysmapArg48 flags");
}

static void test_suspend_struct_layout(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcSuspendArg), 0x10u, "SuspendArg size");
    TEST_ASSERT_EQ(offsetof(AgcGcSuspendArg, field0), 0x00u, "SuspendArg field0");
    TEST_ASSERT_EQ(offsetof(AgcGcSuspendArg, field1), 0x04u, "SuspendArg field1");
    TEST_ASSERT_EQ(offsetof(AgcGcSuspendArg, field2), 0x08u, "SuspendArg field2");
    TEST_ASSERT_EQ(offsetof(AgcGcSuspendArg, field3), 0x0Cu, "SuspendArg field3");
}

static void test_suspend_point_api(void) {
    int32_t ret = sce_agc_initialize();
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "initialize for suspend");

    ret = sceAgcDriverSuspendPointSubmitDirect(1u, 0u, 0u, 0u);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "SuspendPointSubmitDirect");

    ret = sce_agc_internal_suspend_point_submit_final(1u, 0u, 0u, 0u);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "internal suspend point final");

    bool in_flight = sceAgcDriverIsSuspendPointInFlightDirect(0u);
    TEST_ASSERT_EQ(in_flight, false, "IsSuspendPointInFlightDirect");

    ret = sceAgcSuspendPointAndCheckStatus(0u);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "SuspendPointAndCheckStatus");
}

static void test_set_hs_offchip_struct_layout(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcSetHsOffchipArg), 0x10u, "SetHsOffchipArg size");
    TEST_ASSERT_EQ(offsetof(AgcGcSetHsOffchipArg, list_addr), 0x00u, "SetHsOffchipArg list_addr");
    TEST_ASSERT_EQ(offsetof(AgcGcSetHsOffchipArg, num_entries), 0x08u, "SetHsOffchipArg num_entries");
    TEST_ASSERT_EQ(offsetof(AgcGcSetHsOffchipArg, reserved), 0x0Cu, "SetHsOffchipArg reserved");
}

static void test_queue_create_struct_layout(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcQueueCreateArg), 0x40u, "QueueCreateArg size");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, magic1),    0x00u, "QueueCreateArg magic1");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, magic2),    0x04u, "QueueCreateArg magic2");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, magic3),    0x08u, "QueueCreateArg magic3");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, token),     0x0Cu, "QueueCreateArg token");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, read_ptr_addr), 0x10u, "QueueCreateArg read_ptr_addr");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, caller_arg),0x18u, "QueueCreateArg caller_arg");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, mmio_base), 0x20u, "QueueCreateArg mmio_base");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, pipe_id),   0x28u, "QueueCreateArg pipe_id");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, ring_addr), 0x30u, "QueueCreateArg ring_addr");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueCreateArg, ring_size), 0x38u, "QueueCreateArg ring_size");
}

static void test_queue_create_magic_values(void) {
    TEST_ASSERT_EQ(AGC_GC_QUEUE_MAGIC1,    0xaf1e80b7u, "QUEUE_MAGIC1");
    TEST_ASSERT_EQ(AGC_GC_QUEUE_MAGIC2,    0x8b4cdd90u, "QUEUE_MAGIC2");
    TEST_ASSERT_EQ(AGC_GC_QUEUE_MAGIC3,    0x99f68d6cu, "QUEUE_MAGIC3");
    TEST_ASSERT_EQ(AGC_GC_QUEUE_TOKEN,     0xe5fcc174u, "QUEUE_TOKEN");
    TEST_ASSERT_EQ(AGC_GC_QUEUE_PIPE_ID,   0xcu,        "QUEUE_PIPE_ID");
    TEST_ASSERT_EQ(AGC_GC_QUEUE_RING_SIZE, 0x1000u,     "QUEUE_RING_SIZE");
}

static void test_queue_destroy_struct_layout(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcQueueDestroyArg), 0x0Cu, "QueueDestroyArg size");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueDestroyArg, magic1), 0x00u, "QueueDestroyArg magic1");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueDestroyArg, magic2), 0x04u, "QueueDestroyArg magic2");
    TEST_ASSERT_EQ(offsetof(AgcGcQueueDestroyArg, magic3), 0x08u, "QueueDestroyArg magic3");
}

static void test_queue_api(void) {
    int32_t ret = sce_agc_initialize();
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "initialize for queue");

    ret = _sceAgcDriverCreateUserSpecialQueue();
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "CreateUserSpecialQueue");

    ret = _sceAgcDriverDestroyUserSpecialQueue();
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "DestroyUserSpecialQueue");
}

static void test_misc_driver_ioctls(void) {
    int32_t ret = sceAgcDriverSetupAsyncGraphics(0u);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "SetupAsyncGraphics");

    ret = sceAgcDriverSetTFRingDirect();
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "SetTFRingDirect");

    ret = sceAgcDriverSetHsOffchipParamDirect(0u, 0u);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "SetHsOffchipParamDirect");

    ret = sceAgcDriverNotifyDefaultStates(0u);
    TEST_ASSERT_EQ(ret, (int32_t)AGC_OK, "NotifyDefaultStates");
}

static void test_frame_open_struct(void) {
    TEST_ASSERT_EQ(sizeof(AgcGcFrameOpenArg), 0x08u, "FrameOpenArg size");
}

static void test_kernel_error_codes(void) {
    TEST_ASSERT_EQ(AGC_GC_ERROR_NO_QUEUE,     (int32_t)0x804C0001, "ERROR_NO_QUEUE");
    TEST_ASSERT_EQ(AGC_GC_ERROR_NO_PROC,      (int32_t)0x804C0013, "ERROR_NO_PROC");
    TEST_ASSERT_EQ(AGC_GC_ERROR_NO_VA_SPACE,  (int32_t)0x804C0005, "ERROR_NO_VA_SPACE");
    TEST_ASSERT_EQ(AGC_GC_ERROR_TOO_MANY_CBS, (int32_t)0x804C000F, "ERROR_TOO_MANY_CBS");
    TEST_ASSERT_EQ(AGC_GC_ERROR_BAD_OPCODE,   (int32_t)0x804C0010, "ERROR_BAD_OPCODE");
    TEST_ASSERT_EQ(AGC_GC_ERROR_RING_FULL,    (int32_t)0x00000010, "ERROR_RING_FULL");
}

static void test_kernel_offsets(void) {
    TEST_ASSERT_EQ(AGC_GC_KERN_IOCTL_INTERNAL,  0x6ed39cu, "ioctl_internal offset");
    TEST_ASSERT_EQ(AGC_GC_KERN_SUBMIT_WITH_PID, 0x6e65c0u, "submit_with_pid offset");
    TEST_ASSERT_EQ(AGC_GC_KERN_FRAME_SUBMIT,    0xb7da90u, "frame_submit offset");
}

void test_suite_ioctl(void) {
    TEST_SUITE("Ioctl / Submit / Queue Layout");
    TEST_RUN(test_ioctl_encoding);
    TEST_RUN(test_ioctl_nr_enum);
    TEST_RUN(test_submit_struct_layout);
    TEST_RUN(test_command_buffer_struct_layout);
    TEST_RUN(test_cb_header_opcodes);
    TEST_RUN(test_ib_vmid_layout);
    TEST_RUN(test_vmid_and_cbs_ranges);
    TEST_RUN(test_makesysmap_structs);
    TEST_RUN(test_suspend_struct_layout);
    TEST_RUN(test_suspend_point_api);
    TEST_RUN(test_set_hs_offchip_struct_layout);
    TEST_RUN(test_queue_create_struct_layout);
    TEST_RUN(test_queue_create_magic_values);
    TEST_RUN(test_queue_destroy_struct_layout);
    TEST_RUN(test_queue_api);
    TEST_RUN(test_misc_driver_ioctls);
    TEST_RUN(test_frame_open_struct);
    TEST_RUN(test_kernel_error_codes);
    TEST_RUN(test_kernel_offsets);
}
