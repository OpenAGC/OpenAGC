/*
 * openagc native runtime contract tests.
 */

#include "test.h"

#include <stdint.h>

#include "agc_driver_debug.h"
#include "agc_graphics.h"
#include "agc_pm4.h"
#include "agc_texture.h"
#include "openagc/runtime.h"

static AgcDevice create_device(void)
{
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcDevice device = NULL;

    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "native device creation succeeds");
    TEST_ASSERT(device != NULL, "native device handle is non-null");
    return device;
}

static AgcShader create_shader(AgcDevice device, AgcShaderStage stage)
{
    static const uint32_t code[] = {0xBF810000u, 0u, 0u, 0u};
    AgcShaderDesc desc = AGC_SHADER_DESC_INIT;
    AgcShader shader = NULL;

    desc.stage = stage;
    desc.code = code;
    desc.code_size = sizeof(code);
    TEST_ASSERT_EQ(agcCreateShader(device, &desc, &shader), AGC_OK,
        "native shader creation succeeds");
    return shader;
}

static AgcQueue create_queue(AgcDevice device, AgcQueueType type)
{
    AgcQueueDesc desc = AGC_QUEUE_DESC_INIT;
    AgcQueue queue = NULL;

    desc.type = type;
    TEST_ASSERT_EQ(agcCreateQueue(device, &desc, &queue), AGC_OK,
        "native queue creation succeeds");
    return queue;
}

static void test_runtime_descriptor_and_info_contract(void)
{
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcRuntimeInfo info = AGC_RUNTIME_INFO_INIT;
    AgcDevice device = NULL;

    desc.version++;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device),
        AGC_ERROR_INVALID_ARGUMENT,
        "device rejects unknown descriptor version");
    desc.version = AGC_RUNTIME_STRUCTURE_VERSION_1;
    desc.reserved[2] = 1u;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device),
        AGC_ERROR_INVALID_ARGUMENT,
        "device rejects nonzero reserved fields");
    desc.reserved[2] = 0u;
    desc.required_capability_bits = UINT64_C(1) << 63;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device),
        AGC_ERROR_INVALID_ARGUMENT,
        "device rejects unknown required capability bit");

    desc.required_capability_bits = AGC_RUNTIME_CAP_BASELINE;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "device accepts required baseline capabilities");
    TEST_ASSERT_EQ(agcGetRuntimeInfo(device, &info), AGC_OK,
        "runtime info query succeeds");
    TEST_ASSERT_EQ(info.runtime_api_version, AGC_RUNTIME_API_VERSION,
        "runtime info reports API version");
    TEST_ASSERT_EQ(info.firmware_version, 0u,
        "generic runtime reports no firmware version");
    TEST_ASSERT_EQ(info.firmware_abi_key, 0u,
        "generic runtime reports no firmware ABI key");
    TEST_ASSERT_EQ(info.hardware_family, AGC_HARDWARE_FAMILY_HOST_TEST,
        "generic backend reports a host-test environment");
    TEST_ASSERT_EQ(info.agc_version, 7u,
        "runtime info reports caller AGC version");
    TEST_ASSERT_EQ(info.capability_bits, AGC_RUNTIME_CAP_BASELINE,
        "runtime info reports baseline capabilities");
    TEST_ASSERT(strcmp(info.profile_name, "generic-host") == 0,
        "runtime info reports exact generic profile");
    for (uint32_t i = 0; i < AGC_RUNTIME_CAPABILITY_COUNT; ++i) {
        TEST_ASSERT_EQ(info.qualification[i], AGC_QUALIFICATION_HOST_TESTED,
            "generic runtime capabilities are host-tested");
    }

    info = (AgcRuntimeInfo)AGC_RUNTIME_INFO_INIT;
    info.reserved[0] = 1u;
    TEST_ASSERT_EQ(agcGetRuntimeInfo(device, &info),
        AGC_ERROR_INVALID_ARGUMENT,
        "runtime info rejects nonzero reserved fields");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "native device destruction succeeds");
}

typedef struct AllocationProbe {
    uint32_t allocations;
    uint32_t frees;
    uint32_t attempts;
    uint32_t fail_on_attempt;
} AllocationProbe;

static void *PS5_SYSV_ABI probe_allocate(
    void *user_data, size_t size, size_t alignment)
{
    AllocationProbe *probe = user_data;
    (void)alignment;
    probe->attempts++;
    if (probe->fail_on_attempt != 0u &&
        probe->attempts == probe->fail_on_attempt)
        return NULL;
    probe->allocations++;
    return malloc(size);
}

static void PS5_SYSV_ABI probe_free(void *user_data, void *memory)
{
    AllocationProbe *probe = user_data;
    probe->frees++;
    free(memory);
}

static void test_runtime_allocation_callbacks(void)
{
    AllocationProbe probe = {0};
    AgcAllocationCallbacks callbacks = {
        &probe, probe_allocate, probe_free
    };
    AgcDeviceDesc desc = AGC_DEVICE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcDevice device = NULL;
    AgcBuffer buffer = NULL;

    desc.allocation_callbacks = &callbacks;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateDevice(&desc, &device), AGC_OK,
        "device accepts paired allocation callbacks");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "buffer uses application allocation callbacks");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK,
        "callback-owned buffer destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "callback-owned device destruction succeeds");
    TEST_ASSERT_EQ(probe.allocations, probe.frees,
        "allocation callbacks receive balanced frees");
}

static void test_runtime_allocation_failure_rollback(void)
{
    AllocationProbe probe = {0};
    AgcAllocationCallbacks callbacks = {
        &probe, probe_allocate, probe_free
    };
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcMemoryStats stats = AGC_MEMORY_STATS_INIT;
    AgcDevice device = NULL;
    AgcBuffer buffer = NULL;

    device_desc.allocation_callbacks = &callbacks;
    buffer_desc.size = 4096u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateDevice(&device_desc, &device), AGC_OK,
        "rollback test device creation succeeds");
    probe.fail_on_attempt = 4u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer),
        AGC_ERROR_OUT_OF_MEMORY,
        "allocation-record failure propagates out of resource creation");
    TEST_ASSERT(buffer == NULL,
        "allocation-record failure leaves resource output null");
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "post-failure memory statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
        "failed creation leaves no live allocation");
    TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_GARLIC], 0u,
        "failed creation rolls back its newly allocated heap block");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "rollback test device destruction succeeds");
    TEST_ASSERT_EQ(probe.allocations, probe.frees,
        "failed allocation path balances every successful callback allocation");
}

