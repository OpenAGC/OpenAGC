/*
 * openagc native runtime contract tests.
 */

#include "test.h"

#include <stdint.h>

#include "agc_driver_debug.h"
#include "agc_pm4.h"
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
} AllocationProbe;

static void *PS5_SYSV_ABI probe_allocate(
    void *user_data, size_t size, size_t alignment)
{
    AllocationProbe *probe = user_data;
    (void)alignment;
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
    image_desc.format = 1u;
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

void test_suite_runtime(void)
{
    TEST_SUITE("Firmware-neutral Native Runtime");
    TEST_RUN(test_runtime_descriptor_and_info_contract);
    TEST_RUN(test_runtime_allocation_callbacks);
    TEST_RUN(test_runtime_all_object_lifecycle);
    TEST_RUN(test_runtime_fence_and_command_states);
    TEST_RUN(test_runtime_compute_submission);
    TEST_RUN(test_runtime_indexed_graphics_submission);
    TEST_RUN(test_runtime_command_space_atomic_failure);
}
