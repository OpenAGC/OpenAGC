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
#include "agc_memory.h"
#include "agc_runtime_diag.h"
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
#define AGC_FLEXIBLE_ALIGNMENT UINT64_C(0x100)
#define AGC_GARLIC_ALIGNMENT UINT64_C(0x10000)

typedef struct AgcRuntimeBlock AgcRuntimeBlock;
typedef struct AgcRuntimeAllocation AgcRuntimeAllocation;
typedef struct AgcDeferredFree AgcDeferredFree;

struct AgcRuntimeAllocation {
    AgcRuntimeAllocation *next;
    AgcRuntimeBlock *block;
    uint64_t offset;
    uint64_t size;
    uint64_t requested_size;
    uint32_t owner_type;
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
};

struct AgcSamplerImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcSamplerDesc desc;
};

struct AgcShaderImpl {
    uint32_t magic;
    uint32_t dependency_refs;
    AgcDevice device;
    AgcShaderStage stage;
    uint64_t code_size;
    void *code;
    AgcRuntimeAllocation *allocation;
};

struct AgcGraphicsPipelineImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcShader vertex_shader;
    AgcShader pixel_shader;
};

struct AgcComputePipelineImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcShader shader;
    uint32_t local_size[3];
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
    SceAgcCb cursor;
    AgcGraphicsPipeline graphics_pipeline;
    AgcComputePipeline compute_pipeline;
    AgcBuffer index_buffer;
    uint64_t index_offset;
    AgcIndexSize index_size;
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