static void test_runtime_all_object_lifecycle(void)
{
    AgcDevice device = create_device();
    AgcQueue graphics_queue = create_queue(device, kAgcQueueGraphics);
    AgcQueue compute_queue = create_queue(device, kAgcQueueCompute);
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcGraphicsPipelineDesc graphics_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcComputePipelineDesc compute_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcBuffer buffer = NULL;
    AgcImage image = NULL;
    AgcImageView view = NULL;
    AgcSampler sampler = NULL;
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcShader cs = create_shader(device, kAgcShaderStageCs);
    AgcGraphicsPipeline graphics_pipeline = NULL;
    AgcComputePipeline compute_pipeline = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;

    buffer_desc.size = 256u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    sampler_desc.min_filter = AGC_FILTER_LINEAR;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "buffer object creation succeeds");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "image object creation succeeds");
    view_desc.image = image;
    view_desc.format = image_desc.format;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view), AGC_OK,
        "image-view object creation succeeds");
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler), AGC_OK,
        "sampler object creation succeeds");

    graphics_desc.vertex_shader = vs;
    graphics_desc.pixel_shader = ps;
    compute_desc.shader = cs;
    compute_desc.local_size_x = 64u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &graphics_desc,
        &graphics_pipeline), AGC_OK,
        "graphics-pipeline object creation succeeds");
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &compute_desc,
        &compute_pipeline), AGC_OK,
        "compute-pipeline object creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK,
        "command-buffer object creation succeeds");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "fence object creation succeeds");

    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_ERROR_BUSY,
        "device rejects destruction with live children");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_ERROR_BUSY,
        "image rejects destruction while a view owns it");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_ERROR_BUSY,
        "shader rejects destruction while a pipeline owns it");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "fence destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "command-buffer destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(compute_pipeline), AGC_OK,
        "compute-pipeline destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(graphics_pipeline), AGC_OK,
        "graphics-pipeline destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyShader(cs), AGC_OK, "compute shader destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK, "pixel shader destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK, "vertex shader destruction succeeds");
    TEST_ASSERT_EQ(agcDestroySampler(sampler), AGC_OK, "sampler destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyImageView(view), AGC_OK, "image-view destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK, "image destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffer), AGC_OK, "buffer destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyQueue(compute_queue), AGC_OK,
        "compute queue destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyQueue(graphics_queue), AGC_OK,
        "graphics queue destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "device destroys after all children");
}

static void test_runtime_fence_and_command_states(void)
{
    AgcDevice device = create_device();
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFence fence = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcCommandBufferState state = AGC_COMMAND_BUFFER_STATE_PENDING;

    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "unsignaled fence creation succeeds");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_ERROR_BUSY,
        "unsignaled fence status is busy");
    TEST_ASSERT_EQ(agcWaitFence(fence, 0u), AGC_ERROR_TIMEOUT,
        "zero-duration finite fence wait times out");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1000u), AGC_ERROR_TIMEOUT,
        "positive finite fence wait times out");
    TEST_ASSERT_EQ(agcWaitFence(fence, AGC_RUNTIME_INFINITE_TIMEOUT),
        AGC_ERROR_INVALID_ARGUMENT,
        "infinite fence wait is rejected");

    command_desc.capacity_dwords = 4u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "small command buffer creation succeeds");
    TEST_ASSERT_EQ(agcGetCommandBufferState(command_buffer, &state), AGC_OK,
        "command state query succeeds");
    TEST_ASSERT_EQ(state, AGC_COMMAND_BUFFER_STATE_INITIAL,
        "new command buffer is initial");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_ERROR_INVALID_STATE,
        "end before begin is rejected");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "begin transitions to recording");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_ERROR_INVALID_STATE,
        "double begin is rejected");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_ERROR_BUSY,
        "recording command buffer cannot be destroyed");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_ERROR_INVALID_STATE,
        "empty command buffer cannot become executable");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "recording command buffer can recover through reset");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "reset command buffer can be destroyed");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "fence destruction succeeds");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "device destruction succeeds");
}

static void test_runtime_compute_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;
    uint32_t owner = UINT32_MAX;

    pipeline_desc.shader = shader;
    pipeline_desc.local_size_x = 64u;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 5u;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "compute pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "compute command buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "compute fence creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer begins");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 1u, 1u, 1u),
        AGC_ERROR_INVALID_STATE, "dispatch requires a bound pipeline");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command_buffer, pipeline), AGC_OK,
        "compute pipeline bind succeeds");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 3u, 2u, 1u), AGC_OK,
        "compute dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer becomes executable");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_ERROR_BUSY,
        "recorded compute pipeline cannot be destroyed");

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "compute command buffer submits");
    captured = agcDriverDebugLastAcbSubmit(&owner);
    TEST_ASSERT_EQ(captured->dword_count, 5u,
        "compute submission captures five dwords");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ((words[0] >> 8) & 0xffu, AGC_PM4_OP_DISPATCH_DIRECT,
        "compute submission records DISPATCH_DIRECT");
    TEST_ASSERT_EQ(words[0] & 1u, 1u,
        "compute dispatch carries shader-type bit");
    TEST_ASSERT_EQ(words[1], 3u, "compute dispatch records group count X");
    TEST_ASSERT_EQ(words[2], 2u, "compute dispatch records group count Y");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "successful compute submission signals fence");
    TEST_ASSERT_EQ(agcWaitFence(fence, 1u), AGC_OK,
        "finite wait observes signaled compute fence");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "completed compute fence resets");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "completed compute command buffer resets");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "compute fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "compute command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "compute pipeline destroys after reset");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK, "compute shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "compute queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "compute device destroys");
}

