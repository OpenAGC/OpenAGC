/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Firmware-neutral native runtime object model.
 */

#include "openagc/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agc_cb.h"
#include "agc_driver_debug.h"
#include "agc_graphics.h"
#include "agc_memory.h"
#include "agc_pm4.h"
#include "agc_registers.h"
#include "agc_runtime_diag.h"
#include "agc_texture.h"
#include "agcdriver.h"

#define AGC_MAGIC_DEVICE UINT32_C(0x44475641)
#define AGC_MAGIC_QUEUE UINT32_C(0x51575641)
#define AGC_MAGIC_BUFFER UINT32_C(0x42555641)
#define AGC_MAGIC_IMAGE UINT32_C(0x494d5641)
#define AGC_MAGIC_IMAGE_VIEW UINT32_C(0x49565641)
#define AGC_MAGIC_SAMPLER UINT32_C(0x534d5641)
#define AGC_MAGIC_SHADER UINT32_C(0x53485641)
#define AGC_MAGIC_GRAPHICS_PIPELINE UINT32_C(0x47505641)
#define AGC_MAGIC_COMPUTE_PIPELINE UINT32_C(0x43505641)
#define AGC_MAGIC_COMMAND_BUFFER UINT32_C(0x43425641)
#define AGC_MAGIC_FENCE UINT32_C(0x464e5641)

#define AGC_FLEXIBLE_HEAP_BLOCK_SIZE UINT64_C(0x01000000)
#define AGC_GARLIC_HEAP_BLOCK_SIZE UINT64_C(0x02000000)
#define AGC_FLEXIBLE_BLOCK_ALIGNMENT UINT64_C(0x4000)
#define AGC_GARLIC_BLOCK_ALIGNMENT UINT64_C(0x200000)
#define AGC_FLEXIBLE_ALIGNMENT UINT64_C(0x100)
#define AGC_GARLIC_ALIGNMENT UINT64_C(0x10000)
#define AGC_RUNTIME_PIPELINE_BIND_MAX_DWORDS 2048u
#define AGC_RUNTIME_MAX_DESCRIPTOR_WRITES 256u
#define AGC_RUNTIME_MAX_RECORDED_RESOURCES 512u
#define AGC_RUNTIME_MAX_RESOURCE_ARENA_SIZE UINT64_C(0x100000)

typedef struct AgcRuntimeBlock AgcRuntimeBlock;
typedef struct AgcRuntimeAllocation AgcRuntimeAllocation;
typedef struct AgcDeferredFree AgcDeferredFree;

typedef struct AgcRuntimePipelineResourceLayout {
    uint64_t set_offsets[8];
    uint64_t set_sizes[8];
    uint64_t indirect_set_table_offset;
    uint64_t vertex_table_offset;
    uint64_t push_constant_offset;
    uint64_t total_size;
    uint32_t set_mask;
    uint32_t descriptor_element_count;
    uint32_t vertex_binding_mask;
    uint32_t push_constant_size;
    uint32_t uses_indirect_set_table;
} AgcRuntimePipelineResourceLayout;

struct AgcRuntimeAllocation {
    AgcRuntimeAllocation *next;
    AgcRuntimeBlock *block;
    uint64_t offset;
    uint64_t size;
    uint64_t requested_size;
    uint32_t owner_type;
    void *owner;
    char debug_name[AGC_RUNTIME_DEBUG_NAME_SIZE];
};

struct AgcRuntimeBlock {
    AgcRuntimeBlock *next;
    AgcRuntimeAllocation *allocations;
    AgcGpuMemory memory;
    uint32_t heap;
    uint32_t dedicated;
};

struct AgcDeferredFree {
    AgcDeferredFree *next;
    void *object;
    AgcFence fence;
    uint32_t type;
};

struct AgcDeviceImpl {
    uint32_t magic;
    uint32_t child_count;
    uint32_t graphics_queue_count;
    uint32_t compute_queue_count;
    AgcAllocationCallbacks allocation;
    AgcRuntimeInfo runtime_info;
    AgcRuntimeBlock *heaps[AGC_MEMORY_HEAP_COUNT];
    AgcDeferredFree *deferred;
    uint64_t live_allocation_count;
    uint64_t live_bytes;
    uint64_t high_water_allocation_count;
    uint64_t high_water_bytes;
    uint64_t deferred_free_count;
};

struct AgcQueueImpl {
    uint32_t magic;
    uint32_t pending_count;
    AgcDevice device;
    AgcQueueType type;
    int32_t backend_handle;
};

struct AgcBufferImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    uint64_t size;
    AgcBufferUsageFlags usage;
    uint32_t create_flags;
    void *storage;
    AgcRuntimeAllocation *allocation;
    uint32_t deferred;
};

struct AgcImageImpl {
    uint32_t magic;
    uint32_t dependency_refs;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcImageDesc desc;
    AgcImageLayout layout;
    AgcRuntimeAllocation *allocation;
    uint32_t deferred;
};

struct AgcImageViewImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcImage image;
    AgcImageViewDesc desc;
    AgcRuntimeAllocation *allocation;
};

struct AgcSamplerImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcSamplerDesc desc;
    AgcRuntimeAllocation *allocation;
};

struct AgcShaderImpl {
    uint32_t magic;
    uint32_t dependency_refs;
    AgcDevice device;
    AgcShaderStage stage;
    uint64_t code_size;
    void *code;
    AgcRuntimeAllocation *allocation;
    AgcShaderReflection reflection;
    AgcShaderRecord record;
    AgcShaderRecord front_record;
    uint64_t program_gpu_address;
    uint64_t front_program_gpu_address;
    uint64_t front_binary_offset;
    uint32_t has_reflection;
    uint32_t has_front_record;
};

struct AgcGraphicsPipelineImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcShader primitive_shader;
    AgcShader pixel_shader;
    uint32_t descriptor_mapping_count;
    uint32_t push_constant_range_count;
    uint32_t vertex_input_count;
    uint32_t color_attachment_count;
    AgcShaderDescriptorMapping
        descriptor_mappings[AGC_SHADER_MAX_DESCRIPTOR_BINDINGS];
    AgcShaderPushConstantRange
        push_constant_ranges[AGC_SHADER_MAX_PUSH_CONSTANT_RANGES];
    AgcShaderVertexInput vertex_inputs[AGC_SHADER_MAX_VERTEX_INPUTS];
    AgcColorBlendAttachmentState
        color_attachments[AGC_SHADER_MAX_COLOR_EXPORTS];
    AgcRasterizationState rasterization;
    AgcDepthStencilPipelineState depth_stencil;
    AgcMultisampleState multisample;
    AgcDynamicStateFlags dynamic_state_mask;
    AgcRuntimePipelineResourceLayout resource_layout;
    uint32_t bind_dword_count;
    uint32_t resource_layout_requires_bindings;
    uint32_t bind_words[AGC_RUNTIME_PIPELINE_BIND_MAX_DWORDS];
};

struct AgcComputePipelineImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcShader shader;
    uint32_t local_size[3];
    uint32_t descriptor_mapping_count;
    uint32_t push_constant_range_count;
    AgcShaderDescriptorMapping
        descriptor_mappings[AGC_SHADER_MAX_DESCRIPTOR_BINDINGS];
    AgcShaderPushConstantRange
        push_constant_ranges[AGC_SHADER_MAX_PUSH_CONSTANT_RANGES];
    AgcRuntimePipelineResourceLayout resource_layout;
    uint32_t resource_layout_requires_bindings;
};