static int32_t agcCreateMemoryBlock(AgcDevice device, uint32_t heap,
    uint64_t size, uint32_t dedicated, AgcRuntimeBlock **block_out)
{
    AgcRuntimeBlock *block;
    int32_t result;

    if (size > SIZE_MAX)
        return AGC_ERROR_OUT_OF_MEMORY;
    block = agcAlloc(device, sizeof(*block), sizeof(void *));
    if (!block)
        return AGC_ERROR_OUT_OF_MEMORY;
    if (heap == AGC_MEMORY_HEAP_FLEXIBLE) {
        result = agcGpuMemoryAllocateFlexible(&block->memory, (size_t)size,
            0x4000u, "openagc-runtime-flexible");
    } else {
        result = agcGpuMemoryAllocateDirectWriteCombined(&block->memory,
            (size_t)size, 0x200000u);
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

static int32_t agcRuntimeAllocate(AgcDevice device, uint32_t heap,
    uint64_t requested_size, uint64_t alignment, uint32_t dedicated,
    uint32_t owner_type, AgcRuntimeAllocation **allocation_out)
{
    AgcRuntimeBlock *block;
    AgcRuntimeAllocation *allocation;
    AgcRuntimeAllocation **link;
    uint64_t size;
    uint64_t offset = 0u;
    uint64_t default_size;
    int32_t result;

    if (!agcAlignU64(requested_size, alignment, &size))
        return AGC_ERROR_INVALID_ARGUMENT;
    default_size = heap == AGC_MEMORY_HEAP_FLEXIBLE ?
        AGC_FLEXIBLE_HEAP_BLOCK_SIZE : AGC_GARLIC_HEAP_BLOCK_SIZE;
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
        result = agcCreateMemoryBlock(device, heap, block_size, dedicated, &block);
        if (result != AGC_OK)
            return result;
        if (!agcFindBlockOffset(block, size, alignment, &offset))
            return AGC_ERROR_OUT_OF_MEMORY;
    }
    if (device->live_allocation_count == UINT64_MAX ||
        UINT64_MAX - device->live_bytes < size)
        return AGC_ERROR_OUT_OF_MEMORY;
    allocation = agcAlloc(device, sizeof(*allocation), sizeof(void *));
    if (!allocation)
        return AGC_ERROR_OUT_OF_MEMORY;
    allocation->block = block;
    allocation->offset = offset;
    allocation->size = size;
    allocation->requested_size = requested_size;
    allocation->owner_type = owner_type;
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
    if (block->dedicated && !block->allocations) {
        AgcRuntimeBlock **block_link = &device->heaps[block->heap];
        while (*block_link != block)
            block_link = &(*block_link)->next;
        *block_link = block->next;
        if (block->heap == AGC_MEMORY_HEAP_FLEXIBLE)
            (void)agcGpuMemoryFreeFlexible(&block->memory);
        else
            (void)agcGpuMemoryFreeDirect(&block->memory);
        agcFree(device, block);
    }
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

    if (!desc || !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->width == 0u || desc->height == 0u || desc->depth == 0u ||
        desc->mip_levels == 0u || desc->array_layers == 0u ||
        (desc->sample_count != 1u && desc->sample_count != 4u) ||
        desc->usage == 0u || (desc->usage & ~known_usage) != 0u ||
        !agcReservedZero(desc->reserved, 4u) ||
        !agcGetRuntimeFormatInfo(desc->format, &format))
        return 0;
    if ((desc->sample_count > 1u && (desc->mip_levels > 1u ||
            desc->depth > 1u)) ||
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

static int32_t agcComputeSubresource(const AgcImageDesc *desc,
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

    if (!agcImageDescBasicValid(desc) ||
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

int32_t PS5_SYSV_ABI agcGetImageLayout(
    const AgcImageDesc *desc, AgcImageLayout *layout)
{
    if (!layout || !agcHeaderValid(layout->struct_size, sizeof(*layout),
        layout->version) || !agcReservedZero(layout->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    memset((uint8_t *)layout + offsetof(AgcImageLayout, allocation_size), 0,
        sizeof(*layout) - offsetof(AgcImageLayout, allocation_size));
    return agcComputeSubresource(desc, 0u, 0u, 0u, NULL, layout);
}

int32_t PS5_SYSV_ABI agcGetImageSubresourceLayout(const AgcImageDesc *desc,
    uint32_t mip_level, uint32_t array_layer, uint32_t plane,
    AgcImageSubresourceLayout *layout)
{
    if (!layout || !agcHeaderValid(layout->struct_size, sizeof(*layout),
        layout->version) || !agcReservedZero(layout->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    memset((uint8_t *)layout + offsetof(AgcImageSubresourceLayout, mip_level),
        0, sizeof(*layout) - offsetof(AgcImageSubresourceLayout, mip_level));
    return agcComputeSubresource(desc, mip_level, array_layer, plane,
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
        AGC_OBJECT_TYPE_BUFFER, &buffer->allocation);
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
    result = agcGetImageLayout(desc, &layout);
    if (result != AGC_OK)
        return result;
    image = agcCreateChild(device, sizeof(*image));
    if (!image)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_GARLIC,
        layout.allocation_size, layout.alignment,
        (desc->usage & AGC_IMAGE_USAGE_SCANOUT_BIT) != 0u,
        AGC_OBJECT_TYPE_IMAGE, &image->allocation);
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

int32_t PS5_SYSV_ABI agcCreateImageView(
    AgcDevice device, const AgcImageViewDesc *desc, AgcImageView *view_out)
{
    AgcImageView view;
    AgcImage image;

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
        desc->mip_level_count == 0u || desc->array_layer_count == 0u ||
        desc->base_mip_level >= image->desc.mip_levels ||
        desc->mip_level_count > image->desc.mip_levels - desc->base_mip_level ||
        desc->base_array_layer >= image->desc.array_layers ||
        desc->array_layer_count >
            image->desc.array_layers - desc->base_array_layer) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    view = agcCreateChild(device, sizeof(*view));
    if (!view)
        return AGC_ERROR_OUT_OF_MEMORY;
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
    view->magic = 0u;
    agcDestroyChild(device, view);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateSampler(
    AgcDevice device, const AgcSamplerDesc *desc, AgcSampler *sampler_out)
{
    AgcSampler sampler;

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
    sampler = agcCreateChild(device, sizeof(*sampler));
    if (!sampler)
        return AGC_ERROR_OUT_OF_MEMORY;
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
    sampler->magic = 0u;
    agcDestroyChild(device, sampler);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateShader(
    AgcDevice device, const AgcShaderDesc *desc, AgcShader *shader_out)
{
    AgcShader shader;
    int32_t result;

    if (!shader_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *shader_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->stage >= kAgcShaderStageCount || desc->flags != 0u ||
        !desc->code || desc->code_size == 0u || desc->code_size > SIZE_MAX ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    shader = agcCreateChild(device, sizeof(*shader));
    if (!shader)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        desc->code_size, 256u, 0u, AGC_OBJECT_TYPE_SHADER,
        &shader->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, shader);
        return result;
    }
    shader->code = agcAllocationCpuAddress(shader->allocation);
    memcpy(shader->code, desc->code, (size_t)desc->code_size);
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

int32_t PS5_SYSV_ABI agcCreateGraphicsPipeline(AgcDevice device,
    const AgcGraphicsPipelineDesc *desc, AgcGraphicsPipeline *pipeline_out)
{
    AgcGraphicsPipeline pipeline;
    AgcShader vs;
    AgcShader ps;

    if (!pipeline_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *pipeline_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->flags != 0u || desc->reserved0 != 0u ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    vs = desc->vertex_shader;
    ps = desc->pixel_shader;
    if (!vs || !ps || vs->magic != AGC_MAGIC_SHADER ||
        ps->magic != AGC_MAGIC_SHADER || vs->device != device ||
        ps->device != device || vs->stage != kAgcShaderStageVs ||
        ps->stage != kAgcShaderStagePs) {
        return AGC_ERROR_SHADER_INVALID_TYPE;
    }
    pipeline = agcCreateChild(device, sizeof(*pipeline));
    if (!pipeline)
        return AGC_ERROR_OUT_OF_MEMORY;
    pipeline->magic = AGC_MAGIC_GRAPHICS_PIPELINE;
    pipeline->device = device;
    pipeline->vertex_shader = vs;
    pipeline->pixel_shader = ps;
    vs->dependency_refs++;
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
    pipeline->vertex_shader->dependency_refs--;
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

    if (!pipeline_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *pipeline_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->flags != 0u || !agcReservedZero(desc->reserved, 4u) ||
        desc->local_size_x == 0u || desc->local_size_y == 0u ||
        desc->local_size_z == 0u) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    shader = desc->shader;
    if (!shader || shader->magic != AGC_MAGIC_SHADER ||
        shader->device != device || shader->stage != kAgcShaderStageCs) {
        return AGC_ERROR_SHADER_INVALID_TYPE;
    }
    invocations = (uint64_t)desc->local_size_x * desc->local_size_y *
        desc->local_size_z;
    if (invocations > 1024u)
        return AGC_ERROR_NOT_SUPPORTED;
    pipeline = agcCreateChild(device, sizeof(*pipeline));
    if (!pipeline)
        return AGC_ERROR_OUT_OF_MEMORY;
    pipeline->magic = AGC_MAGIC_COMPUTE_PIPELINE;
    pipeline->device = device;
    pipeline->shader = shader;
    pipeline->local_size[0] = desc->local_size_x;
    pipeline->local_size[1] = desc->local_size_y;
    pipeline->local_size[2] = desc->local_size_z;
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
        &command_buffer->allocation);
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
        command_buffer->graphics_pipeline = pipeline;
        pipeline->recorded_refs++;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBindComputePipeline(
    AgcCommandBuffer command_buffer, AgcComputePipeline pipeline)
{
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
        command_buffer->compute_pipeline = pipeline;
        pipeline->recorded_refs++;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBindIndexBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer buffer, uint64_t offset, AgcIndexSize index_size)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(command_buffer->device) ||
        buffer->device != command_buffer->device ||
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
    uint64_t element_size;
    uint64_t byte_offset;
    uint64_t byte_count;

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
    if (vertex_offset != 0 || first_instance != 0u)
        return AGC_ERROR_NOT_SUPPORTED;
    element_size = command_buffer->index_size == kAgcIndexSize16 ? 2u : 4u;
    byte_offset = (uint64_t)first_index * element_size;
    byte_count = (uint64_t)index_count * element_size;
    if (byte_offset > UINT64_MAX - command_buffer->index_offset ||
        byte_count > command_buffer->index_buffer->size ||
        command_buffer->index_offset + byte_offset >
            command_buffer->index_buffer->size - byte_count) {
        return AGC_ERROR_RESOURCE_INVALID;
    }
    if (agcCbRemainingDwords(&command_buffer->cursor) < 11u)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    (void)sceAgcDcbSetIndexSize(&command_buffer->cursor,
        command_buffer->index_size, 0u);
    (void)sceAgcDcbSetNumInstances(&command_buffer->cursor, instance_count);
    (void)sceAgcDcbDrawIndex(&command_buffer->cursor, index_count,
        (uint64_t)(uintptr_t)command_buffer->index_buffer->storage +
            command_buffer->index_offset + byte_offset,
        0u);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdDispatch(AgcCommandBuffer command_buffer,
    uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
    uint32_t *packet;

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
    if (agcCbRemainingDwords(&command_buffer->cursor) < 5u)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    packet = sceAgcCbDispatch(&command_buffer->cursor, group_count_x,
        group_count_y, group_count_z, 0u);
    packet[0] |= 1u;
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
        !agcReservedZero(info->reserved, 4u))
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
        if (!entry->fence->signaled) {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        entry->fence->pending_refs--;
        if (entry->type == AGC_OBJECT_TYPE_BUFFER) {
            AgcBuffer buffer = entry->object;
            buffer->deferred = 0u;
            (void)agcDestroyBuffer(buffer);
        } else {
            AgcImage image = entry->object;
            image->deferred = 0u;
            (void)agcDestroyImage(image);
        }
        device->deferred_free_count--;
        agcFree(device, entry);
    }
    return AGC_OK;
}