static void test_runtime_indexed_graphics_submission(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueGraphics);
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    const AgcCommandBufferSubmit *captured;
    const uint32_t *words;

    pipeline_desc.vertex_shader = vs;
    pipeline_desc.pixel_shader = ps;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    command_desc.capacity_dwords = 11u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "graphics pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "index buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "graphics command buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "graphics fence creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "graphics command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command_buffer, pipeline), AGC_OK,
        "graphics pipeline bind succeeds");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command_buffer, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "index buffer bind succeeds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command_buffer, 6u, 2u, 1u, 0, 0u),
        AGC_OK, "indexed draw records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "graphics command buffer becomes executable");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_ERROR_BUSY,
        "recorded index buffer cannot be destroyed");

    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "indexed graphics command buffer submits");
    captured = agcDriverDebugLastDcbSubmit();
    TEST_ASSERT_EQ(captured->dword_count, 11u,
        "indexed graphics submission captures eleven dwords");
    words = (const uint32_t *)(uintptr_t)captured->command_address;
    TEST_ASSERT_EQ((words[0] >> 8) & 0xffu, AGC_PM4_OP_SET_INDEX_SIZE,
        "indexed submission records SET_INDEX_SIZE");
    TEST_ASSERT_EQ((words[3] >> 8) & 0xffu, AGC_PM4_OP_NUM_INSTANCES,
        "indexed submission records NUM_INSTANCES");
    TEST_ASSERT_EQ(words[4], 2u, "indexed submission records instance count");
    TEST_ASSERT_EQ((words[5] >> 8) & 0xffu, AGC_PM4_OP_DRAW_INDEX_2,
        "indexed submission records DRAW_INDEX_2");
    TEST_ASSERT_EQ(words[9], 6u, "indexed submission records index count");
    TEST_ASSERT_EQ(agcGetFenceStatus(fence), AGC_OK,
        "successful graphics submission signals fence");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "completed graphics command buffer resets");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "index buffer destroys after command reset");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "graphics fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "graphics command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK, "pixel shader destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK, "vertex shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "graphics queue destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK, "graphics device destroys");
}