struct AgcCommandBufferImpl {
    uint32_t magic;
    uint32_t pending_refs;
    AgcDevice device;
    AgcQueueType queue_type;
    AgcCommandBufferState state;
    uint32_t capacity_dwords;
    uint32_t *storage;
    AgcRuntimeAllocation *allocation;
    AgcRuntimeAllocation *resource_allocation;
    SceAgcCb cursor;
    AgcGraphicsPipeline graphics_pipeline;
    AgcComputePipeline compute_pipeline;
    AgcBuffer index_buffer;
    uint64_t index_offset;
    AgcIndexSize index_size;
    uint32_t descriptors_bound;
    uint32_t vertex_binding_mask;
    uint32_t dynamic_state_set_mask;
    uint32_t recorded_buffer_count;
    uint32_t recorded_view_count;
    uint32_t recorded_sampler_count;
    uint64_t push_constant_masks[kAgcShaderStageCount];
    AgcBuffer recorded_buffers[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcImageView recorded_views[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcSampler recorded_samplers[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
};

struct AgcFenceImpl {
    uint32_t magic;
    uint32_t pending_refs;
    AgcDevice device;
    uint32_t signaled;
};

static AgcDevice g_active_device;

static int agcReservedZero(const uint64_t *reserved, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (reserved[i] != 0u)
            return 0;
    }
    return 1;
}

static uint32_t agcRuntimeFloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int agcRuntimeFloatFinite(float value)
{
    return (agcRuntimeFloatBits(value) & UINT32_C(0x7f800000)) !=
        UINT32_C(0x7f800000);
}

static int agcHeaderValid(uint32_t struct_size, uint32_t expected_size,
    uint32_t version)
{
    return struct_size == expected_size &&
        version == AGC_RUNTIME_STRUCTURE_VERSION_1;
}

static int agcDeviceValid(AgcDevice device)
{
    return device != NULL && device == g_active_device &&
        device->magic == AGC_MAGIC_DEVICE;
}

static void *agcAlloc(AgcDevice device, size_t size, size_t alignment)
{
    void *memory;

    if (device && device->allocation.allocate) {
        memory = device->allocation.allocate(device->allocation.user_data,
            size, alignment);
    } else {
        memory = malloc(size);
    }
    if (memory)
        memset(memory, 0, size);
    return memory;
}

static void agcFree(AgcDevice device, void *memory)
{
    if (!memory)
        return;
    if (device && device->allocation.free)
        device->allocation.free(device->allocation.user_data, memory);
    else
        free(memory);
}

static int agcAddU64(uint64_t a, uint64_t b, uint64_t *result)
{
    if (UINT64_MAX - a < b)
        return 0;
    *result = a + b;
    return 1;
}

static int agcMulU64(uint64_t a, uint64_t b, uint64_t *result)
{
    if (a != 0u && b > UINT64_MAX / a)
        return 0;
    *result = a * b;
    return 1;
}

static int agcAlignU64(uint64_t value, uint64_t alignment, uint64_t *result)
{
    uint64_t mask;

    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        return 0;
    mask = alignment - 1u;
    if (value > UINT64_MAX - mask)
        return 0;
    *result = (value + mask) & ~mask;
    return 1;
}

static void *agcAllocationCpuAddress(const AgcRuntimeAllocation *allocation)
{
    return (uint8_t *)allocation->block->memory.cpu_address + allocation->offset;
}

static uint64_t agcAllocationGpuAddress(const AgcRuntimeAllocation *allocation)
{
    return allocation->block->memory.gpu_address + allocation->offset;
}

static int32_t agcFlushRuntimeAllocation(
    const AgcRuntimeAllocation *allocation, uint64_t offset, uint64_t size)
{
    uint64_t absolute_offset;

    if (!allocation || size == 0u || offset > allocation->requested_size ||
        size > allocation->requested_size - offset ||
        size > SIZE_MAX ||
        !agcAddU64(allocation->offset, offset, &absolute_offset) ||
        absolute_offset > SIZE_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    return agcGpuMemoryFlush(&allocation->block->memory,
        (size_t)absolute_offset, (size_t)size);
}

static int32_t agcCreateMemoryBlock(AgcDevice device, uint32_t heap,
    uint64_t size, uint64_t alignment, uint32_t dedicated,
    AgcRuntimeBlock **block_out)
{
    AgcRuntimeBlock *block;
    int32_t result;

    if (size > SIZE_MAX || alignment > SIZE_MAX)
        return AGC_ERROR_OUT_OF_MEMORY;
    block = agcAlloc(device, sizeof(*block), sizeof(void *));
    if (!block)
        return AGC_ERROR_OUT_OF_MEMORY;
    if (heap == AGC_MEMORY_HEAP_FLEXIBLE) {
        result = agcGpuMemoryAllocateFlexible(&block->memory, (size_t)size,
            (size_t)alignment, "openagc-runtime-flexible");
    } else {
        result = agcGpuMemoryAllocateDirectWriteCombined(&block->memory,
            (size_t)size, (size_t)alignment);
    }
    if (result != AGC_OK) {
        agcFree(device, block);
        return result;
    }
    block->heap = heap;
    block->dedicated = dedicated;
    block->next = device->heaps[heap];
    device->heaps[heap] = block;
    *block_out = block;
    return AGC_OK;
}

static int agcFindBlockOffset(AgcRuntimeBlock *block, uint64_t size,
    uint64_t alignment, uint64_t *offset_out)
{
    AgcRuntimeAllocation *allocation;
    uint64_t offset = 0u;
    uint64_t end;

    for (allocation = block->allocations; allocation; allocation = allocation->next) {
        if (!agcAlignU64(offset, alignment, &offset) ||
            !agcAddU64(offset, size, &end))
            return 0;
        if (end <= allocation->offset) {
            *offset_out = offset;
            return 1;
        }
        if (!agcAddU64(allocation->offset, allocation->size, &offset))
            return 0;
    }
    if (!agcAlignU64(offset, alignment, &offset) ||
        !agcAddU64(offset, size, &end) || end > block->memory.size)
        return 0;
    *offset_out = offset;
    return 1;
}

static void agcDestroyMemoryBlock(AgcDevice device, AgcRuntimeBlock *block)
{
    AgcRuntimeBlock **link = &device->heaps[block->heap];

    while (*link != block)
        link = &(*link)->next;
    *link = block->next;
    if (block->heap == AGC_MEMORY_HEAP_FLEXIBLE)
        agcGpuMemoryFreeFlexible(&block->memory);
    else
        agcGpuMemoryFreeDirect(&block->memory);
    agcFree(device, block);
}

static int32_t agcRuntimeAllocate(AgcDevice device, uint32_t heap,
    uint64_t requested_size, uint64_t alignment, uint32_t dedicated,
    uint32_t owner_type, void *owner,
    AgcRuntimeAllocation **allocation_out)
{
    AgcRuntimeBlock *block;
    AgcRuntimeAllocation *allocation;
    AgcRuntimeAllocation **link;
    uint64_t size;
    uint64_t offset = 0u;
    uint64_t default_size;
    uint64_t block_alignment;
    uint32_t created_block = 0u;
    int32_t result;

    if (!agcAlignU64(requested_size, alignment, &size))
        return AGC_ERROR_INVALID_ARGUMENT;
    default_size = heap == AGC_MEMORY_HEAP_FLEXIBLE ?
        AGC_FLEXIBLE_HEAP_BLOCK_SIZE : AGC_GARLIC_HEAP_BLOCK_SIZE;
    block_alignment = heap == AGC_MEMORY_HEAP_FLEXIBLE ?
        AGC_FLEXIBLE_BLOCK_ALIGNMENT : AGC_GARLIC_BLOCK_ALIGNMENT;
    if (heap == AGC_MEMORY_HEAP_FLEXIBLE &&
        alignment > AGC_FLEXIBLE_BLOCK_ALIGNMENT)
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (alignment > block_alignment) {
        dedicated = 1u;
        block_alignment = alignment;
    }
    if (size > default_size / 2u)
        dedicated = 1u;
    block = NULL;
    if (!dedicated) {
        for (block = device->heaps[heap]; block; block = block->next) {
            if (!block->dedicated &&
                agcFindBlockOffset(block, size, alignment, &offset))
                break;
        }
    }
    if (!block) {
        uint64_t block_size = dedicated ? size : default_size;
        result = agcCreateMemoryBlock(device, heap, block_size,
            block_alignment, dedicated, &block);
        if (result != AGC_OK)
            return result;
        created_block = 1u;
        if (!agcFindBlockOffset(block, size, alignment, &offset)) {
            agcDestroyMemoryBlock(device, block);
            return AGC_ERROR_OUT_OF_MEMORY;
        }
    }
    if (device->live_allocation_count == UINT64_MAX ||
        UINT64_MAX - device->live_bytes < size) {
        if (created_block)
            agcDestroyMemoryBlock(device, block);
        return AGC_ERROR_OUT_OF_MEMORY;
    }
    allocation = agcAlloc(device, sizeof(*allocation), sizeof(void *));
    if (!allocation) {
        if (created_block)
            agcDestroyMemoryBlock(device, block);
        return AGC_ERROR_OUT_OF_MEMORY;
    }
    allocation->block = block;
    allocation->offset = offset;
    allocation->size = size;
    allocation->requested_size = requested_size;
    allocation->owner_type = owner_type;
    allocation->owner = owner;
    link = &block->allocations;
    while (*link && (*link)->offset < offset)
        link = &(*link)->next;
    allocation->next = *link;
    *link = allocation;
    device->live_allocation_count++;
    device->live_bytes += size;
    if (device->live_allocation_count > device->high_water_allocation_count)
        device->high_water_allocation_count = device->live_allocation_count;
    if (device->live_bytes > device->high_water_bytes)
        device->high_water_bytes = device->live_bytes;
    *allocation_out = allocation;
    return AGC_OK;
}

static void agcRuntimeFree(AgcDevice device, AgcRuntimeAllocation *allocation)
{
    AgcRuntimeBlock *block = allocation->block;
    AgcRuntimeAllocation **link = &block->allocations;

    while (*link != allocation)
        link = &(*link)->next;
    *link = allocation->next;
    device->live_allocation_count--;
    device->live_bytes -= allocation->size;
    agcFree(device, allocation);
    if (block->dedicated && !block->allocations)
        agcDestroyMemoryBlock(device, block);
}

static void agcDestroyMemoryBlocks(AgcDevice device)
{
    uint32_t heap;
    for (heap = 0u; heap < AGC_MEMORY_HEAP_COUNT; ++heap) {
        AgcRuntimeBlock *block = device->heaps[heap];
        while (block) {
            AgcRuntimeBlock *next = block->next;
            if (heap == AGC_MEMORY_HEAP_FLEXIBLE)
                (void)agcGpuMemoryFreeFlexible(&block->memory);
            else
                (void)agcGpuMemoryFreeDirect(&block->memory);
            agcFree(device, block);
            block = next;
        }
        device->heaps[heap] = NULL;
    }
}

static void *agcCreateChild(AgcDevice device, size_t size)
{
    void *child;

    if (!agcDeviceValid(device))
        return NULL;
    child = agcAlloc(device, size, sizeof(void *));
    if (child)
        device->child_count++;
    return child;
}

static void agcDestroyChild(AgcDevice device, void *child)
{
    if (device->child_count > 0u)
        device->child_count--;
    agcFree(device, child);
}

static void agcPopulateRuntimeInfo(AgcDevice device, uint32_t agc_version)
{
    AgcRuntimeInfo *info = &device->runtime_info;
    uint32_t i;

    memset(info, 0, sizeof(*info));
    info->struct_size = sizeof(*info);
    info->version = AGC_RUNTIME_STRUCTURE_VERSION_1;
    info->runtime_api_version = AGC_RUNTIME_API_VERSION;
    info->agc_version = agc_version;
    info->capability_bits = AGC_RUNTIME_CAP_BASELINE;
    for (i = 0; i < AGC_RUNTIME_CAPABILITY_COUNT; ++i)
        info->qualification[i] = AGC_QUALIFICATION_HOST_TESTED;

#ifdef OPENAGC_PROSPERO
    {
        AgcDriverRuntimeDiagnostics diagnostics;
        if (agcDriverDebugRuntimeProfile(&diagnostics) == AGC_OK) {
            uint16_t key = (uint16_t)(diagnostics.firmware_version >> 16);
            uint8_t qualification = AGC_QUALIFICATION_PROFILE_QUALIFIED;
            info->firmware_version = diagnostics.firmware_version;
            info->firmware_abi_key = key;
            info->hardware_family = diagnostics.profile.is_trinity ?
                AGC_HARDWARE_FAMILY_TRINITY_PS5 :
                AGC_HARDWARE_FAMILY_STANDARD_PS5;
            if (!diagnostics.profile.is_trinity &&
                (key == 0x0550u || key == 0x1160u)) {
                qualification = AGC_QUALIFICATION_HARDWARE_QUALIFIED;
            }
            info->qualification[AGC_RUNTIME_CAP_GRAPHICS_INDEX] = qualification;
            info->qualification[AGC_RUNTIME_CAP_COMPUTE_INDEX] = qualification;
            info->qualification[AGC_RUNTIME_CAP_ASYNC_COMPUTE_QUEUE_INDEX] =
                qualification;
            (void)snprintf(info->profile_name, sizeof(info->profile_name),
                "%s-%s-%s", diagnostics.backend_name,
                agcProsperoAbiFamilyName(diagnostics.profile.family),
                diagnostics.profile.is_trinity ? "trinity" : "standard");
        }
    }
#else
    info->hardware_family = AGC_HARDWARE_FAMILY_HOST_TEST;
    (void)snprintf(info->profile_name, sizeof(info->profile_name),
        "%s", "generic-host");
#endif
}

int32_t PS5_SYSV_ABI agcCreateDevice(
    const AgcDeviceDesc *desc, AgcDevice *device_out)
{
    AgcAllocationCallbacks allocation = {0};
    AgcDevice device;
    int32_t result;

    if (!device_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *device_out = NULL;
    if (!desc || !agcHeaderValid(desc->struct_size, sizeof(*desc),
            desc->version) || desc->flags != 0u ||
        !agcReservedZero(desc->reserved, 4u) ||
        desc->agc_version > 12u ||
        (desc->required_capability_bits & ~AGC_RUNTIME_CAP_BASELINE) != 0u) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (g_active_device)
        return AGC_ERROR_BUSY;
    if (desc->allocation_callbacks) {
        if (!desc->allocation_callbacks->allocate ||
            !desc->allocation_callbacks->free) {
            return AGC_ERROR_INVALID_ARGUMENT;
        }
        allocation = *desc->allocation_callbacks;
        device = allocation.allocate(allocation.user_data, sizeof(*device),
            sizeof(void *));
        if (device)
            memset(device, 0, sizeof(*device));
    } else {
        device = calloc(1u, sizeof(*device));
    }
    if (!device)
        return AGC_ERROR_OUT_OF_MEMORY;
    device->allocation = allocation;
    device->magic = AGC_MAGIC_DEVICE;

    result = sceAgcInit(desc->agc_version);
    if (result == AGC_OK)
        result = sce_agc_initialize_internal_memory();
    if (result == AGC_OK)
        result = sceAgcDriverNotifyDefaultStates(0u);
    if (result != AGC_OK) {
        (void)agcDriverShutdown();
        device->magic = 0u;
        if (allocation.free)
            allocation.free(allocation.user_data, device);
        else
            free(device);
        return result;
    }

    g_active_device = device;
    agcPopulateRuntimeInfo(device, desc->agc_version);
    if ((desc->required_capability_bits & ~device->runtime_info.capability_bits) !=
            0u) {
        (void)agcDriverShutdown();
        g_active_device = NULL;
        device->magic = 0u;
        agcFree(device, device);
        return AGC_ERROR_NOT_SUPPORTED;
    }
    *device_out = device;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyDevice(AgcDevice device)
{
    AgcAllocationCallbacks allocation;
    int32_t result;

    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (device->child_count != 0u)
        return AGC_ERROR_BUSY;
    result = agcDriverShutdown();
    if (result != AGC_OK)
        return result;
    agcDestroyMemoryBlocks(device);
    allocation = device->allocation;
    device->magic = 0u;
    g_active_device = NULL;
    if (allocation.free)
        allocation.free(allocation.user_data, device);
    else
        free(device);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetRuntimeInfo(
    AgcDevice device, AgcRuntimeInfo *info)
{
    if (!agcDeviceValid(device) || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        !agcReservedZero(info->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    *info = device->runtime_info;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateQueue(
    AgcDevice device, const AgcQueueDesc *desc, AgcQueue *queue_out)
{
    AgcQueue queue;
    int32_t handle = -1;

    if (!queue_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *queue_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->priority != 0u || !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (desc->type == kAgcQueueGraphics) {
        if (device->graphics_queue_count != 0u)
            return AGC_ERROR_BUSY;
    } else if (desc->type == kAgcQueueCompute) {
        if (device->compute_queue_count != 0u)
            return AGC_ERROR_BUSY;
        handle = sceAgcDriverSetupAsyncGraphics(0u);
        if (handle != AGC_OK)
            return handle;
        handle = _sceAgcDriverCreateUserSpecialQueue();
        if (handle < 0)
            return handle;
    } else {
        return AGC_ERROR_NOT_SUPPORTED;
    }
    queue = agcCreateChild(device, sizeof(*queue));
    if (!queue) {
        if (desc->type == kAgcQueueCompute)
            (void)_sceAgcDriverDestroyUserSpecialQueue();
        return AGC_ERROR_OUT_OF_MEMORY;
    }
    queue->magic = AGC_MAGIC_QUEUE;
    queue->device = device;
    queue->type = desc->type;
    queue->backend_handle = handle;
    if (desc->type == kAgcQueueGraphics)
        device->graphics_queue_count++;
    else
        device->compute_queue_count++;
    *queue_out = queue;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyQueue(AgcQueue queue)
{
    AgcDevice device;
    int32_t result;

    if (!queue || queue->magic != AGC_MAGIC_QUEUE ||
        !agcDeviceValid(queue->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (queue->pending_count != 0u)
        return AGC_ERROR_BUSY;
    device = queue->device;
    if (queue->type == kAgcQueueCompute) {
        result = _sceAgcDriverDestroyUserSpecialQueue();
        if (result != AGC_OK)
            return result;
        device->compute_queue_count--;
    } else {
        device->graphics_queue_count--;
    }
    queue->magic = 0u;
    agcDestroyChild(device, queue);
    return AGC_OK;
}

typedef struct AgcRuntimeFormatInfo {
    uint32_t block_width;
    uint32_t block_height;
    uint32_t bytes[2];
    uint32_t plane_count;
    uint32_t depth_stencil;
} AgcRuntimeFormatInfo;

static int agcGetRuntimeFormatInfo(uint32_t format, AgcRuntimeFormatInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->block_width = 1u;
    info->block_height = 1u;
    info->plane_count = 1u;
    switch (format) {
    case AGC_FORMAT_RGBA8_UNORM:
        info->bytes[0] = 4u;
        return 1;
    case AGC_FORMAT_BC1_UNORM:
    case AGC_FORMAT_BC1_SRGB:
    case AGC_FORMAT_BC4_UNORM:
    case AGC_FORMAT_BC4_SNORM:
        info->block_width = 4u;
        info->block_height = 4u;
        info->bytes[0] = 8u;
        return 1;
    case AGC_FORMAT_BC2_UNORM:
    case AGC_FORMAT_BC2_SRGB:
    case AGC_FORMAT_BC3_UNORM:
    case AGC_FORMAT_BC3_SRGB:
    case AGC_FORMAT_BC5_UNORM:
    case AGC_FORMAT_BC5_SNORM:
    case AGC_FORMAT_BC6_UFLOAT:
    case AGC_FORMAT_BC6_SFLOAT:
    case AGC_FORMAT_BC7_UNORM:
    case AGC_FORMAT_BC7_SRGB:
        info->block_width = 4u;
        info->block_height = 4u;
        info->bytes[0] = 16u;
        return 1;
    case AGC_FORMAT_D16_UNORM:
        info->bytes[0] = 2u;
        info->depth_stencil = 1u;
        return 1;
    case AGC_FORMAT_D32_FLOAT:
        info->bytes[0] = 4u;
        info->depth_stencil = 1u;
        return 1;
    case AGC_FORMAT_S8_UINT:
        info->bytes[0] = 1u;
        info->depth_stencil = 1u;
        return 1;
    case AGC_FORMAT_D16_UNORM_S8_UINT:
        info->bytes[0] = 2u;
        info->bytes[1] = 1u;
        info->plane_count = 2u;
        info->depth_stencil = 1u;
        return 1;
    case AGC_FORMAT_D32_FLOAT_S8_UINT:
        info->bytes[0] = 4u;
        info->bytes[1] = 1u;
        info->plane_count = 2u;
        info->depth_stencil = 1u;
        return 1;
    default:
        return 0;
    }
}

static int agcImageDescBasicValid(const AgcImageDesc *desc)
{
    AgcRuntimeFormatInfo format;
    uint32_t max_dimension;
    uint32_t max_mips = 1u;
    const uint32_t known_usage = AGC_IMAGE_USAGE_SAMPLED_BIT |
        AGC_IMAGE_USAGE_STORAGE_BIT | AGC_IMAGE_USAGE_COLOR_TARGET_BIT |
        AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT | AGC_IMAGE_USAGE_TRANSFER_SRC_BIT |
        AGC_IMAGE_USAGE_TRANSFER_DST_BIT | AGC_IMAGE_USAGE_SCANOUT_BIT |
        AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT | AGC_IMAGE_USAGE_HTILE_BIT;

    if (!desc)
        return 0;
    if (!agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->width == 0u || desc->height == 0u || desc->depth == 0u ||
        desc->width > 0x4000u || desc->height > 0x4000u ||
        desc->depth > 0x2000u ||
        desc->mip_levels == 0u || desc->array_layers == 0u ||
        desc->mip_levels > 15u || desc->array_layers > 0x2000u ||
        (desc->sample_count != 1u && desc->sample_count != 4u) ||
        desc->usage == 0u || (desc->usage & ~known_usage) != 0u ||
        !agcReservedZero(desc->reserved, 4u) ||
        !agcGetRuntimeFormatInfo(desc->format, &format))
        return 0;
    if ((desc->sample_count > 1u && (desc->mip_levels > 1u ||
            desc->depth > 1u)) ||
        (format.block_width > 1u &&
            (desc->sample_count != 1u || desc->depth != 1u)) ||
        (format.block_width > 1u &&
            (desc->usage & (AGC_IMAGE_USAGE_STORAGE_BIT |
                AGC_IMAGE_USAGE_COLOR_TARGET_BIT)) != 0u) ||
        (format.depth_stencil && desc->depth != 1u) ||
        (desc->sample_count > 1u &&
            ((desc->usage & AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT) != 0u ||
             desc->array_layers != 1u)) ||
        ((desc->usage & AGC_IMAGE_USAGE_COLOR_TARGET_BIT) != 0u &&
            format.depth_stencil) ||
        ((desc->usage & AGC_IMAGE_USAGE_SCANOUT_BIT) != 0u &&
            (desc->format != AGC_FORMAT_RGBA8_UNORM || desc->depth != 1u ||
             desc->mip_levels != 1u || desc->array_layers != 1u ||
             desc->sample_count != 1u)))
        return 0;
    if (desc->depth > 1u && desc->array_layers != 1u)
        return 0;
    if ((desc->usage & AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT) != 0u &&
        (desc->depth != 1u || desc->array_layers % 6u != 0u))
        return 0;
    if (((desc->usage & AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) != 0u) !=
        (format.depth_stencil != 0u))
        return 0;
    if ((desc->usage & AGC_IMAGE_USAGE_HTILE_BIT) != 0u &&
        !format.depth_stencil)
        return 0;
    max_dimension = desc->width;
    if (desc->height > max_dimension)
        max_dimension = desc->height;
    if (desc->depth > max_dimension)
        max_dimension = desc->depth;
    while (max_dimension > 1u) {
        max_dimension >>= 1u;
        max_mips++;
    }
    return desc->mip_levels <= max_mips;
}

static int32_t agcComputeLinearSubresource(const AgcImageDesc *desc,
    uint32_t target_mip, uint32_t target_layer, uint32_t target_plane,
    AgcImageSubresourceLayout *target, AgcImageLayout *aggregate)
{
    AgcRuntimeFormatInfo format;
    uint32_t data_planes;
    uint32_t total_planes;
    uint32_t plane;
    uint32_t layer;
    uint32_t mip;
    uint64_t cursor = 0u;

    if (!desc || !agcImageDescBasicValid(desc) ||
        !agcGetRuntimeFormatInfo(desc->format, &format))
        return AGC_ERROR_INVALID_ARGUMENT;
    data_planes = format.plane_count;
    total_planes = data_planes +
        (((desc->usage & AGC_IMAGE_USAGE_HTILE_BIT) != 0u) ? 1u : 0u);
    if (target && (target_mip >= desc->mip_levels ||
        target_layer >= desc->array_layers || target_plane >= total_planes))
        return AGC_ERROR_INVALID_ARGUMENT;
    for (plane = 0u; plane < total_planes; ++plane) {
        for (layer = 0u; layer < desc->array_layers; ++layer) {
            for (mip = 0u; mip < desc->mip_levels; ++mip) {
                uint64_t width = desc->width >> mip;
                uint64_t height = desc->height >> mip;
                uint64_t depth = desc->depth >> mip;
                uint64_t width_blocks;
                uint64_t height_blocks;
                uint64_t row_pitch;
                uint64_t slice_pitch;
                uint64_t size;
                uint32_t block_width = format.block_width;
                uint32_t block_height = format.block_height;
                uint32_t bytes = plane < data_planes ? format.bytes[plane] : 4u;

                if (width == 0u) width = 1u;
                if (height == 0u) height = 1u;
                if (depth == 0u) depth = 1u;
                if (plane >= data_planes) {
                    block_width = 8u;
                    block_height = 8u;
                }
                width_blocks = (width + block_width - 1u) / block_width;
                height_blocks = (height + block_height - 1u) / block_height;
                if (!agcMulU64(width_blocks, bytes, &row_pitch) ||
                    !agcAlignU64(row_pitch, 256u, &row_pitch) ||
                    !agcMulU64(row_pitch, height_blocks, &slice_pitch) ||
                    !agcMulU64(slice_pitch, depth, &size) ||
                    !agcMulU64(size, desc->sample_count, &size) ||
                    !agcAlignU64(cursor, 512u, &cursor))
                    return AGC_ERROR_INVALID_ARGUMENT;
                if (target && mip == target_mip && layer == target_layer &&
                    plane == target_plane) {
                    target->mip_level = mip;
                    target->array_layer = layer;
                    target->plane = plane;
                    target->width = (uint32_t)width;
                    target->height = (uint32_t)height;
                    target->depth = (uint32_t)depth;
                    target->offset = cursor;
                    target->size = size;
                    target->row_pitch = row_pitch;
                    target->slice_pitch = slice_pitch;
                }
                if (aggregate && plane == data_planes &&
                    aggregate->metadata_size == 0u)
                    aggregate->metadata_offset = cursor;
                if (!agcAddU64(cursor, size, &cursor))
                    return AGC_ERROR_INVALID_ARGUMENT;
            }
        }
    }
    if (!agcAlignU64(cursor, AGC_GARLIC_ALIGNMENT, &cursor))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (aggregate) {
        uint64_t subresources;
        if (!agcMulU64(desc->mip_levels, desc->array_layers, &subresources) ||
            !agcMulU64(subresources, total_planes, &subresources) ||
            subresources > UINT32_MAX)
            return AGC_ERROR_INVALID_ARGUMENT;
        aggregate->allocation_size = cursor;
        aggregate->alignment = AGC_GARLIC_ALIGNMENT;
        aggregate->plane_count = total_planes;
        aggregate->subresource_count = (uint32_t)subresources;
        aggregate->block_width = format.block_width;
        aggregate->block_height = format.block_height;
        aggregate->bytes_per_block = format.bytes[0];
        aggregate->first_mip_in_tail = desc->mip_levels;
        if (total_planes > data_planes)
            aggregate->metadata_size = cursor - aggregate->metadata_offset;
    }
    return AGC_OK;
}

static int agcRuntimeDepthFormat(uint32_t format,
    AgcGfx1013DepthSurfaceFormat *depth_format)
{
    switch (format) {
    case AGC_FORMAT_D16_UNORM:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM;
        return 1;
    case AGC_FORMAT_D32_FLOAT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT;
        return 1;
    case AGC_FORMAT_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_S8_UINT;
        return 1;
    case AGC_FORMAT_D16_UNORM_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM_S8_UINT;
        return 1;
    case AGC_FORMAT_D32_FLOAT_S8_UINT:
        *depth_format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT;
        return 1;
    default:
        return 0;
    }
}

static int agcRuntimeIsBcFormat(uint32_t format)
{
    AgcRuntimeFormatInfo info;
    return agcGetRuntimeFormatInfo(format, &info) && info.block_width == 4u;
}

static int32_t agcComputeBcLayout(const AgcImageDesc *desc,
    uint32_t target_mip, uint32_t target_layer,
    AgcImageSubresourceLayout *target, AgcImageLayout *aggregate)
{
    AgcGfx1013LinearBcSurfaceLayoutInput input;
    int32_t result;

    if (!desc || !agcImageDescBasicValid(desc))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (desc->depth != 1u || desc->sample_count != 1u)
        return AGC_ERROR_INVALID_ARGUMENT;
    input.width = desc->width;
    input.height = desc->height;
    input.layer_count = desc->array_layers;
    input.mip_level_count = desc->mip_levels;
    input.resource_format = desc->format;
    if (aggregate) {
        AgcGfx1013LinearBcSurfaceLayout low = {0};
        uint64_t subresources;
        result = agcGfx1013GetLinearBcSurfaceLayout(&input, &low);
        if (result != AGC_OK)
            return result;
        if (!agcMulU64(desc->mip_levels, desc->array_layers, &subresources) ||
            subresources > UINT32_MAX)
            return AGC_ERROR_INVALID_ARGUMENT;
        aggregate->allocation_size = low.allocation_size;
        aggregate->alignment = low.alignment;
        aggregate->plane_count = 1u;
        aggregate->subresource_count = (uint32_t)subresources;
        aggregate->block_width = low.block_width;
        aggregate->block_height = low.block_height;
        aggregate->bytes_per_block = low.bytes_per_block;
        aggregate->first_mip_in_tail = desc->mip_levels;
    }
    if (target) {
        AgcGfx1013LinearBcSubresourceLayout low = {0};
        result = agcGfx1013GetLinearBcSubresourceLayout(&input, target_mip,
            target_layer, &low);
        if (result != AGC_OK)
            return result;
        target->mip_level = target_mip;
        target->array_layer = target_layer;
        target->plane = 0u;
        target->width = low.width;
        target->height = low.height;
        target->depth = 1u;
        target->offset = low.offset;
        target->size = low.size;
        target->row_pitch = low.row_pitch;
        target->slice_pitch = low.size;
    }
    return AGC_OK;
}

static int32_t agcGetDepthLayouts(AgcDevice device, const AgcImageDesc *desc,
    AgcGfx1013DepthSurfaceLayout *depth, AgcGfx1013HtileLayout *htile,
    uint64_t *stencil_offset, uint64_t *metadata_offset)
{
    AgcGfx1013DepthSurfaceFormat format;
    AgcGfx1013DepthSurfaceLayoutInput input;
    uint64_t cursor = 0u;
    int32_t result;

    if (!agcRuntimeDepthFormat(desc->format, &format) || desc->depth != 1u)
        return AGC_ERROR_INVALID_ARGUMENT;
    input.width = desc->width;
    input.height = desc->height;
    input.layer_count = desc->array_layers;
    input.mip_level_count = desc->mip_levels;
    input.sample_count = desc->sample_count;
    input.format = format;
    input.depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X;
    input.stencil_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X;
    result = agcGfx1013GetDepthSurfaceLayout(&input, depth);
    if (result != AGC_OK)
        return result;
    if (depth->depth.allocation_size != 0u) {
        if (!agcAddU64(cursor, depth->depth.allocation_size, &cursor))
            return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (depth->stencil.allocation_size != 0u) {
        if (!agcAlignU64(cursor, depth->stencil.alignment, &cursor))
            return AGC_ERROR_INVALID_ARGUMENT;
        *stencil_offset = cursor;
        if (!agcAddU64(cursor, depth->stencil.allocation_size, &cursor))
            return AGC_ERROR_INVALID_ARGUMENT;
    }
    if ((desc->usage & AGC_IMAGE_USAGE_HTILE_BIT) != 0u) {
        AgcGfx1013HtileLayoutInput htile_input;
        uint32_t first_mip = depth->depth.allocation_size != 0u ?
            depth->depth.first_mip_in_tail : depth->stencil.first_mip_in_tail;

        if (device->runtime_info.hardware_family ==
                AGC_HARDWARE_FAMILY_TRINITY_PS5 ||
            (device->runtime_info.firmware_abi_key != 0u &&
             device->runtime_info.firmware_abi_key != 0x0550u &&
             device->runtime_info.firmware_abi_key != 0x1160u))
            return AGC_ERROR_NOT_SUPPORTED;
        htile_input.width = desc->width;
        htile_input.height = desc->height;
        htile_input.layer_count = desc->array_layers;
        htile_input.mip_level_count = desc->mip_levels;
        htile_input.first_mip_in_tail = first_mip;
        htile_input.pipe_count = 8u;
        htile_input.swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X;
        result = agcGfx1013GetHtileLayout(&htile_input, htile);
        if (result != AGC_OK)
            return result;
        if (!agcAlignU64(cursor, htile->alignment, &cursor))
            return AGC_ERROR_INVALID_ARGUMENT;
        *metadata_offset = cursor;
    }
    return AGC_OK;
}

static int32_t agcComputeDepthLayout(AgcDevice device,
    const AgcImageDesc *desc, uint32_t target_mip, uint32_t target_layer,
    uint32_t target_plane, AgcImageSubresourceLayout *target,
    AgcImageLayout *aggregate)
{
    AgcGfx1013DepthSurfaceLayout depth = {0};
    AgcGfx1013HtileLayout htile = {0};
    const AgcGfx1013DepthPlaneLayout *planes[2];
    uint32_t bytes[2];
    uint32_t data_plane_count = 0u;
    uint32_t total_plane_count;
    uint64_t plane_offsets[2] = {0u, 0u};
    uint64_t metadata_offset = 0u;
    uint64_t total_size = 0u;
    uint64_t subresources;
    int32_t result;

    if (!desc || !agcImageDescBasicValid(desc))
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGetDepthLayouts(device, desc, &depth, &htile,
        &plane_offsets[1], &metadata_offset);
    if (result != AGC_OK)
        return result;
    if (depth.depth.allocation_size != 0u) {
        planes[data_plane_count] = &depth.depth;
        bytes[data_plane_count++] = desc->format == AGC_FORMAT_D16_UNORM ||
            desc->format == AGC_FORMAT_D16_UNORM_S8_UINT ? 2u : 4u;
    }
    if (depth.stencil.allocation_size != 0u) {
        if (data_plane_count == 0u)
            plane_offsets[0] = plane_offsets[1];
        planes[data_plane_count] = &depth.stencil;
        bytes[data_plane_count++] = 1u;
    }
    if (data_plane_count == 0u)
        return AGC_ERROR_INTERNAL;
    total_plane_count = data_plane_count + (htile.allocation_size != 0u);
    if (!agcMulU64(desc->mip_levels, desc->array_layers, &subresources) ||
        !agcMulU64(subresources, total_plane_count, &subresources) ||
        subresources > UINT32_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (htile.allocation_size != 0u) {
        if (!agcAddU64(metadata_offset, htile.allocation_size, &total_size))
            return AGC_ERROR_INVALID_ARGUMENT;
    } else {
        const AgcGfx1013DepthPlaneLayout *last = planes[data_plane_count - 1u];
        if (!agcAddU64(plane_offsets[data_plane_count - 1u],
                last->allocation_size, &total_size))
            return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (!agcAlignU64(total_size, AGC_GARLIC_ALIGNMENT, &total_size))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (aggregate) {
        aggregate->allocation_size = total_size;
        aggregate->alignment = AGC_GARLIC_ALIGNMENT;
        aggregate->plane_count = total_plane_count;
        aggregate->subresource_count = (uint32_t)subresources;
        aggregate->block_width = planes[0]->block_width;
        aggregate->block_height = planes[0]->block_height;
        aggregate->bytes_per_block = bytes[0];
        aggregate->first_mip_in_tail = planes[0]->first_mip_in_tail;
        aggregate->metadata_offset = metadata_offset;
        aggregate->metadata_size = htile.allocation_size;
    }
    if (!target)
        return AGC_OK;
    if (target_mip >= desc->mip_levels || target_layer >= desc->array_layers ||
        target_plane >= total_plane_count)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (target_plane < data_plane_count) {
        const AgcGfx1013DepthPlaneLayout *plane = planes[target_plane];
        uint64_t row_pitch;
        if (desc->mip_levels != 1u)
            return AGC_ERROR_NOT_SUPPORTED;
        if (!agcMulU64(plane->pitch, bytes[target_plane], &row_pitch) ||
            !agcMulU64(row_pitch, desc->sample_count, &row_pitch))
            return AGC_ERROR_INVALID_ARGUMENT;
        target->offset = plane_offsets[target_plane] +
            plane->slice_size * target_layer;
        target->size = plane->slice_size;
        target->row_pitch = row_pitch;
        target->slice_pitch = plane->slice_size;
    } else {
        AgcGfx1013HtileLayoutInput input;
        AgcGfx1013HtileSubresourceLayout low = {0};
        input.width = desc->width;
        input.height = desc->height;
        input.layer_count = desc->array_layers;
        input.mip_level_count = desc->mip_levels;
        input.first_mip_in_tail = planes[0]->first_mip_in_tail;
        input.pipe_count = 8u;
        input.swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X;
        result = agcGfx1013GetHtileSubresourceLayout(&input, target_mip,
            target_layer, &low);
        if (result != AGC_OK)
            return result;
        target->offset = metadata_offset + low.offset;
        target->size = low.size;
        target->slice_pitch = low.size;
    }
    target->mip_level = target_mip;
    target->array_layer = target_layer;
    target->plane = target_plane;
    target->width = desc->width >> target_mip;
    target->height = desc->height >> target_mip;
    if (target->width == 0u) target->width = 1u;
    if (target->height == 0u) target->height = 1u;
    target->depth = 1u;
    return AGC_OK;
}

static int32_t agcComputeMsaaColorLayout(const AgcImageDesc *desc,
    uint32_t target_layer, AgcImageSubresourceLayout *target,
    AgcImageLayout *aggregate)
{
    AgcGfx1013ColorSurfaceLayoutInput input;
    AgcGfx1013ColorSurfaceLayout low = {0};
    int32_t result;

    if (!desc || !agcImageDescBasicValid(desc) ||
        desc->format != AGC_FORMAT_RGBA8_UNORM || desc->depth != 1u ||
        desc->mip_levels != 1u || desc->sample_count != 4u)
        return AGC_ERROR_INVALID_ARGUMENT;
    input.width = desc->width;
    input.height = desc->height;
    input.layer_count = desc->array_layers;
    input.mip_level_count = 1u;
    input.sample_count = 4u;
    input.format = AGC_GFX1013_RT_FORMAT_RGBA8_UNORM;
    input.swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_R_X;
    result = agcGfx1013GetColorSurfaceLayout(&input, &low);
    if (result != AGC_OK)
        return result;
    if (aggregate) {
        aggregate->allocation_size = low.allocation_size;
        aggregate->alignment = low.alignment;
        aggregate->plane_count = 1u;
        aggregate->subresource_count = desc->array_layers;
        aggregate->block_width = low.block_width;
        aggregate->block_height = low.block_height;
        aggregate->bytes_per_block = 4u;
        aggregate->first_mip_in_tail = 1u;
    }
    if (target) {
        if (target_layer >= desc->array_layers)
            return AGC_ERROR_INVALID_ARGUMENT;
        target->mip_level = 0u;
        target->array_layer = target_layer;
        target->plane = 0u;
        target->width = desc->width;
        target->height = desc->height;
        target->depth = 1u;
        target->offset = low.slice_size * target_layer;
        target->size = low.slice_size;
        target->row_pitch = (uint64_t)low.pitch * 4u * desc->sample_count;
        target->slice_pitch = low.slice_size;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetImageLayout(
    AgcDevice device, const AgcImageDesc *desc, AgcImageLayout *layout)
{
    if (!agcDeviceValid(device) || !layout ||
        !agcHeaderValid(layout->struct_size, sizeof(*layout),
        layout->version) || !agcReservedZero(layout->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    memset((uint8_t *)layout + offsetof(AgcImageLayout, allocation_size), 0,
        sizeof(*layout) - offsetof(AgcImageLayout, allocation_size));
    if (agcRuntimeIsBcFormat(desc ? desc->format : AGC_FORMAT_UNDEFINED))
        return agcComputeBcLayout(desc, 0u, 0u, NULL, layout);
    if (desc && (desc->usage & AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) != 0u)
        return agcComputeDepthLayout(device, desc, 0u, 0u, 0u, NULL, layout);
    if (desc && desc->sample_count == 4u)
        return agcComputeMsaaColorLayout(desc, 0u, NULL, layout);
    return agcComputeLinearSubresource(desc, 0u, 0u, 0u, NULL, layout);
}

int32_t PS5_SYSV_ABI agcGetImageSubresourceLayout(AgcDevice device,
    const AgcImageDesc *desc, uint32_t mip_level, uint32_t array_layer,
    uint32_t plane, AgcImageSubresourceLayout *layout)
{
    if (!agcDeviceValid(device) || !layout ||
        !agcHeaderValid(layout->struct_size, sizeof(*layout),
        layout->version) || !agcReservedZero(layout->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    memset((uint8_t *)layout + offsetof(AgcImageSubresourceLayout, mip_level),
        0, sizeof(*layout) - offsetof(AgcImageSubresourceLayout, mip_level));
    if (agcRuntimeIsBcFormat(desc ? desc->format : AGC_FORMAT_UNDEFINED)) {
        if (plane != 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        return agcComputeBcLayout(desc, mip_level, array_layer, layout, NULL);
    }
    if (desc && (desc->usage & AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) != 0u)
        return agcComputeDepthLayout(device, desc, mip_level, array_layer,
            plane, layout, NULL);
    if (desc && desc->sample_count == 4u) {
        if (mip_level != 0u || plane != 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        return agcComputeMsaaColorLayout(desc, array_layer, layout, NULL);
    }
    return agcComputeLinearSubresource(desc, mip_level, array_layer, plane,
        layout, NULL);
}

int32_t PS5_SYSV_ABI agcCreateBuffer(
    AgcDevice device, const AgcBufferDesc *desc, AgcBuffer *buffer_out)
{
    AgcBuffer buffer;
    uint32_t heap;
    uint32_t valid_flags = AGC_BUFFER_CREATE_UPLOAD_BIT |
        AGC_BUFFER_CREATE_READBACK_BIT | AGC_BUFFER_CREATE_DEDICATED_BIT;
    int32_t result;

    if (!buffer_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *buffer_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->size == 0u || desc->size > SIZE_MAX || desc->usage == 0u ||
        (desc->flags & ~valid_flags) != 0u ||
        ((desc->flags & AGC_BUFFER_CREATE_UPLOAD_BIT) != 0u &&
         (desc->flags & AGC_BUFFER_CREATE_READBACK_BIT) != 0u) ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    buffer = agcCreateChild(device, sizeof(*buffer));
    if (!buffer)
        return AGC_ERROR_OUT_OF_MEMORY;
    heap = (desc->flags & (AGC_BUFFER_CREATE_UPLOAD_BIT |
        AGC_BUFFER_CREATE_READBACK_BIT)) != 0u ?
        AGC_MEMORY_HEAP_FLEXIBLE : AGC_MEMORY_HEAP_GARLIC;
    result = agcRuntimeAllocate(device, heap, desc->size,
        heap == AGC_MEMORY_HEAP_FLEXIBLE ? AGC_FLEXIBLE_ALIGNMENT :
            AGC_GARLIC_ALIGNMENT,
        (desc->flags & AGC_BUFFER_CREATE_DEDICATED_BIT) != 0u,
        AGC_OBJECT_TYPE_BUFFER, buffer, &buffer->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, buffer);
        return result;
    }
    buffer->storage = agcAllocationCpuAddress(buffer->allocation);
    buffer->magic = AGC_MAGIC_BUFFER;
    buffer->device = device;
    buffer->size = desc->size;
    buffer->usage = desc->usage;
    buffer->create_flags = desc->flags;
    *buffer_out = buffer;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyBuffer(AgcBuffer buffer)
{
    AgcDevice device;

    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (buffer->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    if (buffer->deferred)
        return AGC_ERROR_INVALID_STATE;
    device = buffer->device;
    agcRuntimeFree(device, buffer->allocation);
    buffer->magic = 0u;
    agcDestroyChild(device, buffer);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateImage(
    AgcDevice device, const AgcImageDesc *desc, AgcImage *image_out)
{
    AgcImage image;
    AgcImageLayout layout = AGC_IMAGE_LAYOUT_INIT;
    int32_t result;

    if (!image_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *image_out = NULL;
    if (!agcDeviceValid(device) || !agcImageDescBasicValid(desc)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    result = agcGetImageLayout(device, desc, &layout);
    if (result != AGC_OK)
        return result;
    image = agcCreateChild(device, sizeof(*image));
    if (!image)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_GARLIC,
        layout.allocation_size, layout.alignment,
        (desc->usage & AGC_IMAGE_USAGE_SCANOUT_BIT) != 0u,
        AGC_OBJECT_TYPE_IMAGE, image, &image->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, image);
        return result;
    }
    image->magic = AGC_MAGIC_IMAGE;
    image->device = device;
    image->desc = *desc;
    image->layout = layout;
    *image_out = image;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyImage(AgcImage image)
{
    AgcDevice device;

    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        !agcDeviceValid(image->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (image->dependency_refs != 0u || image->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    if (image->deferred)
        return AGC_ERROR_INVALID_STATE;
    device = image->device;
    agcRuntimeFree(device, image->allocation);
    image->magic = 0u;
    agcDestroyChild(device, image);
    return AGC_OK;
}

static int32_t agcRuntimeEncodeImageView(
    AgcImage image, const AgcImageViewDesc *desc,
    AgcGfx1013ImageDescriptor *descriptor)
{
    AgcGfx1013Image2DState state = {0};

    if (desc->base_mip_level != 0u || image->desc.depth != 1u ||
        desc->format > 0x1ffu)
        return AGC_ERROR_NOT_SUPPORTED;
    state.address = agcAllocationGpuAddress(image->allocation);
    state.width = image->desc.width;
    state.height = image->desc.height;
    state.format = desc->format;
    state.dst_sel_x = 4u;
    state.dst_sel_y = 5u;
    state.dst_sel_z = 6u;
    state.dst_sel_w = 7u;
    state.sample_count = image->desc.sample_count;
    state.base_array_layer = desc->base_array_layer;
    state.last_array_layer = desc->base_array_layer +
        desc->array_layer_count - 1u;
    state.mip_level_count = desc->mip_level_count;
    if (image->desc.sample_count == 4u) {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_2D_MSAA;
        state.swizzle_mode = AGC_GFX1013_IMAGE_SWIZZLE_64KB_R_X;
    } else if ((image->desc.usage &
                AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT) != 0u) {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_CUBE;
    } else if (image->desc.array_layers > 1u) {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_2D_ARRAY;
    } else {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_2D;
    }
    return agcGfx1013Image2DDescriptorEncode(descriptor, &state);
}

int32_t PS5_SYSV_ABI agcCreateImageView(
    AgcDevice device, const AgcImageViewDesc *desc, AgcImageView *view_out)
{
    AgcImageView view;
    AgcImage image;
    AgcGfx1013ImageDescriptor descriptor;
    int32_t result;

    if (!view_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *view_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    image = desc->image;
    if (!image || image->magic != AGC_MAGIC_IMAGE || image->device != device ||
        image->deferred || desc->format != image->desc.format ||
        desc->mip_level_count == 0u || desc->array_layer_count == 0u ||
        desc->base_mip_level >= image->desc.mip_levels ||
        desc->mip_level_count > image->desc.mip_levels - desc->base_mip_level ||
        desc->base_array_layer >= image->desc.array_layers ||
        desc->array_layer_count >
            image->desc.array_layers - desc->base_array_layer) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    result = agcRuntimeEncodeImageView(image, desc, &descriptor);
    if (result != AGC_OK)
        return result;
    view = agcCreateChild(device, sizeof(*view));
    if (!view)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        sizeof(AgcGfx1013ImageDescriptor), 32u, 0u,
        AGC_OBJECT_TYPE_IMAGE_VIEW, view, &view->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, view);
        return result;
    }
    memcpy(agcAllocationCpuAddress(view->allocation), &descriptor,
        sizeof(descriptor));
    result = agcFlushRuntimeAllocation(view->allocation, 0u,
        sizeof(AgcGfx1013ImageDescriptor));
    if (result != AGC_OK) {
        agcRuntimeFree(device, view->allocation);
        agcDestroyChild(device, view);
        return result;
    }
    view->magic = AGC_MAGIC_IMAGE_VIEW;
    view->device = device;
    view->image = image;
    view->desc = *desc;
    image->dependency_refs++;
    *view_out = view;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyImageView(AgcImageView view)
{
    AgcDevice device;

    if (!view || view->magic != AGC_MAGIC_IMAGE_VIEW ||
        !agcDeviceValid(view->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (view->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    device = view->device;
    view->image->dependency_refs--;
    agcRuntimeFree(device, view->allocation);
    view->magic = 0u;
    agcDestroyChild(device, view);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateSampler(
    AgcDevice device, const AgcSamplerDesc *desc, AgcSampler *sampler_out)
{
    AgcSampler sampler;
    AgcSamplerDescriptor descriptor;
    int32_t result;

    if (!sampler_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *sampler_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->min_filter > AGC_FILTER_LINEAR ||
        desc->mag_filter > AGC_FILTER_LINEAR ||
        desc->address_u > AGC_ADDRESS_MODE_CLAMP_TO_EDGE ||
        desc->address_v > AGC_ADDRESS_MODE_CLAMP_TO_EDGE ||
        desc->address_w > AGC_ADDRESS_MODE_CLAMP_TO_EDGE ||
        desc->flags != 0u || !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    agcSamplerDescriptorInit(&descriptor);
    agcSamplerDescriptorSetFilterMode(&descriptor,
        desc->min_filter == AGC_FILTER_LINEAR ?
            kAgcFilterBilinear : kAgcFilterPoint,
        desc->mag_filter == AGC_FILTER_LINEAR ?
            kAgcFilterBilinear : kAgcFilterPoint,
        kAgcMipFilterNone);
    agcSamplerDescriptorSetClampMode(&descriptor,
        desc->address_u == AGC_ADDRESS_MODE_REPEAT ?
            kAgcClampRepeat : kAgcClampClamp,
        desc->address_v == AGC_ADDRESS_MODE_REPEAT ?
            kAgcClampRepeat : kAgcClampClamp,
        desc->address_w == AGC_ADDRESS_MODE_REPEAT ?
            kAgcClampRepeat : kAgcClampClamp);
    sampler = agcCreateChild(device, sizeof(*sampler));
    if (!sampler)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        sizeof(AgcSamplerDescriptor), 16u, 0u,
        AGC_OBJECT_TYPE_SAMPLER, sampler, &sampler->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, sampler);
        return result;
    }
    memcpy(agcAllocationCpuAddress(sampler->allocation), &descriptor,
        sizeof(descriptor));
    result = agcFlushRuntimeAllocation(sampler->allocation, 0u,
        sizeof(AgcSamplerDescriptor));
    if (result != AGC_OK) {
        agcRuntimeFree(device, sampler->allocation);
        agcDestroyChild(device, sampler);
        return result;
    }
    sampler->magic = AGC_MAGIC_SAMPLER;
    sampler->device = device;
    sampler->desc = *desc;
    *sampler_out = sampler;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroySampler(AgcSampler sampler)
{
    AgcDevice device;

    if (!sampler || sampler->magic != AGC_MAGIC_SAMPLER ||
        !agcDeviceValid(sampler->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (sampler->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    device = sampler->device;
    agcRuntimeFree(device, sampler->allocation);
    sampler->magic = 0u;
    agcDestroyChild(device, sampler);
    return AGC_OK;
}

static uint64_t agcShaderHashBytes(
    uint64_t hash, const void *data, uint64_t size)
{
    const uint8_t *bytes = data;
    uint64_t i;

    for (i = 0u; i < size; ++i) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int agcShaderRecordMatchesStage(
    AgcShaderStage stage, AgcShaderReflectionFlags flags,
    uint8_t type)
{
    switch (stage) {
    case kAgcShaderStageCs:
        return type == (uint8_t)kAgcShaderTypeCs;
    case kAgcShaderStagePs:
        return type == (uint8_t)kAgcShaderTypePs;
    case kAgcShaderStageVs:
        return type == (uint8_t)kAgcShaderTypeVs ||
            ((flags & AGC_SHADER_REFLECTION_NGG_BIT) != 0u &&
             (type == (uint8_t)kAgcShaderTypeGs ||
              type == (uint8_t)kAgcShaderTypeEs ||
              type == (uint8_t)kAgcShaderBinaryTypeGsBack));
    case kAgcShaderStageGs:
        return type == (uint8_t)kAgcShaderTypeGs ||
            type == (uint8_t)kAgcShaderBinaryTypeGsBack;
    case kAgcShaderStageHs:
        return type == (uint8_t)kAgcShaderTypeHs ||
            type == (uint8_t)kAgcShaderBinaryTypeHsBack;
    case kAgcShaderStageDs:
        return type == (uint8_t)kAgcShaderTypeEs ||
            type == (uint8_t)kAgcShaderTypeEsAlt ||
            ((flags & AGC_SHADER_REFLECTION_NGG_BIT) != 0u &&
             (type == (uint8_t)kAgcShaderTypeGs ||
              type == (uint8_t)kAgcShaderBinaryTypeGsBack));
    default:
        return 0;
    }
}

static int agcShaderFrontRecordMatchesStage(AgcShaderStage stage,
    AgcShaderReflectionFlags flags, uint8_t main_type, uint8_t front_type)
{
    if (stage == kAgcShaderStageHs)
        return main_type == (uint8_t)kAgcShaderBinaryTypeHsBack &&
            front_type == (uint8_t)kAgcShaderBinaryTypeHsFront;
    if ((flags & AGC_SHADER_REFLECTION_NGG_BIT) != 0u &&
        (stage == kAgcShaderStageVs || stage == kAgcShaderStageDs ||
         stage == kAgcShaderStageGs)) {
        return main_type == (uint8_t)kAgcShaderBinaryTypeGsBack &&
            front_type == (uint8_t)kAgcShaderBinaryTypeGsFront;
    }
    return 0;
}

static int agcShaderColorExportFormatValid(
    AgcShaderColorExportFormat format)
{
    switch (format) {
    case AGC_SHADER_COLOR_EXPORT_DEFAULT:
    case AGC_SHADER_COLOR_EXPORT_32_R:
    case AGC_SHADER_COLOR_EXPORT_32_GR:
    case AGC_SHADER_COLOR_EXPORT_FP16_ABGR:
    case AGC_SHADER_COLOR_EXPORT_UINT16_ABGR:
    case AGC_SHADER_COLOR_EXPORT_SINT16_ABGR:
    case AGC_SHADER_COLOR_EXPORT_32_ABGR:
        return 1;
    default:
        return 0;
    }
}

static int agcShaderReflectionValid(
    const AgcShaderReflection *reflection, AgcShaderStage stage,
    const void *binary, uint64_t binary_size, const void *front_binary,
    uint64_t front_binary_size)
{
    const uint32_t known_flags =
        AGC_SHADER_REFLECTION_NGG_BIT |
        AGC_SHADER_REFLECTION_FUSED_STAGE_BIT |
        AGC_SHADER_REFLECTION_DUAL_SOURCE_EXPORT_BIT |
        AGC_SHADER_REFLECTION_WRITES_DEPTH_BIT |
        AGC_SHADER_REFLECTION_WRITES_STENCIL_BIT |
        AGC_SHADER_REFLECTION_USES_SAMPLE_SHADING_BIT |
        AGC_SHADER_REFLECTION_READS_TESS_FACTORS_BIT;
    const uint64_t known_system_sgprs =
        AGC_SHADER_SYSTEM_SGPR_BASE_VERTEX_BIT |
        AGC_SHADER_SYSTEM_SGPR_START_INSTANCE_BIT |
        AGC_SHADER_SYSTEM_SGPR_DRAW_INDEX_BIT |
        AGC_SHADER_SYSTEM_SGPR_WORKGROUP_ID_BIT |
        AGC_SHADER_SYSTEM_SGPR_NUM_WORKGROUPS_BIT;
    uint64_t hash;
    int legacy_reflection;
    int current_reflection;
    uint32_t i;
    uint32_t j;

    if (!reflection)
        return 0;
    legacy_reflection =
        reflection->version == AGC_SHADER_REFLECTION_VERSION_1 &&
        reflection->compiler_api_version ==
            AGC_SHADER_COMPILER_API_VERSION_14;
    current_reflection =
        reflection->version == AGC_SHADER_REFLECTION_VERSION_2 &&
        reflection->compiler_api_version ==
            AGC_SHADER_COMPILER_API_VERSION;
    if (!reflection || reflection->struct_size != sizeof(*reflection) ||
        (!legacy_reflection && !current_reflection) ||
        reflection->stage != stage || stage >= kAgcShaderStageCount ||
        (reflection->flags & ~known_flags) != 0u ||
        (reflection->system_sgpr_mask & ~known_system_sgprs) != 0u ||
        reflection->shader_record_version !=
            AGC_SHADER_RECORD_VERSION_GEN5 ||
        (reflection->wave_size != 32u && reflection->wave_size != 64u) ||
        reflection->hash_algorithm != AGC_SHADER_HASH_FNV1A64 ||
        reflection->entry_point[0] == '\0' ||
        !memchr(reflection->entry_point, '\0',
            sizeof(reflection->entry_point)) ||
        reflection->descriptor_mapping_count >
            AGC_SHADER_MAX_DESCRIPTOR_BINDINGS ||
        reflection->user_sgpr_count > AGC_SHADER_MAX_USER_SGPRS ||
        reflection->push_constant_range_count >
            AGC_SHADER_MAX_PUSH_CONSTANT_RANGES ||
        reflection->vertex_input_count > AGC_SHADER_MAX_VERTEX_INPUTS ||
        reflection->color_export_count > AGC_SHADER_MAX_COLOR_EXPORTS ||
        reflection->push_constant_size > 256u ||
        (reflection->push_constant_size != 0u &&
         reflection->push_constant_alignment != 4u) ||
        reflection->code_offset >= binary_size ||
        reflection->code_size == 0u ||
        reflection->code_size > binary_size - reflection->code_offset ||
        reflection->reserved0 != 0u) {
        return 0;
    }
    if ((legacy_reflection &&
         (reflection->front_stage != 0u ||
          reflection->geometry_input_primitive != 0u ||
          reflection->geometry_output_primitive != 0u ||
          reflection->geometry_vertices_in != 0u ||
          reflection->geometry_vertices_out != 0u ||
          reflection->geometry_invocations != 0u ||
          reflection->reserved1 != 0u ||
          reflection->front_stage_input_mask != 0u ||
          reflection->front_stage_output_mask != 0u ||
          reflection->front_patch_input_mask != 0u ||
          reflection->front_patch_output_mask != 0u)) ||
        (current_reflection &&
         (reflection->reserved1 != 0u ||
          reflection->front_stage > kAgcShaderStageCount ||
          ((front_binary_size == 0u) !=
           (reflection->front_stage == kAgcShaderStageCount))))) {
        return 0;
    }
    if (current_reflection) {
        if (stage == kAgcShaderStageGs) {
            if (reflection->geometry_input_primitive ==
                    AGC_SHADER_PRIMITIVE_UNDEFINED ||
                reflection->geometry_input_primitive >=
                    AGC_SHADER_PRIMITIVE_TOPOLOGY_COUNT ||
                reflection->geometry_output_primitive ==
                    AGC_SHADER_PRIMITIVE_UNDEFINED ||
                reflection->geometry_output_primitive >=
                    AGC_SHADER_PRIMITIVE_TOPOLOGY_COUNT ||
                reflection->geometry_vertices_in == 0u ||
                reflection->geometry_vertices_out == 0u ||
                reflection->geometry_invocations == 0u)
                return 0;
        } else if (reflection->geometry_input_primitive !=
                       AGC_SHADER_PRIMITIVE_UNDEFINED ||
                   reflection->geometry_output_primitive !=
                       AGC_SHADER_PRIMITIVE_UNDEFINED ||
                   reflection->geometry_vertices_in != 0u ||
                   reflection->geometry_vertices_out != 0u ||
                   reflection->geometry_invocations != 0u) {
            return 0;
        }
    }
    if ((stage != kAgcShaderStageVs &&
         !(current_reflection &&
           reflection->front_stage == kAgcShaderStageVs &&
           (stage == kAgcShaderStageHs || stage == kAgcShaderStageGs)) &&
         reflection->vertex_input_count != 0u) ||
        (stage != kAgcShaderStagePs &&
         (reflection->color_export_count != 0u ||
          reflection->pixel_shader_sample_count != 0u ||
          (reflection->flags &
           (AGC_SHADER_REFLECTION_DUAL_SOURCE_EXPORT_BIT |
            AGC_SHADER_REFLECTION_WRITES_DEPTH_BIT |
            AGC_SHADER_REFLECTION_WRITES_STENCIL_BIT |
            AGC_SHADER_REFLECTION_USES_SAMPLE_SHADING_BIT)) != 0u)) ||
        (stage == kAgcShaderStagePs &&
         reflection->pixel_shader_sample_count != 1u &&
         reflection->pixel_shader_sample_count != 2u &&
         reflection->pixel_shader_sample_count != 4u &&
         reflection->pixel_shader_sample_count != 8u)) {
        return 0;
    }
    if ((front_binary_size == 0u) != (front_binary == NULL) ||
        (front_binary_size == 0u) !=
            (reflection->front_code_size == 0u) ||
        (front_binary_size != 0u &&
         (reflection->front_code_offset >= front_binary_size ||
          reflection->front_code_size >
              front_binary_size - reflection->front_code_offset))) {
        return 0;
    }
    for (i = 0u; i < reflection->descriptor_mapping_count; ++i) {
        const AgcShaderDescriptorMapping *mapping =
            &reflection->descriptor_mappings[i];
        if (mapping->set >= 8u ||
            mapping->type >= AGC_SHADER_DESCRIPTOR_TYPE_COUNT ||
            mapping->array_size == 0u || mapping->byte_stride == 0u ||
            (mapping->byte_offset & 3u) != 0u ||
            (mapping->byte_stride & 3u) != 0u) {
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            if (reflection->descriptor_mappings[j].set == mapping->set &&
                reflection->descriptor_mappings[j].binding ==
                    mapping->binding) {
                return 0;
            }
        }
    }
    for (i = 0u; i < reflection->user_sgpr_count; ++i) {
        const AgcShaderUserSgpr *sgpr = &reflection->user_sgprs[i];
        if (sgpr->kind >= AGC_SHADER_USER_SGPR_KIND_COUNT ||
            sgpr->dword_count == 0u || sgpr->dword_count > 16u ||
            sgpr->register_offset > 0x3ffu ||
            sgpr->dword_count > 0x400u - sgpr->register_offset) {
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            const AgcShaderUserSgpr *previous =
                &reflection->user_sgprs[j];
            if (sgpr->register_offset <
                    previous->register_offset + previous->dword_count &&
                previous->register_offset <
                    sgpr->register_offset + sgpr->dword_count) {
                return 0;
            }
        }
    }
    for (i = 0u; i < reflection->push_constant_range_count; ++i) {
        const AgcShaderPushConstantRange *range =
            &reflection->push_constant_ranges[i];
        if (range->size == 0u || range->alignment != 4u ||
            (range->offset & 3u) != 0u || (range->size & 3u) != 0u ||
            range->offset > reflection->push_constant_size ||
            range->size > reflection->push_constant_size - range->offset ||
            (range->stage_mask & (1u << stage)) == 0u) {
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            const AgcShaderPushConstantRange *previous =
                &reflection->push_constant_ranges[j];
            if (range->offset < previous->offset + previous->size &&
                previous->offset < range->offset + range->size) {
                return 0;
            }
        }
    }
    for (i = 0u; i < reflection->vertex_input_count; ++i) {
        const AgcShaderVertexInput *input = &reflection->vertex_inputs[i];
        if (input->location >= 32u || input->binding >= 32u ||
            input->format >= AGC_SHADER_VERTEX_FORMAT_COUNT ||
            input->input_rate >= AGC_SHADER_VERTEX_INPUT_RATE_COUNT ||
            input->stride > 0x3fffu || input->component_mask == 0u ||
            (input->component_mask & ~0xfu) != 0u ||
            (input->input_rate == AGC_SHADER_VERTEX_INPUT_RATE_VERTEX &&
             input->divisor != 0u)) {
            return 0;
        }
        for (j = 0u; j < i; ++j) {
            if (reflection->vertex_inputs[j].location == input->location)
                return 0;
        }
    }
    for (i = 0u; i < reflection->color_export_count; ++i) {
        const AgcShaderColorExport *export_info =
            &reflection->color_exports[i];
        if (export_info->location != i ||
            !agcShaderColorExportFormatValid(export_info->format) ||
            export_info->component_class > AGC_SHADER_COMPONENT_SINT ||
            export_info->write_mask == 0u ||
            (export_info->write_mask & ~0xfu) != 0u ||
            export_info->flags != 0u) {
            return 0;
        }
    }
    if (stage == kAgcShaderStageCs) {
        uint64_t invocations = (uint64_t)reflection->local_size_x *
            reflection->local_size_y * reflection->local_size_z;
        if (reflection->local_size_x == 0u ||
            reflection->local_size_y == 0u ||
            reflection->local_size_z == 0u || invocations > 1024u)
            return 0;
    } else if (reflection->local_size_x != 0u ||
        reflection->local_size_y != 0u || reflection->local_size_z != 0u) {
        return 0;
    }
    hash = agcShaderHashBytes(
        UINT64_C(14695981039346656037), binary, binary_size);
    if (front_binary)
        hash = agcShaderHashBytes(hash, front_binary, front_binary_size);
    return hash == reflection->code_hash;
}

int32_t PS5_SYSV_ABI agcCreateShader(
    AgcDevice device, const AgcShaderDesc *desc, AgcShader *shader_out)
{
    AgcShader shader;
    uint64_t allocation_size;
    uint64_t front_offset = 0u;
    int reflected;
    int32_t result;

    if (!shader_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *shader_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        desc->stage >= kAgcShaderStageCount || desc->flags != 0u ||
        !desc->code || desc->code_size == 0u || desc->code_size > SIZE_MAX ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    reflected = desc->struct_size == sizeof(*desc) &&
        desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2;
    if (!reflected &&
        (desc->struct_size != 64u ||
         desc->version != AGC_RUNTIME_STRUCTURE_VERSION_1)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    allocation_size = desc->code_size;
    if (reflected) {
        if (desc->front_code_size > SIZE_MAX ||
            !agcShaderReflectionValid(desc->reflection, desc->stage,
                desc->code, desc->code_size, desc->front_code,
                desc->front_code_size) ||
            !agcAlignU64(desc->code_size, 256u, &front_offset) ||
            !agcAddU64(front_offset, desc->front_code_size,
                &allocation_size)) {
            return AGC_ERROR_SHADER_INVALID;
        }
    }
    shader = agcCreateChild(device, sizeof(*shader));
    if (!shader)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        allocation_size, 256u, 0u, AGC_OBJECT_TYPE_SHADER,
        shader, &shader->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, shader);
        return result;
    }
    shader->code = agcAllocationCpuAddress(shader->allocation);
    memcpy(shader->code, desc->code, (size_t)desc->code_size);
    if (reflected && desc->front_code_size != 0u) {
        memcpy((uint8_t *)shader->code + front_offset, desc->front_code,
            (size_t)desc->front_code_size);
    }
    result = agcFlushRuntimeAllocation(shader->allocation, 0u,
        allocation_size);
    if (result != AGC_OK) {
        agcRuntimeFree(device, shader->allocation);
        agcDestroyChild(device, shader);
        return result;
    }
    if (reflected) {
        result = agcShaderRecordRelocateBinary(&shader->record,
            shader->code, (size_t)desc->code_size);
        if (result != AGC_OK ||
            !agcShaderRecordMatchesStage(desc->stage,
                desc->reflection->flags,
                shader->record.shader_type) ||
            (desc->front_code_size == 0u &&
             ((desc->stage == kAgcShaderStageHs &&
               shader->record.shader_type ==
                   (uint8_t)kAgcShaderBinaryTypeHsBack) ||
              ((desc->reflection->flags &
                AGC_SHADER_REFLECTION_NGG_BIT) != 0u &&
               shader->record.shader_type ==
                   (uint8_t)kAgcShaderBinaryTypeGsBack)))) {
            agcRuntimeFree(device, shader->allocation);
            agcDestroyChild(device, shader);
            return result != AGC_OK ? result : AGC_ERROR_SHADER_INVALID_TYPE;
        }
        shader->reflection = *desc->reflection;
        shader->has_reflection = 1u;
        shader->program_gpu_address =
            agcAllocationGpuAddress(shader->allocation) +
            shader->reflection.code_offset;
        if (desc->front_code_size != 0u) {
            result = agcShaderRecordRelocateBinary(&shader->front_record,
                (uint8_t *)shader->code + front_offset,
                (size_t)desc->front_code_size);
            if (result != AGC_OK ||
                !agcShaderFrontRecordMatchesStage(desc->stage,
                    desc->reflection->flags, shader->record.shader_type,
                    shader->front_record.shader_type)) {
                agcRuntimeFree(device, shader->allocation);
                agcDestroyChild(device, shader);
                return result != AGC_OK ? result :
                    AGC_ERROR_SHADER_INVALID_TYPE;
            }
            shader->front_binary_offset = front_offset;
            shader->front_program_gpu_address =
                agcAllocationGpuAddress(shader->allocation) + front_offset +
                shader->reflection.front_code_offset;
            shader->has_front_record = 1u;
        }
    }
    shader->magic = AGC_MAGIC_SHADER;
    shader->device = device;
    shader->stage = desc->stage;
    shader->code_size = desc->code_size;
    *shader_out = shader;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyShader(AgcShader shader)
{
    AgcDevice device;

    if (!shader || shader->magic != AGC_MAGIC_SHADER ||
        !agcDeviceValid(shader->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (shader->dependency_refs != 0u)
        return AGC_ERROR_BUSY;
    device = shader->device;
    agcRuntimeFree(device, shader->allocation);
    shader->magic = 0u;
    agcDestroyChild(device, shader);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetShaderReflection(
    AgcShader shader, AgcShaderReflection *reflection)
{
    if (!shader || shader->magic != AGC_MAGIC_SHADER ||
        !agcDeviceValid(shader->device) || !reflection ||
        reflection->struct_size != sizeof(*reflection) ||
        reflection->version != AGC_SHADER_REFLECTION_VERSION ||
        reflection->reserved0 != 0u ||
        reflection->reserved1 != 0u) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (!shader->has_reflection)
        return AGC_ERROR_NOT_FOUND;
    *reflection = shader->reflection;
    return AGC_OK;
}

static int agcPipelineDescriptorEqual(
    const AgcShaderDescriptorMapping *a,
    const AgcShaderDescriptorMapping *b)
{
    return a->set == b->set && a->binding == b->binding &&
        a->type == b->type && a->array_size == b->array_size &&
        a->byte_offset == b->byte_offset &&
        a->byte_stride == b->byte_stride;
}

static int agcPipelineVertexInputEqual(
    const AgcShaderVertexInput *a, const AgcShaderVertexInput *b)
{
    return a->location == b->location && a->binding == b->binding &&
        a->offset == b->offset && a->stride == b->stride &&
        a->format == b->format && a->input_rate == b->input_rate &&
        a->divisor == b->divisor &&
        a->component_mask == b->component_mask;
}

static int agcPipelineReflectionDescriptorsMatch(
    const AgcShaderReflection *reflection,
    const AgcShaderDescriptorMapping *layout, uint32_t layout_count)
{
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < reflection->descriptor_mapping_count; ++i) {
        for (j = 0u; j < layout_count; ++j) {
            if (agcPipelineDescriptorEqual(
                    &reflection->descriptor_mappings[i], &layout[j]))
                break;
        }
        if (j == layout_count)
            return 0;
    }
    return 1;
}

static int agcPipelineReflectionPushRangesMatch(
    const AgcShaderReflection *reflection,
    const AgcShaderPushConstantRange *layout, uint32_t layout_count)
{
    uint32_t i;
    uint32_t j;

    for (i = 0u; i < reflection->push_constant_range_count; ++i) {
        for (j = 0u; j < layout_count; ++j) {
            const AgcShaderPushConstantRange *range =
                &reflection->push_constant_ranges[i];
            if (range->offset == layout[j].offset &&
                range->size == layout[j].size &&
                range->alignment == layout[j].alignment &&
                (layout[j].stage_mask & (1u << reflection->stage)) != 0u)
                break;
        }
        if (j == layout_count)
            return 0;
    }
    return 1;
}

static int agcPipelineLayoutEntryUsed(
    const AgcShaderReflection *a, const AgcShaderReflection *b,
    const AgcShaderDescriptorMapping *entry)
{
    uint32_t i;
    const AgcShaderReflection *reflections[2] = {a, b};
    uint32_t reflection_index;

    for (reflection_index = 0u; reflection_index < 2u;
         ++reflection_index) {
        const AgcShaderReflection *reflection =
            reflections[reflection_index];
        for (i = 0u; i < reflection->descriptor_mapping_count; ++i) {
            if (agcPipelineDescriptorEqual(
                    &reflection->descriptor_mappings[i], entry))
                return 1;
        }
    }
    return 0;
}

static int agcPipelinePushEntryUsed(
    const AgcShaderReflection *a, const AgcShaderReflection *b,
    const AgcShaderPushConstantRange *entry)
{
    const AgcShaderReflection *reflections[2] = {a, b};
    uint32_t reflection_index;
    uint32_t i;

    for (reflection_index = 0u; reflection_index < 2u;
         ++reflection_index) {
        const AgcShaderReflection *reflection =
            reflections[reflection_index];
        for (i = 0u; i < reflection->push_constant_range_count; ++i) {
            const AgcShaderPushConstantRange *range =
                &reflection->push_constant_ranges[i];
            if (range->offset == entry->offset &&
                range->size == entry->size &&
                range->alignment == entry->alignment &&
                (entry->stage_mask & (1u << reflection->stage)) != 0u)
                return 1;
        }
    }
    return 0;
}

static uint32_t agcPipelineDescriptorSize(AgcShaderDescriptorType type)
{
    switch (type) {
    case AGC_SHADER_DESCRIPTOR_SAMPLER:
        return sizeof(AgcSamplerDescriptor);
    case AGC_SHADER_DESCRIPTOR_COMBINED_IMAGE_SAMPLER:
        return sizeof(AgcGfx1013CombinedImageSamplerDescriptor);
    case AGC_SHADER_DESCRIPTOR_SAMPLED_IMAGE:
    case AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE:
    case AGC_SHADER_DESCRIPTOR_INPUT_ATTACHMENT:
        return sizeof(AgcGfx1013ImageDescriptor);
    case AGC_SHADER_DESCRIPTOR_UNIFORM_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_STORAGE_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER:
    case AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER:
        return sizeof(AgcGfx1013BufferDescriptor);
    default:
        return 0u;
    }
}

static const AgcShaderUserSgpr *agcPipelineFindUserSgpr(
    const AgcShaderReflection *reflection, AgcShaderUserSgprKind kind,
    uint32_t index)
{
    uint32_t i;

    for (i = 0u; i < reflection->user_sgpr_count; ++i) {
        if (reflection->user_sgprs[i].kind == kind &&
            reflection->user_sgprs[i].index == index) {
            return &reflection->user_sgprs[i];
        }
    }
    return NULL;
}

static int32_t agcPipelineValidateShaderUserData(
    const AgcShaderReflection *reflection)
{
    uint32_t descriptor_set_mask = 0u;
    uint32_t direct_descriptor_set_mask = 0u;
    uint32_t user_data_base;
    uint32_t occupied_register_mask = 0u;
    uint32_t indirect_descriptor_set_count = 0u;
    uint64_t required_push_mask = 0u;
    uint64_t inline_mask = 0u;
    uint32_t i;

    switch (reflection->stage) {
    case kAgcShaderStageVs:
    case kAgcShaderStageDs:
    case kAgcShaderStageGs:
        user_data_base = AGC_REG_SPI_SHADER_USER_DATA_GS_0;
        break;
    case kAgcShaderStageHs:
        user_data_base = AGC_REG_SPI_SHADER_USER_DATA_HS_0;
        break;
    case kAgcShaderStagePs:
        user_data_base = AGC_REG_SPI_SHADER_USER_DATA_PS_0;
        break;
    case kAgcShaderStageCs:
        user_data_base = AGC_REG_COMPUTE_USER_DATA_0;
        break;
    default:
        return reflection->user_sgpr_count == 0u ?
            AGC_OK : AGC_ERROR_NOT_SUPPORTED;
    }

    for (i = 0u; i < reflection->descriptor_mapping_count; ++i)
        descriptor_set_mask |= 1u << reflection->descriptor_mappings[i].set;
    for (i = 0u; i < reflection->push_constant_range_count; ++i) {
        const AgcShaderPushConstantRange *range =
            &reflection->push_constant_ranges[i];
        uint32_t first = range->offset / 4u;
        uint32_t count = range->size / 4u;
        uint64_t bits = count == 64u ? UINT64_MAX :
            ((UINT64_C(1) << count) - 1u) << first;
        required_push_mask |= bits;
    }
    for (i = 0u; i < reflection->user_sgpr_count; ++i) {
        const AgcShaderUserSgpr *sgpr = &reflection->user_sgprs[i];
        uint32_t register_index;
        uint32_t register_mask;
        if (sgpr->register_offset < user_data_base ||
            sgpr->register_offset >= user_data_base + 16u ||
            sgpr->dword_count == 0u ||
            sgpr->dword_count >
                user_data_base + 16u - sgpr->register_offset)
            return AGC_ERROR_NOT_SUPPORTED;
        register_index = sgpr->register_offset - user_data_base;
        register_mask = ((1u << sgpr->dword_count) - 1u) << register_index;
        if ((occupied_register_mask & register_mask) != 0u)
            return AGC_ERROR_VALIDATION_FAILED;
        occupied_register_mask |= register_mask;
        switch (sgpr->kind) {
        case AGC_SHADER_USER_SGPR_DESCRIPTOR_SET:
            if (sgpr->index >= 8u || sgpr->dword_count != 1u ||
                (descriptor_set_mask & (1u << sgpr->index)) == 0u ||
                (direct_descriptor_set_mask &
                    (1u << sgpr->index)) != 0u)
                return AGC_ERROR_VALIDATION_FAILED;
            direct_descriptor_set_mask |= 1u << sgpr->index;
            break;
        case AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER:
            if (sgpr->index != 0u || sgpr->dword_count != 1u ||
                reflection->push_constant_size == 0u)
                return AGC_ERROR_VALIDATION_FAILED;
            break;
        case AGC_SHADER_USER_SGPR_INLINE_PUSH_CONSTANT:
            if (sgpr->index >= 64u || sgpr->dword_count != 1u ||
                (reflection->inline_push_constant_mask &
                    (UINT64_C(1) << sgpr->index)) == 0u)
                return AGC_ERROR_VALIDATION_FAILED;
            inline_mask |= UINT64_C(1) << sgpr->index;
            break;
        case AGC_SHADER_USER_SGPR_VERTEX_BUFFER_TABLE:
            if (sgpr->index != 0u || sgpr->dword_count != 1u ||
                reflection->vertex_input_count == 0u)
                return AGC_ERROR_VALIDATION_FAILED;
            break;
        case AGC_SHADER_USER_SGPR_BASE_VERTEX:
        case AGC_SHADER_USER_SGPR_START_INSTANCE:
        case AGC_SHADER_USER_SGPR_DRAW_INDEX:
            if (sgpr->index != 0u || sgpr->dword_count != 1u)
                return AGC_ERROR_VALIDATION_FAILED;
            break;
        case AGC_SHADER_USER_SGPR_INDIRECT_DESCRIPTOR_SETS:
            if (sgpr->index != 0u || sgpr->dword_count != 1u)
                return AGC_ERROR_VALIDATION_FAILED;
            indirect_descriptor_set_count++;
            break;
        default:
            return AGC_ERROR_VALIDATION_FAILED;
        }
    }
    if (indirect_descriptor_set_count > 1u ||
        (indirect_descriptor_set_count != 0u &&
         direct_descriptor_set_mask != 0u) ||
        (indirect_descriptor_set_count == 0u &&
         direct_descriptor_set_mask != descriptor_set_mask))
        return AGC_ERROR_VALIDATION_FAILED;
    if (inline_mask != reflection->inline_push_constant_mask)
        return AGC_ERROR_VALIDATION_FAILED;
    if (reflection->vertex_input_count != 0u &&
        !agcPipelineFindUserSgpr(reflection,
            AGC_SHADER_USER_SGPR_VERTEX_BUFFER_TABLE, 0u))
        return AGC_ERROR_VALIDATION_FAILED;
    if (required_push_mask != 0u &&
        !agcPipelineFindUserSgpr(reflection,
            AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER, 0u) &&
        (inline_mask & required_push_mask) != required_push_mask)
        return AGC_ERROR_VALIDATION_FAILED;
    return AGC_OK;
}

static uint32_t agcPipelineUsesIndirectDescriptorSets(
    const AgcShaderReflection *reflection)
{
    return agcPipelineFindUserSgpr(reflection,
        AGC_SHADER_USER_SGPR_INDIRECT_DESCRIPTOR_SETS, 0u) != NULL;
}

static int32_t agcPipelineBuildResourceLayout(
    const AgcShaderDescriptorMapping *mappings, uint32_t mapping_count,
    const AgcShaderVertexInput *vertex_inputs, uint32_t vertex_input_count,
    uint32_t push_constant_size, uint32_t uses_indirect_set_table,
    AgcRuntimePipelineResourceLayout *layout)
{
    uint64_t offset = 0u;
    uint32_t i;

    memset(layout, 0, sizeof(*layout));
    if (uses_indirect_set_table) {
        layout->indirect_set_table_offset = offset;
        layout->uses_indirect_set_table = 1u;
        if (!agcAddU64(offset, 8u * sizeof(uint32_t), &offset))
            return AGC_ERROR_VALIDATION_FAILED;
    }
    for (i = 0u; i < mapping_count; ++i) {
        const AgcShaderDescriptorMapping *mapping = &mappings[i];
        uint32_t descriptor_size = agcPipelineDescriptorSize(mapping->type);
        uint64_t end;
        if (descriptor_size == 0u || mapping->byte_stride < descriptor_size ||
            layout->descriptor_element_count >
                AGC_RUNTIME_MAX_DESCRIPTOR_WRITES - mapping->array_size ||
            !agcMulU64(mapping->byte_stride, mapping->array_size, &end) ||
            !agcAddU64(mapping->byte_offset, end, &end) ||
            end > AGC_RUNTIME_MAX_RESOURCE_ARENA_SIZE)
            return AGC_ERROR_VALIDATION_FAILED;
        if (end > layout->set_sizes[mapping->set])
            layout->set_sizes[mapping->set] = end;
        layout->set_mask |= 1u << mapping->set;
        layout->descriptor_element_count += mapping->array_size;
    }
    for (i = 0u; i < 8u; ++i) {
        if ((layout->set_mask & (1u << i)) == 0u)
            continue;
        if (!agcAlignU64(offset, 256u, &offset))
            return AGC_ERROR_VALIDATION_FAILED;
        layout->set_offsets[i] = offset;
        if (!agcAddU64(offset, layout->set_sizes[i], &offset))
            return AGC_ERROR_VALIDATION_FAILED;
    }
    for (i = 0u; i < vertex_input_count; ++i)
        layout->vertex_binding_mask |= 1u << vertex_inputs[i].binding;
    if (layout->vertex_binding_mask != 0u) {
        uint32_t last_binding = 31u;
        while ((layout->vertex_binding_mask & (1u << last_binding)) == 0u)
            --last_binding;
        if (!agcAlignU64(offset, 16u, &offset))
            return AGC_ERROR_VALIDATION_FAILED;
        layout->vertex_table_offset = offset;
        if (!agcAddU64(offset,
                (uint64_t)(last_binding + 1u) *
                    sizeof(AgcGfx1013BufferDescriptor), &offset))
            return AGC_ERROR_VALIDATION_FAILED;
    }
    if (push_constant_size != 0u) {
        if (!agcAlignU64(offset, 16u, &offset))
            return AGC_ERROR_VALIDATION_FAILED;
        layout->push_constant_offset = offset;
        layout->push_constant_size = push_constant_size;
        if (!agcAddU64(offset, push_constant_size, &offset))
            return AGC_ERROR_VALIDATION_FAILED;
    }
    if (!agcAlignU64(offset, 256u, &layout->total_size) ||
        layout->total_size > AGC_RUNTIME_MAX_RESOURCE_ARENA_SIZE)
        return AGC_ERROR_VALIDATION_FAILED;
    return AGC_OK;
}

static int agcPipelineFormatInfo(
    uint32_t format, AgcShaderComponentClass *component_class,
    uint32_t *component_bits, uint32_t *depth, uint32_t *stencil)
{
    *component_bits = 0u;
    *depth = 0u;
    *stencil = 0u;
    switch ((AgcFormat)format) {
    case AGC_FORMAT_RGBA8_UNORM:
    case AGC_FORMAT_BGRA8_UNORM:
    case AGC_FORMAT_RGBA8_SRGB:
    case AGC_FORMAT_RGBA16_FLOAT:
        *component_class = AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED;
        *component_bits = 16u;
        return 1;
    case AGC_FORMAT_RGBA32_FLOAT:
        *component_class = AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED;
        *component_bits = 32u;
        return 1;
    case AGC_FORMAT_RGBA16_UINT:
        *component_class = AGC_SHADER_COMPONENT_UINT;
        *component_bits = 16u;
        return 1;
    case AGC_FORMAT_RGBA16_SINT:
        *component_class = AGC_SHADER_COMPONENT_SINT;
        *component_bits = 16u;
        return 1;
    case AGC_FORMAT_RGBA32_UINT:
        *component_class = AGC_SHADER_COMPONENT_UINT;
        *component_bits = 32u;
        return 1;
    case AGC_FORMAT_RGBA32_SINT:
        *component_class = AGC_SHADER_COMPONENT_SINT;
        *component_bits = 32u;
        return 1;
    case AGC_FORMAT_D16_UNORM:
    case AGC_FORMAT_D32_FLOAT:
        *component_class = AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED;
        *depth = 1u;
        return 1;
    case AGC_FORMAT_S8_UINT:
        *component_class = AGC_SHADER_COMPONENT_UINT;
        *stencil = 1u;
        return 1;
    case AGC_FORMAT_D16_UNORM_S8_UINT:
    case AGC_FORMAT_D32_FLOAT_S8_UINT:
        *component_class = AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED;
        *depth = 1u;
        *stencil = 1u;
        return 1;
    default:
        return 0;
    }
}

static int agcPipelineColorExportCompatible(
    const AgcShaderColorExport *export_info,
    const AgcColorBlendAttachmentState *attachment)
{
    AgcShaderComponentClass attachment_class;
    uint32_t component_bits;
    uint32_t depth;
    uint32_t stencil;

    if (!agcPipelineFormatInfo(attachment->format, &attachment_class,
            &component_bits, &depth, &stencil) || depth || stencil ||
        attachment_class != export_info->component_class)
        return 0;
    if (component_bits == 16u) {
        if (attachment_class == AGC_SHADER_COMPONENT_UINT)
            return export_info->format ==
                AGC_SHADER_COLOR_EXPORT_UINT16_ABGR;
        if (attachment_class == AGC_SHADER_COMPONENT_SINT)
            return export_info->format ==
                AGC_SHADER_COLOR_EXPORT_SINT16_ABGR;
        return export_info->format == AGC_SHADER_COLOR_EXPORT_FP16_ABGR ||
            export_info->format == AGC_SHADER_COLOR_EXPORT_32_ABGR;
    }
    return export_info->format == AGC_SHADER_COLOR_EXPORT_32_ABGR;
}

static int agcPipelineColorStateValid(
    const AgcColorBlendAttachmentState *state)
{
    return state &&
        agcHeaderValid(state->struct_size, sizeof(*state), state->version) &&
        state->blend_enable <= 1u && state->write_mask != 0u &&
        (state->write_mask & ~0xfu) == 0u &&
        state->source_color_factor < AGC_BLEND_FACTOR_COUNT &&
        state->destination_color_factor < AGC_BLEND_FACTOR_COUNT &&
        state->color_operation < AGC_BLEND_OPERATION_COUNT &&
        state->source_alpha_factor < AGC_BLEND_FACTOR_COUNT &&
        state->destination_alpha_factor < AGC_BLEND_FACTOR_COUNT &&
        state->alpha_operation < AGC_BLEND_OPERATION_COUNT &&
        state->flags == 0u && agcReservedZero(state->reserved, 2u);
}

static int agcPipelineRasterizationStateValid(
    const AgcRasterizationState *state)
{
    return state &&
        agcHeaderValid(state->struct_size, sizeof(*state), state->version) &&
        state->polygon_mode < AGC_POLYGON_MODE_COUNT &&
        (state->cull_mode & ~(AGC_CULL_MODE_FRONT_BIT |
            AGC_CULL_MODE_BACK_BIT)) == 0u &&
        state->front_face <= AGC_FRONT_FACE_CLOCKWISE &&
        state->depth_clamp_enable <= 1u &&
        state->rasterizer_discard_enable <= 1u &&
        state->depth_bias_enable <= 1u && state->line_width > 0.0f &&
        state->flags == 0u && agcReservedZero(state->reserved, 3u);
}

typedef struct AgcDepthStencilPipelineStateV1 {
    uint32_t struct_size;
    uint32_t version;
    uint32_t format;
    uint32_t depth_test_enable;
    uint32_t depth_write_enable;
    AgcCompareOperation depth_compare_operation;
    uint32_t depth_bounds_enable;
    uint32_t stencil_test_enable;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved[3];
} AgcDepthStencilPipelineStateV1;

_Static_assert(sizeof(AgcDepthStencilPipelineStateV1) == 64u,
    "legacy depth-stencil pipeline state size mismatch");

static int agcPipelineDepthStateValid(
    const AgcDepthStencilPipelineState *state)
{
    AgcShaderComponentClass component_class;
    uint32_t component_bits;
    uint32_t depth;
    uint32_t stencil;

    if (!state || state->struct_size != sizeof(*state) ||
        state->version != AGC_RUNTIME_STRUCTURE_VERSION_2 ||
        state->depth_test_enable > 1u || state->depth_write_enable > 1u ||
        state->depth_compare_operation >= AGC_COMPARE_OPERATION_COUNT ||
        state->depth_bounds_enable > 1u || state->stencil_test_enable > 1u ||
        state->back_face_enable > 1u || state->flags != 0u ||
        !agcReservedZero(state->reserved, 2u) ||
        !agcRuntimeFloatFinite(state->min_depth_bounds) ||
        !agcRuntimeFloatFinite(state->max_depth_bounds) ||
        state->min_depth_bounds < 0.0f ||
        state->max_depth_bounds > 1.0f ||
        state->min_depth_bounds > state->max_depth_bounds ||
        (state->depth_write_enable && !state->depth_test_enable) ||
        (state->depth_bounds_enable && !state->depth_test_enable) ||
        (state->back_face_enable && !state->stencil_test_enable)) {
        return 0;
    }
    {
        const AgcStencilFaceState *faces[2] = {&state->front, &state->back};
        uint32_t i;
        for (i = 0u; i < 2u; ++i) {
            const AgcStencilFaceState *face = faces[i];
            if (face->compare_operation >= AGC_COMPARE_OPERATION_COUNT ||
                face->fail_operation >= AGC_STENCIL_OPERATION_COUNT ||
                face->depth_fail_operation >= AGC_STENCIL_OPERATION_COUNT ||
                face->pass_operation >= AGC_STENCIL_OPERATION_COUNT ||
                face->compare_mask > 0xffu || face->write_mask > 0xffu ||
                face->reference > 0xffu || face->flags != 0u ||
                !agcReservedZero(face->reserved, 2u))
                return 0;
        }
    }
    if (state->format == AGC_FORMAT_UNDEFINED)
        return !state->depth_test_enable && !state->depth_write_enable &&
            !state->depth_bounds_enable && !state->stencil_test_enable;
    if (!agcPipelineFormatInfo(state->format, &component_class,
            &component_bits, &depth, &stencil))
        return 0;
    return (!state->depth_test_enable || depth) &&
        (!state->stencil_test_enable || stencil);
}

static int32_t agcPipelineNormalizeDepthState(const void *source,
    AgcDepthStencilPipelineState *target)
{
    uint32_t header[2];

    if (!source || !target)
        return AGC_ERROR_INVALID_ARGUMENT;
    memcpy(header, source, sizeof(header));
    if (header[0] == sizeof(AgcDepthStencilPipelineStateV1) &&
        header[1] == AGC_RUNTIME_STRUCTURE_VERSION_1) {
        AgcDepthStencilPipelineStateV1 legacy;
        memcpy(&legacy, source, sizeof(legacy));
        if (legacy.stencil_test_enable)
            return AGC_ERROR_NOT_SUPPORTED;
        if (legacy.flags != 0u || legacy.reserved0 != 0u ||
            !agcReservedZero(legacy.reserved, 3u))
            return AGC_ERROR_INVALID_ARGUMENT;
        *target = (AgcDepthStencilPipelineState)
            AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
        target->format = legacy.format;
        target->depth_test_enable = legacy.depth_test_enable;
        target->depth_write_enable = legacy.depth_write_enable;
        target->depth_compare_operation = legacy.depth_compare_operation;
        target->depth_bounds_enable = legacy.depth_bounds_enable;
    } else if (header[0] == sizeof(AgcDepthStencilPipelineState) &&
        header[1] == AGC_RUNTIME_STRUCTURE_VERSION_2) {
        *target = *(const AgcDepthStencilPipelineState *)source;
    } else {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    return agcPipelineDepthStateValid(target) ?
        AGC_OK : AGC_ERROR_INVALID_ARGUMENT;
}

static AgcGfx1013StencilOp agcPipelineStencilOperation(
    AgcStencilOperation operation)
{
    static const AgcGfx1013StencilOp table[AGC_STENCIL_OPERATION_COUNT] = {
        AGC_GFX1013_STENCIL_KEEP,
        AGC_GFX1013_STENCIL_ZERO,
        AGC_GFX1013_STENCIL_REPLACE,
        AGC_GFX1013_STENCIL_INCREMENT_CLAMP,
        AGC_GFX1013_STENCIL_DECREMENT_CLAMP,
        AGC_GFX1013_STENCIL_INVERT,
        AGC_GFX1013_STENCIL_INCREMENT_WRAP,
        AGC_GFX1013_STENCIL_DECREMENT_WRAP,
    };
    return table[operation];
}

static void agcPipelineStencilFace(
    const AgcStencilFaceState *source, AgcGfx1013StencilFaceState *target)
{
    target->compare_operation =
        (AgcGfx1013CompareOp)source->compare_operation;
    target->fail_operation =
        agcPipelineStencilOperation(source->fail_operation);
    target->depth_fail_operation =
        agcPipelineStencilOperation(source->depth_fail_operation);
    target->pass_operation =
        agcPipelineStencilOperation(source->pass_operation);
    target->reference = source->reference;
    target->compare_mask = source->compare_mask;
    target->write_mask = source->write_mask;
}

static int agcPipelineMultisampleStateValid(
    const AgcMultisampleState *state)
{
    return state &&
        agcHeaderValid(state->struct_size, sizeof(*state), state->version) &&
        (state->rasterization_samples == 1u ||
         state->rasterization_samples == 4u) &&
        state->sample_shading_enable <= 1u &&
        agcRuntimeFloatFinite(state->minimum_sample_shading) &&
        state->minimum_sample_shading >= 0.0f &&
        state->minimum_sample_shading <= 1.0f &&
        (state->sample_shading_enable ||
         state->minimum_sample_shading == 0.0f) &&
        state->alpha_to_coverage_enable <= 1u &&
        state->alpha_to_one_enable <= 1u && state->flags == 0u &&
        agcReservedZero(state->reserved, 2u);
}

static AgcGfx1013BlendFactor agcPipelineBlendFactor(
    AgcBlendFactor factor)
{
    static const AgcGfx1013BlendFactor table[AGC_BLEND_FACTOR_COUNT] = {
        AGC_GFX1013_BLEND_ZERO,
        AGC_GFX1013_BLEND_ONE,
        AGC_GFX1013_BLEND_SRC_COLOR,
        AGC_GFX1013_BLEND_ONE_MINUS_SRC_COLOR,
        AGC_GFX1013_BLEND_SRC_ALPHA,
        AGC_GFX1013_BLEND_ONE_MINUS_SRC_ALPHA,
        AGC_GFX1013_BLEND_DST_COLOR,
        AGC_GFX1013_BLEND_ONE_MINUS_DST_COLOR,
        AGC_GFX1013_BLEND_DST_ALPHA,
        AGC_GFX1013_BLEND_ONE_MINUS_DST_ALPHA,
        AGC_GFX1013_BLEND_CONSTANT_COLOR,
        AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_COLOR,
        AGC_GFX1013_BLEND_CONSTANT_ALPHA,
        AGC_GFX1013_BLEND_ONE_MINUS_CONSTANT_ALPHA,
        AGC_GFX1013_BLEND_SRC_ALPHA_SATURATE,
        AGC_GFX1013_BLEND_SRC1_COLOR,
        AGC_GFX1013_BLEND_ONE_MINUS_SRC1_COLOR,
        AGC_GFX1013_BLEND_SRC1_ALPHA,
        AGC_GFX1013_BLEND_ONE_MINUS_SRC1_ALPHA,
    };
    return table[factor];
}

static AgcGfx1013BlendOp agcPipelineBlendOperation(
    AgcBlendOperation operation)
{
    static const AgcGfx1013BlendOp table[AGC_BLEND_OPERATION_COUNT] = {
        AGC_GFX1013_BLEND_OP_ADD,
        AGC_GFX1013_BLEND_OP_SUBTRACT,
        AGC_GFX1013_BLEND_OP_REVERSE_SUBTRACT,
        AGC_GFX1013_BLEND_OP_MIN,
        AGC_GFX1013_BLEND_OP_MAX,
    };
    return table[operation];
}

static uint32_t agcPipelineCopyStaticShRegisters(AgcShader shader,
    AgcRegisterValue *destination, uint32_t capacity)
{
    const AgcRegisterValue *source =
        (const AgcRegisterValue *)(uintptr_t)shader->record.sh_registers;
    uint32_t count = 0u;
    uint32_t i;

    for (i = 0u; i < shader->record.num_sh_registers; ++i) {
        uint32_t j;
        int dynamic = 0;
        for (j = 0u; j < shader->reflection.user_sgpr_count; ++j) {
            const AgcShaderUserSgpr *sgpr = &shader->reflection.user_sgprs[j];
            if (source[i].offset >= sgpr->register_offset &&
                source[i].offset < sgpr->register_offset +
                    sgpr->dword_count) {
                dynamic = 1;
                break;
            }
        }
        if (!dynamic && count < capacity)
            destination[count++] = source[i];
    }
    return count;
}

static int32_t agcBuildGraphicsPipelineBind(
    AgcGraphicsPipeline pipeline)
{
    AgcGfx1013Wave32VsPsState shader_state = {0};
    AgcShaderRecord primitive_record = pipeline->primitive_shader->record;
    AgcShaderRecord pixel_record = pipeline->pixel_shader->record;
    AgcRegisterValue primitive_sh[UINT8_MAX];
    AgcRegisterValue pixel_sh[UINT8_MAX];
    AgcGfx1013ColorBlendState blend_state = {0};
    AgcGfx1013DepthStencilState depth_state = {0};
    AgcGfx1013SampleState sample_state;
    AgcRegisterValue raster_register;
    SceAgcCb cb;
    uint32_t i;
    int32_t result;

    shader_state.primitive.record = &primitive_record;
    shader_state.primitive.sh_registers = primitive_sh;
    shader_state.primitive.num_sh_registers =
        agcPipelineCopyStaticShRegisters(pipeline->primitive_shader,
            primitive_sh, UINT8_MAX);
    primitive_record.num_sh_registers =
        (uint8_t)shader_state.primitive.num_sh_registers;
    shader_state.primitive.cx_registers =
        (const AgcRegisterValue *)(uintptr_t)
            pipeline->primitive_shader->record.cx_registers;
    shader_state.primitive.num_cx_registers =
        pipeline->primitive_shader->record.num_cx_registers;
    shader_state.primitive.code_address =
        pipeline->primitive_shader->program_gpu_address;
    shader_state.pixel.record = &pixel_record;
    shader_state.pixel.sh_registers = pixel_sh;
    shader_state.pixel.num_sh_registers =
        agcPipelineCopyStaticShRegisters(pipeline->pixel_shader,
            pixel_sh, UINT8_MAX);
    pixel_record.num_sh_registers =
        (uint8_t)shader_state.pixel.num_sh_registers;
    shader_state.pixel.cx_registers =
        (const AgcRegisterValue *)(uintptr_t)
            pipeline->pixel_shader->record.cx_registers;
    shader_state.pixel.num_cx_registers =
        pipeline->pixel_shader->record.num_cx_registers;
    shader_state.pixel.code_address =
        pipeline->pixel_shader->program_gpu_address;
    shader_state.primitive_back_code_address =
        pipeline->primitive_shader->front_program_gpu_address;
    shader_state.primitive_type = 4u;

    agcCbInit(&cb, pipeline->bind_words, sizeof(pipeline->bind_words));
    result = agcGfx1013BindVsPs(&cb, &shader_state);
    if (result != AGC_OK)
        return result;
    if (pipeline->color_attachment_count != 0u) {
        blend_state.target_count = pipeline->color_attachment_count;
        for (i = 0u; i < pipeline->color_attachment_count; ++i) {
            const AgcColorBlendAttachmentState *source =
                &pipeline->color_attachments[i];
            AgcGfx1013ColorBlendTargetState *target =
                &blend_state.targets[i];
            target->enable = source->blend_enable;
            target->color_source = agcPipelineBlendFactor(
                source->source_color_factor);
            target->color_destination = agcPipelineBlendFactor(
                source->destination_color_factor);
            target->color_operation = agcPipelineBlendOperation(
                source->color_operation);
            target->separate_alpha = 1u;
            target->alpha_source = agcPipelineBlendFactor(
                source->source_alpha_factor);
            target->alpha_destination = agcPipelineBlendFactor(
                source->destination_alpha_factor);
            target->alpha_operation = agcPipelineBlendOperation(
                source->alpha_operation);
            target->write_mask = source->write_mask;
        }
        result = agcGfx1013SetColorBlendState(&cb, &blend_state);
        if (result != AGC_OK)
            return result;
    }
    if (pipeline->depth_stencil.format != AGC_FORMAT_UNDEFINED) {
        depth_state.depth_test_enable =
            pipeline->depth_stencil.depth_test_enable;
        depth_state.depth_write_enable =
            pipeline->depth_stencil.depth_write_enable;
        depth_state.depth_compare_operation =
            (AgcGfx1013CompareOp)
                pipeline->depth_stencil.depth_compare_operation;
        depth_state.depth_bounds_enable =
            pipeline->depth_stencil.depth_bounds_enable;
        depth_state.min_depth_bounds =
            pipeline->depth_stencil.min_depth_bounds;
        depth_state.max_depth_bounds =
            pipeline->depth_stencil.max_depth_bounds;
        depth_state.stencil_test_enable =
            pipeline->depth_stencil.stencil_test_enable;
        depth_state.back_face_enable =
            pipeline->depth_stencil.back_face_enable;
        agcPipelineStencilFace(
            &pipeline->depth_stencil.front, &depth_state.front);
        agcPipelineStencilFace(
            &pipeline->depth_stencil.back, &depth_state.back);
        result = agcGfx1013SetDepthStencilState(&cb, &depth_state);
    } else {
        result = agcGfx1013SetDepthDisabled(&cb);
    }
    if (result != AGC_OK)
        return result;
    sample_state.sample_count = pipeline->multisample.rasterization_samples;
    sample_state.pixel_shader_sample_count =
        pipeline->pixel_shader->reflection.pixel_shader_sample_count == 0u ?
            1u : pipeline->pixel_shader->reflection.pixel_shader_sample_count;
    sample_state.sample_mask =
        (1u << pipeline->multisample.rasterization_samples) - 1u;
    result = agcGfx1013SetSampleState(&cb, &sample_state);
    if (result != AGC_OK)
        return result;
    raster_register.offset = AGC_REG_PA_SU_SC_MODE_CNTL;
    raster_register.value =
        ((pipeline->rasterization.cull_mode & AGC_CULL_MODE_FRONT_BIT) ?
            1u << AGC_REG_PA_SU_SC_MODE_CNTL_CULL_FRONT_SHIFT : 0u) |
        ((pipeline->rasterization.cull_mode & AGC_CULL_MODE_BACK_BIT) ?
            1u << AGC_REG_PA_SU_SC_MODE_CNTL_CULL_BACK_SHIFT : 0u) |
        ((uint32_t)pipeline->rasterization.front_face <<
            AGC_REG_PA_SU_SC_MODE_CNTL_FACE_SHIFT);
    if (pipeline->rasterization.polygon_mode != AGC_POLYGON_MODE_FILL) {
        uint32_t primitive_type =
            pipeline->rasterization.polygon_mode == AGC_POLYGON_MODE_LINE ?
                1u : 0u;
        raster_register.value |=
            1u << AGC_REG_PA_SU_SC_MODE_CNTL_POLY_MODE_SHIFT;
        raster_register.value |= primitive_type <<
            AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_FRONT_PTYPE_SHIFT;
        raster_register.value |= primitive_type <<
            AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_BACK_PTYPE_SHIFT;
    }
    if (pipeline->rasterization.depth_bias_enable)
        raster_register.value |= AGC_GFX1013_DEPTH_BIAS_RASTER_MODE;
    if (!sceAgcCbSetCxRegistersDirect(&cb, &raster_register, 1u))
        return AGC_ERROR_INTERNAL;
    pipeline->bind_dword_count = (uint32_t)agcCbUsedDwords(&cb);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateGraphicsPipeline(AgcDevice device,
    const AgcGraphicsPipelineDesc *desc, AgcGraphicsPipeline *pipeline_out)
{
    AgcGraphicsPipeline pipeline;
    AgcShader primitive;
    AgcShader ps;
    AgcRasterizationState rasterization = AGC_RASTERIZATION_STATE_INIT;
    AgcDepthStencilPipelineState depth_stencil =
        AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT;
    AgcMultisampleState multisample = AGC_MULTISAMPLE_STATE_INIT;
    AgcRuntimePipelineResourceLayout resource_layout;
    const uint32_t known_dynamic_states =
        AGC_DYNAMIC_STATE_VIEWPORT_BIT | AGC_DYNAMIC_STATE_SCISSOR_BIT |
        AGC_DYNAMIC_STATE_BLEND_CONSTANTS_BIT |
        AGC_DYNAMIC_STATE_STENCIL_REFERENCE_BIT |
        AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT;
    uint32_t i;
    uint32_t j;
    int32_t result;

    if (!pipeline_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *pipeline_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        desc->struct_size != sizeof(*desc) ||
        desc->version != AGC_RUNTIME_STRUCTURE_VERSION_2 ||
        desc->flags != 0u || desc->reserved0 != 0u ||
        !agcReservedZero(desc->reserved, 4u) ||
        desc->reserved1 != 0u || !agcReservedZero(desc->reserved2, 4u) ||
        desc->vertex_input_count > AGC_SHADER_MAX_VERTEX_INPUTS ||
        desc->descriptor_mapping_count >
            AGC_SHADER_MAX_DESCRIPTOR_BINDINGS ||
        desc->push_constant_range_count >
            AGC_SHADER_MAX_PUSH_CONSTANT_RANGES ||
        desc->color_attachment_count > AGC_SHADER_MAX_COLOR_EXPORTS ||
        (desc->vertex_input_count != 0u && !desc->vertex_inputs) ||
        (desc->descriptor_mapping_count != 0u &&
         !desc->descriptor_mappings) ||
        (desc->push_constant_range_count != 0u &&
         !desc->push_constant_ranges) ||
        (desc->color_attachment_count != 0u &&
         !desc->color_attachments) ||
        (desc->dynamic_state_mask & ~known_dynamic_states) != 0u) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (desc->tessellation_control_shader ||
        desc->tessellation_evaluation_shader)
        return AGC_ERROR_NOT_SUPPORTED;
    if (desc->geometry_shader) {
        if (desc->vertex_shader)
            return AGC_ERROR_VALIDATION_FAILED;
        primitive = desc->geometry_shader;
    } else {
        primitive = desc->vertex_shader;
    }
    ps = desc->pixel_shader;
    if (!primitive || !ps || primitive->magic != AGC_MAGIC_SHADER ||
        ps->magic != AGC_MAGIC_SHADER || primitive->device != device ||
        ps->device != device ||
        primitive->stage != (desc->geometry_shader ?
            kAgcShaderStageGs : kAgcShaderStageVs) ||
        ps->stage != kAgcShaderStagePs) {
        return AGC_ERROR_SHADER_INVALID_TYPE;
    }
    if (!primitive->has_reflection || !ps->has_reflection)
        return AGC_ERROR_SHADER_INVALID;
    if (desc->geometry_shader &&
        (primitive->reflection.version != AGC_SHADER_REFLECTION_VERSION_2 ||
         primitive->reflection.front_stage != kAgcShaderStageVs ||
         !primitive->has_front_record ||
         (primitive->reflection.flags &
          (AGC_SHADER_REFLECTION_NGG_BIT |
           AGC_SHADER_REFLECTION_FUSED_STAGE_BIT)) !=
             (AGC_SHADER_REFLECTION_NGG_BIT |
              AGC_SHADER_REFLECTION_FUSED_STAGE_BIT) ||
         primitive->reflection.geometry_input_primitive !=
             AGC_SHADER_PRIMITIVE_TRIANGLES ||
         primitive->reflection.geometry_vertices_in != 3u ||
         primitive->reflection.geometry_invocations != 1u)) {
        return AGC_ERROR_NOT_SUPPORTED;
    }
    result = agcPipelineValidateShaderUserData(&primitive->reflection);
    if (result != AGC_OK)
        return result;
    result = agcPipelineValidateShaderUserData(&ps->reflection);
    if (result != AGC_OK)
        return result;
    if ((primitive->reflection.flags & AGC_SHADER_REFLECTION_NGG_BIT) == 0u ||
        ps->reflection.wave_size != 32u)
        return AGC_ERROR_NOT_SUPPORTED;
    if ((ps->reflection.stage_input_mask &
         ~primitive->reflection.stage_output_mask) != 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    if (desc->vertex_input_count != primitive->reflection.vertex_input_count)
        return AGC_ERROR_VALIDATION_FAILED;
    for (i = 0u; i < desc->vertex_input_count; ++i) {
        for (j = 0u; j < primitive->reflection.vertex_input_count; ++j) {
            if (agcPipelineVertexInputEqual(
                    &desc->vertex_inputs[i],
                    &primitive->reflection.vertex_inputs[j]))
                break;
        }
        if (j == primitive->reflection.vertex_input_count)
            return AGC_ERROR_VALIDATION_FAILED;
    }
    if (!agcPipelineReflectionDescriptorsMatch(&primitive->reflection,
            desc->descriptor_mappings, desc->descriptor_mapping_count) ||
        !agcPipelineReflectionDescriptorsMatch(&ps->reflection,
            desc->descriptor_mappings, desc->descriptor_mapping_count) ||
        !agcPipelineReflectionPushRangesMatch(&primitive->reflection,
            desc->push_constant_ranges,
            desc->push_constant_range_count) ||
        !agcPipelineReflectionPushRangesMatch(&ps->reflection,
            desc->push_constant_ranges,
            desc->push_constant_range_count)) {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    for (i = 0u; i < desc->descriptor_mapping_count; ++i) {
        const AgcShaderDescriptorMapping *mapping =
            &desc->descriptor_mappings[i];
        if (mapping->set >= 8u ||
            mapping->type >= AGC_SHADER_DESCRIPTOR_TYPE_COUNT ||
            mapping->array_size == 0u || mapping->byte_stride == 0u ||
            (mapping->byte_offset & 3u) != 0u ||
            (mapping->byte_stride & 3u) != 0u ||
            !agcPipelineLayoutEntryUsed(
                &primitive->reflection, &ps->reflection, mapping)) {
            return AGC_ERROR_VALIDATION_FAILED;
        }
        for (j = 0u; j < i; ++j) {
            if (desc->descriptor_mappings[j].set == mapping->set &&
                desc->descriptor_mappings[j].binding == mapping->binding)
                return AGC_ERROR_VALIDATION_FAILED;
        }
    }
    for (i = 0u; i < desc->push_constant_range_count; ++i) {
        const AgcShaderPushConstantRange *range =
            &desc->push_constant_ranges[i];
        if (range->size == 0u || range->alignment != 4u ||
            (range->offset & 3u) != 0u || (range->size & 3u) != 0u ||
            (range->stage_mask & ~((1u << kAgcShaderStageCount) - 1u)) != 0u ||
            !agcPipelinePushEntryUsed(
                &primitive->reflection, &ps->reflection, range)) {
            return AGC_ERROR_VALIDATION_FAILED;
        }
    }
    if (desc->color_attachment_count != ps->reflection.color_export_count)
        return AGC_ERROR_VALIDATION_FAILED;
    for (i = 0u; i < desc->color_attachment_count; ++i) {
        const AgcColorBlendAttachmentState *attachment =
            &desc->color_attachments[i];
        const AgcShaderColorExport *export_info =
            &ps->reflection.color_exports[i];
        if (!agcPipelineColorStateValid(attachment) ||
            !agcPipelineColorExportCompatible(export_info, attachment) ||
            (attachment->write_mask & ~export_info->write_mask) != 0u ||
            (attachment->blend_enable &&
             export_info->component_class !=
                AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED)) {
            return AGC_ERROR_VALIDATION_FAILED;
        }
    }
    if (desc->rasterization) {
        if (!agcPipelineRasterizationStateValid(desc->rasterization))
            return AGC_ERROR_INVALID_ARGUMENT;
        rasterization = *desc->rasterization;
    }
    if (rasterization.depth_clamp_enable ||
        rasterization.rasterizer_discard_enable ||
        rasterization.line_width != 1.0f)
        return AGC_ERROR_NOT_SUPPORTED;
    if (rasterization.depth_bias_enable !=
        ((desc->dynamic_state_mask & AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT) != 0u))
        return AGC_ERROR_VALIDATION_FAILED;
    if (desc->depth_stencil) {
        result = agcPipelineNormalizeDepthState(
            desc->depth_stencil, &depth_stencil);
        if (result != AGC_OK)
            return result;
    }
    if ((ps->reflection.flags & AGC_SHADER_REFLECTION_WRITES_DEPTH_BIT) != 0u &&
        depth_stencil.format == AGC_FORMAT_UNDEFINED)
        return AGC_ERROR_VALIDATION_FAILED;
    if ((ps->reflection.flags & AGC_SHADER_REFLECTION_WRITES_STENCIL_BIT) != 0u) {
        AgcShaderComponentClass component_class;
        uint32_t component_bits;
        uint32_t has_depth;
        uint32_t has_stencil;
        if (!agcPipelineFormatInfo(depth_stencil.format, &component_class,
                &component_bits, &has_depth, &has_stencil) || !has_stencil)
            return AGC_ERROR_VALIDATION_FAILED;
    }
    if (desc->multisample) {
        if (!agcPipelineMultisampleStateValid(desc->multisample))
            return AGC_ERROR_INVALID_ARGUMENT;
        multisample = *desc->multisample;
    }
    if (multisample.alpha_to_coverage_enable ||
        multisample.alpha_to_one_enable)
        return AGC_ERROR_NOT_SUPPORTED;
    if (ps->reflection.pixel_shader_sample_count != 0u &&
        ps->reflection.pixel_shader_sample_count >
            multisample.rasterization_samples)
        return AGC_ERROR_VALIDATION_FAILED;
    if (((ps->reflection.flags &
          AGC_SHADER_REFLECTION_USES_SAMPLE_SHADING_BIT) != 0u) !=
        (multisample.sample_shading_enable != 0u))
        return AGC_ERROR_VALIDATION_FAILED;
    if (multisample.sample_shading_enable &&
        (float)ps->reflection.pixel_shader_sample_count <
            multisample.minimum_sample_shading *
                (float)multisample.rasterization_samples)
        return AGC_ERROR_VALIDATION_FAILED;
    result = agcPipelineBuildResourceLayout(desc->descriptor_mappings,
        desc->descriptor_mapping_count, desc->vertex_inputs,
        desc->vertex_input_count,
        primitive->reflection.push_constant_size >
            ps->reflection.push_constant_size ?
            primitive->reflection.push_constant_size :
            ps->reflection.push_constant_size,
        agcPipelineUsesIndirectDescriptorSets(&primitive->reflection) ||
            agcPipelineUsesIndirectDescriptorSets(&ps->reflection),
        &resource_layout);
    if (result != AGC_OK)
        return result;
    pipeline = agcCreateChild(device, sizeof(*pipeline));
    if (!pipeline)
        return AGC_ERROR_OUT_OF_MEMORY;
    pipeline->magic = AGC_MAGIC_GRAPHICS_PIPELINE;
    pipeline->device = device;
    pipeline->primitive_shader = primitive;
    pipeline->pixel_shader = ps;
    pipeline->descriptor_mapping_count = desc->descriptor_mapping_count;
    pipeline->push_constant_range_count = desc->push_constant_range_count;
    pipeline->vertex_input_count = desc->vertex_input_count;
    pipeline->color_attachment_count = desc->color_attachment_count;
    if (desc->descriptor_mapping_count != 0u)
        memcpy(pipeline->descriptor_mappings, desc->descriptor_mappings,
            desc->descriptor_mapping_count * sizeof(*desc->descriptor_mappings));
    if (desc->push_constant_range_count != 0u)
        memcpy(pipeline->push_constant_ranges, desc->push_constant_ranges,
            desc->push_constant_range_count *
                sizeof(*desc->push_constant_ranges));
    if (desc->vertex_input_count != 0u)
        memcpy(pipeline->vertex_inputs, desc->vertex_inputs,
            desc->vertex_input_count * sizeof(*desc->vertex_inputs));
    if (desc->color_attachment_count != 0u)
        memcpy(pipeline->color_attachments, desc->color_attachments,
            desc->color_attachment_count * sizeof(*desc->color_attachments));
    pipeline->rasterization = rasterization;
    pipeline->depth_stencil = depth_stencil;
    pipeline->multisample = multisample;
    pipeline->dynamic_state_mask = desc->dynamic_state_mask;
    pipeline->resource_layout = resource_layout;
    pipeline->resource_layout_requires_bindings =
        desc->descriptor_mapping_count != 0u ||
        desc->push_constant_range_count != 0u;
    result = agcBuildGraphicsPipelineBind(pipeline);
    if (result != AGC_OK) {
        pipeline->magic = 0u;
        agcDestroyChild(device, pipeline);
        return result;
    }
    primitive->dependency_refs++;
    ps->dependency_refs++;
    *pipeline_out = pipeline;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyGraphicsPipeline(AgcGraphicsPipeline pipeline)
{
    AgcDevice device;

    if (!pipeline || pipeline->magic != AGC_MAGIC_GRAPHICS_PIPELINE ||
        !agcDeviceValid(pipeline->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (pipeline->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    device = pipeline->device;
    pipeline->primitive_shader->dependency_refs--;
    pipeline->pixel_shader->dependency_refs--;
    pipeline->magic = 0u;
    agcDestroyChild(device, pipeline);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateComputePipeline(AgcDevice device,
    const AgcComputePipelineDesc *desc, AgcComputePipeline *pipeline_out)
{
    AgcComputePipeline pipeline;
    AgcShader shader;
    uint64_t invocations;
    uint32_t i;
    uint32_t j;
    AgcRuntimePipelineResourceLayout resource_layout;
    int32_t result;

    if (!pipeline_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *pipeline_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        desc->struct_size != sizeof(*desc) ||
        desc->version != AGC_RUNTIME_STRUCTURE_VERSION_2 ||
        desc->flags != 0u || !agcReservedZero(desc->reserved, 4u) ||
        !agcReservedZero(desc->reserved2, 4u) ||
        desc->local_size_x == 0u || desc->local_size_y == 0u ||
        desc->local_size_z == 0u ||
        desc->descriptor_mapping_count >
            AGC_SHADER_MAX_DESCRIPTOR_BINDINGS ||
        desc->push_constant_range_count >
            AGC_SHADER_MAX_PUSH_CONSTANT_RANGES ||
        (desc->descriptor_mapping_count != 0u &&
         !desc->descriptor_mappings) ||
        (desc->push_constant_range_count != 0u &&
         !desc->push_constant_ranges)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    shader = desc->shader;
    if (!shader || shader->magic != AGC_MAGIC_SHADER ||
        shader->device != device || shader->stage != kAgcShaderStageCs) {
        return AGC_ERROR_SHADER_INVALID_TYPE;
    }
    if (!shader->has_reflection)
        return AGC_ERROR_SHADER_INVALID;
    result = agcPipelineValidateShaderUserData(&shader->reflection);
    if (result != AGC_OK)
        return result;
    if (shader->reflection.wave_size != 32u ||
        shader->reflection.scratch_bytes_per_wave != 0u ||
        shader->reflection.lds_size > 65536u)
        return AGC_ERROR_NOT_SUPPORTED;
    if (desc->local_size_x != shader->reflection.local_size_x ||
        desc->local_size_y != shader->reflection.local_size_y ||
        desc->local_size_z != shader->reflection.local_size_z ||
        desc->descriptor_mapping_count !=
            shader->reflection.descriptor_mapping_count ||
        desc->push_constant_range_count !=
            shader->reflection.push_constant_range_count ||
        !agcPipelineReflectionDescriptorsMatch(&shader->reflection,
            desc->descriptor_mappings, desc->descriptor_mapping_count) ||
        !agcPipelineReflectionPushRangesMatch(&shader->reflection,
            desc->push_constant_ranges,
            desc->push_constant_range_count)) {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    for (i = 0u; i < desc->descriptor_mapping_count; ++i) {
        const AgcShaderDescriptorMapping *mapping =
            &desc->descriptor_mappings[i];
        if (mapping->set >= 8u ||
            mapping->type >= AGC_SHADER_DESCRIPTOR_TYPE_COUNT ||
            mapping->array_size == 0u || mapping->byte_stride == 0u ||
            (mapping->byte_offset & 3u) != 0u ||
            (mapping->byte_stride & 3u) != 0u) {
            return AGC_ERROR_VALIDATION_FAILED;
        }
        for (j = 0u; j < i; ++j) {
            if (desc->descriptor_mappings[j].set == mapping->set &&
                desc->descriptor_mappings[j].binding == mapping->binding)
                return AGC_ERROR_VALIDATION_FAILED;
        }
    }
    for (i = 0u; i < desc->push_constant_range_count; ++i) {
        const AgcShaderPushConstantRange *range =
            &desc->push_constant_ranges[i];
        if (range->size == 0u || range->alignment != 4u ||
            (range->offset & 3u) != 0u || (range->size & 3u) != 0u ||
            (range->stage_mask & (1u << kAgcShaderStageCs)) == 0u)
            return AGC_ERROR_VALIDATION_FAILED;
    }
    invocations = (uint64_t)desc->local_size_x * desc->local_size_y *
        desc->local_size_z;
    if (invocations > 1024u)
        return AGC_ERROR_NOT_SUPPORTED;
    result = agcPipelineBuildResourceLayout(desc->descriptor_mappings,
        desc->descriptor_mapping_count, NULL, 0u,
        shader->reflection.push_constant_size,
        agcPipelineUsesIndirectDescriptorSets(&shader->reflection),
        &resource_layout);
    if (result != AGC_OK)
        return result;
    pipeline = agcCreateChild(device, sizeof(*pipeline));
    if (!pipeline)
        return AGC_ERROR_OUT_OF_MEMORY;
    pipeline->magic = AGC_MAGIC_COMPUTE_PIPELINE;
    pipeline->device = device;
    pipeline->shader = shader;
    pipeline->local_size[0] = desc->local_size_x;
    pipeline->local_size[1] = desc->local_size_y;
    pipeline->local_size[2] = desc->local_size_z;
    pipeline->descriptor_mapping_count = desc->descriptor_mapping_count;
    pipeline->push_constant_range_count = desc->push_constant_range_count;
    if (desc->descriptor_mapping_count != 0u)
        memcpy(pipeline->descriptor_mappings, desc->descriptor_mappings,
            desc->descriptor_mapping_count * sizeof(*desc->descriptor_mappings));
    if (desc->push_constant_range_count != 0u)
        memcpy(pipeline->push_constant_ranges, desc->push_constant_ranges,
            desc->push_constant_range_count *
                sizeof(*desc->push_constant_ranges));
    pipeline->resource_layout_requires_bindings =
        desc->descriptor_mapping_count != 0u ||
        desc->push_constant_range_count != 0u;
    pipeline->resource_layout = resource_layout;
    shader->dependency_refs++;
    *pipeline_out = pipeline;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyComputePipeline(AgcComputePipeline pipeline)
{
    AgcDevice device;

    if (!pipeline || pipeline->magic != AGC_MAGIC_COMPUTE_PIPELINE ||
        !agcDeviceValid(pipeline->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (pipeline->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    device = pipeline->device;
    pipeline->shader->dependency_refs--;
    pipeline->magic = 0u;
    agcDestroyChild(device, pipeline);
    return AGC_OK;
}

static void agcReleaseCommandReferences(AgcCommandBuffer command_buffer)
{
    uint32_t i;

    if (command_buffer->graphics_pipeline) {
        command_buffer->graphics_pipeline->recorded_refs--;
        command_buffer->graphics_pipeline = NULL;
    }
    if (command_buffer->compute_pipeline) {
        command_buffer->compute_pipeline->recorded_refs--;
        command_buffer->compute_pipeline = NULL;
    }
    if (command_buffer->index_buffer) {
        command_buffer->index_buffer->recorded_refs--;
        command_buffer->index_buffer = NULL;
    }
    for (i = 0u; i < command_buffer->recorded_buffer_count; ++i)
        command_buffer->recorded_buffers[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_view_count; ++i)
        command_buffer->recorded_views[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_sampler_count; ++i)
        command_buffer->recorded_samplers[i]->recorded_refs--;
    command_buffer->recorded_buffer_count = 0u;
    command_buffer->recorded_view_count = 0u;
    command_buffer->recorded_sampler_count = 0u;
    command_buffer->descriptors_bound = 0u;
    command_buffer->vertex_binding_mask = 0u;
    command_buffer->dynamic_state_set_mask = 0u;
    memset(command_buffer->push_constant_masks, 0,
        sizeof(command_buffer->push_constant_masks));
    if (command_buffer->resource_allocation) {
        agcRuntimeFree(command_buffer->device,
            command_buffer->resource_allocation);
        command_buffer->resource_allocation = NULL;
    }
}

static int32_t agcPrepareCommandResources(AgcCommandBuffer command_buffer,
    const AgcRuntimePipelineResourceLayout *layout)
{
    AgcRuntimeAllocation *allocation = NULL;
    uint8_t *cpu_base;
    uint64_t gpu_base;
    int32_t result;

    if (layout->total_size == 0u)
        return AGC_OK;
    if (command_buffer->resource_allocation)
        return AGC_OK;
    result = agcRuntimeAllocate(command_buffer->device,
        AGC_MEMORY_HEAP_FLEXIBLE, layout->total_size, 256u, 0u,
        AGC_OBJECT_TYPE_COMMAND_BUFFER, command_buffer, &allocation);
    if (result != AGC_OK)
        return result;
    cpu_base = agcAllocationCpuAddress(allocation);
    gpu_base = agcAllocationGpuAddress(allocation);
    memset(cpu_base, 0, (size_t)layout->total_size);
    if (layout->uses_indirect_set_table) {
        uint32_t *set_addresses = (uint32_t *)(void *)(cpu_base +
            layout->indirect_set_table_offset);
        uint32_t i;
        for (i = 0u; i < 8u; ++i) {
            if ((layout->set_mask & (1u << i)) != 0u)
                set_addresses[i] = (uint32_t)(gpu_base +
                    layout->set_offsets[i]);
        }
        result = agcFlushRuntimeAllocation(allocation,
            layout->indirect_set_table_offset,
            8u * sizeof(*set_addresses));
        if (result != AGC_OK) {
            agcRuntimeFree(command_buffer->device, allocation);
            return result;
        }
    }
    command_buffer->resource_allocation = allocation;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateCommandBuffer(AgcDevice device,
    const AgcCommandBufferDesc *desc, AgcCommandBuffer *command_buffer_out)
{
    AgcCommandBuffer command_buffer;
    size_t storage_size;
    int32_t result;

    if (!command_buffer_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *command_buffer_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        (desc->queue_type != kAgcQueueGraphics &&
         desc->queue_type != kAgcQueueCompute) ||
        desc->capacity_dwords == 0u ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    storage_size = (size_t)desc->capacity_dwords * sizeof(uint32_t);
    if (storage_size / sizeof(uint32_t) != desc->capacity_dwords)
        return AGC_ERROR_INVALID_ARGUMENT;
    command_buffer = agcCreateChild(device, sizeof(*command_buffer));
    if (!command_buffer)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        storage_size, 256u, 0u, AGC_OBJECT_TYPE_COMMAND_BUFFER,
        command_buffer, &command_buffer->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, command_buffer);
        return result;
    }
    command_buffer->storage = agcAllocationCpuAddress(command_buffer->allocation);
    command_buffer->magic = AGC_MAGIC_COMMAND_BUFFER;
    command_buffer->device = device;
    command_buffer->queue_type = desc->queue_type;
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_INITIAL;
    command_buffer->capacity_dwords = desc->capacity_dwords;
    agcCbInit(&command_buffer->cursor, command_buffer->storage, storage_size);
    *command_buffer_out = command_buffer;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyCommandBuffer(AgcCommandBuffer command_buffer)
{
    AgcDevice device;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state == AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->state == AGC_COMMAND_BUFFER_STATE_PENDING ||
        command_buffer->pending_refs != 0u) {
        return AGC_ERROR_BUSY;
    }
    device = command_buffer->device;
    agcReleaseCommandReferences(command_buffer);
    agcRuntimeFree(device, command_buffer->allocation);
    command_buffer->magic = 0u;
    agcDestroyChild(device, command_buffer);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcBeginCommandBuffer(AgcCommandBuffer command_buffer)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_INITIAL)
        return AGC_ERROR_INVALID_STATE;
    agcCbReset(&command_buffer->cursor, command_buffer->storage,
        (size_t)command_buffer->capacity_dwords * sizeof(uint32_t));
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_RECORDING;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcEndCommandBuffer(AgcCommandBuffer command_buffer)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING)
        return AGC_ERROR_INVALID_STATE;
    if (agcCbUsedDwords(&command_buffer->cursor) == 0u)
        return AGC_ERROR_INVALID_STATE;
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcResetCommandBuffer(AgcCommandBuffer command_buffer)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state == AGC_COMMAND_BUFFER_STATE_PENDING) {
        return AGC_ERROR_INVALID_STATE;
    }
    agcReleaseCommandReferences(command_buffer);
    agcCbReset(&command_buffer->cursor, command_buffer->storage,
        (size_t)command_buffer->capacity_dwords * sizeof(uint32_t));
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_INITIAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetCommandBufferState(
    AgcCommandBuffer command_buffer, AgcCommandBufferState *state_out)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !state_out) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    *state_out = command_buffer->state;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBindGraphicsPipeline(
    AgcCommandBuffer command_buffer, AgcGraphicsPipeline pipeline)
{
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !pipeline || pipeline->magic != AGC_MAGIC_GRAPHICS_PIPELINE ||
        !agcDeviceValid(command_buffer->device) ||
        pipeline->device != command_buffer->device) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics)
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->graphics_pipeline &&
        command_buffer->graphics_pipeline != pipeline)
        return AGC_ERROR_NOT_SUPPORTED;
    if (!command_buffer->graphics_pipeline) {
        uint32_t *commands;
        if (agcCbRemainingDwords(&command_buffer->cursor) <
            pipeline->bind_dword_count)
            return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
        result = agcPrepareCommandResources(
            command_buffer, &pipeline->resource_layout);
        if (result != AGC_OK)
            return result;
        commands = agcCbAllocDwords(
            &command_buffer->cursor, pipeline->bind_dword_count);
        if (!commands)
            return AGC_ERROR_INTERNAL;
        memcpy(commands, pipeline->bind_words,
            pipeline->bind_dword_count * sizeof(uint32_t));
        command_buffer->graphics_pipeline = pipeline;
        pipeline->recorded_refs++;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBindComputePipeline(
    AgcCommandBuffer command_buffer, AgcComputePipeline pipeline)
{
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !pipeline || pipeline->magic != AGC_MAGIC_COMPUTE_PIPELINE ||
        !agcDeviceValid(command_buffer->device) ||
        pipeline->device != command_buffer->device) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueCompute)
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->compute_pipeline &&
        command_buffer->compute_pipeline != pipeline)
        return AGC_ERROR_NOT_SUPPORTED;
    if (!command_buffer->compute_pipeline) {
        result = agcPrepareCommandResources(
            command_buffer, &pipeline->resource_layout);
        if (result != AGC_OK)
            return result;
        command_buffer->compute_pipeline = pipeline;
        pipeline->recorded_refs++;
    }
    return AGC_OK;
}

typedef struct AgcRuntimeEncodedDescriptor {
    uint64_t destination_offset;
    uint32_t size;
    uint8_t bytes[sizeof(AgcGfx1013CombinedImageSamplerDescriptor)];
    AgcBuffer buffer;
    AgcImageView view;
    AgcSampler sampler;
} AgcRuntimeEncodedDescriptor;

static const AgcRuntimePipelineResourceLayout *agcCommandResourceLayout(
    AgcCommandBuffer command_buffer)
{
    if (command_buffer->graphics_pipeline)
        return &command_buffer->graphics_pipeline->resource_layout;
    if (command_buffer->compute_pipeline)
        return &command_buffer->compute_pipeline->resource_layout;
    return NULL;
}

static const AgcShaderDescriptorMapping *agcCommandDescriptorMappings(
    AgcCommandBuffer command_buffer, uint32_t *count)
{
    if (command_buffer->graphics_pipeline) {
        *count = command_buffer->graphics_pipeline->descriptor_mapping_count;
        return command_buffer->graphics_pipeline->descriptor_mappings;
    }
    if (command_buffer->compute_pipeline) {
        *count = command_buffer->compute_pipeline->descriptor_mapping_count;
        return command_buffer->compute_pipeline->descriptor_mappings;
    }
    *count = 0u;
    return NULL;
}

static const AgcShaderPushConstantRange *agcCommandPushRanges(
    AgcCommandBuffer command_buffer, uint32_t *count)
{
    if (command_buffer->graphics_pipeline) {
        *count = command_buffer->graphics_pipeline->push_constant_range_count;
        return command_buffer->graphics_pipeline->push_constant_ranges;
    }
    if (command_buffer->compute_pipeline) {
        *count = command_buffer->compute_pipeline->push_constant_range_count;
        return command_buffer->compute_pipeline->push_constant_ranges;
    }
    *count = 0u;
    return NULL;
}

static int agcCommandRetainBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer buffer)
{
    uint32_t i;
    for (i = 0u; i < command_buffer->recorded_buffer_count; ++i) {
        if (command_buffer->recorded_buffers[i] == buffer)
            return 1;
    }
    if (command_buffer->recorded_buffer_count >=
        AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return 0;
    command_buffer->recorded_buffers[
        command_buffer->recorded_buffer_count++] = buffer;
    buffer->recorded_refs++;
    return 1;
}

static int agcCommandRetainView(AgcCommandBuffer command_buffer,
    AgcImageView view)
{
    uint32_t i;
    for (i = 0u; i < command_buffer->recorded_view_count; ++i) {
        if (command_buffer->recorded_views[i] == view)
            return 1;
    }
    if (command_buffer->recorded_view_count >=
        AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return 0;
    command_buffer->recorded_views[
        command_buffer->recorded_view_count++] = view;
    view->recorded_refs++;
    return 1;
}

static int agcCommandRetainSampler(AgcCommandBuffer command_buffer,
    AgcSampler sampler)
{
    uint32_t i;
    for (i = 0u; i < command_buffer->recorded_sampler_count; ++i) {
        if (command_buffer->recorded_samplers[i] == sampler)
            return 1;
    }
    if (command_buffer->recorded_sampler_count >=
        AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return 0;
    command_buffer->recorded_samplers[
        command_buffer->recorded_sampler_count++] = sampler;
    sampler->recorded_refs++;
    return 1;
}

static int32_t agcCommandEncodeDescriptor(
    AgcCommandBuffer command_buffer, const AgcDescriptorWrite *write,
    const AgcShaderDescriptorMapping *mapping,
    AgcRuntimeEncodedDescriptor *encoded)
{
    uint32_t size = agcPipelineDescriptorSize(mapping->type);
    uint64_t address;
    uint64_t range;
    int32_t result;

    memset(encoded, 0, sizeof(*encoded));
    encoded->size = size;
    encoded->destination_offset =
        agcCommandResourceLayout(command_buffer)->set_offsets[mapping->set] +
        mapping->byte_offset +
        (uint64_t)write->array_element * mapping->byte_stride;
    switch (mapping->type) {
    case AGC_SHADER_DESCRIPTOR_SAMPLER:
        if (write->buffer || write->image_view || !write->sampler ||
            write->sampler->magic != AGC_MAGIC_SAMPLER ||
            write->sampler->device != command_buffer->device)
            return AGC_ERROR_RESOURCE_INVALID;
        memcpy(encoded->bytes,
            agcAllocationCpuAddress(write->sampler->allocation), size);
        encoded->sampler = write->sampler;
        return AGC_OK;
    case AGC_SHADER_DESCRIPTOR_SAMPLED_IMAGE:
    case AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE:
    case AGC_SHADER_DESCRIPTOR_INPUT_ATTACHMENT:
        if (write->buffer || write->sampler || !write->image_view ||
            write->image_view->magic != AGC_MAGIC_IMAGE_VIEW ||
            write->image_view->device != command_buffer->device)
            return AGC_ERROR_RESOURCE_INVALID;
        if ((mapping->type == AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE &&
             (write->image_view->image->desc.usage &
              AGC_IMAGE_USAGE_STORAGE_BIT) == 0u) ||
            (mapping->type != AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE &&
             (write->image_view->image->desc.usage &
              AGC_IMAGE_USAGE_SAMPLED_BIT) == 0u))
            return AGC_ERROR_RESOURCE_INVALID;
        memcpy(encoded->bytes,
            agcAllocationCpuAddress(write->image_view->allocation), size);
        encoded->view = write->image_view;
        return AGC_OK;
    case AGC_SHADER_DESCRIPTOR_COMBINED_IMAGE_SAMPLER:
        if (write->buffer || !write->image_view || !write->sampler ||
            write->image_view->magic != AGC_MAGIC_IMAGE_VIEW ||
            write->sampler->magic != AGC_MAGIC_SAMPLER ||
            write->image_view->device != command_buffer->device ||
            write->sampler->device != command_buffer->device ||
            (write->image_view->image->desc.usage &
             AGC_IMAGE_USAGE_SAMPLED_BIT) == 0u)
            return AGC_ERROR_RESOURCE_INVALID;
        memcpy(encoded->bytes,
            agcAllocationCpuAddress(write->image_view->allocation),
            sizeof(AgcGfx1013ImageDescriptor));
        memcpy(encoded->bytes + sizeof(AgcGfx1013ImageDescriptor),
            agcAllocationCpuAddress(write->sampler->allocation),
            sizeof(AgcSamplerDescriptor));
        encoded->view = write->image_view;
        encoded->sampler = write->sampler;
        return AGC_OK;
    case AGC_SHADER_DESCRIPTOR_UNIFORM_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_STORAGE_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER:
    case AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER:
        if (!write->buffer || write->image_view || write->sampler ||
            write->buffer->magic != AGC_MAGIC_BUFFER ||
            write->buffer->device != command_buffer->device ||
            write->buffer->deferred ||
            write->buffer_offset >= write->buffer->size)
            return AGC_ERROR_RESOURCE_INVALID;
        if ((mapping->type == AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER ||
             mapping->type ==
                AGC_SHADER_DESCRIPTOR_UNIFORM_TEXEL_BUFFER) ?
            (write->buffer->usage & AGC_BUFFER_USAGE_UNIFORM_BIT) == 0u :
            (write->buffer->usage & AGC_BUFFER_USAGE_STORAGE_BIT) == 0u)
            return AGC_ERROR_RESOURCE_INVALID;
        range = write->buffer_range == 0u ?
            write->buffer->size - write->buffer_offset :
            write->buffer_range;
        if (range == 0u || range >
            write->buffer->size - write->buffer_offset ||
            range > UINT32_MAX)
            return AGC_ERROR_RESOURCE_INVALID;
        address = agcAllocationGpuAddress(write->buffer->allocation) +
            write->buffer_offset;
        if (write->buffer_stride != 0u) {
            if ((range % write->buffer_stride) != 0u ||
                range / write->buffer_stride > UINT32_MAX)
                return AGC_ERROR_RESOURCE_INVALID;
            result = agcGfx1013BufferDescriptorEncode(
                (AgcGfx1013BufferDescriptor *)encoded->bytes, address,
                write->buffer_stride,
                (uint32_t)(range / write->buffer_stride));
        } else {
            result = agcGfx1013RawBufferDescriptorEncode(
                (AgcGfx1013BufferDescriptor *)encoded->bytes, address,
                (uint32_t)range);
        }
        if (result != AGC_OK)
            return result;
        encoded->buffer = write->buffer;
        return AGC_OK;
    default:
        return AGC_ERROR_VALIDATION_FAILED;
    }
}

int32_t PS5_SYSV_ABI agcCmdBindDescriptors(AgcCommandBuffer command_buffer,
    uint32_t write_count, const AgcDescriptorWrite *writes)
{
    AgcRuntimeEncodedDescriptor encoded[AGC_RUNTIME_MAX_DESCRIPTOR_WRITES];
    const AgcShaderDescriptorMapping *mappings;
    const AgcRuntimePipelineResourceLayout *layout;
    uint32_t mapping_count;
    uint32_t i;
    uint32_t j;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !writes ||
        write_count == 0u ||
        write_count > AGC_RUNTIME_MAX_DESCRIPTOR_WRITES)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        (!command_buffer->graphics_pipeline &&
         !command_buffer->compute_pipeline))
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->descriptors_bound)
        return AGC_ERROR_NOT_SUPPORTED;
    layout = agcCommandResourceLayout(command_buffer);
    mappings = agcCommandDescriptorMappings(command_buffer, &mapping_count);
    if (!layout || write_count != layout->descriptor_element_count ||
        mapping_count == 0u || !command_buffer->resource_allocation)
        return AGC_ERROR_VALIDATION_FAILED;
    for (i = 0u; i < write_count; ++i) {
        const AgcShaderDescriptorMapping *mapping = NULL;
        if (!agcHeaderValid(writes[i].struct_size, sizeof(writes[i]),
                writes[i].version) || writes[i].reserved0 != 0u ||
            !agcReservedZero(writes[i].reserved, 3u))
            return AGC_ERROR_INVALID_ARGUMENT;
        for (j = 0u; j < mapping_count; ++j) {
            if (mappings[j].set == writes[i].set &&
                mappings[j].binding == writes[i].binding) {
                mapping = &mappings[j];
                break;
            }
        }
        if (!mapping || mapping->type != writes[i].type ||
            writes[i].array_element >= mapping->array_size)
            return AGC_ERROR_VALIDATION_FAILED;
        for (j = 0u; j < i; ++j) {
            if (writes[j].set == writes[i].set &&
                writes[j].binding == writes[i].binding &&
                writes[j].array_element == writes[i].array_element)
                return AGC_ERROR_VALIDATION_FAILED;
        }
        result = agcCommandEncodeDescriptor(
            command_buffer, &writes[i], mapping, &encoded[i]);
        if (result != AGC_OK)
            return result;
    }
    for (i = 0u; i < write_count; ++i) {
        memcpy((uint8_t *)agcAllocationCpuAddress(
                command_buffer->resource_allocation) +
                encoded[i].destination_offset,
            encoded[i].bytes, encoded[i].size);
    }
    result = agcFlushRuntimeAllocation(command_buffer->resource_allocation,
        0u, layout->total_size);
    if (result != AGC_OK)
        return result;
    for (i = 0u; i < write_count; ++i) {
        if ((encoded[i].buffer && !agcCommandRetainBuffer(
                command_buffer, encoded[i].buffer)) ||
            (encoded[i].view && !agcCommandRetainView(
                command_buffer, encoded[i].view)) ||
            (encoded[i].sampler && !agcCommandRetainSampler(
                command_buffer, encoded[i].sampler)))
            return AGC_ERROR_INTERNAL;
    }
    command_buffer->descriptors_bound = 1u;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBindVertexBuffers(AgcCommandBuffer command_buffer,
    uint32_t binding_count, const AgcVertexBufferBinding *bindings)
{
    AgcGfx1013BufferDescriptor descriptors[32];
    const AgcRuntimePipelineResourceLayout *layout;
    uint32_t seen = 0u;
    uint32_t required_count = 0u;
    uint32_t table_size = 0u;
    uint32_t i;
    uint32_t j;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !bindings ||
        binding_count == 0u || binding_count > 32u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        !command_buffer->graphics_pipeline)
        return AGC_ERROR_INVALID_STATE;
    layout = &command_buffer->graphics_pipeline->resource_layout;
    for (i = 0u; i < 32u; ++i) {
        if ((layout->vertex_binding_mask & (1u << i)) != 0u) {
            required_count++;
            table_size = (i + 1u) * sizeof(AgcGfx1013BufferDescriptor);
        }
    }
    if (binding_count != required_count ||
        layout->vertex_binding_mask == 0u ||
        command_buffer->vertex_binding_mask != 0u ||
        !command_buffer->resource_allocation)
        return AGC_ERROR_VALIDATION_FAILED;
    memset(descriptors, 0, sizeof(descriptors));
    for (i = 0u; i < binding_count; ++i) {
        uint64_t range;
        if (!agcHeaderValid(bindings[i].struct_size, sizeof(bindings[i]),
                bindings[i].version) || bindings[i].reserved0 != 0u ||
            bindings[i].reserved1 != 0u ||
            !agcReservedZero(bindings[i].reserved, 3u) ||
            bindings[i].binding >= 32u ||
            (layout->vertex_binding_mask &
             (1u << bindings[i].binding)) == 0u ||
            (seen & (1u << bindings[i].binding)) != 0u ||
            !bindings[i].buffer ||
            bindings[i].buffer->magic != AGC_MAGIC_BUFFER ||
            bindings[i].buffer->device != command_buffer->device ||
            bindings[i].buffer->deferred ||
            (bindings[i].buffer->usage & AGC_BUFFER_USAGE_VERTEX_BIT) == 0u ||
            bindings[i].offset >= bindings[i].buffer->size ||
            bindings[i].stride == 0u)
            return AGC_ERROR_RESOURCE_INVALID;
        for (j = 0u; j <
                command_buffer->graphics_pipeline->vertex_input_count; ++j) {
            const AgcShaderVertexInput *input =
                &command_buffer->graphics_pipeline->vertex_inputs[j];
            if (input->binding == bindings[i].binding &&
                input->stride != bindings[i].stride)
                return AGC_ERROR_VALIDATION_FAILED;
        }
        range = bindings[i].buffer->size - bindings[i].offset;
        if (range / bindings[i].stride == 0u ||
            range / bindings[i].stride > UINT32_MAX)
            return AGC_ERROR_RESOURCE_INVALID;
        result = agcGfx1013BufferDescriptorEncode(
            &descriptors[bindings[i].binding],
            agcAllocationGpuAddress(bindings[i].buffer->allocation) +
                bindings[i].offset,
            bindings[i].stride, (uint32_t)(range / bindings[i].stride));
        if (result != AGC_OK)
            return result;
        seen |= 1u << bindings[i].binding;
    }
    for (i = 0u; i < binding_count; ++i) {
        memcpy((uint8_t *)agcAllocationCpuAddress(
                command_buffer->resource_allocation) +
                layout->vertex_table_offset +
                bindings[i].binding * sizeof(AgcGfx1013BufferDescriptor),
            &descriptors[bindings[i].binding],
            sizeof(AgcGfx1013BufferDescriptor));
    }
    result = agcFlushRuntimeAllocation(command_buffer->resource_allocation,
        layout->vertex_table_offset,
        table_size);
    if (result != AGC_OK)
        return result;
    for (i = 0u; i < binding_count; ++i) {
        if (!agcCommandRetainBuffer(command_buffer, bindings[i].buffer))
            return AGC_ERROR_INTERNAL;
    }
    command_buffer->vertex_binding_mask = seen;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdPushConstants(AgcCommandBuffer command_buffer,
    uint32_t stage_mask, uint32_t offset, uint32_t size, const void *data)
{
    const AgcRuntimePipelineResourceLayout *layout;
    const AgcShaderPushConstantRange *ranges;
    uint32_t range_count;
    uint32_t supported_stage_mask;
    uint32_t stage;
    uint64_t mask;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !data || size == 0u ||
        (offset & 3u) != 0u || (size & 3u) != 0u ||
        offset > 256u || size > 256u - offset || stage_mask == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        (!command_buffer->graphics_pipeline &&
         !command_buffer->compute_pipeline))
        return AGC_ERROR_INVALID_STATE;
    supported_stage_mask = command_buffer->graphics_pipeline ?
        ((1u << command_buffer->graphics_pipeline->primitive_shader->stage) |
         (1u << kAgcShaderStagePs)) :
        (1u << kAgcShaderStageCs);
    if ((stage_mask & ~supported_stage_mask) != 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    layout = agcCommandResourceLayout(command_buffer);
    ranges = agcCommandPushRanges(command_buffer, &range_count);
    if (!layout || layout->push_constant_size == 0u ||
        offset > layout->push_constant_size ||
        size > layout->push_constant_size - offset ||
        !command_buffer->resource_allocation)
        return AGC_ERROR_VALIDATION_FAILED;
    for (stage = 0u; stage < kAgcShaderStageCount; ++stage) {
        uint32_t covered = 0u;
        uint32_t i;
        if ((stage_mask & (1u << stage)) == 0u)
            continue;
        for (i = 0u; i < range_count; ++i) {
            if ((ranges[i].stage_mask & (1u << stage)) != 0u &&
                offset >= ranges[i].offset &&
                size <= ranges[i].size - (offset - ranges[i].offset)) {
                covered = 1u;
                break;
            }
        }
        if (!covered)
            return AGC_ERROR_VALIDATION_FAILED;
    }
    memcpy((uint8_t *)agcAllocationCpuAddress(
            command_buffer->resource_allocation) +
            layout->push_constant_offset + offset, data, size);
    result = agcFlushRuntimeAllocation(command_buffer->resource_allocation,
        layout->push_constant_offset + offset, size);
    if (result != AGC_OK)
        return result;
    mask = size == 256u ? UINT64_MAX :
        ((UINT64_C(1) << (size / 4u)) - 1u) << (offset / 4u);
    for (stage = 0u; stage < kAgcShaderStageCount; ++stage) {
        if ((stage_mask & (1u << stage)) != 0u)
            command_buffer->push_constant_masks[stage] |= mask;
    }
    return AGC_OK;
}

static uint64_t agcCommandRequiredPushMask(
    const AgcShaderReflection *reflection)
{
    uint64_t mask = 0u;
    uint32_t i;

    for (i = 0u; i < reflection->push_constant_range_count; ++i) {
        const AgcShaderPushConstantRange *range =
            &reflection->push_constant_ranges[i];
        uint32_t count = range->size / 4u;
        uint64_t bits = count == 64u ? UINT64_MAX :
            ((UINT64_C(1) << count) - 1u) << (range->offset / 4u);
        mask |= bits;
    }
    return mask;
}

static int32_t agcCommandShaderResourcesReady(
    AgcCommandBuffer command_buffer, AgcShader shader)
{
    const AgcRuntimePipelineResourceLayout *layout =
        agcCommandResourceLayout(command_buffer);
    uint64_t push_mask = agcCommandRequiredPushMask(&shader->reflection);

    if (shader->reflection.descriptor_mapping_count != 0u &&
        !command_buffer->descriptors_bound)
        return AGC_ERROR_RESOURCE_NOT_BOUND;
    if (shader->reflection.vertex_input_count != 0u &&
        (!layout || command_buffer->vertex_binding_mask !=
            layout->vertex_binding_mask))
        return AGC_ERROR_RESOURCE_NOT_BOUND;
    if ((command_buffer->push_constant_masks[shader->stage] & push_mask) !=
        push_mask)
        return AGC_ERROR_RESOURCE_NOT_BOUND;
    return AGC_OK;
}

static int32_t agcCommandUserSgprValue(AgcCommandBuffer command_buffer,
    AgcShader shader, const AgcShaderUserSgpr *sgpr, int32_t vertex_offset,
    uint32_t first_instance, uint32_t draw_index, uint32_t *value)
{
    const AgcRuntimePipelineResourceLayout *layout =
        agcCommandResourceLayout(command_buffer);
    uint64_t gpu_base;
    uint8_t *cpu_base;

    if (!layout || !value)
        return AGC_ERROR_INTERNAL;
    gpu_base = command_buffer->resource_allocation ?
        agcAllocationGpuAddress(command_buffer->resource_allocation) : 0u;
    cpu_base = command_buffer->resource_allocation ?
        agcAllocationCpuAddress(command_buffer->resource_allocation) : NULL;
    switch (sgpr->kind) {
    case AGC_SHADER_USER_SGPR_DESCRIPTOR_SET:
        if (!command_buffer->descriptors_bound || !gpu_base ||
            (layout->set_mask & (1u << sgpr->index)) == 0u)
            return AGC_ERROR_RESOURCE_NOT_BOUND;
        *value = (uint32_t)(gpu_base + layout->set_offsets[sgpr->index]);
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_INDIRECT_DESCRIPTOR_SETS:
        if (!gpu_base || !layout->uses_indirect_set_table ||
            (layout->set_mask != 0u &&
             !command_buffer->descriptors_bound))
            return AGC_ERROR_RESOURCE_NOT_BOUND;
        *value = (uint32_t)(gpu_base +
            layout->indirect_set_table_offset);
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER:
        if (!gpu_base || layout->push_constant_size == 0u)
            return AGC_ERROR_RESOURCE_NOT_BOUND;
        *value = (uint32_t)(gpu_base + layout->push_constant_offset);
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_INLINE_PUSH_CONSTANT:
        if (!cpu_base || sgpr->index >= 64u ||
            (command_buffer->push_constant_masks[shader->stage] &
             (UINT64_C(1) << sgpr->index)) == 0u)
            return AGC_ERROR_RESOURCE_NOT_BOUND;
        memcpy(value, cpu_base + layout->push_constant_offset +
            sgpr->index * sizeof(uint32_t), sizeof(*value));
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_VERTEX_BUFFER_TABLE:
        if (!gpu_base || command_buffer->vertex_binding_mask !=
                layout->vertex_binding_mask)
            return AGC_ERROR_RESOURCE_NOT_BOUND;
        *value = (uint32_t)(gpu_base + layout->vertex_table_offset);
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_BASE_VERTEX:
        *value = (uint32_t)vertex_offset;
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_START_INSTANCE:
        *value = first_instance;
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_DRAW_INDEX:
        *value = draw_index;
        return AGC_OK;
    default:
        return AGC_ERROR_NOT_SUPPORTED;
    }
}

static int32_t agcCommandEmitGraphicsUserData(SceAgcCb *cb,
    AgcCommandBuffer command_buffer, AgcShader shader,
    int32_t vertex_offset, uint32_t first_instance, uint32_t draw_index)
{
    uint32_t i;

    for (i = 0u; i < shader->reflection.user_sgpr_count; ++i) {
        const AgcShaderUserSgpr *sgpr = &shader->reflection.user_sgprs[i];
        AgcRegisterValue reg;
        int32_t result;
        if (sgpr->kind == AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER &&
            agcCommandRequiredPushMask(&shader->reflection) == 0u)
            continue;
        reg.offset = sgpr->register_offset;
        result = agcCommandUserSgprValue(command_buffer, shader, sgpr,
            vertex_offset, first_instance, draw_index, &reg.value);
        if (result != AGC_OK)
            return result;
        if (!sceAgcCbSetShRegistersDirect(cb, &reg, 1u))
            return AGC_ERROR_BUFFER_TOO_SMALL;
    }
    return AGC_OK;
}

static int32_t agcCommandBuildComputeUserData(
    AgcCommandBuffer command_buffer, AgcShader shader,
    uint32_t values[16], uint32_t *value_count)
{
    uint32_t count = 0u;
    uint32_t i;

    memset(values, 0, 16u * sizeof(uint32_t));
    for (i = 0u; i < shader->reflection.user_sgpr_count; ++i) {
        const AgcShaderUserSgpr *sgpr = &shader->reflection.user_sgprs[i];
        uint32_t index;
        int32_t result;
        if (sgpr->register_offset < AGC_REG_COMPUTE_USER_DATA_0 ||
            sgpr->register_offset >= AGC_REG_COMPUTE_USER_DATA_0 + 16u)
            return AGC_ERROR_NOT_SUPPORTED;
        index = sgpr->register_offset - AGC_REG_COMPUTE_USER_DATA_0;
        result = agcCommandUserSgprValue(
            command_buffer, shader, sgpr, 0, 0u, 0u, &values[index]);
        if (result != AGC_OK)
            return result;
        if (index + 1u > count)
            count = index + 1u;
    }
    *value_count = count;
    return AGC_OK;
}

static int32_t agcCommandCommitScratch(AgcCommandBuffer command_buffer,
    const SceAgcCb *scratch, const uint32_t *words)
{
    uint32_t dword_count = (uint32_t)agcCbUsedDwords(scratch);
    uint32_t *destination;

    if (agcCbRemainingDwords(&command_buffer->cursor) < dword_count)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    destination = agcCbAllocDwords(&command_buffer->cursor, dword_count);
    if (!destination)
        return AGC_ERROR_INTERNAL;
    memcpy(destination, words, dword_count * sizeof(uint32_t));
    return AGC_OK;
}

static int32_t agcCommandDynamicStateValid(
    AgcCommandBuffer command_buffer, AgcDynamicStateFlags state)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        !command_buffer->graphics_pipeline)
        return AGC_ERROR_INVALID_STATE;
    if ((command_buffer->graphics_pipeline->dynamic_state_mask & state) == 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdSetViewport(
    AgcCommandBuffer command_buffer, const AgcViewport *viewport)
{
    uint32_t words[15];
    uint32_t values[6];
    uint32_t *packet;
    SceAgcCb scratch;
    int32_t result = agcCommandDynamicStateValid(
        command_buffer, AGC_DYNAMIC_STATE_VIEWPORT_BIT);

    if (result != AGC_OK)
        return result;
    if (!viewport || !agcHeaderValid(viewport->struct_size,
            sizeof(*viewport), viewport->version) ||
        !agcReservedZero(viewport->reserved, 2u) ||
        !agcRuntimeFloatFinite(viewport->x) ||
        !agcRuntimeFloatFinite(viewport->y) ||
        !agcRuntimeFloatFinite(viewport->width) ||
        !agcRuntimeFloatFinite(viewport->height) ||
        !agcRuntimeFloatFinite(viewport->min_depth) ||
        !agcRuntimeFloatFinite(viewport->max_depth) ||
        !(viewport->width > 0.0f) || !(viewport->height > 0.0f) ||
        !(viewport->min_depth >= 0.0f) ||
        !(viewport->max_depth <= 1.0f) ||
        viewport->min_depth > viewport->max_depth)
        return AGC_ERROR_INVALID_ARGUMENT;
    values[0] = agcRuntimeFloatBits(viewport->width * 0.5f);
    values[1] = agcRuntimeFloatBits(viewport->x + viewport->width * 0.5f);
    values[2] = agcRuntimeFloatBits(viewport->height * 0.5f);
    values[3] = agcRuntimeFloatBits(viewport->y + viewport->height * 0.5f);
    values[4] = agcRuntimeFloatBits(viewport->max_depth - viewport->min_depth);
    values[5] = agcRuntimeFloatBits(viewport->min_depth);
    agcCbInit(&scratch, words, sizeof(words));
    packet = agcCbAllocDwords(&scratch, 8u);
    packet[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 8u);
    packet[1] = AGC_REG_PA_CL_VPORT_XSCALE;
    memcpy(&packet[2], values, sizeof(values));
    packet = agcCbAllocDwords(&scratch, 4u);
    packet[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
    packet[1] = AGC_REG_PA_SC_VPORT_ZMIN_0;
    packet[2] = agcRuntimeFloatBits(viewport->min_depth);
    packet[3] = agcRuntimeFloatBits(viewport->max_depth);
    packet = agcCbAllocDwords(&scratch, 3u);
    packet[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 3u);
    packet[1] = AGC_REG_PA_CL_VTE_CNTL;
    packet[2] = 0x0000043fu;
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result == AGC_OK)
        command_buffer->dynamic_state_set_mask |=
            AGC_DYNAMIC_STATE_VIEWPORT_BIT;
    return result;
}

int32_t PS5_SYSV_ABI agcCmdSetScissor(
    AgcCommandBuffer command_buffer, const AgcScissor *scissor)
{
    uint32_t words[22];
    AgcGfx1013ScissorState state;
    SceAgcCb scratch;
    int32_t result = agcCommandDynamicStateValid(
        command_buffer, AGC_DYNAMIC_STATE_SCISSOR_BIT);

    if (result != AGC_OK)
        return result;
    if (!scissor || !agcHeaderValid(scissor->struct_size,
            sizeof(*scissor), scissor->version) ||
        !agcReservedZero(scissor->reserved, 2u) || scissor->x < 0 ||
        scissor->y < 0 || scissor->width == 0u || scissor->height == 0u ||
        (uint32_t)scissor->x > 0x7fffu - scissor->width ||
        (uint32_t)scissor->y > 0x7fffu - scissor->height)
        return AGC_ERROR_INVALID_ARGUMENT;
    state.left = (uint32_t)scissor->x;
    state.top = (uint32_t)scissor->y;
    state.right = state.left + scissor->width;
    state.bottom = state.top + scissor->height;
    agcCbInit(&scratch, words, sizeof(words));
    result = agcGfx1013SetScissor(&scratch, &state);
    if (result != AGC_OK)
        return result;
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result == AGC_OK)
        command_buffer->dynamic_state_set_mask |=
            AGC_DYNAMIC_STATE_SCISSOR_BIT;
    return result;
}

int32_t PS5_SYSV_ABI agcCmdSetBlendConstants(
    AgcCommandBuffer command_buffer, const float constants[4])
{
    uint32_t words[6];
    uint32_t i;
    SceAgcCb scratch;
    uint32_t *packet;
    int32_t result = agcCommandDynamicStateValid(
        command_buffer, AGC_DYNAMIC_STATE_BLEND_CONSTANTS_BIT);

    if (result != AGC_OK)
        return result;
    if (!constants || !agcRuntimeFloatFinite(constants[0]) ||
        !agcRuntimeFloatFinite(constants[1]) ||
        !agcRuntimeFloatFinite(constants[2]) ||
        !agcRuntimeFloatFinite(constants[3]))
        return AGC_ERROR_INVALID_ARGUMENT;
    agcCbInit(&scratch, words, sizeof(words));
    packet = agcCbAllocDwords(&scratch, 6u);
    packet[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 6u);
    packet[1] = AGC_REG_CB_BLEND_RED;
    for (i = 0u; i < 4u; ++i)
        packet[i + 2u] = agcRuntimeFloatBits(constants[i]);
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result == AGC_OK)
        command_buffer->dynamic_state_set_mask |=
            AGC_DYNAMIC_STATE_BLEND_CONSTANTS_BIT;
    return result;
}

int32_t PS5_SYSV_ABI agcCmdSetStencilReference(
    AgcCommandBuffer command_buffer, uint32_t front, uint32_t back)
{
    const AgcDepthStencilPipelineState *state;
    const AgcStencilFaceState *back_state;
    uint32_t words[4];
    uint32_t *packet;
    SceAgcCb scratch;
    int32_t result = agcCommandDynamicStateValid(
        command_buffer, AGC_DYNAMIC_STATE_STENCIL_REFERENCE_BIT);

    if (result != AGC_OK)
        return result;
    if (front > 0xffu || back > 0xffu)
        return AGC_ERROR_INVALID_ARGUMENT;
    state = &command_buffer->graphics_pipeline->depth_stencil;
    if (!state->stencil_test_enable)
        return AGC_ERROR_VALIDATION_FAILED;
    back_state = state->back_face_enable ? &state->back : &state->front;
    if (!state->back_face_enable)
        back = front;
    agcCbInit(&scratch, words, sizeof(words));
    packet = agcCbAllocDwords(&scratch, 4u);
    packet[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 4u);
    packet[1] = AGC_REG_DB_STENCILREFMASK;
    packet[2] = front | (state->front.compare_mask << 8u) |
        (state->front.write_mask << 16u) | (front << 24u);
    packet[3] = back | (back_state->compare_mask << 8u) |
        (back_state->write_mask << 16u) | (back << 24u);
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result == AGC_OK)
        command_buffer->dynamic_state_set_mask |=
            AGC_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
    return result;
}

int32_t PS5_SYSV_ABI agcCmdSetDepthBias(
    AgcCommandBuffer command_buffer, const AgcDepthBias *depth_bias)
{
    uint32_t words[AGC_GFX1013_DEPTH_BIAS_STATE_DWORDS];
    AgcGfx1013DepthBiasState state;
    SceAgcCb scratch;
    int32_t result = agcCommandDynamicStateValid(
        command_buffer, AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT);

    if (result != AGC_OK)
        return result;
    if (!depth_bias || !agcHeaderValid(depth_bias->struct_size,
            sizeof(*depth_bias), depth_bias->version) ||
        depth_bias->flags != 0u ||
        !agcRuntimeFloatFinite(depth_bias->constant_factor) ||
        !agcRuntimeFloatFinite(depth_bias->clamp) ||
        !agcRuntimeFloatFinite(depth_bias->slope_factor) ||
        !agcReservedZero(depth_bias->reserved, 2u))
        return AGC_ERROR_INVALID_ARGUMENT;
    switch ((AgcFormat)command_buffer->graphics_pipeline->depth_stencil.format) {
    case AGC_FORMAT_D16_UNORM:
    case AGC_FORMAT_D16_UNORM_S8_UINT:
        state.format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM;
        break;
    case AGC_FORMAT_D32_FLOAT:
    case AGC_FORMAT_D32_FLOAT_S8_UINT:
        state.format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT;
        break;
    default:
        return AGC_ERROR_VALIDATION_FAILED;
    }
    state.constant_factor = depth_bias->constant_factor;
    state.clamp = depth_bias->clamp;
    state.slope_factor = depth_bias->slope_factor;
    agcCbInit(&scratch, words, sizeof(words));
    result = agcGfx1013SetDepthBiasState(&scratch, &state);
    if (result != AGC_OK)
        return result;
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result == AGC_OK)
        command_buffer->dynamic_state_set_mask |=
            AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT;
    return result;
}

int32_t PS5_SYSV_ABI agcCmdBindIndexBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer buffer, uint64_t offset, AgcIndexSize index_size)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(command_buffer->device) ||
        buffer->device != command_buffer->device || buffer->deferred ||
        (buffer->usage & AGC_BUFFER_USAGE_INDEX_BIT) == 0u ||
        (index_size != kAgcIndexSize16 && index_size != kAgcIndexSize32) ||
        offset >= buffer->size) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics)
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->index_buffer && command_buffer->index_buffer != buffer)
        return AGC_ERROR_NOT_SUPPORTED;
    if (!command_buffer->index_buffer) {
        command_buffer->index_buffer = buffer;
        buffer->recorded_refs++;
    }
    command_buffer->index_offset = offset;
    command_buffer->index_size = index_size;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdDrawIndexed(AgcCommandBuffer command_buffer,
    uint32_t index_count, uint32_t instance_count, uint32_t first_index,
    int32_t vertex_offset, uint32_t first_instance)
{
    uint32_t temporary[512];
    SceAgcCb temporary_cb;
    uint64_t element_size;
    uint64_t byte_offset;
    uint64_t byte_count;
    uint32_t dword_count;
    uint32_t *destination;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics ||
        !command_buffer->graphics_pipeline || !command_buffer->index_buffer)
        return AGC_ERROR_INVALID_STATE;
    if (index_count == 0u || instance_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((vertex_offset != 0 &&
        (command_buffer->graphics_pipeline->primitive_shader->reflection.
          system_sgpr_mask & AGC_SHADER_SYSTEM_SGPR_BASE_VERTEX_BIT) == 0u) ||
        (first_instance != 0u &&
         (command_buffer->graphics_pipeline->primitive_shader->reflection.
          system_sgpr_mask &
          AGC_SHADER_SYSTEM_SGPR_START_INSTANCE_BIT) == 0u))
        return AGC_ERROR_NOT_SUPPORTED;
    result = agcCommandShaderResourcesReady(command_buffer,
        command_buffer->graphics_pipeline->primitive_shader);
    if (result != AGC_OK)
        return result;
    result = agcCommandShaderResourcesReady(command_buffer,
        command_buffer->graphics_pipeline->pixel_shader);
    if (result != AGC_OK)
        return result;
    if ((command_buffer->dynamic_state_set_mask &
         command_buffer->graphics_pipeline->dynamic_state_mask) !=
        command_buffer->graphics_pipeline->dynamic_state_mask)
        return AGC_ERROR_INVALID_STATE;
    element_size = command_buffer->index_size == kAgcIndexSize16 ? 2u : 4u;
    byte_offset = (uint64_t)first_index * element_size;
    byte_count = (uint64_t)index_count * element_size;
    if (byte_offset > UINT64_MAX - command_buffer->index_offset ||
        byte_count > command_buffer->index_buffer->size ||
        command_buffer->index_offset + byte_offset >
            command_buffer->index_buffer->size - byte_count) {
        return AGC_ERROR_RESOURCE_INVALID;
    }
    agcCbInit(&temporary_cb, temporary, sizeof(temporary));
    result = agcCommandEmitGraphicsUserData(&temporary_cb, command_buffer,
        command_buffer->graphics_pipeline->primitive_shader,
        vertex_offset, first_instance, 0u);
    if (result != AGC_OK)
        return result;
    result = agcCommandEmitGraphicsUserData(&temporary_cb, command_buffer,
        command_buffer->graphics_pipeline->pixel_shader,
        vertex_offset, first_instance, 0u);
    if (result != AGC_OK)
        return result;
    (void)sceAgcDcbSetIndexSize(&temporary_cb,
        command_buffer->index_size, 0u);
    (void)sceAgcDcbSetNumInstances(&temporary_cb, instance_count);
    (void)sceAgcDcbDrawIndex(&temporary_cb, index_count,
        agcAllocationGpuAddress(command_buffer->index_buffer->allocation) +
            command_buffer->index_offset + byte_offset,
        0u);
    dword_count = (uint32_t)agcCbUsedDwords(&temporary_cb);
    if (agcCbRemainingDwords(&command_buffer->cursor) < dword_count)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    destination = agcCbAllocDwords(&command_buffer->cursor, dword_count);
    if (!destination)
        return AGC_ERROR_INTERNAL;
    memcpy(destination, temporary, dword_count * sizeof(uint32_t));
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdDispatch(AgcCommandBuffer command_buffer,
    uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
    uint32_t temporary[512];
    uint32_t user_data[16];
    AgcRegisterValue static_sh[UINT8_MAX];
    AgcShaderRecord record;
    SceAgcCb temporary_cb;
    AgcComputePipeline pipeline;
    AgcShader shader;
    AgcGfx1013ComputeState state = {0};
    uint32_t dword_count;
    uint32_t *destination;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueCompute ||
        !command_buffer->compute_pipeline)
        return AGC_ERROR_INVALID_STATE;
    if (group_count_x == 0u || group_count_y == 0u || group_count_z == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    pipeline = command_buffer->compute_pipeline;
    shader = pipeline->shader;
    result = agcCommandShaderResourcesReady(command_buffer, shader);
    if (result != AGC_OK)
        return result;
    result = agcCommandBuildComputeUserData(
        command_buffer, shader, user_data, &state.num_user_data);
    if (result != AGC_OK)
        return result;
    record = shader->record;
    state.num_sh_registers = agcPipelineCopyStaticShRegisters(
        shader, static_sh, UINT8_MAX);
    record.num_sh_registers = (uint8_t)state.num_sh_registers;
    state.record = &record;
    state.sh_registers = static_sh;
    state.user_data = user_data;
    state.code_address = shader->program_gpu_address;
    state.local_size_x = pipeline->local_size[0];
    state.local_size_y = pipeline->local_size[1];
    state.local_size_z = pipeline->local_size[2];
    state.group_count_x = group_count_x;
    state.group_count_y = group_count_y;
    state.group_count_z = group_count_z;
    state.modifier = shader->reflection.wave_size == 32u ?
        AGC_GFX1013_COMPUTE_DISPATCH_WAVE32 :
        AGC_GFX1013_COMPUTE_DISPATCH_WAVE64;
    agcCbInit(&temporary_cb, temporary, sizeof(temporary));
    result = agcGfx1013DispatchCompute(&temporary_cb, &state);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    dword_count = (uint32_t)agcCbUsedDwords(&temporary_cb);
    if (agcCbRemainingDwords(&command_buffer->cursor) < dword_count)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    destination = agcCbAllocDwords(&command_buffer->cursor, dword_count);
    if (!destination)
        return AGC_ERROR_INTERNAL;
    memcpy(destination, temporary, dword_count * sizeof(uint32_t));
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateFence(
    AgcDevice device, const AgcFenceDesc *desc, AgcFence *fence_out)
{
    AgcFence fence;

    if (!fence_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *fence_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->signaled > 1u || desc->flags != 0u ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    fence = agcCreateChild(device, sizeof(*fence));
    if (!fence)
        return AGC_ERROR_OUT_OF_MEMORY;
    fence->magic = AGC_MAGIC_FENCE;
    fence->device = device;
    fence->signaled = desc->signaled;
    *fence_out = fence;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyFence(AgcFence fence)
{
    AgcDevice device;

    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (fence->pending_refs != 0u)
        return AGC_ERROR_BUSY;
    device = fence->device;
    fence->magic = 0u;
    agcDestroyChild(device, fence);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetFenceStatus(AgcFence fence)
{
    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    return fence->signaled ? AGC_OK : AGC_ERROR_BUSY;
}

int32_t PS5_SYSV_ABI agcResetFence(AgcFence fence)
{
    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (fence->pending_refs != 0u)
        return AGC_ERROR_BUSY;
    fence->signaled = 0u;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcWaitFence(AgcFence fence, uint64_t timeout_ns)
{
    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (timeout_ns == AGC_RUNTIME_INFINITE_TIMEOUT)
        return AGC_ERROR_INVALID_ARGUMENT;
    return fence->signaled ? AGC_OK : AGC_ERROR_TIMEOUT;
}

int32_t PS5_SYSV_ABI agcQueueSubmit(
    AgcQueue queue, const AgcSubmitInfo *submit_info, AgcFence fence)
{
    AgcCommandBuffer command_buffer;

    if (!queue || queue->magic != AGC_MAGIC_QUEUE ||
        !agcDeviceValid(queue->device) || !submit_info ||
        !agcHeaderValid(submit_info->struct_size, sizeof(*submit_info),
            submit_info->version) || submit_info->flags != 0u ||
        !agcReservedZero(submit_info->reserved, 4u) ||
        submit_info->command_buffer_count != 1u ||
        !submit_info->command_buffers) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (fence && (fence->magic != AGC_MAGIC_FENCE ||
            fence->device != queue->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (fence && fence->signaled)
        return AGC_ERROR_INVALID_STATE;
    command_buffer = submit_info->command_buffers[0];
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        command_buffer->device != queue->device ||
        command_buffer->queue_type != queue->type)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_EXECUTABLE)
        return AGC_ERROR_INVALID_STATE;
    {
        uint64_t command_size = (uint64_t)
            agcCbUsedDwords(&command_buffer->cursor) * sizeof(uint32_t);
        int32_t flush_result = agcFlushRuntimeAllocation(
            command_buffer->allocation, 0u, command_size);
        if (flush_result != AGC_OK)
            return flush_result;
    }

#ifdef OPENAGC_PROSPERO
    /* Milestone 1 qualifies the object contract on the generic backend. Until
     * pipeline objects emit a complete reflected hardware bind in Milestone 3,
     * reject native submission before GPU mutation. */
    return AGC_ERROR_NOT_SUPPORTED;
#else
    AgcCommandBufferSubmit packet;
    int32_t result;

    packet.command_address = (uintptr_t)command_buffer->storage;
    packet.dword_count = agcCbUsedDwords(&command_buffer->cursor);
    packet.reserved = 0u;
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_PENDING;
    command_buffer->pending_refs++;
    queue->pending_count++;
    if (fence)
        fence->pending_refs++;
    if (queue->type == kAgcQueueCompute)
        result = sceAgcDriverSubmitAcb((uint32_t)queue->backend_handle, &packet);
    else
        result = sceAgcDriverSubmitDcb(&packet);
    if (result == AGC_ERROR_NOT_INITIALIZED)
        result = AGC_ERROR_DEVICE_LOST;
    command_buffer->pending_refs--;
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
    queue->pending_count--;
    if (fence) {
        fence->pending_refs--;
        if (result == AGC_OK)
            fence->signaled = 1u;
    }
    return result;
#endif
}

static AgcRuntimeAllocation *agcObjectAllocation(AgcDevice device,
    AgcObjectType type, const void *object)
{
    if (!object)
        return NULL;
    switch (type) {
    case AGC_OBJECT_TYPE_BUFFER: {
        const AgcBuffer buffer = (AgcBuffer)object;
        return buffer->magic == AGC_MAGIC_BUFFER && buffer->device == device ?
            buffer->allocation : NULL;
    }
    case AGC_OBJECT_TYPE_IMAGE: {
        const AgcImage image = (AgcImage)object;
        return image->magic == AGC_MAGIC_IMAGE && image->device == device ?
            image->allocation : NULL;
    }
    case AGC_OBJECT_TYPE_SHADER: {
        const AgcShader shader = (AgcShader)object;
        return shader->magic == AGC_MAGIC_SHADER && shader->device == device ?
            shader->allocation : NULL;
    }
    case AGC_OBJECT_TYPE_COMMAND_BUFFER: {
        const AgcCommandBuffer command_buffer = (AgcCommandBuffer)object;
        return command_buffer->magic == AGC_MAGIC_COMMAND_BUFFER &&
            command_buffer->device == device ? command_buffer->allocation : NULL;
    }
    case AGC_OBJECT_TYPE_IMAGE_VIEW: {
        const AgcImageView view = (AgcImageView)object;
        return view->magic == AGC_MAGIC_IMAGE_VIEW && view->device == device ?
            view->allocation : NULL;
    }
    case AGC_OBJECT_TYPE_SAMPLER: {
        const AgcSampler sampler = (AgcSampler)object;
        return sampler->magic == AGC_MAGIC_SAMPLER && sampler->device == device ?
            sampler->allocation : NULL;
    }
    default:
        return NULL;
    }
}

int32_t PS5_SYSV_ABI agcGetObjectAllocationInfo(AgcDevice device,
    AgcObjectType type, const void *object, AgcAllocationInfo *info)
{
    AgcRuntimeAllocation *allocation;

    if (!agcDeviceValid(device) || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        !agcReservedZero(info->reserved, 3u))
        return AGC_ERROR_INVALID_ARGUMENT;
    allocation = agcObjectAllocation(device, type, object);
    if (!allocation)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset((uint8_t *)info + offsetof(AgcAllocationInfo, heap), 0,
        sizeof(*info) - offsetof(AgcAllocationInfo, heap));
    info->heap = allocation->block->heap;
    info->dedicated = allocation->block->dedicated;
    info->allocation_size = allocation->size;
    info->requested_size = allocation->requested_size;
    info->heap_offset = allocation->offset;
    info->gpu_address = agcAllocationGpuAddress(allocation);
    info->cpu_address = agcAllocationCpuAddress(allocation);
    info->resident = 1u;
    info->owner_type = allocation->owner_type;
    info->owner = allocation->owner;
    (void)snprintf(info->debug_name, sizeof(info->debug_name), "%s",
        allocation->debug_name);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcSetObjectDebugName(AgcDevice device,
    AgcObjectType type, void *object, const char *name)
{
    AgcRuntimeAllocation *allocation;

    if (!agcDeviceValid(device) || !name)
        return AGC_ERROR_INVALID_ARGUMENT;
    allocation = agcObjectAllocation(device, type, object);
    if (!allocation)
        return AGC_ERROR_INVALID_ARGUMENT;
    (void)snprintf(allocation->debug_name, sizeof(allocation->debug_name),
        "%s", name);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetMemoryStats(
    AgcDevice device, AgcMemoryStats *stats)
{
    uint32_t heap;

    if (!agcDeviceValid(device) || !stats ||
        !agcHeaderValid(stats->struct_size, sizeof(*stats), stats->version) ||
        !agcReservedZero(stats->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    memset((uint8_t *)stats + offsetof(AgcMemoryStats, block_count), 0,
        sizeof(*stats) - offsetof(AgcMemoryStats, block_count));
    for (heap = 0u; heap < AGC_MEMORY_HEAP_COUNT; ++heap) {
        AgcRuntimeBlock *block;
        for (block = device->heaps[heap]; block; block = block->next) {
            stats->block_count[heap]++;
            if (block->dedicated)
                stats->dedicated_block_count++;
        }
    }
    stats->live_allocation_count = device->live_allocation_count;
    stats->live_bytes = device->live_bytes;
    stats->high_water_allocation_count = device->high_water_allocation_count;
    stats->high_water_bytes = device->high_water_bytes;
    stats->deferred_free_count = device->deferred_free_count;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcWriteBuffer(
    AgcBuffer buffer, uint64_t offset, const void *data, uint64_t size)
{
    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device) || buffer->deferred || !data ||
        size == 0u || offset > buffer->size || size > buffer->size - offset ||
        (buffer->create_flags & AGC_BUFFER_CREATE_UPLOAD_BIT) == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    memcpy((uint8_t *)buffer->storage + offset, data, (size_t)size);
    return agcGpuMemoryFlush(&buffer->allocation->block->memory,
        (size_t)(buffer->allocation->offset + offset), (size_t)size);
}

int32_t PS5_SYSV_ABI agcReadBuffer(
    AgcBuffer buffer, uint64_t offset, void *data, uint64_t size)
{
    int32_t result;

    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device) || buffer->deferred || !data ||
        size == 0u || offset > buffer->size || size > buffer->size - offset ||
        (buffer->create_flags & AGC_BUFFER_CREATE_READBACK_BIT) == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGpuMemoryInvalidate(&buffer->allocation->block->memory,
        (size_t)(buffer->allocation->offset + offset), (size_t)size);
    if (result == AGC_OK)
        memcpy(data, (uint8_t *)buffer->storage + offset, (size_t)size);
    return result;
}

static int32_t agcQueueDeferredFree(AgcDevice device, uint32_t type,
    void *object, AgcFence fence)
{
    AgcDeferredFree *entry;

    if (!fence || fence->magic != AGC_MAGIC_FENCE || fence->device != device)
        return AGC_ERROR_INVALID_ARGUMENT;
    entry = agcAlloc(device, sizeof(*entry), sizeof(void *));
    if (!entry)
        return AGC_ERROR_OUT_OF_MEMORY;
    entry->object = object;
    entry->fence = fence;
    entry->type = type;
    entry->next = device->deferred;
    device->deferred = entry;
    device->deferred_free_count++;
    fence->pending_refs++;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyBufferDeferred(
    AgcBuffer buffer, AgcFence fence)
{
    int32_t result;

    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device) || buffer->deferred)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (buffer->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    if (fence && fence->signaled)
        return agcDestroyBuffer(buffer);
    result = agcQueueDeferredFree(buffer->device, AGC_OBJECT_TYPE_BUFFER,
        buffer, fence);
    if (result == AGC_OK)
        buffer->deferred = 1u;
    return result;
}

int32_t PS5_SYSV_ABI agcDestroyImageDeferred(
    AgcImage image, AgcFence fence)
{
    int32_t result;

    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        !agcDeviceValid(image->device) || image->deferred)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (image->dependency_refs != 0u || image->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    if (fence && fence->signaled)
        return agcDestroyImage(image);
    result = agcQueueDeferredFree(image->device, AGC_OBJECT_TYPE_IMAGE,
        image, fence);
    if (result == AGC_OK)
        image->deferred = 1u;
    return result;
}

int32_t PS5_SYSV_ABI agcCollectDeferredFrees(AgcDevice device)
{
    AgcDeferredFree **link;

    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    link = &device->deferred;
    while (*link) {
        AgcDeferredFree *entry = *link;
        int32_t result;
        if (!entry->fence->signaled) {
            link = &entry->next;
            continue;
        }
        if (entry->type == AGC_OBJECT_TYPE_BUFFER) {
            AgcBuffer buffer = entry->object;
            buffer->deferred = 0u;
            result = agcDestroyBuffer(buffer);
            if (result != AGC_OK) {
                buffer->deferred = 1u;
                return result;
            }
        } else {
            AgcImage image = entry->object;
            image->deferred = 0u;
            result = agcDestroyImage(image);
            if (result != AGC_OK) {
                image->deferred = 1u;
                return result;
            }
        }
        *link = entry->next;
        entry->fence->pending_refs--;
        device->deferred_free_count--;
        agcFree(device, entry);
    }
    return AGC_OK;
}
