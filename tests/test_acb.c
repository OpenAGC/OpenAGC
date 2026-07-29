#include "test.h"
#include "agc_cb.h"
#include "agcdriver.h"
#include "agc_pm4.h"

static void test_acb_init_null(void) {
    int32_t r = sceAgcAcbInitializeDefaultHardwareState_pre0090(NULL, 100);
    TEST_ASSERT(r < 0, "NULL acb should fail");
}

static void test_acb_init_small(void) {
    uint32_t buf[2];
    int32_t r = sceAgcAcbInitializeDefaultHardwareState_pre0090(buf, 1);
    TEST_ASSERT(r < 0, "Tiny buffer should fail");
}

static void test_acb_init_ok(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbInitializeDefaultHardwareState_pre0090(buf, 64);
    TEST_ASSERT(r > 0, "Init should return positive dword count");
    TEST_ASSERT_EQ(agcPm4Type(buf[0]), AGC_PM4_TYPE3, "First dword should be PM4 type 3");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 2, "Init NOP should be two dwords");
}

static void test_acb_dispatch_indirect(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbDispatchIndirect(buf, 64, 0x1000);
    TEST_ASSERT(r == 4, "Dispatch indirect should write 4 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_DISPATCH_INDIRECT, "Dispatch indirect opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 4, "Dispatch indirect length");
}

static void test_acb_acquire_mem(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbAcquireMem(buf, 64, 0x2u, 0x80000000u, 0x10000u,
                                    0xAABBCCDD11223344u);
    TEST_ASSERT_EQ(r, 8, "AcquireMem should write 8 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_ACQUIRE_MEM, "AcquireMem opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 8, "AcquireMem length");
    TEST_ASSERT_EQ(buf[1], 0x80000000u, "AcquireMem coher_cntl");
    TEST_ASSERT_EQ(buf[2], 0x10000u, "AcquireMem coher_size");
    TEST_ASSERT_EQ(buf[3], 0x11223344u, "AcquireMem coher_base_lo");
    TEST_ASSERT_EQ(buf[4], 0xAABBCCDDu, "AcquireMem coher_base_hi");
    TEST_ASSERT_EQ(buf[5], 0x0u, "AcquireMem coher_size_hi");
    TEST_ASSERT_EQ(buf[6], 0x2u, "AcquireMem engine_sel");
    TEST_ASSERT_EQ(buf[7], 0x0u, "AcquireMem reserved");
}