static void test_runtime_command_space_atomic_failure(void)
{
    AgcDevice device = create_device();
    AgcShader vs = create_shader(device, kAgcShaderStageVs);
    AgcShader ps = create_shader(device, kAgcShaderStagePs);
    AgcGraphicsPipelineDesc pipeline_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcGraphicsPipeline pipeline = NULL;
    AgcBuffer index_buffer = NULL;
    AgcCommandBuffer command_buffer = NULL;

    pipeline_desc.vertex_shader = vs;
    pipeline_desc.pixel_shader = ps;
    buffer_desc.size = 64u;
    buffer_desc.usage = AGC_BUFFER_USAGE_INDEX_BIT;
    command_desc.capacity_dwords = 10u;
    TEST_ASSERT_EQ(agcCreateGraphicsPipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "small-buffer graphics pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &index_buffer), AGC_OK,
        "small-buffer index buffer creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "ten-dword command buffer creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "ten-dword command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindGraphicsPipeline(command_buffer, pipeline), AGC_OK,
        "small-buffer graphics pipeline binds");
    TEST_ASSERT_EQ(agcCmdBindIndexBuffer(command_buffer, index_buffer, 0u,
        kAgcIndexSize16), AGC_OK, "small-buffer index buffer binds");
    TEST_ASSERT_EQ(agcCmdDrawIndexed(command_buffer, 3u, 1u, 0u, 0, 0u),
        AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
        "indexed draw reports stable command-space exhaustion");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_ERROR_INVALID_STATE,
        "failed draw leaves command buffer empty atomically");
    TEST_ASSERT_EQ(agcResetCommandBuffer(command_buffer), AGC_OK,
        "failed recording resets cleanly");

    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "small command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyBuffer(index_buffer), AGC_OK,
        "small-buffer index buffer destroys");
    TEST_ASSERT_EQ(agcDestroyGraphicsPipeline(pipeline), AGC_OK,
        "small-buffer graphics pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(ps), AGC_OK, "small-buffer PS destroys");
    TEST_ASSERT_EQ(agcDestroyShader(vs), AGC_OK, "small-buffer VS destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "small-buffer device destroys");
}

static void test_runtime_ps5_image_layouts(void)
{
    AgcDevice device = create_device();
    AgcImageDesc desc = AGC_IMAGE_DESC_INIT;
    AgcImageLayout layout = AGC_IMAGE_LAYOUT_INIT;
    AgcImageSubresourceLayout subresource = AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    AgcGfx1013LinearBcSurfaceLayoutInput bc_input;
    AgcGfx1013LinearBcSurfaceLayout bc_layout;
    AgcGfx1013LinearBcSubresourceLayout bc_subresource;

    desc.width = 17u;
    desc.height = 9u;
    desc.mip_levels = 5u;
    desc.array_layers = 6u;
    desc.format = AGC_FORMAT_BC7_UNORM;
    desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT |
        AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "BC cube-array layout succeeds");
    bc_input = (AgcGfx1013LinearBcSurfaceLayoutInput){
        desc.width, desc.height, desc.array_layers, desc.mip_levels,
        desc.format
    };
    TEST_ASSERT_EQ(agcGfx1013GetLinearBcSurfaceLayout(&bc_input, &bc_layout),
        AGC_OK, "qualified BC reference layout succeeds");
    TEST_ASSERT_EQ(layout.allocation_size, bc_layout.allocation_size,
        "runtime BC size matches qualified gfx1013 layout");
    TEST_ASSERT_EQ(layout.block_width, 4u, "BC layout exposes block width");
    TEST_ASSERT_EQ(layout.bytes_per_block, 16u,
        "BC7 layout exposes 16-byte blocks");
    TEST_ASSERT_EQ(layout.plane_count, 1u, "BC layout has one plane");
    TEST_ASSERT_EQ(layout.subresource_count, 30u,
        "cube faces participate in subresource count");
    TEST_ASSERT_EQ(layout.alignment, 256u,
        "linear BC layout retains qualified 256-byte alignment");
    TEST_ASSERT_EQ(agcGetImageSubresourceLayout(device, &desc, 4u, 5u, 0u,
        &subresource), AGC_OK, "last BC cube subresource is queryable");
    TEST_ASSERT_EQ(agcGfx1013GetLinearBcSubresourceLayout(&bc_input, 4u, 5u,
        &bc_subresource), AGC_OK, "qualified BC subresource layout succeeds");
    TEST_ASSERT_EQ(subresource.offset, bc_subresource.offset,
        "runtime BC subresource offset matches qualified layout");
    TEST_ASSERT_EQ(subresource.size, bc_subresource.size,
        "runtime BC subresource size matches qualified layout");
    TEST_ASSERT_EQ(subresource.width, 2u,
        "last BC mip uses qualified ceil-divide dimension");
    TEST_ASSERT_EQ(subresource.row_pitch, 256u,
        "last BC7 mip retains qualified transfer-row alignment");

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    desc.width = 1920u;
    desc.height = 1080u;
    desc.format = AGC_FORMAT_D32_FLOAT_S8_UINT;
    desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT | AGC_IMAGE_USAGE_HTILE_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "depth-stencil layout with HTILE succeeds");
    TEST_ASSERT_EQ(layout.plane_count, 3u,
        "depth, stencil, and HTILE are separate planes");
    TEST_ASSERT(layout.metadata_size != 0u, "HTILE metadata has storage");
    {
        AgcGfx1013DepthSurfaceLayoutInput depth_input = {
            desc.width, desc.height, desc.array_layers, desc.mip_levels,
            desc.sample_count, AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT,
            AGC_GFX1013_SWIZZLE_64KB_Z_X,
            AGC_GFX1013_SWIZZLE_64KB_Z_X
        };
        AgcGfx1013DepthSurfaceLayout depth_layout;
        AgcGfx1013HtileLayoutInput htile_input;
        AgcGfx1013HtileLayout htile_layout;
        AgcGfx1013HtileSubresourceLayout htile_subresource;
        TEST_ASSERT_EQ(agcGfx1013GetDepthSurfaceLayout(&depth_input,
            &depth_layout), AGC_OK, "qualified depth reference layout succeeds");
        htile_input = (AgcGfx1013HtileLayoutInput){
            desc.width, desc.height, desc.array_layers, desc.mip_levels,
            depth_layout.depth.first_mip_in_tail, 8u,
            AGC_GFX1013_SWIZZLE_64KB_Z_X
        };
        TEST_ASSERT_EQ(agcGfx1013GetHtileLayout(&htile_input, &htile_layout),
            AGC_OK, "qualified HTILE reference layout succeeds");
        TEST_ASSERT_EQ(layout.metadata_size, htile_layout.allocation_size,
            "runtime HTILE size matches qualified gfx1013 layout");
        subresource = (AgcImageSubresourceLayout)
            AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        TEST_ASSERT_EQ(agcGetImageSubresourceLayout(device, &desc, 0u, 0u,
            2u, &subresource), AGC_OK,
            "runtime HTILE subresource query succeeds");
        TEST_ASSERT_EQ(agcGfx1013GetHtileSubresourceLayout(&htile_input, 0u,
            0u, &htile_subresource), AGC_OK,
            "qualified HTILE subresource query succeeds");
        TEST_ASSERT_EQ(subresource.offset, layout.metadata_offset +
            htile_subresource.offset,
            "runtime HTILE offset matches qualified gfx1013 layout");
        TEST_ASSERT_EQ(subresource.size, htile_subresource.size,
            "runtime HTILE size matches qualified subresource layout");
    }

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    desc.width = 1280u;
    desc.height = 720u;
    desc.format = AGC_FORMAT_RGBA8_UNORM;
    desc.sample_count = 4u;
    desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "runtime 4x color layout succeeds");
    {
        AgcGfx1013ColorSurfaceLayoutInput color_input = {
            desc.width, desc.height, desc.array_layers, desc.mip_levels,
            desc.sample_count, AGC_GFX1013_RT_FORMAT_RGBA8_UNORM,
            AGC_GFX1013_SWIZZLE_64KB_R_X
        };
        AgcGfx1013ColorSurfaceLayout color_layout;
        TEST_ASSERT_EQ(agcGfx1013GetColorSurfaceLayout(&color_input,
            &color_layout), AGC_OK, "qualified 4x color layout succeeds");
        TEST_ASSERT_EQ(layout.allocation_size, color_layout.allocation_size,
            "runtime 4x color size matches qualified gfx1013 layout");
        TEST_ASSERT_EQ(layout.alignment, color_layout.alignment,
            "runtime 4x color alignment matches qualified layout");
    }

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    subresource = (AgcImageSubresourceLayout)
        AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    desc.width = 32u;
    desc.height = 16u;
    desc.depth = 8u;
    desc.mip_levels = 6u;
    desc.format = AGC_FORMAT_RGBA8_UNORM;
    desc.usage = AGC_IMAGE_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout), AGC_OK,
        "3D mip-chain layout succeeds");
    TEST_ASSERT_EQ(agcGetImageSubresourceLayout(device, &desc, 2u, 0u, 0u,
        &subresource), AGC_OK, "3D mip subresource query succeeds");
    TEST_ASSERT_EQ(subresource.depth, 2u,
        "3D mip subresource reduces depth dimension");
    TEST_ASSERT_EQ(subresource.size, subresource.slice_pitch * 2u,
        "3D mip size includes every depth slice");
    TEST_ASSERT_EQ(layout.allocation_size & 0xffffu, 0u,
        "linear 3D aggregate retains 64-KiB allocation padding");

    desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    layout = (AgcImageLayout)AGC_IMAGE_LAYOUT_INIT;
    desc.width = UINT32_MAX;
    desc.height = UINT32_MAX;
    desc.depth = UINT32_MAX;
    desc.format = AGC_FORMAT_RGBA8_UNORM;
    desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcGetImageLayout(device, &desc, &layout),
        AGC_ERROR_INVALID_ARGUMENT, "overflowing 3D layout fails closed");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "layout test device destruction succeeds");
}

