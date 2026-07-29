#include "test.h"

#include "agc_workload_packet.h"

#include <stdint.h>
#include <string.h>

static void test_sony_workloads_active_standalone(void)
{
    uint32_t packet[AGC_SONY_WORKLOAD_ACTIVE_DWORDS];
    const uint32_t expected[] = {
        0xc0027904u, 0x00000342u, 0xcc000001u, 0x00000002u,
        0xc0033704u, 0x06010000u, 0x0000c343u, 0x00000000u,
        0x00000000u, 0xc0071e00u, 0x40000267u, 0x9abcdef0u,
        0x12345678u, 0x00000002u, 0x00000000u, 0x00000000u,
        0x00000000u, 0x00000000u,
    };

    memset(packet, 0xa5, sizeof(packet));
    TEST_ASSERT_EQ(agcSonyBuildWorkloadsActivePacket(packet,
        AGC_SONY_WORKLOAD_ACTIVE_DWORDS, false, 1u,
        UINT64_C(0x123456789abcdef0), UINT64_C(2)),
        AGC_SONY_WORKLOAD_ACTIVE_DWORDS,
        "Sony workload active packet size");
    TEST_ASSERT(memcmp(packet, expected, sizeof(expected)) == 0,
        "Sony workload active packet bytes");
}

static void test_sony_workload_complete_standalone(void)
{
    uint32_t packet[AGC_SONY_WORKLOAD_COMPLETE_DWORDS];
    const uint32_t expected[] = {
        0xc0017904u, 0x00000342u, 0xcd000021u, 0xc0071e00u,
        0x40000275u, 0x9abcdef0u, 0x12345678u, 0xfffffffdu,
        0xffffffffu, 0x00000000u, 0x00000000u, 0x00000000u,
    };

    memset(packet, 0xa5, sizeof(packet));
    TEST_ASSERT_EQ(agcSonyBuildWorkloadCompletePacket(packet,
        AGC_SONY_WORKLOAD_COMPLETE_DWORDS, false, 1u, 1u,
        UINT64_C(0x123456789abcdef0)),
        AGC_SONY_WORKLOAD_COMPLETE_DWORDS,
        "Sony workload complete packet size");
    TEST_ASSERT(memcmp(packet, expected, sizeof(expected)) == 0,
        "Sony workload complete packet bytes");
}

static void test_sony_workload_control_and_validation(void)
{
    uint32_t packet[AGC_SONY_WORKLOAD_ACTIVE_DWORDS];

    TEST_ASSERT_EQ(agcSonyBuildWorkloadsActivePacket(packet,
        AGC_SONY_WORKLOAD_ACTIVE_DWORDS, true, 31u,
        UINT64_C(0x200000040), UINT64_C(1) << 63),
        AGC_SONY_WORKLOAD_ACTIVE_DWORDS,
        "Sony workload controlled packet size");
    TEST_ASSERT_EQ(packet[0], 0xc0027900u,
        "Sony active controlled prefix");
    TEST_ASSERT_EQ(packet[4], 0xc0033700u,
        "Sony active controlled payload");
    TEST_ASSERT_EQ(packet[10], 0x00000267u,
        "Sony active controlled final packet");
    TEST_ASSERT_EQ(agcSonyBuildWorkloadsActivePacket(packet,
        AGC_SONY_WORKLOAD_ACTIVE_DWORDS, false, 0u,
        UINT64_C(0x200000040), UINT64_C(1)), 0u,
        "Sony active rejects stream zero");
    TEST_ASSERT_EQ(agcSonyBuildWorkloadsActivePacket(packet,
        AGC_SONY_WORKLOAD_ACTIVE_DWORDS, false, 1u,
        UINT64_C(0x200000044), UINT64_C(1)), 0u,
        "Sony active rejects unaligned slot");
    TEST_ASSERT_EQ(agcSonyBuildWorkloadsActivePacket(packet,
        AGC_SONY_WORKLOAD_ACTIVE_DWORDS, false, 1u,
        UINT64_C(0x200000040), UINT64_C(0)), 0u,
        "Sony active rejects empty mask");
    TEST_ASSERT_EQ(agcSonyBuildWorkloadCompletePacket(packet,
        AGC_SONY_WORKLOAD_COMPLETE_DWORDS, false, 1u, 64u,
        UINT64_C(0x200000040)), 0u,
        "Sony complete rejects workload 64");
}

static void test_sony_workload_stream_inactive(void)
{
    uint32_t packet[AGC_SONY_WORKLOAD_INACTIVE_DWORDS];
    const uint32_t expected_dcb[] = {
        0xc0027904u, 0x00000342u, 0xcc000001u, 0x00000000u,
        0xc0033704u, 0x06010000u, 0x0000c343u, 0x00000000u,
        0x00000000u,
    };

    memset(packet, 0xa5, sizeof(packet));
    TEST_ASSERT_EQ(agcSonyBuildWorkloadStreamInactivePacket(packet,
        AGC_SONY_WORKLOAD_INACTIVE_DWORDS, false, 1u),
        AGC_SONY_WORKLOAD_INACTIVE_DWORDS,
        "Sony workload inactive packet size");
    TEST_ASSERT(memcmp(packet, expected_dcb, sizeof(expected_dcb)) == 0,
        "Sony workload inactive DCB bytes");
    TEST_ASSERT_EQ(agcSonyBuildWorkloadStreamInactivePacket(packet,
        AGC_SONY_WORKLOAD_INACTIVE_DWORDS, true, 1u),
        AGC_SONY_WORKLOAD_INACTIVE_DWORDS,
        "Sony workload inactive ACB packet size");
    TEST_ASSERT_EQ(packet[0], 0xc0027900u,
        "Sony inactive ACB prefix control");
    TEST_ASSERT_EQ(packet[4], 0xc0033700u,
        "Sony inactive ACB payload control");
}

void test_suite_workload_packet(void)
{
    TEST_SUITE("Sony Workload Packets");
    TEST_RUN(test_sony_workloads_active_standalone);
    TEST_RUN(test_sony_workload_complete_standalone);
    TEST_RUN(test_sony_workload_control_and_validation);
    TEST_RUN(test_sony_workload_stream_inactive);
}