static void test_acb_push_marker(void) {
    uint32_t buf[64];
    SceAgcCb cb;
    agcCbInit(&cb, buf, sizeof(buf));
    uint32_t *r = sceAgcAcbPushMarker(&cb, "test", 0x11223344u);
    TEST_ASSERT(r == buf, "Push marker should return packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(buf[0]), AGC_PM4_SUB_PUSH_MARKER, "Push marker subcommand");
    TEST_ASSERT_EQ(buf[1], 0x11223344u, "Push marker color");
    TEST_ASSERT_EQ(buf[2], 0x74736574u, "Push marker text");

    r = sceAgcAcbSetMarkerSpan(&cb, "span-tail", 4u, 0xAABBCCDDu);
    TEST_ASSERT(r == &buf[3], "Set marker span advances cursor");
    TEST_ASSERT_EQ(agcPm4Subcommand(r[0]), AGC_PM4_SUB_SET_MARKER,
        "Set marker span subcommand");
    TEST_ASSERT_EQ(r[1], 0xAABBCCDDu, "Set marker span color");
    TEST_ASSERT_EQ(r[2], 0x6E617073u, "Set marker span uses explicit length");
}

static void test_acb_pop_marker(void) {
    uint32_t buf[64];
    SceAgcCb cb;
    agcCbInit(&cb, buf, sizeof(buf));
    uint32_t *r = sceAgcAcbPopMarker(&cb);
    TEST_ASSERT(r == buf, "Pop marker should return packet");
    TEST_ASSERT_EQ(agcPm4Subcommand(buf[0]), AGC_PM4_SUB_POP_MARKER, "Pop marker subcommand");
}

static void test_acb_event_write(void) {
    uint32_t buf[64];
    /* SPRX-confirmed: always 2 dwords, cmd[1] = (type==7 ? 0x400 : 0) | (type & 0x3f) */
    int32_t r = sceAgcAcbEventWrite(buf, 64, 0x12u, 0x1234567890ABCDEFu, 0xA5A5A5A5u, 1u);
    TEST_ASSERT_EQ(r, 2, "Event write should write 2 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_EVENT_WRITE, "Event write opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 2, "Event write length");
    TEST_ASSERT_EQ(buf[1], 0x12u, "Event write cmd[1] = event_type & 0x3f");
}

static void test_acb_event_write_special_type7(void) {
    uint32_t buf[64];
    /* SPRX-confirmed: event_type 7 gets 0x400 prefix */
    int32_t r = sceAgcAcbEventWrite(buf, 64, 0x07u, 0, 0, 0);
    TEST_ASSERT_EQ(r, 2, "Event type 7 should write 2 dwords");
    TEST_ASSERT_EQ(buf[1], 0x407u, "Event type 7 cmd[1] = 0x400 | 7");
}

static void test_acb_atomic_mem(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbAtomicMem(buf, 64, 0x7u, 0xAABBCCDDEEFF0011u, 0x12345678u);
    TEST_ASSERT_EQ(r, 5, "Atomic mem should write 5 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_ATOMIC_MEM, "Atomic mem opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 5, "Atomic mem length");
    TEST_ASSERT_EQ(buf[1], 0x7u, "Atomic op = inc");
    TEST_ASSERT_EQ(buf[2], 0xEEFF0011u, "Atomic addr_lo");
    TEST_ASSERT_EQ(buf[3], 0xAABBCCDDu, "Atomic addr_hi");
    TEST_ASSERT_EQ(buf[4], 0x12345678u, "Atomic data");
}

static void test_acb_cond_exec(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbCondExec(buf, 64, 0x1122334455667788u, 0x20u);
    TEST_ASSERT_EQ(r, 5, "Cond exec should write 5 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_COND_EXEC, "Cond exec opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 5, "Cond exec length");
    TEST_ASSERT_EQ(buf[1], 0x55667788u, "Cond exec addr_lo");
    TEST_ASSERT_EQ(buf[2], 0x11223344u, "Cond exec addr_hi");
    TEST_ASSERT_EQ(buf[3], 0x0u, "Cond exec reserved");
    TEST_ASSERT_EQ(buf[4], 0x20u, "Cond exec count");
}

static void test_acb_wait_reg_mem(void) {
    uint32_t buf[64] = {0};
    SceAgcCb cb;
    agcCbInit(&cb, buf, sizeof(buf));

    uint32_t *cmd = sceAgcAcbWaitRegMem(
        &cb, 1, 5, 2, 0x200000047ULL, 0x1122334455667788ULL,
        0xFFEEDDCCBBAA0099ULL, UINT32_MAX);
    TEST_ASSERT(cmd == buf, "WaitRegMem should return packet start");
    TEST_ASSERT_EQ(agcPm4Opcode(cmd[0]), AGC_PM4_OP_WAIT_REG_MEM64, "WaitRegMem64 opcode");
    TEST_ASSERT_EQ(agcPm4Subcommand(cmd[0]), AGC_PM4_SUB_ZERO, "WaitRegMem64 header controls");
    TEST_ASSERT_EQ(agcPm4Length(cmd[0]), 9, "WaitRegMem64 length");
    TEST_ASSERT_EQ(cmd[1], 0x04000015u, "WaitRegMem64 operation-zero control");
    TEST_ASSERT_EQ(cmd[2], 0x40u, "WaitRegMem64 aligned address low");
    TEST_ASSERT_EQ(cmd[3], 0x2u, "WaitRegMem64 address high");
    TEST_ASSERT_EQ(cmd[4], 0x55667788u, "WaitRegMem64 reference low");
    TEST_ASSERT_EQ(cmd[5], 0x11223344u, "WaitRegMem64 reference high");
    TEST_ASSERT_EQ(cmd[6], 0xBBAA0099u, "WaitRegMem64 mask low");
    TEST_ASSERT_EQ(cmd[7], 0xFFEEDDCCu, "WaitRegMem64 mask high");
    TEST_ASSERT_EQ(cmd[8], 0xFFFFu, "WaitRegMem64 poll saturates");
    TEST_ASSERT_EQ(cb.cursor_up, (uintptr_t)(buf + 9), "WaitRegMem64 cursor advance");
}

static void test_acb_write_data(void) {
    uint32_t buf[64];
    /* op = dst_sel=1, engine_sel=2, vmid_sel=3, addr_incr=4 -> 0x4321 */
    int32_t r = sceAgcAcbWriteData(buf, 64, 0x4321u, 0xAABBCCDD11223344u, 0xDEADBEEFu);
    TEST_ASSERT_EQ(r, 5, "WriteData should write 5 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_WRITE_DATA, "WriteData opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 5, "WriteData length");
    /* control: dst_sel=1<<16 | engine_sel=2<<8 | vmid_sel=3<<20 | addr_incr=4<<24 = 0x04310200 */
    TEST_ASSERT_EQ(buf[1], 0x04310200u, "WriteData control");
    TEST_ASSERT_EQ(buf[2], 0x11223344u, "WriteData addr_lo");
    TEST_ASSERT_EQ(buf[3], 0xAABBCCDDu, "WriteData addr_hi");
    TEST_ASSERT_EQ(buf[4], 0xDEADBEEFu, "WriteData data");
}

static void test_acb_copy_data(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbCopyData(buf, 64, 0x0u, 0x1u, 0xAABBCCDD00112233u,
                                  0x11223344AABBCCDDu, 0x100u);
    TEST_ASSERT_EQ(r, 6, "CopyData should write 6 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_COPY_DATA, "CopyData opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 6, "CopyData length");
    /* control: src_sel=0 | dst_sel=1<<4 | wr_confirm=1<<9 | byte_count=0x100<<16 */
    TEST_ASSERT_EQ(buf[1], 0x1000210u, "CopyData control");
    TEST_ASSERT_EQ(buf[2], 0x00112233u, "CopyData src_addr_lo");
    TEST_ASSERT_EQ(buf[3], 0xAABBCCDDu, "CopyData src_addr_hi");
    TEST_ASSERT_EQ(buf[4], 0xAABBCCDDu, "CopyData dst_addr_lo");
    TEST_ASSERT_EQ(buf[5], 0x11223344u, "CopyData dst_addr_hi");
}

static void test_acb_mem_semaphore(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbMemSemaphore(buf, 64, 0x1u, 0xAABBCCDD11223344u, 0x12345678u);
    TEST_ASSERT_EQ(r, 4, "MemSemaphore should write 4 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_MEM_SEMAPHORE, "MemSemaphore opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 4, "MemSemaphore length");
    TEST_ASSERT_EQ(buf[1], 0x11223344u, "MemSemaphore addr_lo");
    TEST_ASSERT_EQ(buf[2], 0xAABBCCDDu, "MemSemaphore addr_hi");
    TEST_ASSERT_EQ(buf[3], 0x12345678u, "MemSemaphore data");
}

static void test_acb_dma_data(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbDmaData(buf, 64, 0xAABBCCDD00112233u,
                                 0x11223344AABBCCDDu, 0x100u, 0x1u, 0x2u);
    TEST_ASSERT_EQ(r, 8, "DmaData should write 8 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_DMA_DATA, "DmaData opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 8, "DmaData length");
    TEST_ASSERT_EQ(buf[1], 0x00020001u, "DmaData control (src_swap=1, dst_swap=2)");
    TEST_ASSERT_EQ(buf[2], 0x100u, "DmaData byte_count");
    TEST_ASSERT_EQ(buf[3], 0xAABBCCDDu, "DmaData dst_addr_lo");
    TEST_ASSERT_EQ(buf[4], 0x11223344u, "DmaData dst_addr_hi");
    TEST_ASSERT_EQ(buf[5], 0x00112233u, "DmaData src_addr_lo");
    TEST_ASSERT_EQ(buf[6], 0xAABBCCDDu, "DmaData src_addr_hi");
    TEST_ASSERT_EQ(buf[7], 0x0u, "DmaData reserved");
}

static void test_acb_reset_queue(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbResetQueue(buf, 64, 0x5u);
    TEST_ASSERT_EQ(r, 3, "ResetQueue should write 3 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_UCONFIG_REG, "ResetQueue opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 3, "ResetQueue length");
    TEST_ASSERT_EQ(buf[1], 0x00000342u, "ResetQueue control word");
    TEST_ASSERT_EQ(buf[2], 0x5u, "ResetQueue queue_id");
}

static void test_acb_rewind(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbRewind(buf, 64);
    TEST_ASSERT_EQ(r, 2, "Rewind should write 2 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_NOP, "Rewind opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 2, "Rewind length");
    TEST_ASSERT_EQ(buf[1], 0x0u, "Rewind payload");
}

static void test_acb_set_flip(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbSetFlip(buf, 64, 0x0u, 0x2u, 0x1u);
    TEST_ASSERT_EQ(r, 7, "SetFlip should write 7 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_RELEASE_MEM, "SetFlip opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 7, "SetFlip length");
    TEST_ASSERT_EQ(buf[1], 0x0u, "SetFlip event control");
    TEST_ASSERT_EQ(buf[2], 0x2u, "SetFlip buffer_index");
    TEST_ASSERT_EQ(buf[3], 0x1u, "SetFlip vsync");
    TEST_ASSERT_EQ(buf[4], 0x0u, "SetFlip reserved 4");
    TEST_ASSERT_EQ(buf[5], 0x0u, "SetFlip reserved 5");
    TEST_ASSERT_EQ(buf[6], 0x0u, "SetFlip reserved 6");
}

static void test_acb_set_workload_complete(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbSetWorkloadComplete(buf, 64, 0xAABBCCDD11223344u);
    TEST_ASSERT_EQ(r, 8, "SetWorkloadComplete should write 8 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_WORKLOAD, "SetWorkloadComplete opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 8, "SetWorkloadComplete length");
    TEST_ASSERT_EQ(buf[1], 0x11223344u, "SetWorkloadComplete workload lo");
    TEST_ASSERT_EQ(buf[2], 0x0u, "SetWorkloadComplete reserved 2");
    TEST_ASSERT_EQ(buf[7], 0x0u, "SetWorkloadComplete reserved 7");
}

static void test_acb_set_workload_stream_inactive(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbSetWorkloadStreamInactive(buf, 64, 0xAABBCCDD11223344u);
    TEST_ASSERT_EQ(r, 3, "SetWorkloadStreamInactive should write 3 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_UCONFIG_REG, "SetWorkloadStreamInactive opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 3, "SetWorkloadStreamInactive length");
    TEST_ASSERT_EQ(buf[1], 0x00000342u, "SetWorkloadStreamInactive control");
    TEST_ASSERT_EQ(buf[2], 0x11223344u, "SetWorkloadStreamInactive workload lo");
}

static void test_acb_set_workloads_active(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbSetWorkloadsActive(buf, 64, 0x12345678u);
    TEST_ASSERT_EQ(r, 8, "SetWorkloadsActive should write 8 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_SET_WORKLOAD, "SetWorkloadsActive opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 8, "SetWorkloadsActive length");
    TEST_ASSERT_EQ(buf[1], 0x12345678u, "SetWorkloadsActive flags");
    TEST_ASSERT_EQ(buf[7], 0x0u, "SetWorkloadsActive reserved 7");
}

static void test_acb_atomic_gds(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbAtomicGds(buf, 64, 0x7u, 0x1234u, 0xAABBCCDDu, 0x1u);
    TEST_ASSERT_EQ(r, 10, "AtomicGds should write 10 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_ATOMIC_GDS, "AtomicGds opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 10, "AtomicGds length");
    TEST_ASSERT_EQ(buf[1], 0x12340007u, "AtomicGds control (op=7, gds_offset=0x1234)");
    TEST_ASSERT_EQ(buf[2], 0xAABBCCDDu, "AtomicGds data");
    TEST_ASSERT_EQ(buf[3], 0x1u, "AtomicGds src");
    TEST_ASSERT_EQ(buf[9], 0x0u, "AtomicGds reserved 9");
}

static void test_acb_prime_utcl2(void) {
    uint32_t buf[64];
    int32_t r = sceAgcAcbPrimeUtcl2(buf, 64, 0xAABBCCDD11223344u, 0x1000u);
    TEST_ASSERT_EQ(r, 4, "PrimeUtcl2 should write 4 dwords");
    TEST_ASSERT_EQ(agcPm4Opcode(buf[0]), AGC_PM4_OP_PRIME_UTCL2, "PrimeUtcl2 opcode");
    TEST_ASSERT_EQ(agcPm4Length(buf[0]), 4, "PrimeUtcl2 length");
    TEST_ASSERT_EQ(buf[1], 0x11223344u, "PrimeUtcl2 addr_lo");
    TEST_ASSERT_EQ(buf[2], 0xAABBCCDDu, "PrimeUtcl2 addr_hi");
    TEST_ASSERT_EQ(buf[3], 0x1000u, "PrimeUtcl2 size");
}

void test_suite_acb(void) {
    TEST_SUITE("ACB Commands");
    TEST_RUN(test_acb_init_null);
    TEST_RUN(test_acb_init_small);
    TEST_RUN(test_acb_init_ok);
    TEST_RUN(test_acb_dispatch_indirect);
    TEST_RUN(test_acb_acquire_mem);
    TEST_RUN(test_acb_push_marker);
    TEST_RUN(test_acb_pop_marker);
    TEST_RUN(test_acb_event_write);
    TEST_RUN(test_acb_event_write_special_type7);
    TEST_RUN(test_acb_atomic_mem);
    TEST_RUN(test_acb_cond_exec);
    TEST_RUN(test_acb_wait_reg_mem);
    TEST_RUN(test_acb_write_data);
    TEST_RUN(test_acb_copy_data);
    TEST_RUN(test_acb_mem_semaphore);
    TEST_RUN(test_acb_dma_data);
    TEST_RUN(test_acb_reset_queue);
    TEST_RUN(test_acb_rewind);
    TEST_RUN(test_acb_set_flip);
    TEST_RUN(test_acb_set_workload_complete);
    TEST_RUN(test_acb_set_workload_stream_inactive);
    TEST_RUN(test_acb_set_workloads_active);
    TEST_RUN(test_acb_atomic_gds);
    TEST_RUN(test_acb_prime_utcl2);
}
