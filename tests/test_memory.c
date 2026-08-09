#include "test.h"
#include "agc_error.h"
#include "agc_memory.h"

#include <stdint.h>

static void test_flexible_memory_lifecycle(void)
{
    AgcGpuMemory memory = {0};
    TEST_ASSERT_EQ(agcGpuMemoryAllocateFlexible(
        &memory, 0x4100u, 256u, "openagc_test"), AGC_OK,
        "flexible allocation succeeds");
    TEST_ASSERT(memory.cpu_address != NULL, "CPU address returned");
    TEST_ASSERT_EQ(memory.gpu_address,
        (uint64_t)(uintptr_t)memory.cpu_address, "unified GPU address returned");
    TEST_ASSERT_EQ(memory.size, 0x4100u, "requested size retained");
    TEST_ASSERT_EQ(memory.mapped_size, 0x8000u, "mapping rounded to 16 KiB");
    TEST_ASSERT_EQ((uintptr_t)memory.cpu_address & 255u, 0u,
        "requested alignment satisfied");

    ((uint32_t *)memory.cpu_address)[3] = 0x12345678u;
    TEST_ASSERT_EQ(agcGpuMemoryFlush(&memory, 12u, 4u), AGC_OK,
        "CPU writes flush");
    TEST_ASSERT_EQ(agcGpuMemoryInvalidate(&memory, 12u, 4u), AGC_OK,
        "GPU writes invalidate");
    TEST_ASSERT_EQ(agcGpuMemoryWait32(&memory, 12u, 0x12345678u, 0u), AGC_OK,
        "matching label completes immediately");
    TEST_ASSERT_EQ(agcGpuMemoryWait32(&memory, 12u, 0u, 0u),
        AGC_ERROR_TIMEOUT, "non-matching label observes bounded timeout");
    TEST_ASSERT_EQ(agcGpuMemoryFlush(&memory, memory.size, 1u),
        AGC_ERROR_INVALID_ARGUMENT, "out-of-bounds cache range rejected");

    agcGpuMemoryFreeFlexible(&memory);
    TEST_ASSERT(memory.cpu_address == NULL && memory.size == 0u,
        "free clears allocation record");
}

static void test_flexible_memory_invalid_arguments(void)
{
    AgcGpuMemory memory = {0};
    TEST_ASSERT_EQ(agcGpuMemoryAllocateFlexible(NULL, 1u, 1u, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "NULL output rejected");
    TEST_ASSERT_EQ(agcGpuMemoryAllocateFlexible(&memory, 0u, 1u, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "zero size rejected");
    TEST_ASSERT_EQ(agcGpuMemoryAllocateFlexible(&memory, 1u, 3u, NULL),
        AGC_ERROR_INVALID_ARGUMENT, "non-power-of-two alignment rejected");
}

static void test_direct_memory_lifecycle(void)
{
    AgcGpuMemory memory = {0};
    TEST_ASSERT_EQ(agcGpuMemoryAllocateDirectWriteCombined(
        &memory, 0x1000u, 0x1000u), AGC_OK,
        "write-combined direct allocation succeeds");
    TEST_ASSERT_EQ(memory.type,
        AGC_GPU_MEMORY_TYPE_DIRECT_WRITE_COMBINED,
        "direct memory type recorded");
    TEST_ASSERT_EQ(memory.mapped_size, 0x200000u,
        "direct allocation uses 2 MiB granularity");
    TEST_ASSERT_EQ((uintptr_t)memory.cpu_address & 0x1fffffu, 0u,
        "direct mapping is 2 MiB aligned");
    agcGpuMemoryFreeDirect(&memory);
    TEST_ASSERT(memory.cpu_address == NULL && memory.type == 0u,
        "direct free clears allocation record");
}

void test_suite_memory(void)
{
    TEST_SUITE("GPU Memory");
    TEST_RUN(test_flexible_memory_lifecycle);
    TEST_RUN(test_flexible_memory_invalid_arguments);
    TEST_RUN(test_direct_memory_lifecycle);
}