static void test_runtime_all_backing_categories(void)
{
    AgcDevice device = create_device();
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcSamplerDesc sampler_desc = AGC_SAMPLER_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcImage image = NULL;
    AgcImage color_target = NULL;
    AgcImage depth_target = NULL;
    AgcImageView view = NULL;
    AgcSampler sampler = NULL;
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcCommandBuffer command_buffer = NULL;
    AgcAllocationInfo info = AGC_ALLOCATION_INFO_INIT;

    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "backing-category image creation succeeds");
    image_desc.width = 1280u;
    image_desc.height = 720u;
    image_desc.sample_count = 4u;
    image_desc.usage = AGC_IMAGE_USAGE_COLOR_TARGET_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &color_target), AGC_OK,
        "render-target backing creation succeeds");
    image_desc.width = 1024u;
    image_desc.height = 768u;
    image_desc.sample_count = 1u;
    image_desc.format = AGC_FORMAT_D32_FLOAT;
    image_desc.usage = AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT |
        AGC_IMAGE_USAGE_HTILE_BIT;
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &depth_target), AGC_OK,
        "depth/HTILE backing creation succeeds");
    view_desc.image = image;
    view_desc.format = AGC_FORMAT_RGBA8_UNORM;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view), AGC_OK,
        "GPU-visible image descriptor storage creation succeeds");
    TEST_ASSERT_EQ(agcCreateSampler(device, &sampler_desc, &sampler), AGC_OK,
        "GPU-visible sampler descriptor storage creation succeeds");
    command_desc.capacity_dwords = 64u;
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "command storage creation succeeds");

    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
        AGC_OBJECT_TYPE_IMAGE_VIEW, view, &info), AGC_OK,
        "image-view descriptor allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_FLEXIBLE,
        "image descriptor storage uses flexible memory");
    TEST_ASSERT_EQ(info.requested_size, sizeof(AgcGfx1013ImageDescriptor),
        "image descriptor storage has one hardware descriptor slot");
    TEST_ASSERT_EQ(info.gpu_address & 31u, 0u,
        "image descriptor storage retains 32-byte alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_SAMPLER,
        sampler, &info), AGC_OK, "sampler descriptor allocation is queryable");
    TEST_ASSERT_EQ(info.requested_size, sizeof(AgcSamplerDescriptor),
        "sampler descriptor storage has one hardware descriptor slot");
    TEST_ASSERT_EQ(info.gpu_address & 15u, 0u,
        "sampler descriptor storage retains 16-byte alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_SHADER,
        shader, &info), AGC_OK, "shader-code allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_FLEXIBLE,
        "shader code uses CPU-visible flexible memory");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
        AGC_OBJECT_TYPE_COMMAND_BUFFER, command_buffer, &info), AGC_OK,
        "command-storage allocation is queryable");
    TEST_ASSERT_EQ(info.requested_size, command_desc.capacity_dwords * 4u,
        "command allocation matches requested dword capacity");
    TEST_ASSERT_EQ(info.gpu_address & 255u, 0u,
        "command storage retains 256-byte alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        color_target, &info), AGC_OK,
        "render-target allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_GARLIC,
        "render target uses garlic memory");
    TEST_ASSERT_EQ(info.gpu_address & 0xffffu, 0u,
        "4x render target retains qualified 64-KiB alignment");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        depth_target, &info), AGC_OK,
        "depth/HTILE allocation is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_GARLIC,
        "depth/HTILE target uses garlic memory");

    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "backing-category command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK,
        "backing-category shader destroys");
    TEST_ASSERT_EQ(agcDestroySampler(sampler), AGC_OK,
        "backing-category sampler destroys");
    TEST_ASSERT_EQ(agcDestroyImageView(view), AGC_OK,
        "backing-category view destroys");
    TEST_ASSERT_EQ(agcDestroyImage(depth_target), AGC_OK,
        "backing-category depth target destroys");
    TEST_ASSERT_EQ(agcDestroyImage(color_target), AGC_OK,
        "backing-category color target destroys");
    TEST_ASSERT_EQ(agcDestroyImage(image), AGC_OK,
        "backing-category image destroys");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "backing-category device destroys");
}

static void test_runtime_heap_staging_and_stats(void)
{
    enum { BUFFER_COUNT = 128 };
    AgcDevice device = create_device();
    AgcBuffer buffers[BUFFER_COUNT] = {0};
    AgcImage images[16] = {0};
    AgcBufferDesc desc = AGC_BUFFER_DESC_INIT;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcMemoryStats stats = AGC_MEMORY_STATS_INIT;
    AgcAllocationInfo info = AGC_ALLOCATION_INFO_INIT;
    uint32_t upload_data[4] = {1u, 2u, 3u, 4u};
    uint32_t read_data[4] = {0u};
    uint64_t first_buffer_offset = 0u;
    uint64_t reload_buffer_offset = 0u;
    uint64_t reload_image_offset = 0u;
    uint32_t i;
    uint32_t cycle;

    desc.size = 4096u;
    desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    for (i = 0u; i < BUFFER_COUNT; ++i) {
        TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[i]), AGC_OK,
            "pooled garlic buffer creation succeeds");
    }
    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (i = 0u; i < 16u; ++i) {
        TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &images[i]), AGC_OK,
            "pooled garlic image creation succeeds");
    }
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "memory statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, BUFFER_COUNT + 16u,
        "statistics count live resource allocations");
    TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_GARLIC], 1u,
        "many small buffers share one garlic block");
    TEST_ASSERT_EQ(agcSetObjectDebugName(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], "streaming-vertices"), AGC_OK,
        "allocation debug name is accepted");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "buffer allocation info is queryable");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_GARLIC,
        "default GPU buffer resides in garlic");
    TEST_ASSERT(info.owner == buffers[0],
        "allocation info identifies its exact resource owner");
    first_buffer_offset = info.heap_offset;
    TEST_ASSERT(strcmp(info.debug_name, "streaming-vertices") == 0,
        "allocation info preserves debug name");
    for (i = 0u; i < BUFFER_COUNT; ++i)
        TEST_ASSERT_EQ(agcDestroyBuffer(buffers[i]), AGC_OK,
            "pooled garlic buffer destruction succeeds");
    for (i = 0u; i < 16u; ++i)
        TEST_ASSERT_EQ(agcDestroyImage(images[i]), AGC_OK,
            "pooled garlic image destruction succeeds");

    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "freed pooled range accepts a replacement buffer");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "replacement allocation info succeeds");
    TEST_ASSERT_EQ(info.heap_offset, first_buffer_offset,
        "replacement buffer reuses the first freed aligned range");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "replacement pooled buffer destroys");

    for (cycle = 0u; cycle < 8u; ++cycle) {
        for (i = 0u; i < 32u; ++i) {
            TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[i]), AGC_OK,
                "reload-set GPU buffer creation succeeds");
        }
        for (i = 0u; i < 8u; ++i) {
            TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &images[i]),
                AGC_OK, "reload-set GPU image creation succeeds");
        }
        desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
        TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[32]), AGC_OK,
            "reload-set upload staging creation succeeds");
        upload_data[0] = cycle;
        TEST_ASSERT_EQ(agcWriteBuffer(buffers[32], 0u, upload_data,
            sizeof(upload_data)), AGC_OK,
            "reload-set asset staging write succeeds");
        info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
        TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
            AGC_OBJECT_TYPE_BUFFER, buffers[0], &info), AGC_OK,
            "reload-set first buffer allocation is queryable");
        if (cycle == 0u)
            reload_buffer_offset = info.heap_offset;
        else
            TEST_ASSERT_EQ(info.heap_offset, reload_buffer_offset,
                "reloaded buffer asset reuses its prior range");
        info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
        TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
            AGC_OBJECT_TYPE_IMAGE, images[0], &info), AGC_OK,
            "reload-set first image allocation is queryable");
        if (cycle == 0u)
            reload_image_offset = info.heap_offset;
        else
            TEST_ASSERT_EQ(info.heap_offset, reload_image_offset,
                "reloaded image asset reuses its prior range");
        stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
        TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
            "reload-set live statistics query succeeds");
        TEST_ASSERT_EQ(stats.live_allocation_count, 41u,
            "reload set tracks all buffers, images, and staging");
        TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_GARLIC], 1u,
            "reload set remains inside one direct-memory block");
        TEST_ASSERT_EQ(stats.block_count[AGC_MEMORY_HEAP_FLEXIBLE], 1u,
            "reload staging remains inside one flexible-memory block");
        TEST_ASSERT_EQ(agcDestroyBuffer(buffers[32]), AGC_OK,
            "reload-set upload staging destroys");
        desc.flags = 0u;
        for (i = 0u; i < 8u; ++i)
            TEST_ASSERT_EQ(agcDestroyImage(images[i]), AGC_OK,
                "reload-set GPU image destruction succeeds");
        for (i = 0u; i < 32u; ++i)
            TEST_ASSERT_EQ(agcDestroyBuffer(buffers[i]), AGC_OK,
                "reload-set GPU buffer destruction succeeds");
        stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
        TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
            "post-reload statistics query succeeds");
        TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
            "each asset reload returns live allocations to baseline");
    }

    desc.flags = AGC_BUFFER_CREATE_DEDICATED_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "explicit dedicated buffer creation succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "dedicated allocation info succeeds");
    TEST_ASSERT_EQ(info.dedicated, 1u,
        "explicit dedicated buffer owns its block");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "dedicated block is released with its buffer");

    desc.size = UINT64_C(0x01100000);
    desc.flags = 0u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "oversized buffer creation succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "oversized allocation info succeeds");
    TEST_ASSERT_EQ(info.dedicated, 1u,
        "oversized buffer automatically receives a dedicated block");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "oversized dedicated buffer destroys");

    {
        AgcImageDesc scanout_desc = AGC_IMAGE_DESC_INIT;
        scanout_desc.width = 1920u;
        scanout_desc.height = 1080u;
        scanout_desc.format = AGC_FORMAT_RGBA8_UNORM;
        scanout_desc.usage = AGC_IMAGE_USAGE_SCANOUT_BIT;
        TEST_ASSERT_EQ(agcCreateImage(device, &scanout_desc, &images[0]),
            AGC_OK, "scanout-capable image creation succeeds");
        info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
        TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device,
            AGC_OBJECT_TYPE_IMAGE, images[0], &info), AGC_OK,
            "scanout allocation info succeeds");
        TEST_ASSERT_EQ(info.dedicated, 1u,
            "scanout-capable image receives a dedicated block");
        TEST_ASSERT_EQ(agcDestroyImage(images[0]), AGC_OK,
            "scanout dedicated image destroys");
    }

    desc.size = 4096u;
    desc.flags = AGC_BUFFER_CREATE_UPLOAD_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "persistently mapped upload buffer creation succeeds");
    TEST_ASSERT_EQ(agcWriteBuffer(buffers[0], 8u, upload_data,
        sizeof(upload_data)), AGC_OK, "bounded upload flush succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "upload allocation info succeeds");
    TEST_ASSERT_EQ(info.heap, AGC_MEMORY_HEAP_FLEXIBLE,
        "upload buffer resides in flexible memory");
    TEST_ASSERT(memcmp((uint8_t *)info.cpu_address + 8u, upload_data,
        sizeof(upload_data)) == 0, "upload writes persistent CPU mapping");
    TEST_ASSERT_EQ(agcWriteBuffer(buffers[0], desc.size - 8u, upload_data,
        sizeof(upload_data)), AGC_ERROR_INVALID_ARGUMENT,
        "upload rejects an overflowing range without mutation");
    TEST_ASSERT_EQ(agcReadBuffer(buffers[0], 0u, read_data,
        sizeof(read_data)), AGC_ERROR_INVALID_ARGUMENT,
        "upload-only staging rejects readback operations");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "upload buffer destroys");

    desc.flags = AGC_BUFFER_CREATE_READBACK_BIT;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]), AGC_OK,
        "persistently mapped readback buffer creation succeeds");
    info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffers[0], &info), AGC_OK, "readback allocation info succeeds");
    memcpy((uint8_t *)info.cpu_address + 16u, upload_data, sizeof(upload_data));
    TEST_ASSERT_EQ(agcWriteBuffer(buffers[0], 0u, upload_data,
        sizeof(upload_data)), AGC_ERROR_INVALID_ARGUMENT,
        "readback-only staging rejects upload operations");
    TEST_ASSERT_EQ(agcReadBuffer(buffers[0], 16u, read_data,
        sizeof(read_data)), AGC_OK, "bounded readback invalidate succeeds");
    TEST_ASSERT(memcmp(read_data, upload_data, sizeof(read_data)) == 0,
        "readback copies from persistent CPU mapping");
    TEST_ASSERT_EQ(agcReadBuffer(buffers[0], desc.size, read_data,
        sizeof(read_data)), AGC_ERROR_INVALID_ARGUMENT,
        "readback rejects an overflowing range without copying");
    TEST_ASSERT_EQ(agcDestroyBuffer(buffers[0]), AGC_OK,
        "readback buffer destroys");

    desc.size = UINT64_MAX;
    desc.flags = 0u;
    TEST_ASSERT_EQ(agcCreateBuffer(device, &desc, &buffers[0]),
        AGC_ERROR_INVALID_ARGUMENT,
        "buffer allocation alignment overflow fails before mutation");
    TEST_ASSERT(buffers[0] == NULL,
        "overflowing buffer creation leaves the output null");

    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "post-reload memory statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
        "repeated resource reloads return allocation count to baseline");
    TEST_ASSERT_EQ(stats.live_bytes, 0u,
        "repeated resource reloads return live bytes to baseline");
    TEST_ASSERT(stats.high_water_allocation_count >= BUFFER_COUNT,
        "statistics retain allocation high-water mark");
    TEST_ASSERT(stats.high_water_bytes != 0u,
        "statistics retain byte high-water mark");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "heap test device destruction succeeds");
}

static void test_runtime_fence_deferred_free(void)
{
    AgcDevice device = create_device();
    AgcQueue queue = create_queue(device, kAgcQueueCompute);
    AgcShader shader = create_shader(device, kAgcShaderStageCs);
    AgcComputePipelineDesc pipeline_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    AgcCommandBufferDesc command_desc = AGC_COMMAND_BUFFER_DESC_INIT;
    AgcFenceDesc fence_desc = AGC_FENCE_DESC_INIT;
    AgcBufferDesc buffer_desc = AGC_BUFFER_DESC_INIT;
    AgcComputePipeline pipeline = NULL;
    AgcCommandBuffer command_buffer = NULL;
    AgcFence fence = NULL;
    AgcBuffer buffer = NULL;
    AgcBuffer replacement = NULL;
    AgcBuffer recycled = NULL;
    AgcImage image = NULL;
    AgcImage image_replacement = NULL;
    AgcImage image_recycled = NULL;
    AgcImageDesc image_desc = AGC_IMAGE_DESC_INIT;
    AgcImageViewDesc view_desc = AGC_IMAGE_VIEW_DESC_INIT;
    AgcImageView view = NULL;
    AgcSubmitInfo submit = AGC_SUBMIT_INFO_INIT;
    AgcMemoryStats stats = AGC_MEMORY_STATS_INIT;
    AgcAllocationInfo retired_info = AGC_ALLOCATION_INFO_INIT;
    AgcAllocationInfo replacement_info = AGC_ALLOCATION_INFO_INIT;

    pipeline_desc.shader = shader;
    command_desc.queue_type = kAgcQueueCompute;
    command_desc.capacity_dwords = 32u;
    buffer_desc.size = 1024u;
    buffer_desc.usage = AGC_BUFFER_USAGE_STORAGE_BIT;
    TEST_ASSERT_EQ(agcCreateComputePipeline(device, &pipeline_desc, &pipeline),
        AGC_OK, "deferred-free compute pipeline creation succeeds");
    TEST_ASSERT_EQ(agcCreateCommandBuffer(device, &command_desc,
        &command_buffer), AGC_OK, "deferred-free command buffer creation succeeds");
    TEST_ASSERT_EQ(agcBeginCommandBuffer(command_buffer), AGC_OK,
        "deferred-free command buffer begins");
    TEST_ASSERT_EQ(agcCmdBindComputePipeline(command_buffer, pipeline), AGC_OK,
        "deferred-free compute pipeline binds");
    TEST_ASSERT_EQ(agcCmdDispatch(command_buffer, 1u, 1u, 1u), AGC_OK,
        "deferred-free dispatch records");
    TEST_ASSERT_EQ(agcEndCommandBuffer(command_buffer), AGC_OK,
        "deferred-free command buffer ends");
    TEST_ASSERT_EQ(agcCreateFence(device, &fence_desc, &fence), AGC_OK,
        "deferred-free fence creation succeeds");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &buffer), AGC_OK,
        "deferred resource creation succeeds");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        buffer, &retired_info), AGC_OK,
        "retiring buffer allocation is queryable");
    TEST_ASSERT_EQ(agcDestroyBufferDeferred(buffer, fence), AGC_OK,
        "resource retirement queues against unsignaled fence");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &replacement), AGC_OK,
        "replacement buffer creation succeeds before retirement fence");
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        replacement, &replacement_info), AGC_OK,
        "replacement buffer allocation is queryable");
    TEST_ASSERT_NE(replacement_info.heap_offset, retired_info.heap_offset,
        "pending buffer range is not returned to its heap early");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "collection before fence completion is a safe no-op");
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_ERROR_BUSY,
        "retirement owner prevents premature fence reset");
    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_ERROR_BUSY,
        "retirement owner prevents premature fence destruction");
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "deferred memory stats query succeeds");
    TEST_ASSERT_EQ(stats.deferred_free_count, 1u,
        "deferred resource remains resident before fence");
    submit.command_buffer_count = 1u;
    submit.command_buffers = &command_buffer;
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "submission signals deferred retirement fence");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "signaled deferred resources are collected");
    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "collected memory stats query succeeds");
    TEST_ASSERT_EQ(stats.deferred_free_count, 0u,
        "deferred queue returns to baseline after fence");
    TEST_ASSERT_EQ(agcDestroyBuffer(replacement), AGC_OK,
        "pre-fence replacement buffer destroys");
    TEST_ASSERT_EQ(agcCreateBuffer(device, &buffer_desc, &recycled), AGC_OK,
        "post-fence recycled buffer creation succeeds");
    replacement_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_BUFFER,
        recycled, &replacement_info), AGC_OK,
        "post-fence recycled allocation is queryable");
    TEST_ASSERT_EQ(replacement_info.heap_offset, retired_info.heap_offset,
        "signaled buffer range becomes reusable after collection");
    TEST_ASSERT_EQ(agcDestroyBuffer(recycled), AGC_OK,
        "post-fence recycled buffer destroys");

    image_desc.width = 64u;
    image_desc.height = 64u;
    image_desc.format = AGC_FORMAT_RGBA8_UNORM;
    image_desc.usage = AGC_IMAGE_USAGE_SAMPLED_BIT;
    TEST_ASSERT_EQ(agcResetFence(fence), AGC_OK,
        "deferred image fence resets after buffer collection");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image), AGC_OK,
        "deferred image creation succeeds");
    retired_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        image, &retired_info), AGC_OK,
        "retiring image allocation is queryable");
    TEST_ASSERT_EQ(agcDestroyImageDeferred(image, fence), AGC_OK,
        "image retirement queues against unsignaled fence");
    view_desc.image = image;
    view_desc.format = image_desc.format;
    TEST_ASSERT_EQ(agcCreateImageView(device, &view_desc, &view),
        AGC_ERROR_INVALID_ARGUMENT,
        "pending image cannot acquire a new descriptor owner");
    TEST_ASSERT(view == NULL,
        "rejected pending-image view leaves its output null");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image_replacement),
        AGC_OK, "replacement image creation succeeds before fence");
    replacement_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        image_replacement, &replacement_info), AGC_OK,
        "replacement image allocation is queryable");
    TEST_ASSERT_NE(replacement_info.heap_offset, retired_info.heap_offset,
        "pending image range is not returned to its heap early");
    TEST_ASSERT_EQ(agcQueueSubmit(queue, &submit, fence), AGC_OK,
        "second submission signals image retirement fence");
    TEST_ASSERT_EQ(agcCollectDeferredFrees(device), AGC_OK,
        "signaled deferred image is collected");
    TEST_ASSERT_EQ(agcDestroyImage(image_replacement), AGC_OK,
        "pre-fence replacement image destroys");
    TEST_ASSERT_EQ(agcCreateImage(device, &image_desc, &image_recycled), AGC_OK,
        "post-fence recycled image creation succeeds");
    replacement_info = (AgcAllocationInfo)AGC_ALLOCATION_INFO_INIT;
    TEST_ASSERT_EQ(agcGetObjectAllocationInfo(device, AGC_OBJECT_TYPE_IMAGE,
        image_recycled, &replacement_info), AGC_OK,
        "post-fence recycled image allocation is queryable");
    TEST_ASSERT_EQ(replacement_info.heap_offset, retired_info.heap_offset,
        "signaled image range becomes reusable after collection");
    TEST_ASSERT_EQ(agcDestroyImage(image_recycled), AGC_OK,
        "post-fence recycled image destroys");
    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "post-image-retirement statistics query succeeds");
    TEST_ASSERT_EQ(stats.deferred_free_count, 0u,
        "all deferred resources return to baseline after fences");

    TEST_ASSERT_EQ(agcDestroyFence(fence), AGC_OK, "deferred fence destroys");
    TEST_ASSERT_EQ(agcDestroyCommandBuffer(command_buffer), AGC_OK,
        "deferred command buffer destroys");
    TEST_ASSERT_EQ(agcDestroyComputePipeline(pipeline), AGC_OK,
        "deferred pipeline destroys");
    TEST_ASSERT_EQ(agcDestroyShader(shader), AGC_OK, "deferred shader destroys");
    TEST_ASSERT_EQ(agcDestroyQueue(queue), AGC_OK, "deferred queue destroys");
    stats = (AgcMemoryStats)AGC_MEMORY_STATS_INIT;
    TEST_ASSERT_EQ(agcGetMemoryStats(device, &stats), AGC_OK,
        "final deferred-free statistics query succeeds");
    TEST_ASSERT_EQ(stats.live_allocation_count, 0u,
        "all fence-complete allocation owners return to baseline");
    TEST_ASSERT_EQ(stats.live_bytes, 0u,
        "all fence-complete allocation bytes return to baseline");
    TEST_ASSERT_EQ(agcDestroyDevice(device), AGC_OK,
        "deferred-free device destruction succeeds");
}

void test_suite_runtime(void)
{
    TEST_SUITE("Firmware-neutral Native Runtime");
    TEST_RUN(test_runtime_descriptor_and_info_contract);
    TEST_RUN(test_runtime_allocation_callbacks);
    TEST_RUN(test_runtime_allocation_failure_rollback);
    TEST_RUN(test_runtime_all_object_lifecycle);
    TEST_RUN(test_runtime_fence_and_command_states);
    TEST_RUN(test_runtime_compute_submission);
    TEST_RUN(test_runtime_indexed_graphics_submission);
    TEST_RUN(test_runtime_command_space_atomic_failure);
    TEST_RUN(test_runtime_ps5_image_layouts);
    TEST_RUN(test_runtime_all_backing_categories);
    TEST_RUN(test_runtime_heap_staging_and_stats);
    TEST_RUN(test_runtime_fence_deferred_free);
}
