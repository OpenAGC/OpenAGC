/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Firmware-neutral native runtime object model.
 */

#include "openagc/runtime.h"
#include "openagc/capture.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "agc_cb.h"
#include "agc_capabilities.h"
#include "agc_driver_debug.h"
#include "agc_graphics.h"
#include "agc_memory.h"
#include "agc_pm4.h"
#include "agc_registers.h"
#include "agc_runtime_diag.h"
#include "agc_texture.h"
#include "agc_videoout.h"
#include "agcdriver.h"
#include "driver_ops.h"

_Static_assert(AGC_RUNTIME_MAX_VIEWPORTS == AGC_GFX1013_MAX_VIEWPORTS,
    "native runtime and gfx1013 viewport limits must match");

#define AGC_MAGIC_DEVICE UINT32_C(0x44475641)
#define AGC_MAGIC_QUEUE UINT32_C(0x51575641)
#define AGC_MAGIC_MEMORY UINT32_C(0x4d455641)
#define AGC_MAGIC_BUFFER UINT32_C(0x42555641)
#define AGC_MAGIC_IMAGE UINT32_C(0x494d5641)
#define AGC_MAGIC_IMAGE_VIEW UINT32_C(0x49565641)
#define AGC_MAGIC_SAMPLER UINT32_C(0x534d5641)
#define AGC_MAGIC_SHADER UINT32_C(0x53485641)
#define AGC_MAGIC_GRAPHICS_PIPELINE UINT32_C(0x47505641)
#define AGC_MAGIC_COMPUTE_PIPELINE UINT32_C(0x43505641)
#define AGC_MAGIC_COMMAND_BUFFER UINT32_C(0x43425641)
#define AGC_MAGIC_FENCE UINT32_C(0x464e5641)
#define AGC_MAGIC_GPU_LABEL UINT32_C(0x4c425641)
#define AGC_MAGIC_PRESENT_CHAIN UINT32_C(0x50435641)
#define AGC_MAGIC_CAPTURE UINT32_C(0x4350564f)

#define AGC_FLEXIBLE_HEAP_BLOCK_SIZE UINT64_C(0x01000000)
#define AGC_GARLIC_HEAP_BLOCK_SIZE UINT64_C(0x02000000)
#define AGC_FLEXIBLE_BLOCK_ALIGNMENT UINT64_C(0x4000)
#define AGC_GARLIC_BLOCK_ALIGNMENT UINT64_C(0x200000)
#define AGC_FLEXIBLE_ALIGNMENT UINT64_C(0x100)
#define AGC_GARLIC_ALIGNMENT UINT64_C(0x10000)
#define AGC_RUNTIME_PIPELINE_BIND_MAX_DWORDS 2048u
#define AGC_RUNTIME_GRAPHICS_DEFAULT_DWORDS 2184u
#define AGC_RUNTIME_MAX_DESCRIPTOR_WRITES 256u
#define AGC_RUNTIME_MAX_RECORDED_RESOURCES 512u
#define AGC_RUNTIME_MAX_RECORDED_TRANSITIONS 512u
#define AGC_RUNTIME_WAIT_COMPARE_GREATER_EQUAL 5u
#define AGC_RUNTIME_MAX_BUFFER_STATE_RANGES \
    (AGC_RUNTIME_MAX_RECORDED_TRANSITIONS * 2u + 1u)
#define AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS 63u
#define AGC_RUNTIME_MAX_RESOURCE_ARENA_SIZE UINT64_C(0x100000)
#define AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE \
    (AGC_GFX1013_OCCLUSION_QUERY_STRIDE + 16u)

typedef struct AgcRuntimeBlock AgcRuntimeBlock;
typedef struct AgcRuntimeAllocation AgcRuntimeAllocation;
typedef struct AgcDeferredFree AgcDeferredFree;
typedef struct AgcCaptureObjectMap AgcCaptureObjectMap;

typedef struct AgcRuntimeRecordedTransition {
    AgcResourceType resource_type;
    void *resource;
    AgcResourceUsage before;
    AgcResourceOwner before_owner;
    AgcResourceUsage after;
    AgcResourceOwner after_owner;
    uint32_t flags;
    AgcGpuLabel dependency_label;
    uint32_t dependency_value;
    uint64_t buffer_offset;
    uint64_t buffer_size;
    AgcImageSubresourceRange image_range;
} AgcRuntimeRecordedTransition;

typedef struct AgcRuntimeBatchTransitionState {
    AgcResourceType resource_type;
    void *resource;
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    uint32_t flags;
    uint32_t command_index;
    uint64_t buffer_offset;
    uint64_t buffer_size;
    AgcImageSubresourceRange image_range;
} AgcRuntimeBatchTransitionState;

typedef struct AgcRuntimeBufferStateRange {
    /* Sorted, contiguous half-open intervals. Start is the previous end (or
     * zero); adjacent equal states are merged after every commit. */
    uint64_t end;
    AgcResourceUsage usage;
    AgcResourceOwner owner;
} AgcRuntimeBufferStateRange;

typedef struct AgcRuntimePendingTransfer {
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    AgcGpuLabel label;
    AgcCommandBuffer acquire_command;
    uint32_t value;
    uint32_t reserved0;
    uint64_t buffer_offset;
    uint64_t buffer_size;
    AgcImageSubresourceRange image_range;
} AgcRuntimePendingTransfer;

typedef struct AgcRuntimeTransferSnapshot {
    uint32_t pending;
    uint32_t acquire_recorded;
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    AgcGpuLabel label;
    uint32_t value;
} AgcRuntimeTransferSnapshot;

typedef struct AgcRuntimeRecordedLabelWait {
    AgcGpuLabel label;
    uint32_t value;
} AgcRuntimeRecordedLabelWait;

typedef struct AgcRuntimeRecordedLabelSignal {
    AgcGpuLabel label;
    uint32_t value;
} AgcRuntimeRecordedLabelSignal;

static int agcCommandRetainGpuLabel(AgcCommandBuffer command_buffer,
    AgcGpuLabel label);
static void agcCaptureRecordValidation(
    AgcDevice device, const AgcDebugMessage *message);
static uint64_t agcShaderHashBytes(
    uint64_t hash, const void *data, uint64_t size);

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
    uint32_t push_constant_stride;
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

struct AgcCaptureObjectMap {
    AgcCaptureObjectMap *next;
    const void *object;
    uint64_t id;
    uint32_t type;
};

struct AgcCaptureImpl {
    uint32_t magic;
    uint32_t flags;
    AgcDevice device;
    AgcCaptureWriteFunction write;
    void *user_data;
    uint32_t active;
    int32_t status;
    uint64_t record_count;
    uint64_t byte_count;
    uint64_t sequence;
    uint64_t next_object_id;
    AgcCaptureObjectMap *objects;
};

struct AgcDeviceImpl {
    uint32_t magic;
    uint32_t child_count;
    struct AgcDeviceImpl *next_device;
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
    AgcRuntimeAllocation *tessellation_offchip_allocation;
    AgcRuntimeAllocation *tessellation_factor_allocation;
    AgcRuntimeAllocation *tessellation_table_allocation;
    AgcRuntimeAllocation *border_color_allocation;
    uint32_t tessellation_initialized;
    AgcDebugMessageFunction debug_callback;
    void *debug_user_data;
    AgcDebugMessageSeverityFlags debug_severity_mask;
    AgcDebugMessageCategoryFlags debug_category_mask;
    uint64_t debug_sequence;
    uint32_t has_debug_message;
    AgcDebugMessage last_debug_message;
    AgcCapture active_capture;
};

struct AgcQueueImpl {
    uint32_t magic;
    uint32_t pending_count;
    uint32_t label_refs;
    AgcDevice device;
    AgcQueueType type;
    int32_t backend_handle;
    uint64_t next_submission_id;
    uint64_t last_completed_submission_id;
};

struct AgcMemoryImpl {
    uint32_t magic;
    uint32_t resource_refs;
    AgcDevice device;
    AgcRuntimeAllocation *allocation;
    uint64_t size;
    AgcMemoryHeap heap;
    uint32_t released;
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
    AgcMemory memory;
    uint64_t memory_offset;
    uint32_t owns_allocation;
    uint32_t transfer_count;
    uint32_t transfer_capacity;
    AgcRuntimePendingTransfer *transfers;
    AgcRuntimePendingTransfer inline_transfer;
    uint32_t deferred;
    uint32_t state_range_count;
    uint32_t state_range_capacity;
    AgcRuntimeBufferStateRange *state_ranges;
    AgcRuntimeBufferStateRange inline_state_range;
};

struct AgcImageImpl {
    uint32_t magic;
    uint32_t dependency_refs;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcImageDesc desc;
    AgcImageLayout layout;
    AgcRuntimeAllocation *allocation;
    AgcMemory memory;
    uint64_t memory_offset;
    uint32_t owns_allocation;
    AgcResourceUsage usage_state;
    AgcResourceOwner owner_state;
    uint32_t subresource_state_count;
    uint16_t *subresource_states;
    uint32_t transfer_count;
    uint32_t transfer_capacity;
    AgcRuntimePendingTransfer *transfers;
    AgcRuntimePendingTransfer inline_transfer;
    uint32_t deferred;
};

struct AgcPresentChainImpl {
    uint32_t magic;
    uint32_t image_count;
    AgcDevice device;
    AgcVideoOut *video_out;
    AgcImage images[AGC_PRESENT_CHAIN_MAX_IMAGES];
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
    AgcShader hull_shader;
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
    AgcDepthBias static_depth_bias;
    AgcDynamicStateFlags dynamic_state_mask;
    uint32_t has_static_depth_bias;
    uint32_t logic_operation_enable;
    AgcLogicOperation logic_operation;
    AgcRuntimePipelineResourceLayout resource_layout;
    uint32_t tessellation_input_control_points;
    uint32_t tessellation_tcs_offchip_layout;
    uint32_t tessellation_tes_offchip_layout;
    uint32_t primitive_type;
    uint32_t geometry_input_vertices;
    AgcPrimitiveTopology primitive_topology;
    uint32_t primitive_restart_enable;
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
    AgcFence completion_fence;
    SceAgcCb cursor;
    AgcGraphicsPipeline graphics_pipeline;
    AgcComputePipeline compute_pipeline;
    AgcBuffer index_buffer;
    AgcImage depth_stencil_target;
    uint64_t index_offset;
    AgcIndexSize index_size;
    uint32_t descriptors_bound;
    uint32_t vertex_binding_mask;
    uint32_t dynamic_state_set_mask;
    uint32_t color_target_count;
    uint32_t color_target_width;
    uint32_t color_target_height;
    uint32_t graphics_defaults_emitted;
    uint32_t recorded_buffer_count;
    uint32_t recorded_image_count;
    uint32_t recorded_view_count;
    uint32_t recorded_sampler_count;
    uint32_t recorded_transition_count;
    uint32_t recorded_label_count;
    uint32_t recorded_graphics_pipeline_count;
    uint32_t recorded_compute_pipeline_count;
    uint32_t recorded_resource_allocation_count;
    uint32_t recorded_label_wait_count;
    uint32_t recorded_label_signal_count;
    uint64_t push_constant_masks[kAgcShaderStageCount];
    AgcBuffer recorded_buffers[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcImage recorded_images[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcImageView recorded_views[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcSampler recorded_samplers[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcGpuLabel recorded_labels[AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcGraphicsPipeline recorded_graphics_pipelines[
        AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcComputePipeline recorded_compute_pipelines[
        AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcRuntimeAllocation *recorded_resource_allocations[
        AGC_RUNTIME_MAX_RECORDED_RESOURCES];
    AgcRuntimeRecordedTransition
        recorded_transitions[AGC_RUNTIME_MAX_RECORDED_TRANSITIONS];
    AgcRuntimeRecordedLabelWait
        recorded_label_waits[AGC_RUNTIME_MAX_RECORDED_TRANSITIONS];
    AgcRuntimeRecordedLabelSignal
        recorded_label_signals[AGC_RUNTIME_MAX_RECORDED_TRANSITIONS];
};

struct AgcGpuLabelImpl {
    uint32_t magic;
    uint32_t recorded_refs;
    AgcDevice device;
    AgcRuntimeAllocation *allocation;
    AgcQueue last_signal_queue;
    uint32_t last_signal_value;
    uint32_t last_signal_queue_type;
    uint64_t last_signal_submission_id;
    uint32_t last_wait_value;
    int32_t last_wait_result;
    uint64_t timeout_count;
    uint64_t last_timeout_ns;
};

struct AgcFenceImpl {
    uint32_t magic;
    uint32_t pending_refs;
    AgcDevice device;
    AgcRuntimeAllocation *allocation;
    AgcQueue queue;
    uint32_t pending_command_buffer_count;
    AgcCommandBuffer pending_command_buffers[
        AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS];
    uint32_t completion_value;
    uint32_t observed_completion_value;
    uint32_t signaled;
    uint32_t last_queue_type;
    uint32_t last_command_buffer_state;
    int32_t last_wait_result;
    uint64_t submission_id;
    uint64_t last_completed_submission_id;
    uint64_t timeout_count;
    uint64_t last_timeout_ns;
};

_Static_assert(sizeof(AgcDebugMessage) == 368u,
    "AgcDebugMessage ABI size mismatch");
_Static_assert(offsetof(AgcDebugMessage, message) == 144u,
    "AgcDebugMessage message offset mismatch");
_Static_assert(sizeof(AgcDebugCallbackDesc) == 64u,
    "AgcDebugCallbackDesc ABI size mismatch");
_Static_assert(sizeof(AgcCaptureDesc) == 64u,
    "AgcCaptureDesc ABI size mismatch");
_Static_assert(sizeof(AgcCaptureInfo) == 72u,
    "AgcCaptureInfo ABI size mismatch");

static int agcCommandImageRangeState(AgcCommandBuffer command_buffer,
    AgcImage image, const AgcImageSubresourceRange *range,
    AgcResourceUsage *usage, AgcResourceOwner *owner);
static uint32_t agcCommandLatestLabelSignalValue(
    const AgcCommandBuffer command_buffer, const AgcGpuLabel label);

static AgcDevice g_devices;
static uint32_t g_device_count;
static uint32_t g_backend_agc_version;
static atomic_flag g_device_registry_lock = ATOMIC_FLAG_INIT;

static void agcDeviceRegistryLock(void)
{
    while (atomic_flag_test_and_set_explicit(
               &g_device_registry_lock, memory_order_acquire)) {}
}

static void agcDeviceRegistryUnlock(void)
{
    atomic_flag_clear_explicit(&g_device_registry_lock, memory_order_release);
}

static int agcDeviceRegistered(AgcDevice device)
{
    AgcDevice current;

    for (current = g_devices; current; current = current->next_device) {
        if (current == device)
            return current->magic == AGC_MAGIC_DEVICE;
    }
    return 0;
}

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
    int valid;

    if (!device)
        return 0;
    agcDeviceRegistryLock();
    valid = agcDeviceRegistered(device);
    agcDeviceRegistryUnlock();
    return valid;
}

static int32_t agcDebugReport(AgcDevice device,
    AgcDebugMessageSeverityFlags severity,
    AgcDebugMessageCategoryFlags category, int32_t result,
    const char *function_name, uint32_t object_type,
    const char *object_name, const char *message)
{
    AgcDebugMessage debug_message = AGC_DEBUG_MESSAGE_INIT;
    int callback_selected;
    int capture_selected;

    if (!agcDeviceValid(device))
        return result;
    callback_selected = device->debug_callback != NULL &&
        (device->debug_severity_mask & severity) != 0u &&
        (device->debug_category_mask & category) != 0u;
    capture_selected = device->active_capture != NULL &&
        device->active_capture->active;
    if (!callback_selected && !capture_selected)
        return result;
    if (callback_selected) {
        device->debug_sequence++;
        debug_message.sequence = device->debug_sequence;
    }
    debug_message.severity = severity;
    debug_message.category = category;
    debug_message.result = result;
    debug_message.object_type = object_type;
    (void)snprintf(debug_message.function_name,
        sizeof(debug_message.function_name), "%s",
        function_name ? function_name : "");
    (void)snprintf(debug_message.object_name,
        sizeof(debug_message.object_name), "%s", object_name ? object_name : "");
    (void)snprintf(debug_message.message, sizeof(debug_message.message), "%s",
        message ? message : "");
    if (capture_selected)
        agcCaptureRecordValidation(device, &debug_message);
    if (callback_selected) {
        device->last_debug_message = debug_message;
        device->has_debug_message = 1u;
        device->debug_callback(device->debug_user_data,
            &device->last_debug_message);
    }
    return result;
}

static int32_t agcPipelineDebugReport(AgcDevice device,
    const char *function_name, int32_t result,
    AgcDebugMessageCategoryFlags category, const char *message)
{
    return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
        category, result, function_name, AGC_DEBUG_OBJECT_TYPE_NONE, NULL,
        message);
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

static void agcCaptureWrite(AgcCapture capture, const void *data, size_t size)
{
    if (!capture || !capture->active || capture->status != AGC_OK ||
        size == 0u)
        return;
    if (UINT64_MAX - capture->byte_count < size) {
        capture->status = AGC_ERROR_OUT_OF_MEMORY;
        return;
    }
    capture->write(capture->user_data, data, size);
    capture->byte_count += size;
}

static void agcCaptureWriteU16(AgcCapture capture, uint16_t value)
{
    uint8_t bytes[2] = {
        (uint8_t)value, (uint8_t)(value >> 8)
    };
    agcCaptureWrite(capture, bytes, sizeof(bytes));
}

static void agcCaptureWriteU32(AgcCapture capture, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)
    };
    agcCaptureWrite(capture, bytes, sizeof(bytes));
}

static void agcCaptureWriteU64(AgcCapture capture, uint64_t value)
{
    uint8_t bytes[8];
    uint32_t i;

    for (i = 0u; i < 8u; ++i)
        bytes[i] = (uint8_t)(value >> (i * 8u));
    agcCaptureWrite(capture, bytes, sizeof(bytes));
}

static int agcCaptureBeginRecord(AgcCapture capture, uint16_t type,
    uint32_t payload_size)
{
    uint32_t record_size;

    if (!capture || !capture->active || capture->status != AGC_OK ||
        payload_size > UINT32_MAX - AGC_CAPTURE_RECORD_HEADER_SIZE) {
        if (capture && payload_size >
                UINT32_MAX - AGC_CAPTURE_RECORD_HEADER_SIZE)
            capture->status = AGC_ERROR_OUT_OF_MEMORY;
        return 0;
    }
    record_size = AGC_CAPTURE_RECORD_HEADER_SIZE + payload_size;
    capture->sequence++;
    capture->record_count++;
    agcCaptureWriteU16(capture, type);
    agcCaptureWriteU16(capture, AGC_CAPTURE_FORMAT_VERSION);
    agcCaptureWriteU32(capture, record_size);
    agcCaptureWriteU64(capture, capture->sequence);
    return capture->status == AGC_OK;
}

static void agcCaptureClearObjects(AgcCapture capture)
{
    AgcCaptureObjectMap *entry = capture->objects;

    while (entry) {
        AgcCaptureObjectMap *next = entry->next;
        agcFree(capture->device, entry);
        entry = next;
    }
    capture->objects = NULL;
}

static uint64_t agcCaptureObjectId(AgcCapture capture, const void *object,
    uint32_t type)
{
    AgcCaptureObjectMap *entry;

    if (!capture || !capture->active || !object)
        return 0u;
    for (entry = capture->objects; entry; entry = entry->next) {
        if (entry->object == object && entry->type == type)
            return entry->id;
    }
    if (capture->next_object_id == UINT64_MAX) {
        capture->status = AGC_ERROR_NOT_SUPPORTED;
        return 0u;
    }
    entry = agcAlloc(capture->device, sizeof(*entry), sizeof(void *));
    if (!entry) {
        capture->status = AGC_ERROR_OUT_OF_MEMORY;
        return 0u;
    }
    entry->object = object;
    entry->type = type;
    entry->id = capture->next_object_id++;
    entry->next = capture->objects;
    capture->objects = entry;
    return entry->id;
}

static void agcCaptureRecordRuntimeInfo(AgcCapture capture)
{
    const AgcRuntimeInfo *info = &capture->device->runtime_info;

    if (!agcCaptureBeginRecord(capture, AGC_CAPTURE_RECORD_RUNTIME_INFO, 76u))
        return;
    agcCaptureWriteU32(capture, info->firmware_version);
    agcCaptureWriteU16(capture, info->firmware_abi_key);
    agcCaptureWriteU16(capture, info->hardware_family);
    agcCaptureWriteU32(capture, info->agc_version);
    agcCaptureWriteU64(capture, info->capability_bits);
    agcCaptureWrite(capture, info->qualification,
        AGC_RUNTIME_CAPABILITY_COUNT);
    agcCaptureWrite(capture, info->profile_name,
        AGC_RUNTIME_PROFILE_NAME_SIZE);
}

static void agcCaptureRecordObjectCreate(AgcDevice device,
    const void *object, uint32_t type, uint32_t detail0, uint32_t detail1)
{
    AgcCapture capture = device->active_capture;
    uint64_t id;

    if (!capture || !capture->active)
        return;
    id = agcCaptureObjectId(capture, object, type);
    if (id == 0u || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_OBJECT_CREATE, 24u))
        return;
    agcCaptureWriteU64(capture, id);
    agcCaptureWriteU32(capture, type);
    agcCaptureWriteU32(capture, 0u);
    agcCaptureWriteU32(capture, detail0);
    agcCaptureWriteU32(capture, detail1);
}

static void agcCaptureRecordObjectDestroy(AgcDevice device,
    const void *object, uint32_t type)
{
    AgcCapture capture = device->active_capture;
    uint64_t id;

    if (!capture || !capture->active)
        return;
    id = agcCaptureObjectId(capture, object, type);
    if (id == 0u || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_OBJECT_DESTROY, 16u))
        return;
    agcCaptureWriteU64(capture, id);
    agcCaptureWriteU32(capture, type);
    agcCaptureWriteU32(capture, 0u);
}

static void agcCaptureRecordObjectName(AgcDevice device,
    const void *object, uint32_t type, const char *name)
{
    AgcCapture capture = device->active_capture;
    uint64_t id;
    char bounded_name[AGC_RUNTIME_DEBUG_NAME_SIZE] = {0};

    if (!capture || !capture->active)
        return;
    id = agcCaptureObjectId(capture, object, type);
    if (id == 0u || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_OBJECT_NAME, 76u))
        return;
    (void)snprintf(bounded_name, sizeof(bounded_name), "%s", name);
    agcCaptureWriteU64(capture, id);
    agcCaptureWriteU32(capture, type);
    agcCaptureWrite(capture, bounded_name, sizeof(bounded_name));
}

static uint32_t agcCaptureObjectTypeFromRuntime(AgcObjectType type)
{
    switch (type) {
    case AGC_OBJECT_TYPE_BUFFER:
        return AGC_CAPTURE_OBJECT_BUFFER;
    case AGC_OBJECT_TYPE_IMAGE:
        return AGC_CAPTURE_OBJECT_IMAGE;
    case AGC_OBJECT_TYPE_SHADER:
        return AGC_CAPTURE_OBJECT_SHADER;
    case AGC_OBJECT_TYPE_COMMAND_BUFFER:
        return AGC_CAPTURE_OBJECT_COMMAND_BUFFER;
    case AGC_OBJECT_TYPE_IMAGE_VIEW:
        return AGC_CAPTURE_OBJECT_IMAGE_VIEW;
    case AGC_OBJECT_TYPE_SAMPLER:
        return AGC_CAPTURE_OBJECT_SAMPLER;
    default:
        return UINT32_MAX;
    }
}

static void agcCaptureRecordCommandBegin(AgcCommandBuffer command_buffer)
{
    AgcCapture capture = command_buffer->device->active_capture;
    uint64_t id;

    if (!capture || !capture->active)
        return;
    id = agcCaptureObjectId(capture, command_buffer,
        AGC_CAPTURE_OBJECT_COMMAND_BUFFER);
    if (id == 0u || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_COMMAND_BEGIN, 16u))
        return;
    agcCaptureWriteU64(capture, id);
    agcCaptureWriteU32(capture, command_buffer->queue_type);
    agcCaptureWriteU32(capture, command_buffer->capacity_dwords);
}

static void agcCaptureRecordCommandWords(AgcCommandBuffer command_buffer,
    uint16_t record_type)
{
    AgcCapture capture = command_buffer->device->active_capture;
    uint32_t used;
    uint32_t payload_size;
    uint32_t i;
    uint64_t id;

    if (!capture || !capture->active)
        return;
    used = agcCbUsedDwords(&command_buffer->cursor);
    if (used > (UINT32_MAX - 16u) / sizeof(uint32_t)) {
        capture->status = AGC_ERROR_OUT_OF_MEMORY;
        return;
    }
    payload_size = 16u + used * sizeof(uint32_t);
    id = agcCaptureObjectId(capture, command_buffer,
        AGC_CAPTURE_OBJECT_COMMAND_BUFFER);
    if (id == 0u || !agcCaptureBeginRecord(capture,
            record_type, payload_size))
        return;
    agcCaptureWriteU64(capture, id);
    agcCaptureWriteU32(capture, command_buffer->queue_type);
    agcCaptureWriteU32(capture, used);
    for (i = 0u; i < used; ++i)
        agcCaptureWriteU32(capture, command_buffer->storage[i]);
}

static void agcCaptureRecordValidation(
    AgcDevice device, const AgcDebugMessage *message)
{
    AgcCapture capture = device->active_capture;

    if (!capture || !capture->active || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_VALIDATION_MESSAGE, 328u))
        return;
    agcCaptureWriteU64(capture, message->sequence);
    agcCaptureWriteU32(capture, message->severity);
    agcCaptureWriteU32(capture, message->category);
    agcCaptureWriteU32(capture, (uint32_t)message->result);
    agcCaptureWriteU32(capture, message->object_type);
    agcCaptureWrite(capture, message->function_name,
        AGC_RUNTIME_DEBUG_FUNCTION_NAME_SIZE);
    agcCaptureWrite(capture, message->object_name,
        AGC_RUNTIME_DEBUG_NAME_SIZE);
    agcCaptureWrite(capture, message->message,
        AGC_RUNTIME_DEBUG_MESSAGE_SIZE);
}

static void agcCaptureRecordSubmission(AgcQueue queue,
    const AgcSubmitInfo *submit_info, AgcFence fence, int32_t result,
    uint64_t submission_id)
{
    AgcCapture capture = queue->device->active_capture;
    uint32_t wait_count = 0u;
    uint32_t signal_count = 0u;
    uint64_t payload_size;
    uint64_t fence_id = 0u;
    uint32_t i;

    if (!capture || !capture->active)
        return;
    if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_2) {
        wait_count = submit_info->wait_count;
        signal_count = submit_info->signal_count;
    }
    payload_size = 40u + (uint64_t)submit_info->command_buffer_count * 8u +
        (uint64_t)(wait_count + signal_count) * 16u;
    if (payload_size > UINT32_MAX) {
        capture->status = AGC_ERROR_OUT_OF_MEMORY;
        return;
    }
    if (fence)
        fence_id = agcCaptureObjectId(capture, fence,
            AGC_CAPTURE_OBJECT_FENCE);
    if (!agcCaptureBeginRecord(capture, AGC_CAPTURE_RECORD_SUBMISSION,
            (uint32_t)payload_size))
        return;
    agcCaptureWriteU64(capture, submission_id);
    agcCaptureWriteU32(capture, queue->type);
    agcCaptureWriteU32(capture, submit_info->command_buffer_count);
    agcCaptureWriteU32(capture, wait_count);
    agcCaptureWriteU32(capture, signal_count);
    agcCaptureWriteU32(capture, (uint32_t)result);
    agcCaptureWriteU32(capture, 0u);
    agcCaptureWriteU64(capture, fence_id);
    for (i = 0u; i < submit_info->command_buffer_count; ++i) {
        agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
            submit_info->command_buffers[i],
            AGC_CAPTURE_OBJECT_COMMAND_BUFFER));
    }
    for (i = 0u; i < wait_count; ++i) {
        agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
            submit_info->waits[i].label, AGC_CAPTURE_OBJECT_GPU_LABEL));
        agcCaptureWriteU32(capture, submit_info->waits[i].value);
        agcCaptureWriteU32(capture, 0u);
    }
    for (i = 0u; i < signal_count; ++i) {
        agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
            submit_info->signals[i].label, AGC_CAPTURE_OBJECT_GPU_LABEL));
        agcCaptureWriteU32(capture, submit_info->signals[i].value);
        agcCaptureWriteU32(capture, 0u);
    }
}

static void agcCaptureRecordFenceResult(AgcFence fence, int32_t result,
    uint64_t timeout_ns)
{
    AgcCapture capture = fence->device->active_capture;
    uint64_t id;

    if (!capture || !capture->active)
        return;
    id = agcCaptureObjectId(capture, fence, AGC_CAPTURE_OBJECT_FENCE);
    if (id == 0u || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_FENCE_RESULT, 32u))
        return;
    agcCaptureWriteU64(capture, id);
    agcCaptureWriteU64(capture, fence->submission_id);
    agcCaptureWriteU32(capture, (uint32_t)result);
    agcCaptureWriteU32(capture, 1u);
    agcCaptureWriteU64(capture, timeout_ns);
}

static void agcCaptureRecordResourceDesc(AgcDevice device,
    const void *object, uint32_t type)
{
    AgcCapture capture = device->active_capture;
    uint64_t id;

    if (!capture || !capture->active)
        return;
    id = agcCaptureObjectId(capture, object, type);
    if (id == 0u)
        return;
    if (type == AGC_CAPTURE_OBJECT_BUFFER) {
        const AgcBuffer buffer = (AgcBuffer)object;
        if (!agcCaptureBeginRecord(capture,
                AGC_CAPTURE_RECORD_RESOURCE_DESC, 32u))
            return;
        agcCaptureWriteU64(capture, id);
        agcCaptureWriteU32(capture, type);
        agcCaptureWriteU32(capture, AGC_RUNTIME_STRUCTURE_VERSION_1);
        agcCaptureWriteU64(capture, buffer->size);
        agcCaptureWriteU32(capture, buffer->usage);
        agcCaptureWriteU32(capture, buffer->create_flags);
    } else if (type == AGC_CAPTURE_OBJECT_IMAGE) {
        const AgcImage image = (AgcImage)object;
        if (!agcCaptureBeginRecord(capture,
                AGC_CAPTURE_RECORD_RESOURCE_DESC, 48u))
            return;
        agcCaptureWriteU64(capture, id);
        agcCaptureWriteU32(capture, type);
        agcCaptureWriteU32(capture, image->desc.version);
        agcCaptureWriteU32(capture, image->desc.width);
        agcCaptureWriteU32(capture, image->desc.height);
        agcCaptureWriteU32(capture, image->desc.depth);
        agcCaptureWriteU32(capture, image->desc.mip_levels);
        agcCaptureWriteU32(capture, image->desc.array_layers);
        agcCaptureWriteU32(capture, image->desc.format);
        agcCaptureWriteU32(capture, image->desc.sample_count);
        agcCaptureWriteU32(capture, image->desc.usage);
    } else if (type == AGC_CAPTURE_OBJECT_IMAGE_VIEW) {
        const AgcImageView view = (AgcImageView)object;
        if (!agcCaptureBeginRecord(capture,
                AGC_CAPTURE_RECORD_RESOURCE_DESC, 48u))
            return;
        agcCaptureWriteU64(capture, id);
        agcCaptureWriteU32(capture, type);
        agcCaptureWriteU32(capture, view->desc.version);
        agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
            view->image, AGC_CAPTURE_OBJECT_IMAGE));
        agcCaptureWriteU32(capture, view->desc.format);
        agcCaptureWriteU32(capture, view->desc.base_mip_level);
        agcCaptureWriteU32(capture, view->desc.mip_level_count);
        agcCaptureWriteU32(capture, view->desc.base_array_layer);
        agcCaptureWriteU32(capture, view->desc.array_layer_count);
        agcCaptureWriteU32(capture, 0u);
    } else if (type == AGC_CAPTURE_OBJECT_SAMPLER) {
        const AgcSampler sampler = (AgcSampler)object;
        if (!agcCaptureBeginRecord(capture,
                AGC_CAPTURE_RECORD_RESOURCE_DESC, 40u))
            return;
        agcCaptureWriteU64(capture, id);
        agcCaptureWriteU32(capture, type);
        agcCaptureWriteU32(capture, sampler->desc.version);
        agcCaptureWriteU32(capture, sampler->desc.min_filter);
        agcCaptureWriteU32(capture, sampler->desc.mag_filter);
        agcCaptureWriteU32(capture, sampler->desc.address_u);
        agcCaptureWriteU32(capture, sampler->desc.address_v);
        agcCaptureWriteU32(capture, sampler->desc.address_w);
        agcCaptureWriteU32(capture, sampler->desc.flags);
    }
}

static void agcCaptureRecordShaderDesc(AgcShader shader)
{
    AgcCapture capture = shader->device->active_capture;
    uint64_t id;
    uint64_t front_size = 0u;
    uint64_t code_hash;
    uint64_t front_hash = 0u;

    if (!capture || !capture->active)
        return;
    id = agcCaptureObjectId(capture, shader, AGC_CAPTURE_OBJECT_SHADER);
    if (id == 0u)
        return;
    code_hash = agcShaderHashBytes(UINT64_C(14695981039346656037),
        shader->code, shader->code_size);
    if (shader->has_front_record) {
        front_size = shader->allocation->requested_size -
            shader->front_binary_offset;
        front_hash = agcShaderHashBytes(UINT64_C(14695981039346656037),
            (const uint8_t *)shader->code + shader->front_binary_offset,
            front_size);
    }
    if (agcCaptureBeginRecord(capture, AGC_CAPTURE_RECORD_SHADER_DESC, 64u)) {
        agcCaptureWriteU64(capture, id);
        agcCaptureWriteU32(capture, AGC_CAPTURE_OBJECT_SHADER);
        agcCaptureWriteU32(capture, shader->stage);
        agcCaptureWriteU32(capture, 0u);
        agcCaptureWriteU32(capture,
            shader->has_reflection ? shader->record.version : 0u);
        agcCaptureWriteU32(capture,
            shader->has_front_record ? shader->front_record.version : 0u);
        agcCaptureWriteU32(capture, shader->has_reflection);
        agcCaptureWriteU64(capture, shader->code_size);
        agcCaptureWriteU64(capture, code_hash);
        agcCaptureWriteU64(capture, front_size);
        agcCaptureWriteU64(capture, front_hash);
    }
    if ((capture->flags & AGC_CAPTURE_INCLUDE_SHADER_BYTES_BIT) != 0u &&
        shader->code_size <= UINT32_MAX - 16u &&
        agcCaptureBeginRecord(capture, AGC_CAPTURE_RECORD_SHADER_BYTES,
            16u + (uint32_t)shader->code_size)) {
        agcCaptureWriteU64(capture, id);
        agcCaptureWriteU32(capture, 0u);
        agcCaptureWriteU32(capture, (uint32_t)shader->code_size);
        agcCaptureWrite(capture, shader->code, (size_t)shader->code_size);
    }
    if ((capture->flags & AGC_CAPTURE_INCLUDE_SHADER_BYTES_BIT) != 0u &&
        front_size != 0u && front_size <= UINT32_MAX - 16u &&
        agcCaptureBeginRecord(capture, AGC_CAPTURE_RECORD_SHADER_BYTES,
            16u + (uint32_t)front_size)) {
        agcCaptureWriteU64(capture, id);
        agcCaptureWriteU32(capture, 1u);
        agcCaptureWriteU32(capture, (uint32_t)front_size);
        agcCaptureWrite(capture,
            (const uint8_t *)shader->code + shader->front_binary_offset,
            (size_t)front_size);
    }
}

static void agcCaptureWritePipelineArrays(AgcCapture capture,
    const AgcShaderVertexInput *vertices, uint32_t vertex_count,
    const AgcShaderDescriptorMapping *mappings, uint32_t mapping_count,
    const AgcShaderPushConstantRange *ranges, uint32_t range_count)
{
    uint32_t i;

    for (i = 0u; i < vertex_count; ++i) {
        agcCaptureWriteU32(capture, vertices[i].location);
        agcCaptureWriteU32(capture, vertices[i].binding);
        agcCaptureWriteU32(capture, vertices[i].offset);
        agcCaptureWriteU32(capture, vertices[i].stride);
        agcCaptureWriteU32(capture, vertices[i].format);
        agcCaptureWriteU32(capture, vertices[i].input_rate);
        agcCaptureWriteU32(capture, vertices[i].divisor);
        agcCaptureWriteU32(capture, vertices[i].component_mask);
    }
    for (i = 0u; i < mapping_count; ++i) {
        agcCaptureWriteU32(capture, mappings[i].set);
        agcCaptureWriteU32(capture, mappings[i].binding);
        agcCaptureWriteU32(capture, mappings[i].type);
        agcCaptureWriteU32(capture, mappings[i].array_size);
        agcCaptureWriteU32(capture, mappings[i].byte_offset);
        agcCaptureWriteU32(capture, mappings[i].byte_stride);
    }
    for (i = 0u; i < range_count; ++i) {
        agcCaptureWriteU32(capture, ranges[i].offset);
        agcCaptureWriteU32(capture, ranges[i].size);
        agcCaptureWriteU32(capture, ranges[i].alignment);
        agcCaptureWriteU32(capture, ranges[i].stage_mask);
    }
}

static void agcCaptureRecordGraphicsPipeline(AgcGraphicsPipeline pipeline)
{
    AgcCapture capture = pipeline->device->active_capture;
    uint64_t payload_size;
    uint32_t i;

    if (!capture || !capture->active)
        return;
    payload_size = 120u + (uint64_t)pipeline->vertex_input_count * 32u +
        (uint64_t)pipeline->descriptor_mapping_count * 24u +
        (uint64_t)pipeline->push_constant_range_count * 16u +
        (uint64_t)pipeline->color_attachment_count * 48u;
    if (payload_size > UINT32_MAX) {
        capture->status = AGC_ERROR_OUT_OF_MEMORY;
        return;
    }
    if (!agcCaptureBeginRecord(capture, AGC_CAPTURE_RECORD_PIPELINE_DESC,
            (uint32_t)payload_size))
        return;
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture, pipeline,
        AGC_CAPTURE_OBJECT_GRAPHICS_PIPELINE));
    agcCaptureWriteU32(capture, AGC_CAPTURE_OBJECT_GRAPHICS_PIPELINE);
    agcCaptureWriteU32(capture, AGC_RUNTIME_STRUCTURE_VERSION_4);
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
        pipeline->hull_shader, AGC_CAPTURE_OBJECT_SHADER));
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
        pipeline->primitive_shader, AGC_CAPTURE_OBJECT_SHADER));
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
        pipeline->pixel_shader, AGC_CAPTURE_OBJECT_SHADER));
    agcCaptureWriteU32(capture, pipeline->vertex_input_count);
    agcCaptureWriteU32(capture, pipeline->descriptor_mapping_count);
    agcCaptureWriteU32(capture, pipeline->push_constant_range_count);
    agcCaptureWriteU32(capture, pipeline->color_attachment_count);
    agcCaptureWriteU32(capture, pipeline->dynamic_state_mask);
    agcCaptureWriteU32(capture, pipeline->primitive_type);
    agcCaptureWriteU32(capture, pipeline->depth_stencil.format);
    agcCaptureWriteU32(capture, pipeline->multisample.rasterization_samples);
    agcCaptureWriteU32(capture, pipeline->rasterization.polygon_mode);
    agcCaptureWriteU32(capture, pipeline->rasterization.cull_mode);
    agcCaptureWriteU32(capture, pipeline->rasterization.front_face);
    agcCaptureWriteU32(capture, pipeline->rasterization.depth_clamp_enable);
    agcCaptureWriteU32(capture,
        pipeline->rasterization.rasterizer_discard_enable);
    agcCaptureWriteU32(capture, pipeline->rasterization.depth_bias_enable);
    agcCaptureWriteU32(capture, pipeline->logic_operation_enable);
    agcCaptureWriteU32(capture, pipeline->logic_operation);
    agcCaptureWriteU32(capture,
        agcRuntimeFloatBits(pipeline->static_depth_bias.constant_factor));
    agcCaptureWriteU32(capture,
        agcRuntimeFloatBits(pipeline->static_depth_bias.clamp));
    agcCaptureWriteU32(capture,
        agcRuntimeFloatBits(pipeline->static_depth_bias.slope_factor));
    agcCaptureWriteU32(capture, pipeline->primitive_restart_enable);
    agcCaptureWritePipelineArrays(capture, pipeline->vertex_inputs,
        pipeline->vertex_input_count, pipeline->descriptor_mappings,
        pipeline->descriptor_mapping_count, pipeline->push_constant_ranges,
        pipeline->push_constant_range_count);
    for (i = 0u; i < pipeline->color_attachment_count; ++i) {
        const AgcColorBlendAttachmentState *state =
            &pipeline->color_attachments[i];
        agcCaptureWriteU32(capture, state->format);
        agcCaptureWriteU32(capture, state->blend_enable);
        agcCaptureWriteU32(capture, state->write_mask);
        agcCaptureWriteU32(capture, state->source_color_factor);
        agcCaptureWriteU32(capture, state->destination_color_factor);
        agcCaptureWriteU32(capture, state->color_operation);
        agcCaptureWriteU32(capture, state->source_alpha_factor);
        agcCaptureWriteU32(capture, state->destination_alpha_factor);
        agcCaptureWriteU32(capture, state->alpha_operation);
        agcCaptureWriteU32(capture, state->flags);
        agcCaptureWriteU32(capture, 0u);
        agcCaptureWriteU32(capture, 0u);
    }
}

static void agcCaptureRecordComputePipeline(AgcComputePipeline pipeline)
{
    AgcCapture capture = pipeline->device->active_capture;
    uint64_t payload_size;

    if (!capture || !capture->active)
        return;
    payload_size = 48u +
        (uint64_t)pipeline->descriptor_mapping_count * 24u +
        (uint64_t)pipeline->push_constant_range_count * 16u;
    if (payload_size > UINT32_MAX) {
        capture->status = AGC_ERROR_OUT_OF_MEMORY;
        return;
    }
    if (!agcCaptureBeginRecord(capture, AGC_CAPTURE_RECORD_PIPELINE_DESC,
            (uint32_t)payload_size))
        return;
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture, pipeline,
        AGC_CAPTURE_OBJECT_COMPUTE_PIPELINE));
    agcCaptureWriteU32(capture, AGC_CAPTURE_OBJECT_COMPUTE_PIPELINE);
    agcCaptureWriteU32(capture, AGC_RUNTIME_STRUCTURE_VERSION_2);
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture,
        pipeline->shader, AGC_CAPTURE_OBJECT_SHADER));
    agcCaptureWriteU32(capture, pipeline->local_size[0]);
    agcCaptureWriteU32(capture, pipeline->local_size[1]);
    agcCaptureWriteU32(capture, pipeline->local_size[2]);
    agcCaptureWriteU32(capture, pipeline->descriptor_mapping_count);
    agcCaptureWriteU32(capture, pipeline->push_constant_range_count);
    agcCaptureWriteU32(capture, 0u);
    agcCaptureWritePipelineArrays(capture, NULL, 0u,
        pipeline->descriptor_mappings, pipeline->descriptor_mapping_count,
        pipeline->push_constant_ranges, pipeline->push_constant_range_count);
}

static void agcCaptureRecordTransition(AgcCommandBuffer command_buffer,
    const AgcResourceTransition *transition, void *resource, uint32_t flags,
    AgcGpuLabel label, uint32_t value)
{
    AgcCapture capture = command_buffer->device->active_capture;
    uint32_t capture_type;
    uint64_t resource_id;

    if (!capture || !capture->active)
        return;
    capture_type = transition->resource_type == kAgcResourceTypeBuffer ?
        AGC_CAPTURE_OBJECT_BUFFER : AGC_CAPTURE_OBJECT_IMAGE;
    resource_id = agcCaptureObjectId(capture, resource, capture_type);
    if (resource_id == 0u || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_RESOURCE_TRANSITION, 80u))
        return;
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture, command_buffer,
        AGC_CAPTURE_OBJECT_COMMAND_BUFFER));
    agcCaptureWriteU64(capture, resource_id);
    agcCaptureWriteU64(capture, agcCaptureObjectId(capture, label,
        AGC_CAPTURE_OBJECT_GPU_LABEL));
    agcCaptureWriteU32(capture, transition->resource_type);
    agcCaptureWriteU32(capture, flags);
    agcCaptureWriteU32(capture, transition->before);
    agcCaptureWriteU32(capture, transition->before_owner);
    agcCaptureWriteU32(capture, transition->after);
    agcCaptureWriteU32(capture, transition->after_owner);
    agcCaptureWriteU32(capture, value);
    agcCaptureWriteU32(capture, 0u);
    if (transition->resource_type == kAgcResourceTypeBuffer) {
        agcCaptureWriteU64(capture, transition->buffer_offset);
        agcCaptureWriteU64(capture, transition->buffer_size);
        agcCaptureWriteU64(capture, 0u);
    } else {
        agcCaptureWriteU32(capture, transition->image_range.aspect_mask);
        agcCaptureWriteU32(capture, transition->image_range.base_mip_level);
        agcCaptureWriteU32(capture, transition->image_range.mip_level_count);
        agcCaptureWriteU32(capture,
            transition->image_range.base_array_layer);
        agcCaptureWriteU32(capture,
            transition->image_range.array_layer_count);
        agcCaptureWriteU32(capture, 0u);
    }
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

static void *agcBufferCpuAddress(const AgcBuffer buffer)
{
    return (uint8_t *)agcAllocationCpuAddress(buffer->allocation) +
        buffer->memory_offset;
}

static uint64_t agcBufferGpuAddress(const AgcBuffer buffer)
{
    return agcAllocationGpuAddress(buffer->allocation) + buffer->memory_offset;
}

static void *agcImageCpuAddress(const AgcImage image)
{
    return (uint8_t *)agcAllocationCpuAddress(image->allocation) +
        image->memory_offset;
}

static uint64_t agcImageGpuAddress(const AgcImage image)
{
    return agcAllocationGpuAddress(image->allocation) + image->memory_offset;
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

static int32_t agcInvalidateRuntimeAllocation(
    const AgcRuntimeAllocation *allocation, uint64_t offset, uint64_t size)
{
    uint64_t absolute_offset;

    if (!allocation || size == 0u || offset > allocation->requested_size ||
        size > allocation->requested_size - offset || size > SIZE_MAX ||
        !agcAddU64(allocation->offset, offset, &absolute_offset) ||
        absolute_offset > SIZE_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    return agcGpuMemoryInvalidate(&allocation->block->memory,
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

static void agcRuntimeDestroyTessellationStorage(AgcDevice device)
{
    if (device->tessellation_table_allocation)
        agcRuntimeFree(device, device->tessellation_table_allocation);
    if (device->tessellation_factor_allocation)
        agcRuntimeFree(device, device->tessellation_factor_allocation);
    if (device->tessellation_offchip_allocation)
        agcRuntimeFree(device, device->tessellation_offchip_allocation);
    device->tessellation_table_allocation = NULL;
    device->tessellation_factor_allocation = NULL;
    device->tessellation_offchip_allocation = NULL;
    device->tessellation_initialized = 0u;
}

static int agcRuntimeTessellationSupported(AgcDevice device)
{
    (void)device;
#ifdef OPENAGC_PROSPERO
    {
        AgcDriverRuntimeDiagnostics diagnostics;
        return agcDriverDebugRuntimeProfile(&diagnostics) == AGC_OK &&
            diagnostics.profile.supports_tf_ring;
    }
#else
    return 1;
#endif
}

static int32_t agcRuntimeInitializeTessellationStorage(AgcDevice device)
{
    AgcGfx1013TessellationRingTable table;
    AgcGfx1013TessellationState state;
    int32_t result;

    if (device->tessellation_initialized)
        return AGC_OK;
    if (!agcRuntimeTessellationSupported(device))
        return AGC_ERROR_NOT_SUPPORTED;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_GARLIC,
        AGC_GFX1013_TESS_OFFCHIP_RING_SIZE, AGC_GARLIC_ALIGNMENT, 0u,
        AGC_OBJECT_TYPE_COUNT, device,
        &device->tessellation_offchip_allocation);
    if (result != AGC_OK)
        return result;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_GARLIC,
        AGC_GFX1013_TESS_FACTOR_RING_SIZE, AGC_GARLIC_ALIGNMENT, 0u,
        AGC_OBJECT_TYPE_COUNT, device,
        &device->tessellation_factor_allocation);
    if (result != AGC_OK)
        goto fail;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        sizeof(table), 256u, 0u, AGC_OBJECT_TYPE_COUNT, device,
        &device->tessellation_table_allocation);
    if (result != AGC_OK)
        goto fail;
    (void)snprintf(device->tessellation_offchip_allocation->debug_name,
        AGC_RUNTIME_DEBUG_NAME_SIZE, "%s", "runtime-tess-offchip-ring");
    (void)snprintf(device->tessellation_factor_allocation->debug_name,
        AGC_RUNTIME_DEBUG_NAME_SIZE, "%s", "runtime-tess-factor-ring");
    (void)snprintf(device->tessellation_table_allocation->debug_name,
        AGC_RUNTIME_DEBUG_NAME_SIZE, "%s", "runtime-tess-ring-table");
    memset(&state, 0, sizeof(state));
    state.offchip_ring_address = agcAllocationGpuAddress(
        device->tessellation_offchip_allocation);
    state.factor_ring_address = agcAllocationGpuAddress(
        device->tessellation_factor_allocation);
    state.offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE;
    state.factor_ring_size = AGC_GFX1013_TESS_FACTOR_RING_SIZE;
    state.offchip_param = AGC_GFX1013_TESS_OFFCHIP_PARAM;
    result = agcGfx1013BuildTessellationRingTable(&table, &state);
    if (result != AGC_OK)
        goto fail;
    memset(agcAllocationCpuAddress(device->tessellation_offchip_allocation),
        0, AGC_GFX1013_TESS_OFFCHIP_RING_SIZE);
    memset(agcAllocationCpuAddress(device->tessellation_factor_allocation),
        0, AGC_GFX1013_TESS_FACTOR_RING_SIZE);
    memcpy(agcAllocationCpuAddress(device->tessellation_table_allocation),
        &table, sizeof(table));
    result = agcFlushRuntimeAllocation(
        device->tessellation_offchip_allocation, 0u,
        AGC_GFX1013_TESS_OFFCHIP_RING_SIZE);
    if (result == AGC_OK)
        result = agcFlushRuntimeAllocation(
            device->tessellation_factor_allocation, 0u,
            AGC_GFX1013_TESS_FACTOR_RING_SIZE);
    if (result == AGC_OK)
        result = agcFlushRuntimeAllocation(
            device->tessellation_table_allocation, 0u, sizeof(table));
    if (result == AGC_OK)
        result = sceAgcDriverSetTFRing((uintptr_t)agcAllocationGpuAddress(
            device->tessellation_factor_allocation),
            AGC_GFX1013_TESS_FACTOR_RING_SIZE);
    if (result != AGC_OK)
        goto fail;
    device->tessellation_initialized = 1u;
    return AGC_OK;

fail:
    agcRuntimeDestroyTessellationStorage(device);
    return result;
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
            info->firmware_version = diagnostics.firmware_version;
            info->firmware_abi_key = key;
            info->hardware_family = diagnostics.profile.is_trinity ?
                AGC_HARDWARE_FAMILY_TRINITY_PS5 :
                AGC_HARDWARE_FAMILY_STANDARD_PS5;
            /* Direct-carrier qualification does not qualify an unrun native
             * runtime slice. Keep its capability labels host-tested until a
             * public runtime artifact passes its own exact-firmware oracle. */
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
    int initialize_backend;
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
    agcDeviceRegistryLock();
    if (g_device_count != 0u && desc->agc_version != g_backend_agc_version) {
        agcDeviceRegistryUnlock();
        return AGC_ERROR_BUSY;
    }
    initialize_backend = g_device_count == 0u;
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
    if (!device) {
        agcDeviceRegistryUnlock();
        return AGC_ERROR_OUT_OF_MEMORY;
    }
    device->allocation = allocation;
    device->magic = AGC_MAGIC_DEVICE;

    result = AGC_OK;
    if (initialize_backend) {
        result = sceAgcInit(desc->agc_version);
        if (result == AGC_OK)
            result = sce_agc_initialize_internal_memory();
        if (result == AGC_OK)
            result = sceAgcDriverNotifyDefaultStates(0u);
    }
    if (result != AGC_OK) {
        if (initialize_backend)
            (void)agcDriverShutdown();
        device->magic = 0u;
        if (allocation.free)
            allocation.free(allocation.user_data, device);
        else
            free(device);
        agcDeviceRegistryUnlock();
        return result;
    }

    agcPopulateRuntimeInfo(device, desc->agc_version);
    if ((desc->required_capability_bits & ~device->runtime_info.capability_bits) !=
            0u) {
        if (initialize_backend)
            (void)agcDriverShutdown();
        device->magic = 0u;
        agcFree(device, device);
        agcDeviceRegistryUnlock();
        return AGC_ERROR_NOT_SUPPORTED;
    }
    device->next_device = g_devices;
    g_devices = device;
    g_device_count++;
    if (initialize_backend)
        g_backend_agc_version = desc->agc_version;
    agcDeviceRegistryUnlock();
    *device_out = device;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyDevice(AgcDevice device)
{
    AgcAllocationCallbacks allocation;
    AgcDevice *link;
    int last_device;
    int32_t result;

    agcDeviceRegistryLock();
    if (!agcDeviceRegistered(device)) {
        agcDeviceRegistryUnlock();
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (device->child_count != 0u) {
        agcDeviceRegistryUnlock();
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT, AGC_ERROR_BUSY,
            "agcDestroyDevice", AGC_DEBUG_OBJECT_TYPE_NONE, NULL,
            "device destruction requires all child objects to be destroyed");
    }
    last_device = g_device_count == 1u;
    if (last_device) {
        result = agcDriverShutdown();
        if (result != AGC_OK) {
            agcDeviceRegistryUnlock();
            return result;
        }
    }
    link = &g_devices;
    while (*link != device)
        link = &(*link)->next_device;
    *link = device->next_device;
    device->next_device = NULL;
    g_device_count--;
    if (last_device)
        g_backend_agc_version = 0u;
    agcDeviceRegistryUnlock();
    agcRuntimeDestroyTessellationStorage(device);
    if (device->border_color_allocation)
        agcRuntimeFree(device, device->border_color_allocation);
    agcDestroyMemoryBlocks(device);
    allocation = device->allocation;
    device->magic = 0u;
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

int32_t PS5_SYSV_ABI agcGetDeviceProperties(
    AgcDevice device, AgcDeviceProperties *properties)
{
    AgcGfx1013Capabilities capabilities;
    uint32_t i;

    if ((device && !agcDeviceValid(device)) || !properties ||
        !agcHeaderValid(properties->struct_size, sizeof(*properties),
            properties->version) ||
        !agcReservedZero(properties->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (agcGfx1013GetCapabilities(&capabilities) != AGC_OK)
        return AGC_ERROR_NOT_SUPPORTED;
    properties->max_image_dimension_1d = capabilities.max_image_dimension_1d;
    properties->max_image_dimension_2d = capabilities.max_image_dimension_2d;
    properties->max_image_dimension_3d = capabilities.max_image_dimension_3d;
    properties->max_image_dimension_cube = capabilities.max_image_dimension_cube;
    properties->max_image_array_layers = capabilities.max_image_array_layers;
    properties->max_color_targets = capabilities.max_color_targets;
    properties->subgroup_size = capabilities.subgroup_size;
    properties->max_compute_shared_memory_size =
        capabilities.max_compute_shared_memory_size;
    properties->max_compute_workgroup_invocations =
        capabilities.max_compute_workgroup_invocations;
    memcpy(properties->max_compute_workgroup_size,
        capabilities.max_compute_workgroup_size,
        sizeof(properties->max_compute_workgroup_size));
    properties->color_sample_counts = capabilities.color_sample_counts;
    properties->depth_sample_counts = capabilities.depth_sample_counts;
    properties->color_target_format_mask =
        capabilities.color_target_format_mask;
    properties->depth_stencil_format_mask =
        capabilities.depth_stencil_format_mask;
    properties->memory_heap_count = capabilities.memory_profile_count;
    for (i = 0u; i < properties->memory_heap_count; ++i) {
        properties->memory_heaps[i].size = capabilities.memory_profiles[i].size;
        properties->memory_heaps[i].minimum_alignment =
            capabilities.memory_profiles[i].minimum_alignment;
        properties->memory_heaps[i].property_flags =
            capabilities.memory_profiles[i].property_flags;
        properties->memory_heaps[i].reserved = 0u;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateCapture(AgcDevice device,
    const AgcCaptureDesc *desc, AgcCapture *capture_out)
{
    AgcCapture capture;

    if (!capture_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *capture_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        (desc->flags & ~AGC_CAPTURE_INCLUDE_SHADER_BYTES_BIT) != 0u ||
        desc->reserved0 != 0u || !desc->write ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    capture = agcCreateChild(device, sizeof(*capture));
    if (!capture)
        return AGC_ERROR_OUT_OF_MEMORY;
    capture->magic = AGC_MAGIC_CAPTURE;
    capture->flags = desc->flags;
    capture->device = device;
    capture->write = desc->write;
    capture->user_data = desc->user_data;
    capture->status = AGC_OK;
    capture->next_object_id = 1u;
    *capture_out = capture;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyCapture(AgcCapture capture)
{
    AgcDevice device;

    if (!capture || capture->magic != AGC_MAGIC_CAPTURE ||
        !agcDeviceValid(capture->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (capture->active)
        return AGC_ERROR_BUSY;
    device = capture->device;
    agcCaptureClearObjects(capture);
    capture->magic = 0u;
    agcDestroyChild(device, capture);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcBeginCapture(AgcCapture capture)
{
    static const uint8_t magic[8] = {
        AGC_CAPTURE_MAGIC_BYTE_0, AGC_CAPTURE_MAGIC_BYTE_1,
        AGC_CAPTURE_MAGIC_BYTE_2, AGC_CAPTURE_MAGIC_BYTE_3,
        AGC_CAPTURE_MAGIC_BYTE_4, AGC_CAPTURE_MAGIC_BYTE_5,
        AGC_CAPTURE_MAGIC_BYTE_6, AGC_CAPTURE_MAGIC_BYTE_7
    };
    AgcDevice device;

    if (!capture || capture->magic != AGC_MAGIC_CAPTURE ||
        !agcDeviceValid(capture->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    device = capture->device;
    if (capture->active || device->active_capture)
        return AGC_ERROR_BUSY;
    agcCaptureClearObjects(capture);
    capture->active = 1u;
    capture->status = AGC_OK;
    capture->record_count = 0u;
    capture->byte_count = 0u;
    capture->sequence = 0u;
    capture->next_object_id = 1u;
    device->active_capture = capture;
    agcCaptureWrite(capture, magic, sizeof(magic));
    agcCaptureWriteU32(capture, AGC_CAPTURE_FORMAT_VERSION);
    agcCaptureWriteU32(capture, AGC_CAPTURE_FILE_HEADER_SIZE);
    agcCaptureWriteU32(capture, AGC_CAPTURE_ENDIAN_TAG);
    agcCaptureWriteU32(capture, AGC_RUNTIME_API_VERSION);
    agcCaptureWriteU32(capture, capture->flags);
    agcCaptureWriteU32(capture, 0u);
    agcCaptureRecordRuntimeInfo(capture);
    return capture->status;
}

int32_t PS5_SYSV_ABI agcEndCapture(AgcCapture capture)
{
    int32_t status;

    if (!capture || capture->magic != AGC_MAGIC_CAPTURE ||
        !agcDeviceValid(capture->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (!capture->active || capture->device->active_capture != capture)
        return AGC_ERROR_INVALID_STATE;
    status = capture->status;
    if (status == AGC_OK && agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_END, 24u)) {
        uint64_t final_byte_count = capture->byte_count + 24u;
        agcCaptureWriteU32(capture, (uint32_t)status);
        agcCaptureWriteU32(capture, 0u);
        agcCaptureWriteU64(capture, capture->record_count);
        agcCaptureWriteU64(capture, final_byte_count);
        status = capture->status;
    }
    capture->device->active_capture = NULL;
    capture->active = 0u;
    agcCaptureClearObjects(capture);
    return status;
}

int32_t PS5_SYSV_ABI agcGetCaptureInfo(
    AgcCapture capture, AgcCaptureInfo *info)
{
    if (!capture || capture->magic != AGC_MAGIC_CAPTURE ||
        !agcDeviceValid(capture->device) || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        !agcReservedZero(info->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    info->active = capture->active;
    info->status = capture->status;
    info->record_count = capture->record_count;
    info->byte_count = capture->byte_count;
    info->next_object_id = capture->next_object_id;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCaptureRecordReadbackHash(AgcCapture capture,
    AgcCaptureObjectType object_type, const void *object,
    uint64_t offset, uint64_t size)
{
    AgcRuntimeAllocation *allocation;
    const uint8_t *bytes;
    uint64_t object_size;
    uint64_t allocation_offset;
    uint64_t object_id;
    uint64_t hash;
    int32_t result;

    if (!capture || capture->magic != AGC_MAGIC_CAPTURE ||
        !agcDeviceValid(capture->device) || !capture->active ||
        capture->device->active_capture != capture || !object || size == 0u ||
        size > SIZE_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (object_type == AGC_CAPTURE_OBJECT_BUFFER) {
        AgcBuffer buffer = (AgcBuffer)object;
        if (buffer->magic != AGC_MAGIC_BUFFER ||
            buffer->device != capture->device || buffer->deferred ||
            (buffer->create_flags & AGC_BUFFER_CREATE_READBACK_BIT) == 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        allocation = buffer->allocation;
        bytes = buffer->storage;
        object_size = buffer->size;
        allocation_offset = buffer->memory_offset;
    } else if (object_type == AGC_CAPTURE_OBJECT_IMAGE) {
        AgcImage image = (AgcImage)object;
        if (image->magic != AGC_MAGIC_IMAGE ||
            image->device != capture->device || image->deferred ||
            (image->desc.usage & AGC_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        allocation = image->allocation;
        bytes = agcImageCpuAddress(image);
        object_size = image->layout.allocation_size;
        allocation_offset = image->memory_offset;
    } else {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (offset > object_size || size > object_size - offset ||
        allocation_offset > UINT64_MAX - offset ||
        allocation->offset > SIZE_MAX - (allocation_offset + offset))
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGpuMemoryInvalidate(&allocation->block->memory,
        (size_t)(allocation->offset + allocation_offset + offset),
        (size_t)size);
    if (result != AGC_OK)
        return result;
    hash = agcShaderHashBytes(UINT64_C(14695981039346656037),
        bytes + offset, size);
    object_id = agcCaptureObjectId(capture, object, object_type);
    if (object_id == 0u || !agcCaptureBeginRecord(capture,
            AGC_CAPTURE_RECORD_READBACK_HASH, 40u))
        return capture->status;
    agcCaptureWriteU64(capture, object_id);
    agcCaptureWriteU32(capture, object_type);
    agcCaptureWriteU32(capture, AGC_CAPTURE_HASH_FNV1A64);
    agcCaptureWriteU64(capture, offset);
    agcCaptureWriteU64(capture, size);
    agcCaptureWriteU64(capture, hash);
    return capture->status;
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
#ifdef OPENAGC_PROSPERO
        /* The qualified FW 5.50 compute stream uses the direct DCB carrier
         * after async graphics setup.  A user-special queue/ACB submission
         * accepted the runtime stream but did not reach its EOP fence. */
        handle = sceAgcDriverSetupAsyncGraphics(1u);
        if (handle != AGC_OK)
            return handle;
        handle = -1;
#else
        handle = sceAgcDriverSetupAsyncGraphics(0u);
        if (handle != AGC_OK)
            return handle;
        handle = _sceAgcDriverCreateUserSpecialQueue();
        if (handle < 0)
            return handle;
#endif
    } else {
        return AGC_ERROR_NOT_SUPPORTED;
    }
    queue = agcCreateChild(device, sizeof(*queue));
    if (!queue) {
#ifndef OPENAGC_PROSPERO
        if (desc->type == kAgcQueueCompute)
            (void)_sceAgcDriverDestroyUserSpecialQueue();
#endif
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
    agcCaptureRecordObjectCreate(device, queue, AGC_CAPTURE_OBJECT_QUEUE,
        (uint32_t)queue->type, desc->priority);
    *queue_out = queue;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyQueue(AgcQueue queue)
{
    AgcDevice device;
#ifndef OPENAGC_PROSPERO
    int32_t result;
#endif

    if (!queue || queue->magic != AGC_MAGIC_QUEUE ||
        !agcDeviceValid(queue->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (queue->pending_count != 0u || queue->label_refs != 0u)
        return AGC_ERROR_BUSY;
    device = queue->device;
    if (queue->type == kAgcQueueCompute) {
#ifndef OPENAGC_PROSPERO
        result = agcDriverDestroyUserSpecialQueueHandle(
            queue->backend_handle);
        if (result != AGC_OK)
            return result;
#endif
        device->compute_queue_count--;
    } else {
        device->graphics_queue_count--;
    }
    agcCaptureRecordObjectDestroy(device, queue, AGC_CAPTURE_OBJECT_QUEUE);
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
    case AGC_FORMAT_R8_UNORM:
        info->bytes[0] = 1u;
        return 1;
    case AGC_FORMAT_RG8_UNORM:
    case AGC_FORMAT_R16_FLOAT:
        info->bytes[0] = 2u;
        return 1;
    case AGC_FORMAT_RGBA8_UNORM:
    case AGC_FORMAT_BGRA8_UNORM:
    case AGC_FORMAT_RGBA8_SRGB:
    case AGC_FORMAT_BGRA8_SRGB:
    case AGC_FORMAT_RGB10A2_UNORM:
    case AGC_FORMAT_RG16_FLOAT:
    case AGC_FORMAT_R32_FLOAT:
    case AGC_FORMAT_R11G11B10_FLOAT:
        info->bytes[0] = 4u;
        return 1;
    case AGC_FORMAT_RG32_FLOAT:
    case AGC_FORMAT_RGBA16_FLOAT:
    case AGC_FORMAT_RGBA16_UINT:
    case AGC_FORMAT_RGBA16_SINT:
        info->bytes[0] = 8u;
        return 1;
    case AGC_FORMAT_RGBA32_FLOAT:
    case AGC_FORMAT_RGBA32_UINT:
    case AGC_FORMAT_RGBA32_SINT:
        info->bytes[0] = 16u;
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

static int agcImageDescHeaderValid(const AgcImageDesc *desc)
{
    return desc &&
        ((desc->version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
          desc->struct_size == offsetof(AgcImageDesc, tiling)) ||
         (desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2 &&
          desc->struct_size == sizeof(*desc)));
}

static AgcImageTiling agcImageDescTiling(const AgcImageDesc *desc)
{
    return desc->version >= AGC_RUNTIME_STRUCTURE_VERSION_2 ? desc->tiling :
        AGC_IMAGE_TILING_OPTIMAL;
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

    if (!agcImageDescHeaderValid(desc) ||
        desc->width == 0u || desc->height == 0u || desc->depth == 0u ||
        desc->width > 0x4000u || desc->height > 0x4000u ||
        desc->depth > 0x2000u ||
        desc->mip_levels == 0u || desc->array_layers == 0u ||
        desc->mip_levels > 15u || desc->array_layers > 0x2000u ||
        (desc->sample_count != 1u && desc->sample_count != 4u) ||
        desc->usage == 0u || (desc->usage & ~known_usage) != 0u ||
        !agcReservedZero(desc->reserved, 4u) ||
        (desc->version >= AGC_RUNTIME_STRUCTURE_VERSION_2 &&
         (desc->tiling > AGC_IMAGE_TILING_OPTIMAL || desc->flags != 0u)) ||
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
             desc->array_layers != 1u ||
             agcImageDescTiling(desc) != AGC_IMAGE_TILING_OPTIMAL)) ||
        ((desc->usage & AGC_IMAGE_USAGE_COLOR_TARGET_BIT) != 0u &&
            format.depth_stencil) ||
        ((desc->usage & AGC_IMAGE_USAGE_SCANOUT_BIT) != 0u &&
            ((desc->format != AGC_FORMAT_RGBA8_UNORM &&
              desc->format != AGC_FORMAT_BGRA8_SRGB) || desc->depth != 1u ||
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
        (!format.depth_stencil ||
         agcImageDescTiling(desc) != AGC_IMAGE_TILING_OPTIMAL))
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
        aggregate->alignment = AGC_FLEXIBLE_ALIGNMENT;
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
    if (desc && (desc->usage & AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) != 0u &&
        agcImageDescTiling(desc) == AGC_IMAGE_TILING_OPTIMAL)
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
    if (desc && (desc->usage & AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) != 0u &&
        agcImageDescTiling(desc) == AGC_IMAGE_TILING_OPTIMAL)
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

static int agcBufferCommittedRangeState(const AgcBuffer buffer,
    uint64_t offset, uint64_t size, AgcResourceUsage *usage,
    AgcResourceOwner *owner)
{
    const uint64_t end = offset + size;
    uint64_t start = 0u;
    uint32_t i;
    int found = 0;

    for (i = 0u; i < buffer->state_range_count; ++i) {
        const AgcRuntimeBufferStateRange *range = &buffer->state_ranges[i];

        if (range->end > offset && start < end) {
            if (!found) {
                *usage = range->usage;
                *owner = range->owner;
                found = 1;
            } else if (*usage != range->usage || *owner != range->owner) {
                return 0;
            }
        }
        start = range->end;
        if (start >= end)
            break;
    }
    return found;
}

static void agcBufferCommittedStateAt(const AgcBuffer buffer, uint64_t offset,
    AgcResourceUsage *usage, AgcResourceOwner *owner, uint64_t *next_boundary)
{
    uint32_t i;

    for (i = 0u; i < buffer->state_range_count; ++i) {
        if (offset < buffer->state_ranges[i].end) {
            *usage = buffer->state_ranges[i].usage;
            *owner = buffer->state_ranges[i].owner;
            *next_boundary = buffer->state_ranges[i].end;
            return;
        }
    }
    *usage = kAgcResourceUsageUndefined;
    *owner = kAgcResourceOwnerHost;
    *next_boundary = buffer->size;
}

static int32_t agcBufferEnsureStateCapacity(
    AgcBuffer buffer, uint32_t required)
{
    AgcRuntimeBufferStateRange *ranges;
    uint32_t capacity;

    if (required <= buffer->state_range_capacity)
        return AGC_OK;
    if (required > AGC_RUNTIME_MAX_BUFFER_STATE_RANGES)
        return AGC_ERROR_OUT_OF_MEMORY;
    capacity = buffer->state_range_capacity;
    while (capacity < required) {
        if (capacity > AGC_RUNTIME_MAX_BUFFER_STATE_RANGES / 2u) {
            capacity = AGC_RUNTIME_MAX_BUFFER_STATE_RANGES;
            break;
        }
        capacity *= 2u;
    }
    ranges = agcAlloc(buffer->device,
        (size_t)capacity * sizeof(*ranges),
        _Alignof(AgcRuntimeBufferStateRange));
    if (!ranges)
        return AGC_ERROR_OUT_OF_MEMORY;
    memcpy(ranges, buffer->state_ranges,
        (size_t)buffer->state_range_count * sizeof(*ranges));
    if (buffer->state_ranges != &buffer->inline_state_range)
        agcFree(buffer->device, buffer->state_ranges);
    buffer->state_ranges = ranges;
    buffer->state_range_capacity = capacity;
    return AGC_OK;
}

static void agcBufferCommitRangeState(AgcBuffer buffer, uint64_t offset,
    uint64_t size, AgcResourceUsage usage, AgcResourceOwner owner)
{
    const uint64_t end = offset + size;
    AgcRuntimeBufferStateRange first_range;
    AgcRuntimeBufferStateRange last_range;
    uint64_t first_start;
    uint32_t first = 0u;
    uint32_t last;
    uint32_t tail_count;
    uint32_t destination;
    uint32_t write;
    uint32_t read;
    int left;
    int right;

    while (buffer->state_ranges[first].end <= offset)
        first++;
    last = first;
    while (buffer->state_ranges[last].end < end)
        last++;
    first_range = buffer->state_ranges[first];
    last_range = buffer->state_ranges[last];
    first_start = first == 0u ? 0u : buffer->state_ranges[first - 1u].end;
    left = offset > first_start;
    right = end < last_range.end;
    tail_count = buffer->state_range_count - last - 1u;
    destination = first + (uint32_t)left + 1u + (uint32_t)right;
    memmove(&buffer->state_ranges[destination],
        &buffer->state_ranges[last + 1u],
        (size_t)tail_count * sizeof(*buffer->state_ranges));
    write = first;
    if (left) {
        buffer->state_ranges[write] = first_range;
        buffer->state_ranges[write++].end = offset;
    }
    buffer->state_ranges[write++] = (AgcRuntimeBufferStateRange){
        end, usage, owner };
    if (right) {
        buffer->state_ranges[write++] = last_range;
    }
    buffer->state_range_count = destination + tail_count;

    write = 0u;
    for (read = 0u; read < buffer->state_range_count; ++read) {
        if (write != 0u &&
            buffer->state_ranges[write - 1u].usage ==
                buffer->state_ranges[read].usage &&
            buffer->state_ranges[write - 1u].owner ==
                buffer->state_ranges[read].owner) {
            buffer->state_ranges[write - 1u].end =
                buffer->state_ranges[read].end;
        } else {
            buffer->state_ranges[write++] = buffer->state_ranges[read];
        }
    }
    buffer->state_range_count = write;
}

static void agcReleasePlacedMemory(AgcMemory memory)
{
    AgcDevice device;

    if (!memory)
        return;
    memory->resource_refs--;
    if (memory->resource_refs != 0u || !memory->released)
        return;
    device = memory->device;
    agcRuntimeFree(device, memory->allocation);
    memory->magic = 0u;
    agcDestroyChild(device, memory);
}

int32_t PS5_SYSV_ABI agcCreateMemory(
    AgcDevice device, const AgcMemoryDesc *desc, AgcMemory *memory_out)
{
    AgcMemory memory;
    uint64_t alignment;
    uint64_t maximum_alignment;
    int32_t result;

    if (!memory_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *memory_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->size == 0u || desc->size > SIZE_MAX ||
        (uint32_t)desc->heap >= AGC_MEMORY_HEAP_COUNT ||
        (desc->flags & ~AGC_MEMORY_CREATE_DEDICATED_BIT) != 0u ||
        !agcReservedZero(desc->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    alignment = desc->alignment ? desc->alignment :
        (desc->heap == AGC_MEMORY_HEAP_FLEXIBLE ? AGC_FLEXIBLE_ALIGNMENT :
            AGC_GARLIC_ALIGNMENT);
    maximum_alignment = desc->heap == AGC_MEMORY_HEAP_FLEXIBLE ?
        AGC_FLEXIBLE_BLOCK_ALIGNMENT : UINT64_MAX;
    if ((alignment & (alignment - 1u)) != 0u ||
        alignment > maximum_alignment)
        return AGC_ERROR_INVALID_ALIGNMENT;
    memory = agcCreateChild(device, sizeof(*memory));
    if (!memory)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, desc->heap, desc->size, alignment,
        (desc->flags & AGC_MEMORY_CREATE_DEDICATED_BIT) != 0u,
        AGC_OBJECT_TYPE_MEMORY, memory, &memory->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, memory);
        return result;
    }
    memory->magic = AGC_MAGIC_MEMORY;
    memory->device = device;
    memory->size = desc->size;
    memory->heap = desc->heap;
    *memory_out = memory;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyMemory(AgcMemory memory)
{
    AgcDevice device;

    if (!memory || memory->magic != AGC_MAGIC_MEMORY ||
        !agcDeviceValid(memory->device) || memory->released)
        return AGC_ERROR_INVALID_ARGUMENT;
    memory->released = 1u;
    if (memory->resource_refs != 0u)
        return AGC_OK;
    device = memory->device;
    agcRuntimeFree(device, memory->allocation);
    memory->magic = 0u;
    agcDestroyChild(device, memory);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcMapMemory(
    AgcMemory memory, uint64_t offset, uint64_t size, void **data_out)
{
    if (!data_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *data_out = NULL;
    if (!memory || memory->magic != AGC_MAGIC_MEMORY || memory->released ||
        !agcDeviceValid(memory->device) || size == 0u ||
        offset > memory->size || size > memory->size - offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    *data_out = (uint8_t *)agcAllocationCpuAddress(memory->allocation) + offset;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcUnmapMemory(AgcMemory memory)
{
    if (!memory || memory->magic != AGC_MAGIC_MEMORY || memory->released ||
        !agcDeviceValid(memory->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcFlushMemory(
    AgcMemory memory, uint64_t offset, uint64_t size)
{
    if (!memory || memory->magic != AGC_MAGIC_MEMORY || memory->released ||
        !agcDeviceValid(memory->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    return agcFlushRuntimeAllocation(memory->allocation, offset, size);
}

int32_t PS5_SYSV_ABI agcInvalidateMemory(
    AgcMemory memory, uint64_t offset, uint64_t size)
{
    if (!memory || memory->magic != AGC_MAGIC_MEMORY || memory->released ||
        !agcDeviceValid(memory->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    return agcInvalidateRuntimeAllocation(memory->allocation, offset, size);
}

static int32_t agcCreateBufferInternal(AgcDevice device,
    const AgcBufferDesc *desc, AgcMemory memory, uint64_t memory_offset,
    AgcBuffer *buffer_out)
{
    AgcBuffer buffer;
    uint32_t heap;
    const uint32_t valid_usage = AGC_BUFFER_USAGE_INDEX_BIT |
        AGC_BUFFER_USAGE_VERTEX_BIT | AGC_BUFFER_USAGE_UNIFORM_BIT |
        AGC_BUFFER_USAGE_STORAGE_BIT | AGC_BUFFER_USAGE_TRANSFER_SRC_BIT |
        AGC_BUFFER_USAGE_TRANSFER_DST_BIT | AGC_BUFFER_USAGE_INDIRECT_BIT |
        AGC_BUFFER_USAGE_QUERY_BIT;
    uint32_t valid_flags = AGC_BUFFER_CREATE_UPLOAD_BIT |
        AGC_BUFFER_CREATE_READBACK_BIT | AGC_BUFFER_CREATE_DEDICATED_BIT;
    uint64_t alignment;
    int32_t result;

    if (!buffer_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *buffer_out = NULL;
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->size == 0u || desc->size > SIZE_MAX || desc->usage == 0u ||
        (desc->usage & ~valid_usage) != 0u ||
        (desc->flags & ~valid_flags) != 0u ||
        !agcReservedZero(desc->reserved, 4u)) {
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCreateBuffer",
            AGC_OBJECT_TYPE_BUFFER, NULL,
            "buffer descriptor has an invalid version, size, usage, flags, or reserved field");
    }
    buffer = agcCreateChild(device, sizeof(*buffer));
    if (!buffer)
        return AGC_ERROR_OUT_OF_MEMORY;
    heap = (desc->flags & (AGC_BUFFER_CREATE_UPLOAD_BIT |
        AGC_BUFFER_CREATE_READBACK_BIT)) != 0u ?
        AGC_MEMORY_HEAP_FLEXIBLE : AGC_MEMORY_HEAP_GARLIC;
    alignment = heap == AGC_MEMORY_HEAP_FLEXIBLE ? AGC_FLEXIBLE_ALIGNMENT :
        AGC_GARLIC_ALIGNMENT;
    if (memory) {
        if (memory->magic != AGC_MAGIC_MEMORY || memory->released ||
            memory->device != device || memory->heap != (AgcMemoryHeap)heap ||
            (memory_offset & (alignment - 1u)) != 0u ||
            memory_offset > memory->size ||
            desc->size > memory->size - memory_offset ||
            memory->resource_refs == UINT32_MAX) {
            agcDestroyChild(device, buffer);
            return AGC_ERROR_INVALID_ARGUMENT;
        }
        buffer->allocation = memory->allocation;
        buffer->memory = memory;
        buffer->memory_offset = memory_offset;
        memory->resource_refs++;
    } else {
        result = agcRuntimeAllocate(device, heap, desc->size, alignment,
            (desc->flags & AGC_BUFFER_CREATE_DEDICATED_BIT) != 0u,
            AGC_OBJECT_TYPE_BUFFER, buffer, &buffer->allocation);
        if (result != AGC_OK) {
            agcDestroyChild(device, buffer);
            return result;
        }
        buffer->owns_allocation = 1u;
    }
    buffer->storage = agcBufferCpuAddress(buffer);
    buffer->magic = AGC_MAGIC_BUFFER;
    buffer->device = device;
    buffer->size = desc->size;
    buffer->usage = desc->usage;
    buffer->create_flags = desc->flags;
    buffer->transfer_capacity = 1u;
    buffer->transfers = &buffer->inline_transfer;
    buffer->state_range_count = 1u;
    buffer->state_range_capacity = 1u;
    buffer->state_ranges = &buffer->inline_state_range;
    buffer->inline_state_range = (AgcRuntimeBufferStateRange){
        buffer->size, kAgcResourceUsageUndefined, kAgcResourceOwnerHost };
    *buffer_out = buffer;
    agcCaptureRecordObjectCreate(device, buffer, AGC_CAPTURE_OBJECT_BUFFER,
        buffer->usage, buffer->create_flags);
    agcCaptureRecordResourceDesc(device, buffer, AGC_CAPTURE_OBJECT_BUFFER);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateBuffer(
    AgcDevice device, const AgcBufferDesc *desc, AgcBuffer *buffer_out)
{
    return agcCreateBufferInternal(device, desc, NULL, 0u, buffer_out);
}

int32_t PS5_SYSV_ABI agcCreatePlacedBuffer(AgcDevice device,
    const AgcBufferDesc *desc, AgcMemory memory, uint64_t memory_offset,
    AgcBuffer *buffer_out)
{
    return agcCreateBufferInternal(device, desc, memory, memory_offset,
        buffer_out);
}

int32_t PS5_SYSV_ABI agcDestroyBuffer(AgcBuffer buffer)
{
    AgcDevice device;

    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (buffer->recorded_refs != 0u || buffer->transfer_count != 0u)
        return agcDebugReport(buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT, AGC_ERROR_BUSY,
            "agcDestroyBuffer", AGC_OBJECT_TYPE_BUFFER,
            buffer->allocation->debug_name,
            "buffer destruction requires recorded references and pending ownership transfers to be released");
    if (buffer->deferred)
        return agcDebugReport(buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcDestroyBuffer",
            AGC_OBJECT_TYPE_BUFFER, buffer->allocation->debug_name,
            "buffer is already queued for deferred destruction");
    device = buffer->device;
    agcCaptureRecordObjectDestroy(device, buffer, AGC_CAPTURE_OBJECT_BUFFER);
    if (buffer->owns_allocation)
        agcRuntimeFree(device, buffer->allocation);
    else
        agcReleasePlacedMemory(buffer->memory);
    if (buffer->state_ranges != &buffer->inline_state_range)
        agcFree(device, buffer->state_ranges);
    if (buffer->transfers != &buffer->inline_transfer)
        agcFree(device, buffer->transfers);
    buffer->magic = 0u;
    agcDestroyChild(device, buffer);
    return AGC_OK;
}

static int agcBufferRangeTransferSnapshot(const AgcBuffer buffer,
    uint64_t offset, uint64_t size, AgcRuntimeTransferSnapshot *snapshot);

static int32_t agcGetBufferRangeStateInfoImpl(AgcBuffer buffer,
    uint64_t offset, uint64_t size, AgcResourceStateInfo *info)
{
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    AgcRuntimeTransferSnapshot transfer;

    if (!agcBufferCommittedRangeState(buffer, offset, size, &usage, &owner) ||
        !agcBufferRangeTransferSnapshot(buffer, offset, size, &transfer))
        return AGC_ERROR_NOT_SUPPORTED;

    *info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    info->resource_type = kAgcResourceTypeBuffer;
    info->usage = usage;
    info->owner = owner;
    info->recorded_reference_count = buffer->recorded_refs;
    if (transfer.pending) {
        info->flags |= AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT;
        info->transfer_usage = transfer.usage;
        info->transfer_owner = transfer.owner;
        info->transfer_label = transfer.label;
        info->transfer_value = transfer.value;
        if (transfer.acquire_recorded)
            info->flags |= AGC_RESOURCE_STATE_ACQUIRE_RECORDED_BIT;
    }
    if (buffer->deferred)
        info->flags |= AGC_RESOURCE_STATE_DEFERRED_BIT;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetBufferRangeStateInfo(AgcBuffer buffer,
    uint64_t offset, uint64_t size, AgcResourceStateInfo *info)
{
    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device) || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        info->reserved0 != 0u || !agcReservedZero(info->reserved, 4u) ||
        size == 0u || offset > buffer->size || size > buffer->size - offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    return agcGetBufferRangeStateInfoImpl(buffer, offset, size, info);
}

int32_t PS5_SYSV_ABI agcGetBufferStateInfo(
    AgcBuffer buffer, AgcResourceStateInfo *info)
{
    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device) || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        info->reserved0 != 0u || !agcReservedZero(info->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    return agcGetBufferRangeStateInfoImpl(buffer, 0u, buffer->size, info);
}

static AgcImageAspectFlags agcRuntimeImageAspectMask(const AgcImage image)
{
    switch (image->desc.format) {
    case AGC_FORMAT_D16_UNORM:
    case AGC_FORMAT_D32_FLOAT:
        return AGC_IMAGE_ASPECT_DEPTH_BIT;
    case AGC_FORMAT_S8_UINT:
        return AGC_IMAGE_ASPECT_STENCIL_BIT;
    case AGC_FORMAT_D16_UNORM_S8_UINT:
    case AGC_FORMAT_D32_FLOAT_S8_UINT:
        return AGC_IMAGE_ASPECT_DEPTH_BIT | AGC_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return AGC_IMAGE_ASPECT_COLOR_BIT;
    }
}

static uint32_t agcImageAspectCount(AgcImageAspectFlags aspects)
{
    uint32_t count = 0u;
    while (aspects != 0u) {
        count += aspects & 1u;
        aspects >>= 1u;
    }
    return count;
}

static int agcImageRangeValid(const AgcImage image,
    const AgcImageSubresourceRange *range)
{
    const AgcImageAspectFlags supported = agcRuntimeImageAspectMask(image);

    return range && range->aspect_mask != 0u &&
        (range->aspect_mask & ~supported) == 0u &&
        range->mip_level_count != 0u && range->array_layer_count != 0u &&
        range->base_mip_level < image->desc.mip_levels &&
        range->mip_level_count <=
            image->desc.mip_levels - range->base_mip_level &&
        range->base_array_layer < image->desc.array_layers &&
        range->array_layer_count <=
            image->desc.array_layers - range->base_array_layer &&
        range->reserved0 == 0u;
}

static int agcImageRangeIsWhole(const AgcImage image,
    const AgcImageSubresourceRange *range)
{
    return range->aspect_mask == agcRuntimeImageAspectMask(image) &&
        range->base_mip_level == 0u &&
        range->mip_level_count == image->desc.mip_levels &&
        range->base_array_layer == 0u &&
        range->array_layer_count == image->desc.array_layers;
}

static int agcBufferRangesOverlap(uint64_t first_offset, uint64_t first_size,
    uint64_t second_offset, uint64_t second_size)
{
    return first_offset < second_offset + second_size &&
        second_offset < first_offset + first_size;
}

static int agcImageRangesEqual(const AgcImageSubresourceRange *first,
    const AgcImageSubresourceRange *second)
{
    return first->aspect_mask == second->aspect_mask &&
        first->base_mip_level == second->base_mip_level &&
        first->mip_level_count == second->mip_level_count &&
        first->base_array_layer == second->base_array_layer &&
        first->array_layer_count == second->array_layer_count;
}

static int agcImageRangesOverlap(const AgcImageSubresourceRange *first,
    const AgcImageSubresourceRange *second)
{
    return (first->aspect_mask & second->aspect_mask) != 0u &&
        first->base_mip_level <
            second->base_mip_level + second->mip_level_count &&
        second->base_mip_level <
            first->base_mip_level + first->mip_level_count &&
        first->base_array_layer <
            second->base_array_layer + second->array_layer_count &&
        second->base_array_layer <
            first->base_array_layer + first->array_layer_count;
}

static int agcImageRangeContainsSubresource(
    const AgcImageSubresourceRange *range, AgcImageAspectFlags aspect,
    uint32_t mip, uint32_t layer)
{
    return (range->aspect_mask & aspect) != 0u &&
        mip >= range->base_mip_level &&
        mip - range->base_mip_level < range->mip_level_count &&
        layer >= range->base_array_layer &&
        layer - range->base_array_layer < range->array_layer_count;
}

static int32_t agcEnsureTransferCapacity(AgcDevice device,
    AgcRuntimePendingTransfer **transfers, uint32_t *capacity,
    AgcRuntimePendingTransfer *inline_transfer, uint32_t count,
    uint32_t required)
{
    AgcRuntimePendingTransfer *replacement;
    uint32_t new_capacity;

    if (required <= *capacity)
        return AGC_OK;
    if (required > AGC_RUNTIME_MAX_RECORDED_TRANSITIONS)
        return AGC_ERROR_OUT_OF_MEMORY;
    new_capacity = *capacity;
    while (new_capacity < required) {
        if (new_capacity > AGC_RUNTIME_MAX_RECORDED_TRANSITIONS / 2u) {
            new_capacity = AGC_RUNTIME_MAX_RECORDED_TRANSITIONS;
            break;
        }
        new_capacity *= 2u;
    }
    replacement = agcAlloc(device,
        (size_t)new_capacity * sizeof(*replacement),
        _Alignof(AgcRuntimePendingTransfer));
    if (!replacement)
        return AGC_ERROR_OUT_OF_MEMORY;
    if (count != 0u)
        memcpy(replacement, *transfers, (size_t)count * sizeof(*replacement));
    if (*transfers != inline_transfer)
        agcFree(device, *transfers);
    *transfers = replacement;
    *capacity = new_capacity;
    return AGC_OK;
}

static int32_t agcBufferEnsureTransferCapacity(AgcBuffer buffer,
    uint32_t required)
{
    return agcEnsureTransferCapacity(buffer->device, &buffer->transfers,
        &buffer->transfer_capacity, &buffer->inline_transfer,
        buffer->transfer_count, required);
}

static int32_t agcImageEnsureTransferCapacity(AgcImage image,
    uint32_t required)
{
    return agcEnsureTransferCapacity(image->device, &image->transfers,
        &image->transfer_capacity, &image->inline_transfer,
        image->transfer_count, required);
}

static AgcRuntimePendingTransfer *agcBufferFindTransfer(AgcBuffer buffer,
    uint64_t offset, uint64_t size)
{
    uint32_t i;
    for (i = 0u; i < buffer->transfer_count; ++i) {
        AgcRuntimePendingTransfer *transfer = &buffer->transfers[i];
        if (transfer->buffer_offset == offset && transfer->buffer_size == size)
            return transfer;
    }
    return NULL;
}

static AgcRuntimePendingTransfer *agcImageFindTransfer(AgcImage image,
    const AgcImageSubresourceRange *range)
{
    uint32_t i;
    for (i = 0u; i < image->transfer_count; ++i) {
        AgcRuntimePendingTransfer *transfer = &image->transfers[i];
        if (agcImageRangesEqual(&transfer->image_range, range))
            return transfer;
    }
    return NULL;
}

static int agcBufferTransferOverlaps(const AgcBuffer buffer,
    uint64_t offset, uint64_t size)
{
    uint32_t i;
    for (i = 0u; i < buffer->transfer_count; ++i)
        if (agcBufferRangesOverlap(offset, size,
                buffer->transfers[i].buffer_offset,
                buffer->transfers[i].buffer_size))
            return 1;
    return 0;
}

static int agcImageTransferOverlaps(const AgcImage image,
    const AgcImageSubresourceRange *range)
{
    uint32_t i;
    for (i = 0u; i < image->transfer_count; ++i)
        if (agcImageRangesOverlap(range, &image->transfers[i].image_range))
            return 1;
    return 0;
}

static void agcRemoveTransfer(AgcRuntimePendingTransfer *transfers,
    uint32_t *count, AgcRuntimePendingTransfer *transfer)
{
    uint32_t index = (uint32_t)(transfer - transfers);
    if (index + 1u < *count)
        memmove(&transfers[index], &transfers[index + 1u],
            (size_t)(*count - index - 1u) * sizeof(*transfers));
    (*count)--;
}

static int agcTransferSnapshotsEqual(
    const AgcRuntimeTransferSnapshot *first,
    const AgcRuntimeTransferSnapshot *second)
{
    return first->pending == second->pending &&
        (!first->pending ||
         (first->acquire_recorded == second->acquire_recorded &&
          first->usage == second->usage && first->owner == second->owner &&
          first->label == second->label && first->value == second->value));
}

static AgcRuntimeTransferSnapshot agcTransferSnapshot(
    const AgcRuntimePendingTransfer *transfer)
{
    AgcRuntimeTransferSnapshot snapshot = {0};
    if (transfer) {
        snapshot.pending = 1u;
        snapshot.acquire_recorded = transfer->acquire_command != NULL;
        snapshot.usage = transfer->usage;
        snapshot.owner = transfer->owner;
        snapshot.label = transfer->label;
        snapshot.value = transfer->value;
    }
    return snapshot;
}

static int agcBufferRangeTransferSnapshot(const AgcBuffer buffer,
    uint64_t offset, uint64_t size, AgcRuntimeTransferSnapshot *snapshot)
{
    const uint64_t end = offset + size;
    uint64_t position = offset;
    int found = 0;

    while (position < end) {
        const AgcRuntimePendingTransfer *covering = NULL;
        AgcRuntimeTransferSnapshot segment;
        uint64_t next = end;
        uint32_t i;

        for (i = 0u; i < buffer->transfer_count; ++i) {
            const AgcRuntimePendingTransfer *transfer = &buffer->transfers[i];
            uint64_t transfer_end =
                transfer->buffer_offset + transfer->buffer_size;
            if (transfer->buffer_offset <= position && position < transfer_end)
                covering = transfer;
            if (transfer->buffer_offset > position &&
                transfer->buffer_offset < next)
                next = transfer->buffer_offset;
            if (transfer_end > position && transfer_end < next)
                next = transfer_end;
        }
        segment = agcTransferSnapshot(covering);
        if (!found) {
            *snapshot = segment;
            found = 1;
        } else if (!agcTransferSnapshotsEqual(snapshot, &segment)) {
            return 0;
        }
        position = next;
    }
    return found;
}

static int agcImageRangeTransferSnapshot(const AgcImage image,
    const AgcImageSubresourceRange *range,
    AgcRuntimeTransferSnapshot *snapshot)
{
    AgcImageAspectFlags aspect;
    int found = 0;

    for (aspect = 1u; aspect <= AGC_IMAGE_ASPECT_STENCIL_BIT;
         aspect <<= 1u) {
        uint32_t layer;
        if ((range->aspect_mask & aspect) == 0u)
            continue;
        for (layer = range->base_array_layer;
             layer < range->base_array_layer + range->array_layer_count;
             ++layer) {
            uint32_t mip;
            for (mip = range->base_mip_level;
                 mip < range->base_mip_level + range->mip_level_count;
                 ++mip) {
                const AgcRuntimePendingTransfer *covering = NULL;
                AgcRuntimeTransferSnapshot cell;
                uint32_t i;

                for (i = 0u; i < image->transfer_count; ++i)
                    if (agcImageRangeContainsSubresource(
                            &image->transfers[i].image_range,
                            aspect, mip, layer)) {
                        covering = &image->transfers[i];
                        break;
                    }
                cell = agcTransferSnapshot(covering);
                if (!found) {
                    *snapshot = cell;
                    found = 1;
                } else if (!agcTransferSnapshotsEqual(snapshot, &cell)) {
                    return 0;
                }
            }
        }
    }
    return found;
}

static uint16_t agcImagePackState(AgcResourceUsage usage,
    AgcResourceOwner owner)
{
    return (uint16_t)((uint32_t)usage | ((uint32_t)owner << 8u));
}

static void agcImageUnpackState(uint16_t state, AgcResourceUsage *usage,
    AgcResourceOwner *owner)
{
    *usage = (AgcResourceUsage)(state & 0xffu);
    *owner = (AgcResourceOwner)(state >> 8u);
}

static uint32_t agcImageSubresourceIndex(const AgcImage image,
    AgcImageAspectFlags aspect, uint32_t mip, uint32_t layer)
{
    AgcImageAspectFlags supported = agcRuntimeImageAspectMask(image);
    uint32_t aspect_index = 0u;
    AgcImageAspectFlags bit;

    for (bit = 1u; bit < aspect; bit <<= 1u)
        if ((supported & bit) != 0u)
            aspect_index++;
    return (aspect_index * image->desc.array_layers + layer) *
        image->desc.mip_levels + mip;
}

static int32_t agcImageEnsureSubresourceStates(AgcImage image)
{
    size_t count;
    uint16_t initial;
    uint16_t *states;
    size_t i;

    if (image->subresource_states)
        return AGC_OK;
    count = (size_t)agcImageAspectCount(agcRuntimeImageAspectMask(image)) *
        image->desc.mip_levels * image->desc.array_layers;
    if (count == 0u || count > UINT32_MAX ||
        count > SIZE_MAX / sizeof(*states))
        return AGC_ERROR_OUT_OF_MEMORY;
    states = agcAlloc(image->device, count * sizeof(*states),
        _Alignof(uint16_t));
    if (!states)
        return AGC_ERROR_OUT_OF_MEMORY;
    initial = agcImagePackState(image->usage_state, image->owner_state);
    for (i = 0u; i < count; ++i)
        states[i] = initial;
    image->subresource_states = states;
    image->subresource_state_count = (uint32_t)count;
    return AGC_OK;
}

static int agcImageCommittedRangeState(const AgcImage image,
    const AgcImageSubresourceRange *range, AgcResourceUsage *usage,
    AgcResourceOwner *owner)
{
    AgcImageAspectFlags aspect;
    uint16_t first = 0u;
    int found = 0;

    if (!agcImageRangeValid(image, range))
        return 0;
    if (!image->subresource_states) {
        *usage = image->usage_state;
        *owner = image->owner_state;
        return 1;
    }
    for (aspect = 1u; aspect <= AGC_IMAGE_ASPECT_STENCIL_BIT;
         aspect <<= 1u) {
        uint32_t layer;
        if ((range->aspect_mask & aspect) == 0u)
            continue;
        for (layer = range->base_array_layer;
             layer < range->base_array_layer + range->array_layer_count;
             ++layer) {
            uint32_t mip;
            for (mip = range->base_mip_level;
                 mip < range->base_mip_level + range->mip_level_count;
                 ++mip) {
                uint16_t state = image->subresource_states[
                    agcImageSubresourceIndex(image, aspect, mip, layer)];
                if (!found) {
                    first = state;
                    found = 1;
                } else if (state != first) {
                    return 0;
                }
            }
        }
    }
    if (!found)
        return 0;
    agcImageUnpackState(first, usage, owner);
    return 1;
}

static void agcImageCommitRangeState(AgcImage image,
    const AgcImageSubresourceRange *range, AgcResourceUsage usage,
    AgcResourceOwner owner)
{
    uint16_t state = agcImagePackState(usage, owner);
    AgcImageAspectFlags aspect;
    uint32_t i;

    if (agcImageRangeIsWhole(image, range)) {
        if (image->subresource_states)
            agcFree(image->device, image->subresource_states);
        image->subresource_states = NULL;
        image->subresource_state_count = 0u;
        image->usage_state = usage;
        image->owner_state = owner;
        return;
    }
    for (aspect = 1u; aspect <= AGC_IMAGE_ASPECT_STENCIL_BIT;
         aspect <<= 1u) {
        uint32_t layer;
        if ((range->aspect_mask & aspect) == 0u)
            continue;
        for (layer = range->base_array_layer;
             layer < range->base_array_layer + range->array_layer_count;
             ++layer) {
            uint32_t mip;
            for (mip = range->base_mip_level;
                 mip < range->base_mip_level + range->mip_level_count;
                 ++mip) {
                image->subresource_states[
                    agcImageSubresourceIndex(image, aspect, mip, layer)] =
                    state;
            }
        }
    }
    state = image->subresource_states[0];
    for (i = 1u; i < image->subresource_state_count; ++i)
        if (image->subresource_states[i] != state)
            return;
    agcImageUnpackState(state, &image->usage_state, &image->owner_state);
    agcFree(image->device, image->subresource_states);
    image->subresource_states = NULL;
    image->subresource_state_count = 0u;
}

static int32_t agcCreateImageInternal(AgcDevice device,
    const AgcImageDesc *desc, AgcMemory memory, uint64_t memory_offset,
    AgcImage *image_out)
{
    AgcImage image;
    AgcImageLayout layout = AGC_IMAGE_LAYOUT_INIT;
    int32_t result;

    if (!image_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *image_out = NULL;
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcImageDescBasicValid(desc))
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCreateImage",
            AGC_OBJECT_TYPE_IMAGE, NULL,
            "image descriptor has an invalid version, extent, format, sample count, usage, or reserved field");
    result = agcGetImageLayout(device, desc, &layout);
    if (result != AGC_OK)
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            result == AGC_ERROR_NOT_SUPPORTED ?
                AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT :
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            result, "agcCreateImage", AGC_OBJECT_TYPE_IMAGE, NULL,
            "image format, usage, sample count, or layout is unsupported by this runtime profile");
    image = agcCreateChild(device, sizeof(*image));
    if (!image)
        return AGC_ERROR_OUT_OF_MEMORY;
    if (memory) {
        if (memory->magic != AGC_MAGIC_MEMORY || memory->released ||
            memory->device != device ||
            (memory->heap == AGC_MEMORY_HEAP_FLEXIBLE &&
             layout.alignment > AGC_FLEXIBLE_BLOCK_ALIGNMENT) ||
            (memory_offset & (layout.alignment - 1u)) != 0u ||
            memory_offset > memory->size ||
            layout.allocation_size > memory->size - memory_offset ||
            memory->resource_refs == UINT32_MAX) {
            agcDestroyChild(device, image);
            return AGC_ERROR_INVALID_ARGUMENT;
        }
        image->allocation = memory->allocation;
        image->memory = memory;
        image->memory_offset = memory_offset;
        memory->resource_refs++;
    } else {
        result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_GARLIC,
            layout.allocation_size, layout.alignment,
            (desc->usage & AGC_IMAGE_USAGE_SCANOUT_BIT) != 0u,
            AGC_OBJECT_TYPE_IMAGE, image, &image->allocation);
        if (result != AGC_OK) {
            agcDestroyChild(device, image);
            return result;
        }
        image->owns_allocation = 1u;
    }
    image->magic = AGC_MAGIC_IMAGE;
    image->device = device;
    image->desc = (AgcImageDesc)AGC_IMAGE_DESC_INIT;
    memcpy(&image->desc, desc, desc->struct_size);
    if (desc->version == AGC_RUNTIME_STRUCTURE_VERSION_1) {
        image->desc.struct_size = sizeof(image->desc);
        image->desc.version = AGC_RUNTIME_STRUCTURE_VERSION_2;
        image->desc.tiling = AGC_IMAGE_TILING_OPTIMAL;
        image->desc.flags = 0u;
    }
    image->layout = layout;
    image->transfer_capacity = 1u;
    image->transfers = &image->inline_transfer;
    *image_out = image;
    agcCaptureRecordObjectCreate(device, image, AGC_CAPTURE_OBJECT_IMAGE,
        image->desc.format, image->desc.usage);
    agcCaptureRecordResourceDesc(device, image, AGC_CAPTURE_OBJECT_IMAGE);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateImage(
    AgcDevice device, const AgcImageDesc *desc, AgcImage *image_out)
{
    return agcCreateImageInternal(device, desc, NULL, 0u, image_out);
}

int32_t PS5_SYSV_ABI agcCreatePlacedImage(AgcDevice device,
    const AgcImageDesc *desc, AgcMemory memory, uint64_t memory_offset,
    AgcImage *image_out)
{
    return agcCreateImageInternal(device, desc, memory, memory_offset,
        image_out);
}

int32_t PS5_SYSV_ABI agcDestroyImage(AgcImage image)
{
    AgcDevice device;

    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        !agcDeviceValid(image->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (image->dependency_refs != 0u || image->recorded_refs != 0u ||
        image->transfer_count != 0u)
        return agcDebugReport(image->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT, AGC_ERROR_BUSY,
            "agcDestroyImage", AGC_OBJECT_TYPE_IMAGE,
            image->allocation->debug_name,
            "image destruction requires views, recorded references, and pending ownership transfers to be released");
    if (image->deferred)
        return agcDebugReport(image->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcDestroyImage",
            AGC_OBJECT_TYPE_IMAGE, image->allocation->debug_name,
            "image is already queued for deferred destruction");
    device = image->device;
    agcCaptureRecordObjectDestroy(device, image, AGC_CAPTURE_OBJECT_IMAGE);
    if (image->owns_allocation)
        agcRuntimeFree(device, image->allocation);
    else
        agcReleasePlacedMemory(image->memory);
    if (image->subresource_states)
        agcFree(device, image->subresource_states);
    if (image->transfers != &image->inline_transfer)
        agcFree(device, image->transfers);
    image->magic = 0u;
    agcDestroyChild(device, image);
    return AGC_OK;
}

static int32_t agcGetImageSubresourceStateInfoImpl(AgcImage image,
    const AgcImageSubresourceRange *range, AgcResourceStateInfo *info)
{
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    AgcRuntimeTransferSnapshot transfer;

    if (!agcImageCommittedRangeState(image, range, &usage, &owner) ||
        !agcImageRangeTransferSnapshot(image, range, &transfer))
        return AGC_ERROR_NOT_SUPPORTED;

    *info = (AgcResourceStateInfo)AGC_RESOURCE_STATE_INFO_INIT;
    info->resource_type = kAgcResourceTypeImage;
    info->usage = usage;
    info->owner = owner;
    info->recorded_reference_count = image->recorded_refs;
    info->dependency_reference_count = image->dependency_refs;
    if (transfer.pending) {
        info->flags |= AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT;
        info->transfer_usage = transfer.usage;
        info->transfer_owner = transfer.owner;
        info->transfer_label = transfer.label;
        info->transfer_value = transfer.value;
        if (transfer.acquire_recorded)
            info->flags |= AGC_RESOURCE_STATE_ACQUIRE_RECORDED_BIT;
    }
    if (image->deferred)
        info->flags |= AGC_RESOURCE_STATE_DEFERRED_BIT;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetImageSubresourceStateInfo(AgcImage image,
    const AgcImageSubresourceRange *range, AgcResourceStateInfo *info)
{
    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        !agcDeviceValid(image->device) || !range || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        info->reserved0 != 0u || !agcReservedZero(info->reserved, 4u) ||
        !agcImageRangeValid(image, range))
        return AGC_ERROR_INVALID_ARGUMENT;
    return agcGetImageSubresourceStateInfoImpl(image, range, info);
}

int32_t PS5_SYSV_ABI agcGetImageStateInfo(
    AgcImage image, AgcResourceStateInfo *info)
{
    AgcImageSubresourceRange range;

    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        !agcDeviceValid(image->device) || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        info->reserved0 != 0u || !agcReservedZero(info->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    range = (AgcImageSubresourceRange){ agcRuntimeImageAspectMask(image), 0u,
        image->desc.mip_levels, 0u, image->desc.array_layers, 0u };
    return agcGetImageSubresourceStateInfoImpl(image, &range, info);
}

int32_t PS5_SYSV_ABI agcCreatePresentChain(AgcDevice device,
    const AgcPresentChainDesc *desc, AgcPresentChain *present_chain_out)
{
    AgcPresentChain present_chain;
    AgcVideoOutMode mode;
    AgcVideoOutCreateInfo video_desc;
    void *buffers[AGC_PRESENT_CHAIN_MAX_IMAGES];
    uint32_t pitch_pixels = 0u;
    uint32_t i;
    int32_t result;

    if (!present_chain_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *present_chain_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->flags != 0u || !agcReservedZero(desc->reserved, 4u) ||
        desc->image_count < AGC_PRESENT_CHAIN_MIN_IMAGES ||
        desc->image_count > AGC_PRESENT_CHAIN_MAX_IMAGES || !desc->images)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcVideoOutGetDefaultMode(&mode);
    if (result != AGC_OK)
        return result;
    for (i = 0u; i < desc->image_count; ++i) {
        AgcImage image = desc->images[i];
        AgcImageSubresourceLayout layout =
            AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        uint32_t j;

        if (!image || image->magic != AGC_MAGIC_IMAGE ||
            image->device != device || image->deferred ||
            image->desc.width != mode.width ||
            image->desc.height != mode.height || image->desc.depth != 1u ||
            image->desc.mip_levels != 1u || image->desc.array_layers != 1u ||
            image->desc.sample_count != 1u ||
            (image->desc.format != AGC_FORMAT_RGBA8_UNORM &&
             image->desc.format != AGC_FORMAT_BGRA8_SRGB) ||
            (image->desc.usage & AGC_IMAGE_USAGE_SCANOUT_BIT) == 0u)
            return AGC_ERROR_NOT_SUPPORTED;
        for (j = 0u; j < i; ++j)
            if (desc->images[j] == image)
                return AGC_ERROR_INVALID_ARGUMENT;
        result = agcGetImageSubresourceLayout(device, &image->desc,
            0u, 0u, 0u, &layout);
        if (result != AGC_OK)
            return result;
        if (layout.offset != 0u || layout.row_pitch % 4u != 0u ||
            layout.row_pitch / 4u > UINT32_MAX)
            return AGC_ERROR_NOT_SUPPORTED;
        if (i == 0u)
            pitch_pixels = (uint32_t)(layout.row_pitch / 4u);
        else if (pitch_pixels != layout.row_pitch / 4u)
            return AGC_ERROR_NOT_SUPPORTED;
        buffers[i] = (uint8_t *)agcImageCpuAddress(image) +
            layout.offset;
    }
    present_chain = agcCreateChild(device, sizeof(*present_chain));
    if (!present_chain)
        return AGC_ERROR_OUT_OF_MEMORY;
    video_desc.width = mode.width;
    video_desc.height = mode.height;
    video_desc.pitch_pixels = pitch_pixels;
    video_desc.buffer_count = desc->image_count;
    video_desc.buffers = buffers;
    video_desc.format = AGC_VIDEO_OUT_FORMAT_BGRA8_SRGB;
    result = agcVideoOutOpen(&video_desc, &present_chain->video_out);
    if (result != AGC_OK) {
        agcDestroyChild(device, present_chain);
        return result;
    }
    present_chain->magic = AGC_MAGIC_PRESENT_CHAIN;
    present_chain->device = device;
    present_chain->image_count = desc->image_count;
    for (i = 0u; i < desc->image_count; ++i) {
        present_chain->images[i] = desc->images[i];
        desc->images[i]->dependency_refs++;
    }
    *present_chain_out = present_chain;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyPresentChain(
    AgcPresentChain present_chain)
{
    AgcDevice device;
    uint32_t i;

    if (!present_chain || present_chain->magic != AGC_MAGIC_PRESENT_CHAIN ||
        !agcDeviceValid(present_chain->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    device = present_chain->device;
    agcVideoOutClose(present_chain->video_out);
    for (i = 0u; i < present_chain->image_count; ++i)
        present_chain->images[i]->dependency_refs--;
    present_chain->magic = 0u;
    agcDestroyChild(device, present_chain);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcPresent(AgcPresentChain present_chain,
    uint32_t image_index, uint64_t frame_id, AgcFence ready_fence,
    uint64_t timeout_ns)
{
    AgcImage image;
    AgcImageSubresourceRange range;
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    uint64_t timeout_us;
    int32_t result;

    if (!present_chain || present_chain->magic != AGC_MAGIC_PRESENT_CHAIN ||
        !agcDeviceValid(present_chain->device) ||
        image_index >= present_chain->image_count || !ready_fence ||
        ready_fence->magic != AGC_MAGIC_FENCE ||
        ready_fence->device != present_chain->device)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (timeout_ns == AGC_RUNTIME_INFINITE_TIMEOUT)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (timeout_ns == 0u)
        return AGC_ERROR_TIMEOUT;
    image = present_chain->images[image_index];
    range = (AgcImageSubresourceRange){ agcRuntimeImageAspectMask(image), 0u,
        image->desc.mip_levels, 0u, image->desc.array_layers, 0u };
    if (image->magic != AGC_MAGIC_IMAGE || image->deferred ||
        image->transfer_count != 0u ||
        !agcImageCommittedRangeState(image, &range, &usage, &owner) ||
        usage != kAgcResourceUsageVideoOutScanout ||
        owner != kAgcResourceOwnerGraphics)
        return AGC_ERROR_INVALID_STATE;
    result = agcWaitFence(ready_fence, timeout_ns);
    if (result != AGC_OK)
        return result;
    timeout_us = timeout_ns / 1000u + (timeout_ns % 1000u != 0u);
    if (timeout_us > UINT32_MAX)
        timeout_us = UINT32_MAX;
    return agcVideoOutPresent(present_chain->video_out, image_index,
        frame_id, timeout_us);
}

static int32_t agcRuntimeEncodeImageView(
    AgcImage image, const AgcImageViewDesc *desc,
    AgcGfx1013ImageDescriptor *descriptor)
{
    AgcGfx1013Image2DState state = {0};
    uint32_t view_type = desc->version >= AGC_RUNTIME_STRUCTURE_VERSION_2 ?
        desc->view_type : ((image->desc.usage &
            AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT) != 0u ?
            AGC_IMAGE_VIEW_TYPE_CUBE : image->desc.array_layers > 1u ?
            AGC_IMAGE_VIEW_TYPE_2D_ARRAY : AGC_IMAGE_VIEW_TYPE_2D);
    uint32_t swizzles[4] = { AGC_COMPONENT_SWIZZLE_IDENTITY,
        AGC_COMPONENT_SWIZZLE_IDENTITY, AGC_COMPONENT_SWIZZLE_IDENTITY,
        AGC_COMPONENT_SWIZZLE_IDENTITY };
    uint32_t *selectors[4] = { &state.dst_sel_x, &state.dst_sel_y,
        &state.dst_sel_z, &state.dst_sel_w };
    const uint32_t base[4] = { 4u, 5u, 6u, 7u };
    uint32_t i;

    if (desc->base_mip_level != 0u || image->desc.depth != 1u ||
        desc->format > 0x1ffu)
        return AGC_ERROR_NOT_SUPPORTED;
    state.address = agcImageGpuAddress(image);
    state.width = image->desc.width;
    state.height = image->desc.height;
    state.format = desc->format;
    if (desc->version >= AGC_RUNTIME_STRUCTURE_VERSION_2) {
        swizzles[0] = desc->swizzle_r;
        swizzles[1] = desc->swizzle_g;
        swizzles[2] = desc->swizzle_b;
        swizzles[3] = desc->swizzle_a;
    }
    for (i = 0u; i < 4u; ++i) {
        if (swizzles[i] == AGC_COMPONENT_SWIZZLE_IDENTITY)
            *selectors[i] = base[i];
        else if (swizzles[i] == AGC_COMPONENT_SWIZZLE_ZERO)
            *selectors[i] = 0u;
        else if (swizzles[i] == AGC_COMPONENT_SWIZZLE_ONE)
            *selectors[i] = 1u;
        else
            *selectors[i] = base[swizzles[i] - AGC_COMPONENT_SWIZZLE_R];
    }
    state.sample_count = image->desc.sample_count;
    if (view_type == AGC_IMAGE_VIEW_TYPE_2D) {
        AgcImageSubresourceLayout layout = AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        int32_t result = agcGetImageSubresourceLayout(image->device,
            &image->desc, desc->base_mip_level, desc->base_array_layer, 0u,
            &layout);
        if (result != AGC_OK || layout.offset > UINT64_MAX - state.address)
            return result != AGC_OK ? result : AGC_ERROR_INVALID_ARGUMENT;
        state.address += layout.offset;
        state.base_array_layer = 0u;
        state.last_array_layer = 0u;
    } else {
        state.base_array_layer = desc->base_array_layer;
        state.last_array_layer = desc->base_array_layer +
            desc->array_layer_count - 1u;
    }
    state.mip_level_count = desc->mip_level_count;
    if (image->desc.sample_count == 4u) {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_2D_MSAA;
        state.swizzle_mode = AGC_GFX1013_IMAGE_SWIZZLE_64KB_R_X;
    } else if (view_type == AGC_IMAGE_VIEW_TYPE_CUBE ||
               view_type == AGC_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_CUBE;
    } else if (view_type == AGC_IMAGE_VIEW_TYPE_2D_ARRAY) {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_2D_ARRAY;
    } else {
        state.image_type = AGC_GFX1013_IMAGE_TYPE_2D;
    }
    return agcGfx1013Image2DDescriptorEncode(descriptor, &state);
}

static int agcImageViewDescHeaderValid(const AgcImageViewDesc *desc)
{
    return desc &&
        ((desc->version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
          desc->struct_size == offsetof(AgcImageViewDesc, view_type)) ||
         (desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2 &&
          desc->struct_size == sizeof(*desc)));
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
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcImageViewDescHeaderValid(desc) ||
        !agcReservedZero(desc->reserved, 4u) ||
        (desc->version >= AGC_RUNTIME_STRUCTURE_VERSION_2 &&
         (desc->view_type > AGC_IMAGE_VIEW_TYPE_CUBE_ARRAY ||
          desc->swizzle_r > AGC_COMPONENT_SWIZZLE_A ||
          desc->swizzle_g > AGC_COMPONENT_SWIZZLE_A ||
          desc->swizzle_b > AGC_COMPONENT_SWIZZLE_A ||
          desc->swizzle_a > AGC_COMPONENT_SWIZZLE_A || desc->flags != 0u))) {
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCreateImageView",
            AGC_OBJECT_TYPE_IMAGE_VIEW, NULL,
            "image-view descriptor version or reserved fields are invalid");
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
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCreateImageView",
            AGC_OBJECT_TYPE_IMAGE_VIEW, NULL,
            "image view requires a live same-device image, matching format, and in-range mip/layer interval");
    }
    if (desc->version >= AGC_RUNTIME_STRUCTURE_VERSION_2 &&
        ((desc->view_type == AGC_IMAGE_VIEW_TYPE_2D &&
          desc->array_layer_count != 1u) ||
         ((desc->view_type == AGC_IMAGE_VIEW_TYPE_CUBE ||
           desc->view_type == AGC_IMAGE_VIEW_TYPE_CUBE_ARRAY) &&
          (((image->desc.usage & AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT) == 0u) ||
           desc->array_layer_count % 6u != 0u ||
           (desc->view_type == AGC_IMAGE_VIEW_TYPE_CUBE &&
            desc->array_layer_count != 6u)))))
        return AGC_ERROR_INVALID_ARGUMENT;
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
    view->desc = (AgcImageViewDesc)AGC_IMAGE_VIEW_DESC_INIT;
    memcpy(&view->desc, desc, desc->struct_size);
    image->dependency_refs++;
    *view_out = view;
    agcCaptureRecordObjectCreate(device, view,
        AGC_CAPTURE_OBJECT_IMAGE_VIEW, view->desc.format, 0u);
    agcCaptureRecordResourceDesc(device, view,
        AGC_CAPTURE_OBJECT_IMAGE_VIEW);
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
        return agcDebugReport(view->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT, AGC_ERROR_BUSY,
            "agcDestroyImageView", AGC_OBJECT_TYPE_IMAGE_VIEW,
            view->allocation->debug_name,
            "image-view destruction requires recorded command references to be released");
    device = view->device;
    agcCaptureRecordObjectDestroy(device, view,
        AGC_CAPTURE_OBJECT_IMAGE_VIEW);
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
    int version2;
    int version3;

    if (!sampler_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *sampler_out = NULL;
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    version3 = desc && desc->version == AGC_RUNTIME_STRUCTURE_VERSION_3 &&
        desc->struct_size == sizeof(*desc);
    version2 = desc &&
        ((desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2 &&
          desc->struct_size == 112u) || version3);
    if (!desc ||
        !((desc->version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
           desc->struct_size == offsetof(AgcSamplerDesc, mip_filter)) ||
          version2) ||
        desc->min_filter > AGC_FILTER_LINEAR ||
        desc->mag_filter > AGC_FILTER_LINEAR ||
        desc->address_u > (version2 ? AGC_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE :
            AGC_ADDRESS_MODE_CLAMP_TO_EDGE) ||
        desc->address_v > (version2 ? AGC_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE :
            AGC_ADDRESS_MODE_CLAMP_TO_EDGE) ||
        desc->address_w > (version2 ? AGC_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE :
            AGC_ADDRESS_MODE_CLAMP_TO_EDGE) ||
        desc->flags != 0u || !agcReservedZero(desc->reserved, 4u) ||
        (version2 && (desc->mip_filter > AGC_MIP_FILTER_LINEAR ||
         desc->anisotropy_enable > 1u || desc->max_anisotropy == 0u ||
         desc->max_anisotropy > 16u || desc->compare_enable > 1u ||
         desc->compare_operation >= AGC_COMPARE_OPERATION_COUNT ||
         desc->border_color > AGC_SAMPLER_BORDER_CUSTOM ||
         !agcRuntimeFloatFinite(desc->min_lod) ||
         !agcRuntimeFloatFinite(desc->max_lod) ||
         !agcRuntimeFloatFinite(desc->lod_bias) || desc->min_lod < 0.0f ||
         desc->max_lod < desc->min_lod || desc->reserved2 != 0u ||
         (desc->border_color == AGC_SAMPLER_BORDER_CUSTOM &&
          !version3)))) {
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCreateSampler",
            AGC_OBJECT_TYPE_SAMPLER, NULL,
            "sampler descriptor has an invalid enum, flag, version, or reserved field");
    }
    agcSamplerDescriptorInit(&descriptor);
    agcSamplerDescriptorSetFilterMode(&descriptor,
        desc->min_filter == AGC_FILTER_LINEAR ?
            (version2 && desc->anisotropy_enable ? kAgcFilterAnisoLinear :
                kAgcFilterBilinear) :
            (version2 && desc->anisotropy_enable ? kAgcFilterAnisoPoint :
                kAgcFilterPoint),
        desc->mag_filter == AGC_FILTER_LINEAR ?
            (version2 && desc->anisotropy_enable ? kAgcFilterAnisoLinear :
                kAgcFilterBilinear) :
            (version2 && desc->anisotropy_enable ? kAgcFilterAnisoPoint :
                kAgcFilterPoint),
        !version2 || desc->mip_filter == AGC_MIP_FILTER_NONE ?
            kAgcMipFilterNone : desc->mip_filter == AGC_MIP_FILTER_NEAREST ?
            kAgcMipFilterPoint : kAgcMipFilterLinear);
    agcSamplerDescriptorSetClampMode(&descriptor,
        desc->address_u == AGC_ADDRESS_MODE_REPEAT ? kAgcClampRepeat :
            desc->address_u == AGC_ADDRESS_MODE_CLAMP_TO_EDGE ? kAgcClampClamp :
            desc->address_u == AGC_ADDRESS_MODE_MIRRORED_REPEAT ? kAgcClampMirror :
            desc->address_u == AGC_ADDRESS_MODE_CLAMP_TO_BORDER ? kAgcClampBorder :
            kAgcClampMirrorOnce,
        desc->address_v == AGC_ADDRESS_MODE_REPEAT ? kAgcClampRepeat :
            desc->address_v == AGC_ADDRESS_MODE_CLAMP_TO_EDGE ? kAgcClampClamp :
            desc->address_v == AGC_ADDRESS_MODE_MIRRORED_REPEAT ? kAgcClampMirror :
            desc->address_v == AGC_ADDRESS_MODE_CLAMP_TO_BORDER ? kAgcClampBorder :
            kAgcClampMirrorOnce,
        desc->address_w == AGC_ADDRESS_MODE_REPEAT ? kAgcClampRepeat :
            desc->address_w == AGC_ADDRESS_MODE_CLAMP_TO_EDGE ? kAgcClampClamp :
            desc->address_w == AGC_ADDRESS_MODE_MIRRORED_REPEAT ? kAgcClampMirror :
            desc->address_w == AGC_ADDRESS_MODE_CLAMP_TO_BORDER ? kAgcClampBorder :
            kAgcClampMirrorOnce);
    if (version2) {
        agcSamplerDescriptorSetLod(&descriptor, desc->min_lod,
            desc->max_lod, desc->lod_bias);
        if (desc->anisotropy_enable)
            agcSamplerDescriptorSetMaxAnisotropy(&descriptor,
                desc->max_anisotropy);
        if (desc->compare_enable)
            agcSamplerDescriptorSetCompareFunc(&descriptor,
                desc->compare_operation);
        if (desc->border_color == AGC_SAMPLER_BORDER_CUSTOM) {
            uint8_t *table;
            result = agcSamplerDescriptorSetCustomBorderColor(&descriptor,
                desc->custom_border_color_index);
            if (result != AGC_OK)
                return result;
            if (!device->border_color_allocation) {
                result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
                    4096u * 16u, 256u, 0u, AGC_OBJECT_TYPE_SAMPLER, device,
                    &device->border_color_allocation);
                if (result != AGC_OK)
                    return result;
                memset(agcAllocationCpuAddress(
                    device->border_color_allocation), 0, 4096u * 16u);
            }
            table = agcAllocationCpuAddress(device->border_color_allocation);
            memcpy(table + desc->custom_border_color_index * 16u,
                desc->custom_border_color, 16u);
            result = agcFlushRuntimeAllocation(
                device->border_color_allocation,
                desc->custom_border_color_index * 16u, 16u);
            if (result != AGC_OK)
                return result;
        } else {
            agcSamplerDescriptorSetBorderColor(&descriptor,
                desc->border_color == AGC_SAMPLER_BORDER_OPAQUE_BLACK ?
                    kAgcBorderOpaqueBlack :
                desc->border_color == AGC_SAMPLER_BORDER_OPAQUE_WHITE ?
                    kAgcBorderWhite : kAgcBorderTransparentBlack);
        }
    }
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
    sampler->desc = (AgcSamplerDesc)AGC_SAMPLER_DESC_INIT;
    memcpy(&sampler->desc, desc, desc->struct_size);
    *sampler_out = sampler;
    agcCaptureRecordObjectCreate(device, sampler,
        AGC_CAPTURE_OBJECT_SAMPLER, sampler->desc.min_filter,
        sampler->desc.mag_filter);
    agcCaptureRecordResourceDesc(device, sampler,
        AGC_CAPTURE_OBJECT_SAMPLER);
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
        return agcDebugReport(sampler->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT, AGC_ERROR_BUSY,
            "agcDestroySampler", AGC_OBJECT_TYPE_SAMPLER,
            sampler->allocation->debug_name,
            "sampler destruction requires recorded command references to be released");
    device = sampler->device;
    agcCaptureRecordObjectDestroy(device, sampler,
        AGC_CAPTURE_OBJECT_SAMPLER);
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

static uint64_t agcShaderLinkageHash(
    const AgcShaderReflection *reflection)
{
    uint64_t hash = UINT64_C(14695981039346656037);

    hash = agcShaderHashBytes(hash, &reflection->stage_input_mask,
        sizeof(reflection->stage_input_mask));
    hash = agcShaderHashBytes(hash, &reflection->stage_output_mask,
        sizeof(reflection->stage_output_mask));
    hash = agcShaderHashBytes(hash, &reflection->patch_input_mask,
        sizeof(reflection->patch_input_mask));
    return agcShaderHashBytes(hash, &reflection->patch_output_mask,
        sizeof(reflection->patch_output_mask));
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

static int agcShaderTessellationReflectionValid(
    const AgcShaderReflection *reflection)
{
    const int reads_factors_flag =
        (reflection->flags &
         AGC_SHADER_REFLECTION_READS_TESS_FACTORS_BIT) != 0u;
    const int has_tessellation =
        reflection->tessellation_patch_count != 0u ||
        reflection->tessellation_input_control_points != 0u ||
        reflection->tessellation_output_control_points != 0u ||
        reflection->tessellation_vertex_output_count != 0u ||
        reflection->tessellation_control_output_count != 0u ||
        reflection->tessellation_primitive_mode != 0u ||
        reflection->tessellation_reads_factors != 0u ||
        reflection->tessellation_lds_size != 0u;

    if (!has_tessellation)
        return !reads_factors_flag;
    if (reflection->tessellation_patch_count == 0u ||
        reflection->tessellation_patch_count > 0x7fu ||
        reflection->tessellation_output_control_points == 0u ||
        reflection->tessellation_output_control_points > 32u ||
        reflection->tessellation_reads_factors > 1u ||
        reads_factors_flag !=
            (reflection->tessellation_reads_factors != 0u))
        return 0;
    if (reflection->stage == kAgcShaderStageHs) {
        return (reflection->version != AGC_SHADER_REFLECTION_VERSION_2 ||
                reflection->front_stage == kAgcShaderStageVs) &&
            reflection->tessellation_input_control_points != 0u &&
            reflection->tessellation_input_control_points <= 32u &&
            reflection->tessellation_vertex_output_count <= 0x3fu &&
            reflection->tessellation_control_output_count <= 0x3fu &&
            reflection->tessellation_primitive_mode == 0u &&
            reflection->tessellation_reads_factors == 0u &&
            reflection->tessellation_lds_size != 0u &&
            reflection->tessellation_lds_size <=
                AGC_GFX1013_HS_LDS_MAX_SIZE;
    }
    if (reflection->stage == kAgcShaderStageDs ||
        reflection->stage == kAgcShaderStageGs) {
        return (reflection->version != AGC_SHADER_REFLECTION_VERSION_2 ||
                reflection->front_stage == kAgcShaderStageDs) &&
            reflection->tessellation_input_control_points == 0u &&
            reflection->tessellation_vertex_output_count == 0u &&
            reflection->tessellation_control_output_count == 0u &&
            reflection->tessellation_primitive_mode != 0u &&
            reflection->tessellation_primitive_mode <= 3u &&
            reflection->tessellation_lds_size == 0u;
    }
    return 0;
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
        AGC_SHADER_REFLECTION_READS_TESS_FACTORS_BIT |
        AGC_SHADER_REFLECTION_ALPHA_TO_ONE_BIT;
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
        (reflection->compiler_api_version ==
             AGC_SHADER_COMPILER_API_VERSION_15 ||
         reflection->compiler_api_version ==
             AGC_SHADER_COMPILER_API_VERSION_16 ||
         reflection->compiler_api_version ==
             AGC_SHADER_COMPILER_API_VERSION);
    if (!reflection || reflection->struct_size != sizeof(*reflection) ||
        (!legacy_reflection && !current_reflection) ||
        reflection->stage != stage || stage >= kAgcShaderStageCount ||
        (reflection->flags & ~known_flags) != 0u ||
        (reflection->system_sgpr_mask & ~known_system_sgprs) != 0u ||
        reflection->shader_record_version !=
            AGC_SHADER_RECORD_VERSION_GEN5 ||
        (reflection->wave_size != 32u && reflection->wave_size != 64u) ||
        reflection->hash_algorithm != AGC_SHADER_HASH_FNV1A64 ||
        reflection->stage_linkage_hash != agcShaderLinkageHash(reflection) ||
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
        reflection->reserved0 != 0u ||
        !agcShaderTessellationReflectionValid(reflection)) {
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
            AGC_SHADER_REFLECTION_USES_SAMPLE_SHADING_BIT |
            AGC_SHADER_REFLECTION_ALPHA_TO_ONE_BIT)) != 0u)) ||
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
        AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size) == 0u ||
        mapping->byte_stride == 0u ||
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
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!desc ||
        desc->stage >= kAgcShaderStageCount || desc->flags != 0u ||
        !desc->code || desc->code_size == 0u || desc->code_size > SIZE_MAX ||
        !agcReservedZero(desc->reserved, 4u)) {
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCreateShader",
            AGC_OBJECT_TYPE_SHADER, NULL,
            "shader descriptor has an invalid stage, flags, binary pointer, size, or reserved field");
    }
    reflected = desc->struct_size == sizeof(*desc) &&
        desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2;
    if (!reflected &&
        (desc->struct_size != 64u ||
         desc->version != AGC_RUNTIME_STRUCTURE_VERSION_1)) {
        return agcDebugReport(device, AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCreateShader",
            AGC_OBJECT_TYPE_SHADER, NULL,
            "shader descriptor structure version is unsupported");
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
            return agcDebugReport(device,
                AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                AGC_ERROR_SHADER_INVALID, "agcCreateShader",
                AGC_OBJECT_TYPE_SHADER, NULL,
                "shader reflection, binary hash, code range, front half, or linkage metadata is invalid");
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
    agcCaptureRecordObjectCreate(device, shader, AGC_CAPTURE_OBJECT_SHADER,
        shader->stage, shader->has_reflection);
    agcCaptureRecordShaderDesc(shader);
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
        return agcDebugReport(shader->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT, AGC_ERROR_BUSY,
            "agcDestroyShader", AGC_OBJECT_TYPE_SHADER,
            shader->allocation->debug_name,
            "shader destruction requires all dependent pipelines to be destroyed");
    device = shader->device;
    agcCaptureRecordObjectDestroy(device, shader, AGC_CAPTURE_OBJECT_SHADER);
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
    const AgcShaderReflection *c,
    const AgcShaderDescriptorMapping *entry)
{
    uint32_t i;
    const AgcShaderReflection *reflections[3] = {a, b, c};
    uint32_t reflection_index;

    for (reflection_index = 0u; reflection_index < 3u;
         ++reflection_index) {
        const AgcShaderReflection *reflection =
            reflections[reflection_index];
        if (!reflection)
            continue;
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
    const AgcShaderReflection *c,
    const AgcShaderPushConstantRange *entry)
{
    const AgcShaderReflection *reflections[3] = {a, b, c};
    uint32_t reflection_index;
    uint32_t i;

    for (reflection_index = 0u; reflection_index < 3u;
         ++reflection_index) {
        const AgcShaderReflection *reflection =
            reflections[reflection_index];
        if (!reflection)
            continue;
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
        AGC_RUNTIME_MAX_DESCRIPTOR_WRITES -
            AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size) ||
        !agcMulU64(mapping->byte_stride,
            AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size), &end) ||
            !agcAddU64(mapping->byte_offset, end, &end) ||
            end > AGC_RUNTIME_MAX_RESOURCE_ARENA_SIZE)
            return AGC_ERROR_VALIDATION_FAILED;
        if (end > layout->set_sizes[mapping->set])
            layout->set_sizes[mapping->set] = end;
        layout->set_mask |= 1u << mapping->set;
        layout->descriptor_element_count +=
            AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size);
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
        uint64_t push_constant_stride;
        uint64_t push_constant_span;
        if (!agcAlignU64(offset, 16u, &offset))
            return AGC_ERROR_VALIDATION_FAILED;
        layout->push_constant_offset = offset;
        layout->push_constant_size = push_constant_size;
        if (!agcAlignU64(push_constant_size, 16u,
                &push_constant_stride) ||
            push_constant_stride > UINT32_MAX ||
            !agcMulU64(push_constant_stride, kAgcShaderStageCount,
                &push_constant_span) ||
            !agcAddU64(offset, push_constant_span, &offset))
            return AGC_ERROR_VALIDATION_FAILED;
        layout->push_constant_stride = (uint32_t)push_constant_stride;
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
    case AGC_FORMAT_R8_UNORM:
    case AGC_FORMAT_RG8_UNORM:
    case AGC_FORMAT_RGBA8_UNORM:
    case AGC_FORMAT_BGRA8_UNORM:
    case AGC_FORMAT_RGBA8_SRGB:
    case AGC_FORMAT_BGRA8_SRGB:
    case AGC_FORMAT_RGB10A2_UNORM:
    case AGC_FORMAT_R16_FLOAT:
    case AGC_FORMAT_RG16_FLOAT:
    case AGC_FORMAT_R32_FLOAT:
    case AGC_FORMAT_RG32_FLOAT:
    case AGC_FORMAT_R11G11B10_FLOAT:
    case AGC_FORMAT_RGBA16_FLOAT:
        *component_class = AGC_SHADER_COMPONENT_FLOAT_OR_NORMALIZED;
        *component_bits = format == AGC_FORMAT_R32_FLOAT ||
            format == AGC_FORMAT_RG32_FLOAT ? 32u : 16u;
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

static int agcRuntimeColorTargetFormat(uint32_t format,
    AgcGfx1013ColorTargetFormat *target_format)
{
    if (!target_format)
        return 0;
    switch ((AgcFormat)format) {
    case AGC_FORMAT_R8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_R8_UNORM;
        return 1;
    case AGC_FORMAT_RG8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_RG8_UNORM;
        return 1;
    case AGC_FORMAT_RGBA8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA8_UNORM;
        return 1;
    case AGC_FORMAT_BGRA8_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_BGRA8_UNORM;
        return 1;
    case AGC_FORMAT_RGBA8_SRGB:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA8_SRGB;
        return 1;
    case AGC_FORMAT_BGRA8_SRGB:
        *target_format = AGC_GFX1013_RT_FORMAT_BGRA8_SRGB;
        return 1;
    case AGC_FORMAT_RGB10A2_UNORM:
        *target_format = AGC_GFX1013_RT_FORMAT_RGB10A2_UNORM;
        return 1;
    case AGC_FORMAT_R16_FLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_R16_FLOAT;
        return 1;
    case AGC_FORMAT_RG16_FLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RG16_FLOAT;
        return 1;
    case AGC_FORMAT_R32_FLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_R32_FLOAT;
        return 1;
    case AGC_FORMAT_RG32_FLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RG32_FLOAT;
        return 1;
    case AGC_FORMAT_R11G11B10_FLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_R11G11B10_FLOAT;
        return 1;
    case AGC_FORMAT_RGBA16_FLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA16_FLOAT;
        return 1;
    case AGC_FORMAT_RGBA32_FLOAT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA32_FLOAT;
        return 1;
    case AGC_FORMAT_RGBA16_UINT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA16_UINT;
        return 1;
    case AGC_FORMAT_RGBA16_SINT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA16_SINT;
        return 1;
    case AGC_FORMAT_RGBA32_UINT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA32_UINT;
        return 1;
    case AGC_FORMAT_RGBA32_SINT:
        *target_format = AGC_GFX1013_RT_FORMAT_RGBA32_SINT;
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

static int agcPipelineBlendFactorUsesSource1(AgcBlendFactor factor)
{
    return factor == AGC_BLEND_FACTOR_SRC1_COLOR ||
        factor == AGC_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR ||
        factor == AGC_BLEND_FACTOR_SRC1_ALPHA ||
        factor == AGC_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
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

static int agcPipelineDepthBiasValid(const AgcDepthBias *state)
{
    return state &&
        agcHeaderValid(state->struct_size, sizeof(*state), state->version) &&
        agcRuntimeFloatFinite(state->constant_factor) &&
        agcRuntimeFloatFinite(state->clamp) &&
        agcRuntimeFloatFinite(state->slope_factor) &&
        state->flags == 0u && agcReservedZero(state->reserved, 2u);
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

static int agcPipelineShaderHasShValue(AgcShader shader, uint32_t value)
{
    const AgcRegisterValue *registers =
        (const AgcRegisterValue *)(uintptr_t)shader->record.sh_registers;
    uint32_t i;

    for (i = 0u; i < shader->record.num_sh_registers; ++i) {
        if (registers[i].value == value)
            return 1;
    }
    return 0;
}

static void agcPipelinePatchProgramAddress(AgcRegisterValue *registers,
    uint32_t count, uint32_t program_lo, uint64_t address)
{
    uint32_t i;

    for (i = 0u; i < count; ++i) {
        if (registers[i].offset == program_lo)
            registers[i].value = (uint32_t)(address >> 8u);
        else if (registers[i].offset == program_lo + 1u)
            registers[i].value = (uint32_t)(address >> 40u);
    }
}

static void agcPipelineBuildShaderBinding(AgcShader shader,
    AgcShaderRecord *record, AgcRegisterValue *sh_registers,
    AgcGfx1013ShaderBinding *binding)
{
    *record = shader->record;
    binding->record = record;
    binding->sh_registers = sh_registers;
    binding->num_sh_registers = agcPipelineCopyStaticShRegisters(
        shader, sh_registers, UINT8_MAX);
    record->num_sh_registers = (uint8_t)binding->num_sh_registers;
    binding->cx_registers = (const AgcRegisterValue *)(uintptr_t)
        shader->record.cx_registers;
    binding->num_cx_registers = shader->record.num_cx_registers;
    binding->code_address = shader->program_gpu_address;
}

static int32_t agcBuildGraphicsPipelineBind(
    AgcGraphicsPipeline pipeline)
{
    AgcGfx1013Wave32VsPsState shader_state = {0};
    AgcGfx1013Wave32TessVsPsState tess_shader_state = {0};
    AgcGfx1013TessellationState tessellation = {0};
    AgcShaderRecord hull_record = {0};
    AgcShaderRecord primitive_record = {0};
    AgcShaderRecord pixel_record = {0};
    AgcRegisterValue hull_sh[UINT8_MAX];
    AgcRegisterValue primitive_sh[UINT8_MAX];
    AgcRegisterValue pixel_sh[UINT8_MAX];
    AgcGfx1013ColorBlendState blend_state = {0};
    AgcGfx1013DepthStencilState depth_state = {0};
    AgcGfx1013DepthBiasState depth_bias_state = {0};
    AgcGfx1013SampleState sample_state;
    AgcGfx1013PrimitiveSizeState primitive_size_state = {
        1.0f, 1.0f, 64.0f, 1.0f};
    AgcRegisterValue color_format_register;
    AgcRegisterValue raster_register;
    AgcRegisterValue clip_register;
    SceAgcCb cb;
    uint32_t color_export_format = 0u;
    uint32_t i;
    int32_t result;

    agcCbInit(&cb, pipeline->bind_words, sizeof(pipeline->bind_words));
    if (pipeline->hull_shader) {
        agcPipelineBuildShaderBinding(pipeline->hull_shader,
            &hull_record, hull_sh, &tess_shader_state.hull);
        agcPipelineBuildShaderBinding(pipeline->primitive_shader,
            &primitive_record, primitive_sh,
            &tess_shader_state.primitive);
        if (pipeline->primitive_shader->has_front_record &&
            !agcPipelineShaderHasShValue(pipeline->primitive_shader,
                OPENAGC_NEXT_STAGE_PC_PLACEHOLDER)) {
            agcPipelinePatchProgramAddress(primitive_sh,
                tess_shader_state.primitive.num_sh_registers,
                AGC_REG_SPI_SHADER_PGM_LO_ES,
                pipeline->primitive_shader->front_program_gpu_address);
        }
        agcPipelineBuildShaderBinding(pipeline->pixel_shader,
            &pixel_record, pixel_sh, &tess_shader_state.pixel);
        tess_shader_state.hull_back_code_address =
            pipeline->hull_shader->front_program_gpu_address;
        tess_shader_state.primitive_back_code_address =
            pipeline->primitive_shader->front_program_gpu_address;
        tess_shader_state.ring_descriptor_address = agcAllocationGpuAddress(
            pipeline->device->tessellation_table_allocation);
        tess_shader_state.tcs_offchip_layout =
            pipeline->tessellation_tcs_offchip_layout;
        tess_shader_state.tes_offchip_layout =
            pipeline->tessellation_tes_offchip_layout;
        tess_shader_state.hull_lds_size =
            pipeline->hull_shader->reflection.tessellation_lds_size;
        tess_shader_state.primitive_type = pipeline->primitive_type;
        tessellation.offchip_ring_address = agcAllocationGpuAddress(
            pipeline->device->tessellation_offchip_allocation);
        tessellation.factor_ring_address = agcAllocationGpuAddress(
            pipeline->device->tessellation_factor_allocation);
        tessellation.offchip_ring_size =
            AGC_GFX1013_TESS_OFFCHIP_RING_SIZE;
        tessellation.factor_ring_size =
            AGC_GFX1013_TESS_FACTOR_RING_SIZE;
        tessellation.offchip_param = AGC_GFX1013_TESS_OFFCHIP_PARAM;
        tessellation.max_tess_level = UINT32_C(0x42800000);
        tessellation.min_tess_level = 0u;
        tessellation.esgs_ring_itemsize = 1u;
        tessellation.distribution = UINT32_C(0xd8181e0c);
        tessellation.tf_param = UINT32_C(0x61);
        result = agcGfx1013SetTessellationRings(&cb, &tessellation);
        if (result == AGC_OK)
            result = agcGfx1013BindWave32TessVsPs(
                &cb, &tess_shader_state);
        if (result == AGC_OK)
            result = agcGfx1013SetTessellationContext(
                &cb, &tessellation);
    } else {
        agcPipelineBuildShaderBinding(pipeline->primitive_shader,
            &primitive_record, primitive_sh, &shader_state.primitive);
        if (pipeline->primitive_shader->has_front_record &&
            !agcPipelineShaderHasShValue(pipeline->primitive_shader,
                OPENAGC_NEXT_STAGE_PC_PLACEHOLDER)) {
            agcPipelinePatchProgramAddress(primitive_sh,
                shader_state.primitive.num_sh_registers,
                AGC_REG_SPI_SHADER_PGM_LO_ES,
                pipeline->primitive_shader->front_program_gpu_address);
        }
        agcPipelineBuildShaderBinding(pipeline->pixel_shader,
            &pixel_record, pixel_sh, &shader_state.pixel);
        shader_state.primitive_back_code_address =
            pipeline->primitive_shader->front_program_gpu_address;
        shader_state.primitive_type = pipeline->primitive_type;
        result = agcGfx1013BindVsPs(&cb, &shader_state);
    }
    if (result != AGC_OK)
        return result;
    if (pipeline->rasterization.line_width != 1.0f) {
        primitive_size_state.line_width = pipeline->rasterization.line_width;
        result = agcGfx1013SetPrimitiveSizeState(
            &cb, &primitive_size_state);
        if (result != AGC_OK)
            return result;
    }
    if (pipeline->rasterization.depth_clamp_enable ||
        pipeline->rasterization.rasterizer_discard_enable) {
        clip_register.offset = AGC_REG_PA_CL_CLIP_CNTL;
        clip_register.value = pipeline->rasterization.depth_clamp_enable ?
            AGC_GFX1013_DEPTH_CLAMP_CLIP_CONTROL :
            AGC_GFX1013_VULKAN_CLIP_CONTROL;
        if (pipeline->rasterization.rasterizer_discard_enable)
            clip_register.value |=
                1u << AGC_REG_PA_CL_CLIP_CNTL_DX_RASTERIZATION_KILL_SHIFT;
        if (!sceAgcCbSetCxRegistersDirect(&cb, &clip_register, 1u))
            return AGC_ERROR_INTERNAL;
    }
    for (i = 0u; i < pipeline->color_attachment_count; ++i) {
        color_export_format |= (uint32_t)pipeline->pixel_shader->reflection.
            color_exports[i].format << (i * 4u);
    }
    color_format_register.offset = AGC_REG_SPI_SHADER_COL_FORMAT;
    color_format_register.value = color_export_format;
    if (!sceAgcCbSetCxRegistersDirect(&cb, &color_format_register, 1u))
        return AGC_ERROR_INTERNAL;
    if (pipeline->color_attachment_count != 0u) {
        blend_state.target_count = pipeline->color_attachment_count;
        blend_state.logic_enable = pipeline->logic_operation_enable;
        blend_state.logic_operation =
            (AgcGfx1013LogicOp)pipeline->logic_operation;
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
    if (pipeline->has_static_depth_bias &&
        pipeline->depth_stencil.format != AGC_FORMAT_UNDEFINED) {
        switch ((AgcFormat)pipeline->depth_stencil.format) {
        case AGC_FORMAT_D16_UNORM:
        case AGC_FORMAT_D16_UNORM_S8_UINT:
            depth_bias_state.format = AGC_GFX1013_DEPTH_FORMAT_D16_UNORM;
            break;
        case AGC_FORMAT_D32_FLOAT:
        case AGC_FORMAT_D32_FLOAT_S8_UINT:
            depth_bias_state.format = AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT;
            break;
        default:
            return AGC_ERROR_VALIDATION_FAILED;
        }
        depth_bias_state.constant_factor =
            pipeline->static_depth_bias.constant_factor;
        depth_bias_state.clamp = pipeline->static_depth_bias.clamp;
        depth_bias_state.slope_factor =
            pipeline->static_depth_bias.slope_factor;
        result = agcGfx1013SetDepthBiasState(&cb, &depth_bias_state);
        if (result != AGC_OK)
            return result;
    }
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
    {
        const AgcRegisterValue restart_register = {
            AGC_REG_GE_MULTI_PRIM_IB_RESET_EN,
            pipeline->primitive_restart_enable,
        };
        if (!sceAgcCbSetCxRegistersDirect(&cb, &restart_register, 1u))
            return AGC_ERROR_INTERNAL;
    }
    pipeline->bind_dword_count = (uint32_t)agcCbUsedDwords(&cb);
    return AGC_OK;
}

static int agcPipelineGeometrySubsetValid(
    const AgcShaderReflection *reflection)
{
    uint32_t required_vertices;

    if (reflection->geometry_input_primitive == AGC_SHADER_PRIMITIVE_POINTS)
        required_vertices = 1u;
    else if (reflection->geometry_input_primitive ==
             AGC_SHADER_PRIMITIVE_LINES)
        required_vertices = 2u;
    else if (reflection->geometry_input_primitive ==
             AGC_SHADER_PRIMITIVE_LINES_ADJACENCY)
        required_vertices = 4u;
    else if (reflection->geometry_input_primitive ==
            AGC_SHADER_PRIMITIVE_TRIANGLES)
        required_vertices = 3u;
    else if (reflection->geometry_input_primitive ==
             AGC_SHADER_PRIMITIVE_TRIANGLES_ADJACENCY)
        required_vertices = 6u;
    else
        return 0;
    return reflection->geometry_vertices_in == required_vertices &&
        (reflection->geometry_output_primitive == AGC_SHADER_PRIMITIVE_POINTS ||
         reflection->geometry_output_primitive ==
             AGC_SHADER_PRIMITIVE_LINE_STRIP ||
         reflection->geometry_output_primitive ==
             AGC_SHADER_PRIMITIVE_TRIANGLE_STRIP);
}

static int agcPipelineGeometryTopologyValid(
    const AgcShaderReflection *reflection, AgcPrimitiveTopology topology)
{
    switch (reflection->geometry_input_primitive) {
    case AGC_SHADER_PRIMITIVE_POINTS:
        return topology == AGC_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case AGC_SHADER_PRIMITIVE_LINES:
        return topology == AGC_PRIMITIVE_TOPOLOGY_LINE_LIST ||
            topology == AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case AGC_SHADER_PRIMITIVE_LINES_ADJACENCY:
        return topology ==
                AGC_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY ||
            topology ==
                AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
    case AGC_SHADER_PRIMITIVE_TRIANGLES:
        return topology == AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ||
            topology == AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP ||
            topology == AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case AGC_SHADER_PRIMITIVE_TRIANGLES_ADJACENCY:
        return topology ==
                AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY ||
            topology ==
                AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
    default:
        return 0;
    }
}

static int agcPipelineGeometryIndexCountValid(
    AgcPrimitiveTopology topology, uint32_t index_count)
{
    switch (topology) {
    case AGC_PRIMITIVE_TOPOLOGY_POINT_LIST:
        return 1;
    case AGC_PRIMITIVE_TOPOLOGY_LINE_LIST:
        return index_count % 2u == 0u;
    case AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP:
        return index_count >= 2u;
    case AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
        return index_count % 3u == 0u;
    case AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    case AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
        return index_count >= 3u;
    case AGC_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
        return index_count % 4u == 0u;
    case AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
        return index_count >= 4u;
    case AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
        return index_count % 6u == 0u;
    case AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
        return index_count >= 6u && index_count % 2u == 0u;
    default:
        return 0;
    }
}

static uint32_t agcPipelinePrimitiveType(AgcPrimitiveTopology topology)
{
    static const uint32_t primitive_types[AGC_PRIMITIVE_TOPOLOGY_COUNT] = {
        1u, 2u, 3u, 4u, 6u, 5u, 10u, 11u, 12u, 13u, 9u};
    return primitive_types[topology];
}

int32_t PS5_SYSV_ABI agcCreateGraphicsPipeline(AgcDevice device,
    const AgcGraphicsPipelineDesc *desc, AgcGraphicsPipeline *pipeline_out)
{
    AgcGraphicsPipeline pipeline;
    AgcShader hull = NULL;
    AgcShader primitive;
    AgcShader vertex_stage;
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
        AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT |
        AGC_DYNAMIC_STATE_LINE_WIDTH_BIT;
    uint32_t i;
    uint32_t j;
    uint32_t tessellation_tcs_layout = 0u;
    uint32_t tessellation_tes_layout = 0u;
    uint32_t push_constant_size;
    int tessellated;
    int32_t result;

    if (!pipeline_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *pipeline_out = NULL;
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!desc ||
        desc->struct_size != sizeof(*desc) ||
        (desc->version != AGC_RUNTIME_STRUCTURE_VERSION_2 &&
         desc->version != AGC_RUNTIME_STRUCTURE_VERSION_3 &&
         desc->version != AGC_RUNTIME_STRUCTURE_VERSION_4 &&
         desc->version != AGC_RUNTIME_STRUCTURE_VERSION_5) ||
        desc->flags != 0u || desc->reserved0 != 0u ||
        !agcReservedZero(desc->reserved, 4u) ||
        (desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2 ?
            desc->primitive_topology != AGC_PRIMITIVE_TOPOLOGY_POINT_LIST :
            desc->primitive_topology >= AGC_PRIMITIVE_TOPOLOGY_COUNT) ||
        (desc->version < AGC_RUNTIME_STRUCTURE_VERSION_4 ?
            (desc->static_depth_bias != NULL ||
             desc->logic_operation_enable != 0u ||
             desc->logic_operation != AGC_LOGIC_OPERATION_CLEAR ||
             desc->primitive_restart_enable != 0u ||
             desc->reserved3 != 0u ||
             !agcReservedZero(desc->reserved2, 1u)) :
         desc->version < AGC_RUNTIME_STRUCTURE_VERSION_5 ?
            (desc->logic_operation_enable > 1u ||
             desc->logic_operation >= AGC_LOGIC_OPERATION_COUNT ||
             desc->primitive_restart_enable != 0u ||
             desc->reserved3 != 0u ||
             !agcReservedZero(desc->reserved2, 1u)) :
            (desc->logic_operation_enable > 1u ||
             desc->logic_operation >= AGC_LOGIC_OPERATION_COUNT ||
             desc->primitive_restart_enable > 1u ||
             desc->reserved3 != 0u ||
             !agcReservedZero(desc->reserved2, 1u))) ||
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
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_INVALID_ARGUMENT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            "graphics pipeline descriptor has an invalid version, count, pointer, flag, dynamic state, or reserved field");
    }
    tessellated = desc->tessellation_control_shader != NULL;
    {
        const AgcPrimitiveTopology topology =
            desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2 ?
                AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST :
                desc->primitive_topology;
        if ((tessellated &&
             topology != AGC_PRIMITIVE_TOPOLOGY_PATCH_LIST) ||
            (!tessellated &&
             topology == AGC_PRIMITIVE_TOPOLOGY_PATCH_LIST))
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "primitive topology does not match the tessellation stage graph");
        if (desc->primitive_restart_enable &&
            topology != AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP &&
            topology != AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
            topology != AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN &&
            topology !=
                AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY &&
            topology !=
                AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY)
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "primitive restart requires a strip or fan topology");
    }
    if (tessellated) {
        if (desc->vertex_shader ||
            (desc->tessellation_evaluation_shader != NULL) ==
                (desc->geometry_shader != NULL))
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "tessellation pipeline requires a hull shader and exactly one evaluation or geometry primitive shader");
        hull = desc->tessellation_control_shader;
        primitive = desc->geometry_shader ? desc->geometry_shader :
            desc->tessellation_evaluation_shader;
    } else {
        if (desc->tessellation_evaluation_shader)
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "tessellation-evaluation shader requires a tessellation-control shader");
        if (desc->geometry_shader) {
            if (desc->vertex_shader)
                return agcPipelineDebugReport(device,
                    "agcCreateGraphicsPipeline",
                    AGC_ERROR_VALIDATION_FAILED,
                    AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                    "graphics pipeline cannot provide both separate vertex and fused geometry shaders");
            primitive = desc->geometry_shader;
        } else {
            primitive = desc->vertex_shader;
        }
    }
    vertex_stage = hull ? hull : primitive;
    ps = desc->pixel_shader;
    if (!primitive || !ps || primitive->magic != AGC_MAGIC_SHADER ||
        ps->magic != AGC_MAGIC_SHADER || primitive->device != device ||
        ps->device != device || (hull &&
        (hull->magic != AGC_MAGIC_SHADER || hull->device != device ||
         hull->stage != kAgcShaderStageHs)) ||
        primitive->stage != (desc->geometry_shader ? kAgcShaderStageGs :
            (tessellated ? kAgcShaderStageDs : kAgcShaderStageVs)) ||
        ps->stage != kAgcShaderStagePs) {
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_SHADER_INVALID_TYPE,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "graphics pipeline shader handles, devices, or stage types do not match the requested stage graph");
    }
    if (!primitive->has_reflection || !ps->has_reflection ||
        (hull && !hull->has_reflection))
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_SHADER_INVALID,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "graphics pipeline shaders require valid compiler reflection metadata");
    if (desc->geometry_shader && !tessellated &&
        (primitive->reflection.version != AGC_SHADER_REFLECTION_VERSION_2 ||
         primitive->reflection.front_stage != kAgcShaderStageVs ||
         !primitive->has_front_record ||
         (primitive->reflection.flags &
          (AGC_SHADER_REFLECTION_NGG_BIT |
           AGC_SHADER_REFLECTION_FUSED_STAGE_BIT)) !=
             (AGC_SHADER_REFLECTION_NGG_BIT |
              AGC_SHADER_REFLECTION_FUSED_STAGE_BIT) ||
         (!agcPipelineGeometrySubsetValid(&primitive->reflection) ||
          !agcPipelineGeometryTopologyValid(&primitive->reflection,
              desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2 ?
                  AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST :
                  desc->primitive_topology)))) {
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "geometry pipeline requires a qualified fused NGG front/back shader and supported primitive topology");
    }
    if (tessellated) {
        const AgcShaderReflection *hs = &hull->reflection;
        const AgcShaderReflection *prim = &primitive->reflection;
        const uint64_t primitive_input_mask = desc->geometry_shader ?
            prim->front_stage_input_mask : prim->stage_input_mask;
        const uint64_t primitive_patch_input_mask = desc->geometry_shader ?
            prim->front_patch_input_mask : prim->patch_input_mask;
        AgcGfx1013TessellationLayoutState layout;

        if (hs->version != AGC_SHADER_REFLECTION_VERSION_2 ||
            hs->front_stage != kAgcShaderStageVs ||
            !hull->has_front_record || hs->wave_size != 32u ||
            (hs->flags & AGC_SHADER_REFLECTION_FUSED_STAGE_BIT) == 0u ||
            prim->version != AGC_SHADER_REFLECTION_VERSION_2 ||
            prim->front_stage != kAgcShaderStageDs ||
            !primitive->has_front_record || prim->wave_size != 32u ||
            (prim->flags & AGC_SHADER_REFLECTION_NGG_BIT) == 0u ||
            (desc->geometry_shader &&
             (prim->flags & AGC_SHADER_REFLECTION_FUSED_STAGE_BIT) == 0u) ||
            hs->tessellation_patch_count !=
                prim->tessellation_patch_count ||
            hs->tessellation_output_control_points !=
                prim->tessellation_output_control_points) {
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_NOT_SUPPORTED,
                AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
                "tessellation pipeline reflection does not match the qualified fused-stage, wave, patch, or control-point subset");
        }
        if ((hs->stage_input_mask & ~hs->front_stage_output_mask) != 0u ||
            (primitive_input_mask & ~hs->stage_output_mask) != 0u ||
            (primitive_patch_input_mask & ~hs->patch_output_mask) != 0u ||
            (desc->geometry_shader &&
             (prim->stage_input_mask & ~prim->front_stage_output_mask) !=
                 0u)) {
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "graphics shader stage input/output or patch linkage masks are incompatible");
        }
        if (desc->geometry_shader &&
            !agcPipelineGeometrySubsetValid(prim)) {
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_NOT_SUPPORTED,
                AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
                "geometry shader primitive topology is outside the qualified subset");
        }
        layout = (AgcGfx1013TessellationLayoutState){
            hs->tessellation_patch_count,
            hs->tessellation_input_control_points,
            hs->tessellation_output_control_points,
            hs->tessellation_vertex_output_count,
            hs->tessellation_control_output_count,
            prim->tessellation_primitive_mode,
            prim->tessellation_reads_factors,
        };
        result = agcGfx1013BuildTessellationOffchipLayouts(&layout,
            &tessellation_tcs_layout, &tessellation_tes_layout);
        if (result != AGC_OK)
            return result == AGC_ERROR_INVALID_ARGUMENT ?
                AGC_ERROR_VALIDATION_FAILED : result;
    }
    if (hull) {
        result = agcPipelineValidateShaderUserData(&hull->reflection);
        if (result != AGC_OK)
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", result,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "hull-shader user-SGPR reflection is incompatible with the native resource binding contract");
    }
    result = agcPipelineValidateShaderUserData(&primitive->reflection);
    if (result != AGC_OK)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            result, AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "primitive-shader user-SGPR reflection is incompatible with the native resource binding contract");
    result = agcPipelineValidateShaderUserData(&ps->reflection);
    if (result != AGC_OK)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            result, AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "pixel-shader user-SGPR reflection is incompatible with the native resource binding contract");
    if (primitive->reflection.scratch_bytes_per_wave != 0u ||
        ps->reflection.scratch_bytes_per_wave != 0u ||
        (hull && hull->reflection.scratch_bytes_per_wave != 0u))
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "graphics pipeline requests scratch, LDS, wave, or legacy geometry behavior outside the qualified profile");
    if (primitive->reflection.lds_size > 65536u ||
        ps->reflection.lds_size != 0u ||
        (hull && hull->reflection.lds_size > 65536u))
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "graphics pipeline requests scratch, LDS, wave, or legacy geometry behavior outside the qualified profile");
    if ((primitive->reflection.flags & AGC_SHADER_REFLECTION_NGG_BIT) == 0u ||
        ps->reflection.wave_size != 32u)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "graphics pipeline requires qualified NGG primitive and wave32 pixel shaders");
    if ((ps->reflection.stage_input_mask &
         ~primitive->reflection.stage_output_mask) != 0u)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "pixel shader inputs are not produced by the preceding graphics stage");
    if (desc->vertex_input_count !=
            vertex_stage->reflection.vertex_input_count)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "vertex-input count does not match shader reflection");
    for (i = 0u; i < desc->vertex_input_count; ++i) {
        for (j = 0u; j < vertex_stage->reflection.vertex_input_count; ++j) {
            if (agcPipelineVertexInputEqual(
                    &desc->vertex_inputs[i],
                    &vertex_stage->reflection.vertex_inputs[j]))
                break;
        }
        if (j == vertex_stage->reflection.vertex_input_count)
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "vertex-input format, binding, stride, rate, or component mask does not match shader reflection");
    }
    if ((hull && !agcPipelineReflectionDescriptorsMatch(&hull->reflection,
            desc->descriptor_mappings, desc->descriptor_mapping_count)) ||
        !agcPipelineReflectionDescriptorsMatch(&primitive->reflection,
            desc->descriptor_mappings, desc->descriptor_mapping_count) ||
        !agcPipelineReflectionDescriptorsMatch(&ps->reflection,
            desc->descriptor_mappings, desc->descriptor_mapping_count) ||
        (hull && !agcPipelineReflectionPushRangesMatch(&hull->reflection,
            desc->push_constant_ranges,
            desc->push_constant_range_count)) ||
        !agcPipelineReflectionPushRangesMatch(&primitive->reflection,
            desc->push_constant_ranges,
            desc->push_constant_range_count) ||
        !agcPipelineReflectionPushRangesMatch(&ps->reflection,
            desc->push_constant_ranges,
            desc->push_constant_range_count)) {
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "descriptor mappings or push-constant ranges do not match every shader stage reflection");
    }
    for (i = 0u; i < desc->descriptor_mapping_count; ++i) {
        const AgcShaderDescriptorMapping *mapping =
            &desc->descriptor_mappings[i];
        if (mapping->set >= 8u ||
            mapping->type >= AGC_SHADER_DESCRIPTOR_TYPE_COUNT ||
        AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size) == 0u ||
        mapping->byte_stride == 0u ||
            (mapping->byte_offset & 3u) != 0u ||
            (mapping->byte_stride & 3u) != 0u ||
            !agcPipelineLayoutEntryUsed(
                hull ? &hull->reflection : NULL,
                &primitive->reflection, &ps->reflection, mapping)) {
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "descriptor mapping is invalid, misaligned, unused, or unsupported by the reflected pipeline layout");
        }
        for (j = 0u; j < i; ++j) {
            if (desc->descriptor_mappings[j].set == mapping->set &&
                desc->descriptor_mappings[j].binding == mapping->binding)
                return agcPipelineDebugReport(device,
                    "agcCreateGraphicsPipeline",
                    AGC_ERROR_VALIDATION_FAILED,
                    AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                    "descriptor mappings contain a duplicate set and binding");
        }
    }
    for (i = 0u; i < desc->push_constant_range_count; ++i) {
        const AgcShaderPushConstantRange *range =
            &desc->push_constant_ranges[i];
        if (range->size == 0u || range->alignment != 4u ||
            (range->offset & 3u) != 0u || (range->size & 3u) != 0u ||
            (range->stage_mask & ~((1u << kAgcShaderStageCount) - 1u)) != 0u ||
            !agcPipelinePushEntryUsed(
                hull ? &hull->reflection : NULL,
                &primitive->reflection, &ps->reflection, range)) {
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "push-constant range is empty, misaligned, unused, or has an invalid stage mask");
        }
    }
    if ((ps->reflection.flags &
         AGC_SHADER_REFLECTION_DUAL_SOURCE_EXPORT_BIT) != 0u) {
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "dual-source shader exports are not supported by the qualified graphics profile");
    }
    if (desc->color_attachment_count != ps->reflection.color_export_count)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "color-attachment count does not match pixel-shader exports");
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
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "color attachment format, write mask, or component class is incompatible with the shader export; integer targets cannot enable blending");
        }
        if (agcPipelineBlendFactorUsesSource1(
                attachment->source_color_factor) ||
            agcPipelineBlendFactorUsesSource1(
                attachment->destination_color_factor) ||
            agcPipelineBlendFactorUsesSource1(
                attachment->source_alpha_factor) ||
            agcPipelineBlendFactorUsesSource1(
                attachment->destination_alpha_factor)) {
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_NOT_SUPPORTED,
                AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
                "source-1 blend factors require unsupported dual-source blending");
        }
    }
    if (desc->rasterization) {
        if (!agcPipelineRasterizationStateValid(desc->rasterization))
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_INVALID_ARGUMENT,
                AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
                "rasterization state contains an invalid enum, value, version, flag, or reserved field");
        rasterization = *desc->rasterization;
    }
    if ((desc->dynamic_state_mask & AGC_DYNAMIC_STATE_LINE_WIDTH_BIT) == 0u &&
        rasterization.line_width > 64.0f)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "static line width exceeds the qualified range");
    if (!rasterization.depth_bias_enable &&
        ((desc->dynamic_state_mask & AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT) != 0u ||
         desc->static_depth_bias != NULL))
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "disabled depth bias cannot declare dynamic or static state");
    if (rasterization.depth_bias_enable) {
        const int dynamic_depth_bias =
            (desc->dynamic_state_mask &
                AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT) != 0u;
        if (dynamic_depth_bias == (desc->static_depth_bias != NULL) ||
            (desc->static_depth_bias &&
             !agcPipelineDepthBiasValid(desc->static_depth_bias)))
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "enabled depth bias requires exactly one valid static or dynamic state");
    }
    if (desc->depth_stencil) {
        result = agcPipelineNormalizeDepthState(
            desc->depth_stencil, &depth_stencil);
        if (result != AGC_OK)
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", result,
                result == AGC_ERROR_NOT_SUPPORTED ?
                    AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT :
                    AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
                "depth/stencil state contains an invalid or unsupported format, operation, bound, flag, or reserved field");
    }
    if ((ps->reflection.flags & AGC_SHADER_REFLECTION_WRITES_DEPTH_BIT) != 0u &&
        depth_stencil.format == AGC_FORMAT_UNDEFINED)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "pixel shader writes depth but the pipeline has no depth attachment format");
    if ((ps->reflection.flags & AGC_SHADER_REFLECTION_WRITES_STENCIL_BIT) != 0u) {
        AgcShaderComponentClass component_class;
        uint32_t component_bits;
        uint32_t has_depth;
        uint32_t has_stencil;
        if (!agcPipelineFormatInfo(depth_stencil.format, &component_class,
                &component_bits, &has_depth, &has_stencil) || !has_stencil)
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "pixel shader writes stencil but the pipeline attachment format has no stencil aspect");
    }
    if (desc->multisample) {
        if (!agcPipelineMultisampleStateValid(desc->multisample))
            return agcPipelineDebugReport(device,
                "agcCreateGraphicsPipeline", AGC_ERROR_INVALID_ARGUMENT,
                AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
                "multisample state contains an invalid sample count, shading value, version, flag, or reserved field");
        multisample = *desc->multisample;
    }
    if (multisample.alpha_to_coverage_enable)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "alpha-to-coverage is outside the qualified multisample subset");
    if (((ps->reflection.flags &
          AGC_SHADER_REFLECTION_ALPHA_TO_ONE_BIT) != 0u) !=
        (multisample.alpha_to_one_enable != 0u))
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "pixel-shader alpha-to-one reflection disagrees with pipeline multisample state");
    if (ps->reflection.pixel_shader_sample_count != 0u &&
        ps->reflection.pixel_shader_sample_count >
            multisample.rasterization_samples)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "pixel-shader sample count exceeds pipeline rasterization samples");
    if (((ps->reflection.flags &
          AGC_SHADER_REFLECTION_USES_SAMPLE_SHADING_BIT) != 0u) !=
        (multisample.sample_shading_enable != 0u))
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "pixel-shader sample-shading reflection disagrees with pipeline multisample state");
    if (multisample.sample_shading_enable &&
        (float)ps->reflection.pixel_shader_sample_count <
            multisample.minimum_sample_shading *
                (float)multisample.rasterization_samples)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "pixel-shader sample frequency does not satisfy minimum sample shading");
    push_constant_size = primitive->reflection.push_constant_size >
        ps->reflection.push_constant_size ?
        primitive->reflection.push_constant_size :
        ps->reflection.push_constant_size;
    if (hull && hull->reflection.push_constant_size > push_constant_size)
        push_constant_size = hull->reflection.push_constant_size;
    result = agcPipelineBuildResourceLayout(desc->descriptor_mappings,
        desc->descriptor_mapping_count, desc->vertex_inputs,
        desc->vertex_input_count, push_constant_size,
        agcPipelineUsesIndirectDescriptorSets(&primitive->reflection) ||
            (hull && agcPipelineUsesIndirectDescriptorSets(
                &hull->reflection)) ||
        agcPipelineUsesIndirectDescriptorSets(&ps->reflection),
        &resource_layout);
    if (result != AGC_OK)
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            result, AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "reflected descriptor, vertex-input, or push-constant layout exceeds the native resource arena contract");
    if (tessellated) {
        result = agcRuntimeInitializeTessellationStorage(device);
        if (result != AGC_OK)
            return result;
    }
    pipeline = agcCreateChild(device, sizeof(*pipeline));
    if (!pipeline)
        return AGC_ERROR_OUT_OF_MEMORY;
    pipeline->magic = AGC_MAGIC_GRAPHICS_PIPELINE;
    pipeline->device = device;
    pipeline->hull_shader = hull;
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
    pipeline->has_static_depth_bias = desc->static_depth_bias != NULL;
    if (desc->static_depth_bias)
        pipeline->static_depth_bias = *desc->static_depth_bias;
    pipeline->dynamic_state_mask = desc->dynamic_state_mask;
    pipeline->logic_operation_enable = desc->logic_operation_enable;
    pipeline->logic_operation = desc->logic_operation;
    pipeline->primitive_restart_enable =
        desc->version >= AGC_RUNTIME_STRUCTURE_VERSION_5 ?
            desc->primitive_restart_enable : 0u;
    pipeline->resource_layout = resource_layout;
    pipeline->primitive_topology =
        desc->version == AGC_RUNTIME_STRUCTURE_VERSION_2 ?
            AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST :
            desc->primitive_topology;
    pipeline->primitive_type = agcPipelinePrimitiveType(
        pipeline->primitive_topology);
    if (primitive->stage == kAgcShaderStageGs)
        pipeline->geometry_input_vertices =
            primitive->reflection.geometry_vertices_in;
    if (hull) {
        pipeline->tessellation_input_control_points =
            hull->reflection.tessellation_input_control_points;
        pipeline->tessellation_tcs_offchip_layout =
            tessellation_tcs_layout;
        pipeline->tessellation_tes_offchip_layout =
            tessellation_tes_layout;
    }
    pipeline->resource_layout_requires_bindings =
        desc->descriptor_mapping_count != 0u ||
        desc->push_constant_range_count != 0u;
    result = agcBuildGraphicsPipelineBind(pipeline);
    if (result != AGC_OK) {
        pipeline->magic = 0u;
        agcDestroyChild(device, pipeline);
        return agcPipelineDebugReport(device, "agcCreateGraphicsPipeline",
            result, AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "reflected shader registers and fixed-function state cannot be encoded into the native graphics-pipeline bind packet");
    }
    if (hull)
        hull->dependency_refs++;
    primitive->dependency_refs++;
    ps->dependency_refs++;
    *pipeline_out = pipeline;
    agcCaptureRecordObjectCreate(device, pipeline,
        AGC_CAPTURE_OBJECT_GRAPHICS_PIPELINE,
        pipeline->color_attachment_count, pipeline->multisample.rasterization_samples);
    agcCaptureRecordGraphicsPipeline(pipeline);
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
        return agcPipelineDebugReport(pipeline->device,
            "agcDestroyGraphicsPipeline", AGC_ERROR_BUSY,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT,
            "graphics-pipeline destruction requires recorded command references to be released");
    device = pipeline->device;
    agcCaptureRecordObjectDestroy(device, pipeline,
        AGC_CAPTURE_OBJECT_GRAPHICS_PIPELINE);
    if (pipeline->hull_shader)
        pipeline->hull_shader->dependency_refs--;
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
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!desc ||
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
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            AGC_ERROR_INVALID_ARGUMENT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            "compute pipeline descriptor has an invalid version, local size, count, pointer, flag, or reserved field");
    }
    shader = desc->shader;
    if (!shader || shader->magic != AGC_MAGIC_SHADER ||
        shader->device != device || shader->stage != kAgcShaderStageCs) {
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            AGC_ERROR_SHADER_INVALID_TYPE,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "compute pipeline requires a live same-device compute shader");
    }
    if (!shader->has_reflection)
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            AGC_ERROR_SHADER_INVALID,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "compute pipeline shader requires valid compiler reflection metadata");
    result = agcPipelineValidateShaderUserData(&shader->reflection);
    if (result != AGC_OK)
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            result, AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "compute-shader user-SGPR reflection is incompatible with the native resource binding contract");
    if (shader->reflection.wave_size != 32u ||
        shader->reflection.scratch_bytes_per_wave != 0u ||
        shader->reflection.lds_size > 65536u)
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "compute shader wave, scratch, or LDS requirements are outside the qualified profile");
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
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            AGC_ERROR_VALIDATION_FAILED,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "compute local size, descriptor mappings, or push-constant ranges do not match shader reflection");
    }
    for (i = 0u; i < desc->descriptor_mapping_count; ++i) {
        const AgcShaderDescriptorMapping *mapping =
            &desc->descriptor_mappings[i];
        if (mapping->set >= 8u ||
            mapping->type >= AGC_SHADER_DESCRIPTOR_TYPE_COUNT ||
        AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size) == 0u ||
        mapping->byte_stride == 0u ||
            (mapping->byte_offset & 3u) != 0u ||
            (mapping->byte_stride & 3u) != 0u) {
            return agcPipelineDebugReport(device,
                "agcCreateComputePipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "compute descriptor mapping is invalid, misaligned, or unsupported by reflection");
        }
        for (j = 0u; j < i; ++j) {
            if (desc->descriptor_mappings[j].set == mapping->set &&
                desc->descriptor_mappings[j].binding == mapping->binding)
                return agcPipelineDebugReport(device,
                    "agcCreateComputePipeline",
                    AGC_ERROR_VALIDATION_FAILED,
                    AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                    "compute descriptor mappings contain a duplicate set and binding");
        }
    }
    for (i = 0u; i < desc->push_constant_range_count; ++i) {
        const AgcShaderPushConstantRange *range =
            &desc->push_constant_ranges[i];
        if (range->size == 0u || range->alignment != 4u ||
            (range->offset & 3u) != 0u || (range->size & 3u) != 0u ||
            (range->stage_mask & (1u << kAgcShaderStageCs)) == 0u)
            return agcPipelineDebugReport(device,
                "agcCreateComputePipeline", AGC_ERROR_VALIDATION_FAILED,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                "compute push-constant range is empty, misaligned, or missing the compute stage");
    }
    invocations = (uint64_t)desc->local_size_x * desc->local_size_y *
        desc->local_size_z;
    if (invocations > 1024u)
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            AGC_ERROR_NOT_SUPPORTED, AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            "compute workgroup exceeds the qualified 1024-invocation limit");
    result = agcPipelineBuildResourceLayout(desc->descriptor_mappings,
        desc->descriptor_mapping_count, NULL, 0u,
        shader->reflection.push_constant_size,
        agcPipelineUsesIndirectDescriptorSets(&shader->reflection),
        &resource_layout);
    if (result != AGC_OK)
        return agcPipelineDebugReport(device, "agcCreateComputePipeline",
            result, AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            "reflected descriptor or push-constant layout exceeds the native resource arena contract");
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
    agcCaptureRecordObjectCreate(device, pipeline,
        AGC_CAPTURE_OBJECT_COMPUTE_PIPELINE, pipeline->local_size[0],
        pipeline->local_size[1]);
    agcCaptureRecordComputePipeline(pipeline);
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
        return agcPipelineDebugReport(pipeline->device,
            "agcDestroyComputePipeline", AGC_ERROR_BUSY,
            AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT,
            "compute-pipeline destruction requires recorded command references to be released");
    device = pipeline->device;
    agcCaptureRecordObjectDestroy(device, pipeline,
        AGC_CAPTURE_OBJECT_COMPUTE_PIPELINE);
    pipeline->shader->dependency_refs--;
    pipeline->magic = 0u;
    agcDestroyChild(device, pipeline);
    return AGC_OK;
}

static void agcReleaseCommandReferences(AgcCommandBuffer command_buffer)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_graphics_pipeline_count; ++i)
        command_buffer->recorded_graphics_pipelines[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_compute_pipeline_count; ++i)
        command_buffer->recorded_compute_pipelines[i]->recorded_refs--;
    command_buffer->graphics_pipeline = NULL;
    command_buffer->compute_pipeline = NULL;
    if (command_buffer->index_buffer) {
        command_buffer->index_buffer->recorded_refs--;
        command_buffer->index_buffer = NULL;
    }
    for (i = 0u; i < command_buffer->recorded_buffer_count; ++i)
        command_buffer->recorded_buffers[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_image_count; ++i)
        command_buffer->recorded_images[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_view_count; ++i)
        command_buffer->recorded_views[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_sampler_count; ++i)
        command_buffer->recorded_samplers[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_label_count; ++i)
        command_buffer->recorded_labels[i]->recorded_refs--;
    for (i = 0u; i < command_buffer->recorded_transition_count; ++i) {
        const AgcRuntimeRecordedTransition *record =
            &command_buffer->recorded_transitions[i];

        if ((record->flags & AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) == 0u)
            continue;
        if (record->resource_type == kAgcResourceTypeBuffer) {
            AgcBuffer buffer = (AgcBuffer)record->resource;
            AgcRuntimePendingTransfer *transfer = agcBufferFindTransfer(buffer,
                record->buffer_offset, record->buffer_size);
            if (transfer && transfer->acquire_command == command_buffer)
                transfer->acquire_command = NULL;
        } else {
            AgcImage image = (AgcImage)record->resource;
            AgcRuntimePendingTransfer *transfer = agcImageFindTransfer(image,
                &record->image_range);
            if (transfer && transfer->acquire_command == command_buffer)
                transfer->acquire_command = NULL;
        }
    }
    command_buffer->recorded_buffer_count = 0u;
    command_buffer->recorded_image_count = 0u;
    command_buffer->recorded_view_count = 0u;
    command_buffer->recorded_sampler_count = 0u;
    command_buffer->recorded_transition_count = 0u;
    command_buffer->recorded_label_count = 0u;
    command_buffer->recorded_graphics_pipeline_count = 0u;
    command_buffer->recorded_compute_pipeline_count = 0u;
    command_buffer->recorded_label_wait_count = 0u;
    command_buffer->recorded_label_signal_count = 0u;
    command_buffer->descriptors_bound = 0u;
    command_buffer->vertex_binding_mask = 0u;
    command_buffer->dynamic_state_set_mask = 0u;
    command_buffer->color_target_count = 0u;
    command_buffer->color_target_width = 0u;
    command_buffer->color_target_height = 0u;
    command_buffer->graphics_defaults_emitted = 0u;
    command_buffer->depth_stencil_target = NULL;
    memset(command_buffer->push_constant_masks, 0,
        sizeof(command_buffer->push_constant_masks));
    for (i = 0u; i < command_buffer->recorded_resource_allocation_count; ++i)
        agcRuntimeFree(command_buffer->device,
            command_buffer->recorded_resource_allocations[i]);
    command_buffer->recorded_resource_allocation_count = 0u;
    command_buffer->resource_allocation = NULL;
}

static int32_t agcPrepareCommandResources(AgcCommandBuffer command_buffer,
    const AgcRuntimePipelineResourceLayout *layout)
{
    AgcRuntimeAllocation *allocation = NULL;
    uint8_t *cpu_base;
    uint64_t gpu_base;
    int32_t result;

    if (layout->total_size == 0u) {
        command_buffer->resource_allocation = NULL;
        return AGC_OK;
    }
    if (command_buffer->recorded_resource_allocation_count >=
        AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return AGC_ERROR_OUT_OF_MEMORY;
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
    command_buffer->recorded_resource_allocations[
        command_buffer->recorded_resource_allocation_count++] = allocation;
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
    /* A submitted DCB is a kernel-consumed command stream, not ordinary
     * upload data.  Keep its mapping isolated from mutable buffer/image
     * allocations so a DMA packet never names operands in the same backing
     * mapping that carries the packet itself. */
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        storage_size, 256u, 1u, AGC_OBJECT_TYPE_COMMAND_BUFFER,
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
    agcCaptureRecordObjectCreate(device, command_buffer,
        AGC_CAPTURE_OBJECT_COMMAND_BUFFER, (uint32_t)desc->queue_type,
        desc->capacity_dwords);
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
    agcCaptureRecordObjectDestroy(device, command_buffer,
        AGC_CAPTURE_OBJECT_COMMAND_BUFFER);
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
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcBeginCommandBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer must be in Initial state before begin");
    agcCbReset(&command_buffer->cursor, command_buffer->storage,
        (size_t)command_buffer->capacity_dwords * sizeof(uint32_t));
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_RECORDING;
    agcCaptureRecordCommandBegin(command_buffer);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcEndCommandBuffer(AgcCommandBuffer command_buffer)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcEndCommandBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer must be Recording before end");
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
    agcCaptureRecordCommandWords(command_buffer,
        AGC_CAPTURE_RECORD_COMMAND_END);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcResetCommandBuffer(AgcCommandBuffer command_buffer)
{
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state == AGC_COMMAND_BUFFER_STATE_PENDING) {
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcResetCommandBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "pending command buffer cannot be reset before completion");
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

static int32_t agcCommandCommitScratch(AgcCommandBuffer command_buffer,
    const SceAgcCb *scratch, const uint32_t *words);
static int agcCommandBufferRangeState(AgcCommandBuffer command_buffer,
    AgcBuffer buffer, uint64_t offset, uint64_t size,
    AgcResourceUsage *usage, AgcResourceOwner *owner);
static void agcCommandTransitionState(AgcCommandBuffer command_buffer,
    AgcResourceType resource_type, void *resource, AgcResourceUsage *usage,
    AgcResourceOwner *owner);

int32_t PS5_SYSV_ABI agcCmdBindGraphicsPipeline(
    AgcCommandBuffer command_buffer, AgcGraphicsPipeline pipeline)
{
    uint32_t defaults[AGC_RUNTIME_GRAPHICS_DEFAULT_DWORDS];
    SceAgcCb defaults_cb;
    uint32_t defaults_dword_count;
    uint32_t border_words[4];
    SceAgcCb border_cb;
    uint32_t border_dword_count;
    uint32_t pipeline_index;
    uint32_t first_bind;
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
    if (command_buffer->graphics_pipeline == pipeline)
        return AGC_OK;
    first_bind = !command_buffer->graphics_defaults_emitted;
    for (pipeline_index = 0u;
         pipeline_index < command_buffer->recorded_graphics_pipeline_count;
         ++pipeline_index) {
        if (command_buffer->recorded_graphics_pipelines[pipeline_index] ==
            pipeline)
            break;
    }
    if (pipeline_index == command_buffer->recorded_graphics_pipeline_count &&
        pipeline_index >= AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return AGC_ERROR_OUT_OF_MEMORY;
    {
        uint32_t *commands;
        defaults_dword_count = 0u;
        if (first_bind) {
        agcCbInit(&defaults_cb, defaults, sizeof(defaults));
        /* Mirror the qualified FW 5.50 graphics path: establish V8 register
         * defaults before runtime-owned shader and fixed-function state.
         * CLEAR_STATE remains excluded because it is not hardware-safe here. */
        result = agcGfx1013ApplyGraphicsDefaultsV8(&defaults_cb, NULL);
        if (result != AGC_OK)
            return result == AGC_ERROR_BUFFER_TOO_SMALL ?
                AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
        defaults_dword_count = (uint32_t)agcCbUsedDwords(&defaults_cb);
        }
        border_dword_count = 0u;
        if (first_bind && command_buffer->device->border_color_allocation) {
            agcCbInit(&border_cb, border_words, sizeof(border_words));
            result = agcGfx1013SetBorderColorTable(&border_cb,
                agcAllocationGpuAddress(
                    command_buffer->device->border_color_allocation));
            if (result != AGC_OK)
                return result;
            border_dword_count = (uint32_t)agcCbUsedDwords(&border_cb);
        }
        if (agcCbRemainingDwords(&command_buffer->cursor) <
            defaults_dword_count + border_dword_count +
                pipeline->bind_dword_count)
            return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
        result = agcPrepareCommandResources(
            command_buffer, &pipeline->resource_layout);
        if (result != AGC_OK)
            return result;
        if (defaults_dword_count != 0u) {
            commands = agcCbAllocDwords(&command_buffer->cursor,
                defaults_dword_count);
            if (!commands)
                return AGC_ERROR_INTERNAL;
            memcpy(commands, defaults,
                defaults_dword_count * sizeof(uint32_t));
            command_buffer->graphics_defaults_emitted = 1u;
        }
        if (border_dword_count != 0u) {
            commands = agcCbAllocDwords(&command_buffer->cursor,
                border_dword_count);
            if (!commands)
                return AGC_ERROR_INTERNAL;
            memcpy(commands, border_words,
                border_dword_count * sizeof(uint32_t));
        }
        commands = agcCbAllocDwords(
            &command_buffer->cursor, pipeline->bind_dword_count);
        if (!commands)
            return AGC_ERROR_INTERNAL;
        memcpy(commands, pipeline->bind_words,
            pipeline->bind_dword_count * sizeof(uint32_t));
        command_buffer->graphics_pipeline = pipeline;
        command_buffer->descriptors_bound = 0u;
        command_buffer->vertex_binding_mask = 0u;
        memset(command_buffer->push_constant_masks, 0,
            sizeof(command_buffer->push_constant_masks));
        if (pipeline_index ==
            command_buffer->recorded_graphics_pipeline_count) {
            command_buffer->recorded_graphics_pipelines[pipeline_index] =
                pipeline;
            command_buffer->recorded_graphics_pipeline_count++;
            pipeline->recorded_refs++;
        }
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBindColorTargets(
    AgcCommandBuffer command_buffer, uint32_t target_count,
    const AgcColorTargetBinding *targets)
{
    AgcGfx1013ColorTargetState states[AGC_GFX1013_MAX_COLOR_TARGETS];
    uint32_t words[AGC_GFX1013_MAX_COLOR_TARGETS * 28u];
    SceAgcCb scratch;
    uint32_t width = 0u;
    uint32_t height = 0u;
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    uint32_t i;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !targets) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics ||
        !command_buffer->graphics_pipeline) {
        return AGC_ERROR_INVALID_STATE;
    }
    if (target_count == 0u || target_count > AGC_GFX1013_MAX_COLOR_TARGETS ||
        target_count != command_buffer->graphics_pipeline->
            color_attachment_count) {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    if (command_buffer->color_target_count != 0u)
        return AGC_ERROR_NOT_SUPPORTED;
    if (command_buffer->recorded_image_count >
        AGC_RUNTIME_MAX_RECORDED_RESOURCES - target_count) {
        return AGC_ERROR_OUT_OF_MEMORY;
    }
    for (i = 0u; i < target_count; ++i) {
        const AgcColorTargetBinding *binding = &targets[i];
        AgcImage image;
        AgcImageSubresourceLayout layout = AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        AgcGfx1013ColorTargetFormat format;
        uint64_t address;
        uint32_t j;

        if (!agcHeaderValid(binding->struct_size, sizeof(*binding),
                binding->version) || binding->flags != 0u ||
            binding->reserved0 != 0u ||
            !agcReservedZero(binding->reserved, 4u)) {
            return AGC_ERROR_INVALID_ARGUMENT;
        }
        image = binding->image;
        if (!image || image->magic != AGC_MAGIC_IMAGE ||
            image->device != command_buffer->device || image->deferred ||
            (image->desc.usage & AGC_IMAGE_USAGE_COLOR_TARGET_BIT) == 0u ||
            image->desc.depth != 1u) {
            return AGC_ERROR_INVALID_ARGUMENT;
        }
        if (image->desc.format != command_buffer->graphics_pipeline->
                color_attachments[i].format ||
            image->desc.sample_count != command_buffer->graphics_pipeline->
                multisample.rasterization_samples ||
            !agcRuntimeColorTargetFormat(image->desc.format, &format)) {
            return AGC_ERROR_VALIDATION_FAILED;
        }
        for (j = 0u; j < i; ++j) {
            if (targets[j].image == image &&
                targets[j].mip_level == binding->mip_level &&
                targets[j].array_layer == binding->array_layer) {
                return AGC_ERROR_VALIDATION_FAILED;
            }
        }
        result = agcGetImageSubresourceLayout(command_buffer->device,
            &image->desc, binding->mip_level, binding->array_layer, 0u,
            &layout);
        if (result != AGC_OK)
            return result;
        address = agcImageGpuAddress(image);
        if (layout.depth != 1u || layout.offset > UINT64_MAX - address)
            return AGC_ERROR_RESOURCE_INVALID;
        result = agcGfx1013InitColorTarget(&states[i], address + layout.offset,
            layout.width, layout.height, format);
        if (result != AGC_OK)
            return result;
        if (image->desc.sample_count == 4u) {
            states[i].sample_count = 4u;
            states[i].fragment_count = 4u;
            states[i].swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_R_X;
        }
        if (i == 0u) {
            width = layout.width;
            height = layout.height;
        } else if (layout.width != width || layout.height != height) {
            return AGC_ERROR_VALIDATION_FAILED;
        }
    }
    for (i = 0u; i < target_count; ++i) {
        AgcImageSubresourceRange range = { AGC_IMAGE_ASPECT_COLOR_BIT,
            targets[i].mip_level, 1u, targets[i].array_layer, 1u, 0u };
        if (!agcCommandImageRangeState(command_buffer, targets[i].image,
                &range, &usage, &owner) ||
            usage != kAgcResourceUsageColorTarget ||
            owner != kAgcResourceOwnerGraphics)
            return AGC_ERROR_INVALID_STATE;
    }
    agcCbInit(&scratch, words, sizeof(words));
    for (i = 0u; i < target_count; ++i) {
        result = agcGfx1013SetColorTargetSlot(&scratch, i, &states[i]);
        if (result != AGC_OK)
            return result == AGC_ERROR_BUFFER_TOO_SMALL ?
                AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    }
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result != AGC_OK)
        return result;
    for (i = 0u; i < target_count; ++i) {
        AgcImage image = targets[i].image;
        command_buffer->recorded_images[
            command_buffer->recorded_image_count++] = image;
        image->recorded_refs++;
    }
    command_buffer->color_target_count = target_count;
    command_buffer->color_target_width = width;
    command_buffer->color_target_height = height;
    return AGC_OK;
}

static int agcPipelineStencilFaceWrites(const AgcStencilFaceState *face)
{
    return face->write_mask != 0u &&
        (face->fail_operation != AGC_STENCIL_OPERATION_KEEP ||
         face->depth_fail_operation != AGC_STENCIL_OPERATION_KEEP ||
         face->pass_operation != AGC_STENCIL_OPERATION_KEEP);
}

static int agcPipelineDepthStencilWrites(const AgcGraphicsPipeline pipeline)
{
    const AgcDepthStencilPipelineState *state = &pipeline->depth_stencil;

    if ((pipeline->pixel_shader && (pipeline->pixel_shader->reflection.flags &
            (AGC_SHADER_REFLECTION_WRITES_DEPTH_BIT |
             AGC_SHADER_REFLECTION_WRITES_STENCIL_BIT)) != 0u) ||
        state->depth_write_enable) {
        return 1;
    }
    return state->stencil_test_enable &&
        (agcPipelineStencilFaceWrites(&state->front) ||
         (state->back_face_enable &&
          agcPipelineStencilFaceWrites(&state->back)));
}

int32_t PS5_SYSV_ABI agcCmdBindDepthStencilTarget(
    AgcCommandBuffer command_buffer,
    const AgcDepthStencilTargetBinding *target)
{
    AgcGfx1013DepthSurfaceState state = {0};
    AgcImageSubresourceLayout layout = AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    AgcGfx1013DepthSurfaceFormat format;
    AgcImage image;
    uint32_t words[AGC_GFX1013_DEPTH_SURFACE_DWORDS];
    SceAgcCb scratch;
    uint64_t address;
    AgcResourceUsage usage;
    AgcResourceUsage required_usage;
    AgcResourceOwner owner;
    int has_depth;
    int has_stencil;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !target ||
        !agcHeaderValid(target->struct_size, sizeof(*target),
            target->version) || target->flags != 0u ||
        target->reserved0 != 0u || !agcReservedZero(target->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics ||
        !command_buffer->graphics_pipeline) {
        return AGC_ERROR_INVALID_STATE;
    }
    if (command_buffer->depth_stencil_target)
        return AGC_ERROR_NOT_SUPPORTED;
    if (command_buffer->recorded_image_count ==
        AGC_RUNTIME_MAX_RECORDED_RESOURCES) {
        return AGC_ERROR_OUT_OF_MEMORY;
    }
    if (command_buffer->graphics_pipeline->depth_stencil.format ==
        AGC_FORMAT_UNDEFINED) {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    image = target->image;
    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        image->device != command_buffer->device || image->deferred ||
        (image->desc.usage & AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) == 0u ||
        image->desc.depth != 1u || image->desc.mip_levels != 1u ||
        target->mip_level != 0u) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (image->desc.format != command_buffer->graphics_pipeline->
            depth_stencil.format ||
        image->desc.sample_count != command_buffer->graphics_pipeline->
            multisample.rasterization_samples ||
        !agcRuntimeDepthFormat(image->desc.format, &format)) {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    has_depth = image->desc.format != AGC_FORMAT_S8_UINT;
    has_stencil = image->desc.format == AGC_FORMAT_S8_UINT ||
        image->desc.format == AGC_FORMAT_D16_UNORM_S8_UINT ||
        image->desc.format == AGC_FORMAT_D32_FLOAT_S8_UINT;
    result = agcGetImageSubresourceLayout(command_buffer->device,
        &image->desc, 0u, target->array_layer, 0u, &layout);
    if (result != AGC_OK)
        return result;
    if (command_buffer->color_target_count != 0u &&
        (layout.width != command_buffer->color_target_width ||
         layout.height != command_buffer->color_target_height)) {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    required_usage = agcPipelineDepthStencilWrites(
        command_buffer->graphics_pipeline) ?
        kAgcResourceUsageDepthStencilWrite :
        kAgcResourceUsageDepthStencilRead;
    {
        AgcImageSubresourceRange range = {
            agcRuntimeImageAspectMask(image), target->mip_level, 1u,
            target->array_layer, 1u, 0u };
        if (!agcCommandImageRangeState(command_buffer, image, &range,
                &usage, &owner) || usage != required_usage ||
            owner != kAgcResourceOwnerGraphics)
            return AGC_ERROR_INVALID_STATE;
    }
    address = agcImageGpuAddress(image);
    state.width = layout.width;
    state.height = layout.height;
    state.format = format;
    state.mip_level_count = 1u;
    state.first_layer = target->array_layer;
    state.last_layer = target->array_layer;
    state.sample_count = image->desc.sample_count;
    if (has_depth) {
        state.depth_read_address = address;
        state.depth_write_address = address;
        state.depth_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X;
    }
    if (has_stencil) {
        AgcImageSubresourceLayout stencil =
            AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
        result = agcGetImageSubresourceLayout(command_buffer->device,
            &image->desc, 0u, target->array_layer,
            has_depth ? 1u : 0u, &stencil);
        if (result != AGC_OK || stencil.offset > UINT64_MAX - address)
            return result != AGC_OK ? result : AGC_ERROR_RESOURCE_INVALID;
        state.stencil_read_address = address + stencil.offset -
            stencil.slice_pitch * target->array_layer;
        state.stencil_write_address = state.stencil_read_address;
        state.stencil_swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_Z_X;
    }
    if ((image->desc.usage & AGC_IMAGE_USAGE_HTILE_BIT) != 0u) {
        if (image->layout.metadata_offset > UINT64_MAX - address)
            return AGC_ERROR_RESOURCE_INVALID;
        state.htile_address = address + image->layout.metadata_offset;
        state.htile_enable = 1u;
    }
    agcCbInit(&scratch, words, sizeof(words));
    result = agcGfx1013SetDepthSurface(&scratch, &state);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result != AGC_OK)
        return result;
    command_buffer->recorded_images[command_buffer->recorded_image_count++] =
        image;
    image->recorded_refs++;
    command_buffer->depth_stencil_target = image;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBindComputePipeline(
    AgcCommandBuffer command_buffer, AgcComputePipeline pipeline)
{
    uint32_t pipeline_index;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !pipeline || pipeline->magic != AGC_MAGIC_COMPUTE_PIPELINE ||
        !agcDeviceValid(command_buffer->device) ||
        pipeline->device != command_buffer->device) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        (command_buffer->queue_type != kAgcQueueGraphics &&
         command_buffer->queue_type != kAgcQueueCompute))
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->compute_pipeline == pipeline)
        return AGC_OK;
    for (pipeline_index = 0u;
         pipeline_index < command_buffer->recorded_compute_pipeline_count;
         ++pipeline_index) {
        if (command_buffer->recorded_compute_pipelines[pipeline_index] ==
            pipeline)
            break;
    }
    if (pipeline_index == command_buffer->recorded_compute_pipeline_count &&
        pipeline_index >= AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcPrepareCommandResources(
        command_buffer, &pipeline->resource_layout);
    if (result != AGC_OK)
        return result;
    command_buffer->compute_pipeline = pipeline;
    command_buffer->descriptors_bound = 0u;
    memset(command_buffer->push_constant_masks, 0,
        sizeof(command_buffer->push_constant_masks));
    if (pipeline_index == command_buffer->recorded_compute_pipeline_count) {
        command_buffer->recorded_compute_pipelines[pipeline_index] = pipeline;
        command_buffer->recorded_compute_pipeline_count++;
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

static int agcCommandCanRetainBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer buffer)
{
    uint32_t i;
    for (i = 0u; i < command_buffer->recorded_buffer_count; ++i) {
        if (command_buffer->recorded_buffers[i] == buffer)
            return 1;
    }
    return command_buffer->recorded_buffer_count <
        AGC_RUNTIME_MAX_RECORDED_RESOURCES;
}

static int agcCommandRetainImage(AgcCommandBuffer command_buffer,
    AgcImage image)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_image_count; ++i) {
        if (command_buffer->recorded_images[i] == image)
            return 1;
    }
    if (command_buffer->recorded_image_count >=
        AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return 0;
    command_buffer->recorded_images[
        command_buffer->recorded_image_count++] = image;
    image->recorded_refs++;
    return 1;
}

static int agcRuntimeUsageIsHost(AgcResourceUsage usage)
{
    return usage == kAgcResourceUsageHostRead ||
        usage == kAgcResourceUsageHostWrite;
}

static int agcRuntimeUsageIsGpu(AgcResourceUsage usage)
{
    return usage != kAgcResourceUsageUndefined &&
        !agcRuntimeUsageIsHost(usage);
}

static AgcResourceOwner agcRuntimeCommandOwner(
    const AgcCommandBuffer command_buffer)
{
    return command_buffer->queue_type == kAgcQueueGraphics ?
        kAgcResourceOwnerGraphics : kAgcResourceOwnerCompute;
}

static int agcRuntimeUsageSupportsQueue(AgcResourceUsage usage,
    AgcQueueType queue_type)
{
    /* Graphics DCBs may contain compute dispatch packets.  Shader-write
     * resources are therefore valid on either queue type; color/depth and
     * scanout states remain graphics-only. */
    if (usage == kAgcResourceUsageShaderWrite)
        return queue_type == kAgcQueueGraphics ||
            queue_type == kAgcQueueCompute;
    if (usage == kAgcResourceUsageColorTarget ||
        usage == kAgcResourceUsageDepthStencilRead ||
        usage == kAgcResourceUsageDepthStencilWrite ||
        usage == kAgcResourceUsageVideoOutScanout ||
        usage == kAgcResourceUsageQueryWrite)
        return queue_type == kAgcQueueGraphics;
    return 1;
}

static int agcRuntimeBufferUsageSupports(const AgcBuffer buffer,
    AgcResourceUsage usage)
{
    switch (usage) {
    case kAgcResourceUsageUndefined:
        return 1;
    case kAgcResourceUsageCopySource:
        return (buffer->usage & AGC_BUFFER_USAGE_TRANSFER_SRC_BIT) != 0u;
    case kAgcResourceUsageCopyDestination:
        return (buffer->usage & AGC_BUFFER_USAGE_TRANSFER_DST_BIT) != 0u;
    case kAgcResourceUsageShaderRead:
        return (buffer->usage & (AGC_BUFFER_USAGE_UNIFORM_BIT |
            AGC_BUFFER_USAGE_STORAGE_BIT | AGC_BUFFER_USAGE_VERTEX_BIT |
            AGC_BUFFER_USAGE_INDEX_BIT | AGC_BUFFER_USAGE_INDIRECT_BIT)) != 0u;
    case kAgcResourceUsageShaderWrite:
        return (buffer->usage & AGC_BUFFER_USAGE_STORAGE_BIT) != 0u;
    case kAgcResourceUsageQueryWrite:
        return (buffer->usage & AGC_BUFFER_USAGE_QUERY_BIT) != 0u;
    case kAgcResourceUsageHostRead:
        return (buffer->create_flags & AGC_BUFFER_CREATE_READBACK_BIT) != 0u;
    case kAgcResourceUsageHostWrite:
        return (buffer->create_flags & AGC_BUFFER_CREATE_UPLOAD_BIT) != 0u;
    default:
        return 0;
    }
}

static int agcRuntimeImageUsageSupports(const AgcImage image,
    AgcResourceUsage usage)
{
    switch (usage) {
    case kAgcResourceUsageUndefined:
        return 1;
    case kAgcResourceUsageCopySource:
        return (image->desc.usage & AGC_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    case kAgcResourceUsageCopyDestination:
        return (image->desc.usage & AGC_IMAGE_USAGE_TRANSFER_DST_BIT) != 0u;
    case kAgcResourceUsageShaderRead:
        return (image->desc.usage & (AGC_IMAGE_USAGE_SAMPLED_BIT |
            AGC_IMAGE_USAGE_STORAGE_BIT)) != 0u;
    case kAgcResourceUsageShaderWrite:
        return (image->desc.usage & AGC_IMAGE_USAGE_STORAGE_BIT) != 0u;
    case kAgcResourceUsageColorTarget:
        return (image->desc.usage & AGC_IMAGE_USAGE_COLOR_TARGET_BIT) != 0u;
    case kAgcResourceUsageDepthStencilRead:
    case kAgcResourceUsageDepthStencilWrite:
        return (image->desc.usage & AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT) != 0u;
    case kAgcResourceUsageVideoOutScanout:
        return (image->desc.usage & AGC_IMAGE_USAGE_SCANOUT_BIT) != 0u;
    case kAgcResourceUsageHostRead:
        return (image->desc.usage & AGC_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    case kAgcResourceUsageHostWrite:
        return (image->desc.usage & AGC_IMAGE_USAGE_TRANSFER_DST_BIT) != 0u;
    default:
        return 0;
    }
}

static int32_t agcRuntimeMapLowUsage(AgcResourceUsage usage,
    AgcGfx1013ResourceUsage *low_usage)
{
    switch (usage) {
    case kAgcResourceUsageUndefined:
    case kAgcResourceUsageHostWrite:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_UNDEFINED;
        return AGC_OK;
    case kAgcResourceUsageCopySource:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_COPY_SOURCE;
        return AGC_OK;
    case kAgcResourceUsageCopyDestination:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_COPY_DESTINATION;
        return AGC_OK;
    case kAgcResourceUsageShaderRead:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_SHADER_READ;
        return AGC_OK;
    case kAgcResourceUsageShaderWrite:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE;
        return AGC_OK;
    case kAgcResourceUsageQueryWrite:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE;
        return AGC_OK;
    case kAgcResourceUsageColorTarget:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
        return AGC_OK;
    case kAgcResourceUsageDepthStencilRead:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_READ;
        return AGC_OK;
    case kAgcResourceUsageDepthStencilWrite:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE;
        return AGC_OK;
    case kAgcResourceUsageVideoOutScanout:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_PRESENT;
        return AGC_OK;
    case kAgcResourceUsageHostRead:
        *low_usage = AGC_GFX1013_RESOURCE_USAGE_HOST_READ;
        return AGC_OK;
    default:
        return AGC_ERROR_NOT_SUPPORTED;
    }
}

static int agcCommandBufferRangeState(AgcCommandBuffer command_buffer,
    AgcBuffer buffer, uint64_t offset, uint64_t size,
    AgcResourceUsage *usage, AgcResourceOwner *owner)
{
    const uint64_t end = offset + size;
    uint64_t position = offset;
    int found = 0;

    while (position < end) {
        AgcResourceUsage segment_usage;
        AgcResourceOwner segment_owner;
        uint64_t next_boundary;
        uint32_t i;

        agcBufferCommittedStateAt(buffer, position, &segment_usage,
            &segment_owner, &next_boundary);
        if (next_boundary > end)
            next_boundary = end;
        for (i = 0u; i < command_buffer->recorded_transition_count; ++i) {
            const AgcRuntimeRecordedTransition *record =
                &command_buffer->recorded_transitions[i];
            uint64_t record_end;

            if (record->resource_type != kAgcResourceTypeBuffer ||
                record->resource != buffer)
                continue;
            record_end = record->buffer_offset + record->buffer_size;
            if (record->buffer_offset <= position && position < record_end &&
                record->flags != AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
                segment_usage = record->after;
                segment_owner = record->after_owner;
            }
            if (record->buffer_offset > position &&
                record->buffer_offset < next_boundary)
                next_boundary = record->buffer_offset;
            if (record_end > position && record_end < next_boundary)
                next_boundary = record_end;
        }
        if (!found) {
            *usage = segment_usage;
            *owner = segment_owner;
            found = 1;
        } else if (*usage != segment_usage || *owner != segment_owner) {
            return 0;
        }
        position = next_boundary;
    }
    return found;
}

int32_t PS5_SYSV_ABI agcGetCommandBufferRangeStateInfo(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset,
    uint64_t size, AgcResourceStateInfo *info)
{
    AgcResourceUsage usage;
    AgcResourceOwner owner;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !buffer || buffer->magic != AGC_MAGIC_BUFFER || !info ||
        !agcDeviceValid(command_buffer->device) ||
        buffer->device != command_buffer->device || buffer->deferred ||
        command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        info->reserved0 != 0u || !agcReservedZero(info->reserved, 4u) ||
        size == 0u || offset > buffer->size || size > buffer->size - offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!agcCommandBufferRangeState(command_buffer, buffer, offset, size,
            &usage, &owner))
        return AGC_ERROR_NOT_SUPPORTED;
    info->usage = usage;
    info->owner = owner;
    return AGC_OK;
}

static int agcCommandImageRangeState(AgcCommandBuffer command_buffer,
    AgcImage image, const AgcImageSubresourceRange *range,
    AgcResourceUsage *usage, AgcResourceOwner *owner)
{
    AgcImageAspectFlags aspect;
    AgcResourceUsage first_usage = kAgcResourceUsageCount;
    AgcResourceOwner first_owner = kAgcResourceOwnerCount;
    int found = 0;

    if (!agcImageRangeValid(image, range))
        return 0;
    for (aspect = 1u; aspect <= AGC_IMAGE_ASPECT_STENCIL_BIT;
         aspect <<= 1u) {
        uint32_t layer;
        if ((range->aspect_mask & aspect) == 0u)
            continue;
        for (layer = range->base_array_layer;
             layer < range->base_array_layer + range->array_layer_count;
             ++layer) {
            uint32_t mip;
            for (mip = range->base_mip_level;
                 mip < range->base_mip_level + range->mip_level_count;
                 ++mip) {
                AgcResourceUsage cell_usage;
                AgcResourceOwner cell_owner;
                uint32_t i;
                if (image->subresource_states) {
                    agcImageUnpackState(image->subresource_states[
                        agcImageSubresourceIndex(image, aspect, mip, layer)],
                        &cell_usage, &cell_owner);
                } else {
                    cell_usage = image->usage_state;
                    cell_owner = image->owner_state;
                }
                for (i = 0u;
                     i < command_buffer->recorded_transition_count; ++i) {
                    const AgcRuntimeRecordedTransition *record =
                        &command_buffer->recorded_transitions[i];
                    if (record->resource_type == kAgcResourceTypeImage &&
                        record->resource == image &&
                        record->flags != AGC_RESOURCE_TRANSITION_RELEASE_BIT &&
                        agcImageRangeContainsSubresource(&record->image_range,
                            aspect, mip, layer)) {
                        cell_usage = record->after;
                        cell_owner = record->after_owner;
                    }
                }
                if (!found) {
                    first_usage = cell_usage;
                    first_owner = cell_owner;
                    found = 1;
                } else if (cell_usage != first_usage ||
                    cell_owner != first_owner) {
                    return 0;
                }
            }
        }
    }
    if (!found)
        return 0;
    *usage = first_usage;
    *owner = first_owner;
    return 1;
}

static void agcCommandTransitionState(AgcCommandBuffer command_buffer,
    AgcResourceType resource_type, void *resource, AgcResourceUsage *usage,
    AgcResourceOwner *owner)
{
    if (resource_type == kAgcResourceTypeBuffer) {
        const AgcBuffer buffer = (const AgcBuffer)resource;
        if (!agcCommandBufferRangeState(command_buffer, (AgcBuffer)buffer,
                0u, buffer->size, usage, owner)) {
            *usage = kAgcResourceUsageCount;
            *owner = kAgcResourceOwnerCount;
        }
        return;
    }
    {
        const AgcImage image = (const AgcImage)resource;
        const AgcImageSubresourceRange range = {
            agcRuntimeImageAspectMask(image), 0u, image->desc.mip_levels,
            0u, image->desc.array_layers, 0u };
        if (!agcCommandImageRangeState(command_buffer, (AgcImage)image,
                &range, usage, owner)) {
            *usage = kAgcResourceUsageCount;
            *owner = kAgcResourceOwnerCount;
        }
    }
}

static int agcCommandRecordedReleaseOverlaps(
    const AgcCommandBuffer command_buffer, AgcResourceType resource_type,
    const void *resource, uint64_t buffer_offset, uint64_t buffer_size,
    const AgcImageSubresourceRange *image_range)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_transition_count; ++i) {
        const AgcRuntimeRecordedTransition *record =
            &command_buffer->recorded_transitions[i];
        if (record->resource_type != resource_type ||
            record->resource != resource ||
            record->flags != AGC_RESOURCE_TRANSITION_RELEASE_BIT)
            continue;
        if (resource_type == kAgcResourceTypeBuffer) {
            if (agcBufferRangesOverlap(buffer_offset, buffer_size,
                    record->buffer_offset, record->buffer_size))
                return 1;
        } else if (agcImageRangesOverlap(image_range, &record->image_range)) {
            return 1;
        }
    }
    return 0;
}

static int agcCommandRecordedAcquireOverlaps(
    const AgcCommandBuffer command_buffer, AgcResourceType resource_type,
    const void *resource, uint64_t buffer_offset, uint64_t buffer_size,
    const AgcImageSubresourceRange *image_range)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_transition_count; ++i) {
        const AgcRuntimeRecordedTransition *record =
            &command_buffer->recorded_transitions[i];
        if (record->resource_type != resource_type ||
            record->resource != resource || record->flags !=
                AGC_RESOURCE_TRANSITION_ACQUIRE_BIT)
            continue;
        if (resource_type == kAgcResourceTypeBuffer) {
            if (agcBufferRangesOverlap(buffer_offset, buffer_size,
                    record->buffer_offset, record->buffer_size))
                return 1;
        } else if (agcImageRangesOverlap(image_range, &record->image_range)) {
            return 1;
        }
    }
    return 0;
}

static int32_t agcRuntimeValidateTransition(
    AgcCommandBuffer command_buffer, const AgcResourceTransition *transition,
    void **resource_out, AgcGfx1013ResourceTransition *low_transition,
    uint32_t *emit_low_transition, uint32_t *transition_flags,
    AgcGpuLabel *dependency_label, uint32_t *dependency_value)
{
    AgcResourceUsage current_usage;
    AgcResourceOwner current_owner;
    AgcGfx1013ResourceUsage before;
    AgcGfx1013ResourceUsage after;
    void *resource;
    int32_t result;

    uint32_t flags;
    AgcGpuLabel label = NULL;
    uint32_t value = 0u;

    if (!transition || !resource_out || !low_transition ||
        !emit_low_transition || !transition_flags || !dependency_label ||
        !dependency_value || !agcReservedZero(transition->reserved, 5u) ||
        transition->resource_type > kAgcResourceTypeImage ||
        transition->before >= kAgcResourceUsageCount ||
        transition->after >= kAgcResourceUsageCount ||
        transition->before_owner >= kAgcResourceOwnerCount ||
        transition->after_owner >= kAgcResourceOwnerCount) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (transition->version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
        transition->struct_size == AGC_RESOURCE_TRANSITION_V1_SIZE) {
        if (transition->flags != 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        flags = 0u;
    } else if (transition->version == AGC_RUNTIME_STRUCTURE_VERSION_2 &&
        transition->struct_size == sizeof(*transition)) {
        if (transition->flags != AGC_RESOURCE_TRANSITION_RELEASE_BIT &&
            transition->flags != AGC_RESOURCE_TRANSITION_ACQUIRE_BIT &&
            transition->flags != AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT)
            return AGC_ERROR_INVALID_ARGUMENT;
        if (transition->reserved_v2 != 0u ||
            !agcReservedZero(transition->reserved2, 2u))
            return AGC_ERROR_INVALID_ARGUMENT;
        flags = transition->flags;
        label = transition->dependency_label;
        value = transition->dependency_value;
        if (flags == AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT) {
            if (label || value != 0u)
                return AGC_ERROR_INVALID_ARGUMENT;
        } else if (!label || label->magic != AGC_MAGIC_GPU_LABEL ||
            label->device != command_buffer->device || value == 0u) {
            return AGC_ERROR_INVALID_ARGUMENT;
        }
    } else {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (transition->resource_type == kAgcResourceTypeBuffer) {
        AgcBuffer buffer = transition->buffer;
        if (!buffer || transition->image ||
            buffer->magic != AGC_MAGIC_BUFFER ||
            buffer->device != command_buffer->device || buffer->deferred ||
            transition->buffer_size == 0u ||
            transition->buffer_offset > buffer->size ||
            transition->buffer_size >
                buffer->size - transition->buffer_offset ||
            !agcRuntimeBufferUsageSupports(buffer, transition->before) ||
            !agcRuntimeBufferUsageSupports(buffer, transition->after))
            return AGC_ERROR_INVALID_ARGUMENT;
        resource = buffer;
    } else {
        AgcImage image = transition->image;
        if (!image || transition->buffer || image->magic != AGC_MAGIC_IMAGE ||
            image->device != command_buffer->device || image->deferred ||
            image->desc.usage & AGC_IMAGE_USAGE_HTILE_BIT ||
            transition->buffer_offset != 0u || transition->buffer_size != 0u ||
            !agcRuntimeImageUsageSupports(image, transition->before) ||
            !agcRuntimeImageUsageSupports(image, transition->after) ||
            !agcImageRangeValid(image, &transition->image_range))
            return AGC_ERROR_INVALID_ARGUMENT;
        resource = image;
    }
    if (agcRuntimeUsageIsGpu(transition->after) &&
        transition->after_owner != agcRuntimeCommandOwner(command_buffer) &&
        flags != AGC_RESOURCE_TRANSITION_RELEASE_BIT)
        return AGC_ERROR_VALIDATION_FAILED;
    if (!agcRuntimeUsageIsGpu(transition->after) &&
        transition->after_owner != kAgcResourceOwnerHost)
        return AGC_ERROR_VALIDATION_FAILED;
    if (!agcRuntimeUsageSupportsQueue(transition->after,
            flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT ?
                (transition->after_owner == kAgcResourceOwnerGraphics ?
                    kAgcQueueGraphics : kAgcQueueCompute) :
                command_buffer->queue_type))
        return AGC_ERROR_NOT_SUPPORTED;
    if (transition->resource_type == kAgcResourceTypeBuffer) {
        if (!agcCommandBufferRangeState(command_buffer, (AgcBuffer)resource,
                transition->buffer_offset, transition->buffer_size,
                &current_usage, &current_owner))
            return AGC_ERROR_INVALID_STATE;
    } else {
        if (!agcCommandImageRangeState(command_buffer, (AgcImage)resource,
                &transition->image_range, &current_usage, &current_owner))
            return AGC_ERROR_INVALID_STATE;
    }
    if ((current_usage != transition->before ||
         current_owner != transition->before_owner) &&
        flags != AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT)
        return AGC_ERROR_INVALID_STATE;
    /* A batch dependency is resolved against earlier command buffers in the
     * ordered submission.  It must therefore be allowed to follow an acquire
     * recorded by one of those buffers; submission-time validation verifies
     * that complete sequence before any work is emitted. */
    if (flags != AGC_RESOURCE_TRANSITION_ACQUIRE_BIT &&
        flags != AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT &&
        !(flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT &&
          agcCommandRecordedAcquireOverlaps(command_buffer,
              transition->resource_type, resource,
              transition->buffer_offset, transition->buffer_size,
              &transition->image_range))) {
        int overlap;
        if (transition->resource_type == kAgcResourceTypeBuffer) {
            overlap = agcBufferTransferOverlaps((AgcBuffer)resource,
                transition->buffer_offset, transition->buffer_size);
        } else {
            overlap = agcImageTransferOverlaps((AgcImage)resource,
                &transition->image_range);
        }
        if (overlap || agcCommandRecordedReleaseOverlaps(command_buffer,
                transition->resource_type, resource,
                transition->buffer_offset, transition->buffer_size,
                &transition->image_range))
            return AGC_ERROR_INVALID_STATE;
    }
    if (agcRuntimeUsageIsGpu(transition->before) &&
        transition->before_owner != agcRuntimeCommandOwner(command_buffer))
        if (flags != AGC_RESOURCE_TRANSITION_ACQUIRE_BIT)
            return AGC_ERROR_NOT_SUPPORTED;
    if (agcRuntimeUsageIsGpu(transition->before) &&
        agcRuntimeUsageIsGpu(transition->after) &&
        transition->before_owner != transition->after_owner)
        if (flags == 0u)
            return AGC_ERROR_NOT_SUPPORTED;
    result = agcRuntimeMapLowUsage(transition->before, &before);
    if (result != AGC_OK)
        return result;
    result = agcRuntimeMapLowUsage(transition->after, &after);
    if (result != AGC_OK)
        return result;
    if (flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
        if (!agcRuntimeUsageIsGpu(transition->before) ||
            !agcRuntimeUsageIsGpu(transition->after) ||
            transition->before_owner == transition->after_owner ||
            transition->before_owner != agcRuntimeCommandOwner(command_buffer))
            return AGC_ERROR_NOT_SUPPORTED;
        if (value <=
                agcCommandLatestLabelSignalValue(command_buffer, label))
            return AGC_ERROR_INVALID_STATE;
        low_transition->before = before;
        low_transition->after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ;
        low_transition->completion_address = agcAllocationGpuAddress(
            label->allocation);
        low_transition->completion_value = value;
        *emit_low_transition = 1u;
    } else if (flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) {
        AgcRuntimePendingTransfer *pending_transfer;

        if (!agcRuntimeUsageIsGpu(transition->before) ||
            !agcRuntimeUsageIsGpu(transition->after) ||
            transition->before_owner == transition->after_owner ||
            transition->after_owner != agcRuntimeCommandOwner(command_buffer))
            return AGC_ERROR_NOT_SUPPORTED;
        if (transition->resource_type == kAgcResourceTypeBuffer) {
            pending_transfer = agcBufferFindTransfer((AgcBuffer)resource,
                transition->buffer_offset, transition->buffer_size);
        } else {
            pending_transfer = agcImageFindTransfer((AgcImage)resource,
                &transition->image_range);
        }
        if (!pending_transfer || pending_transfer->usage != transition->after ||
            pending_transfer->owner != transition->after_owner ||
            pending_transfer->label != label ||
            pending_transfer->value != value ||
            pending_transfer->acquire_command ||
            label->last_signal_submission_id == 0u ||
            label->last_signal_value < value)
            return AGC_ERROR_INVALID_STATE;
        /* PRESENT is a low-level acquire-only carrier: it emits no release,
         * while preserving the proven all-cache invalidate sequence. */
        low_transition->before = AGC_GFX1013_RESOURCE_USAGE_PRESENT;
        low_transition->after = after;
        low_transition->completion_address = 0u;
        low_transition->completion_value = 0u;
        *emit_low_transition = 1u;
    } else {
        *emit_low_transition = transition->after != kAgcResourceUsageUndefined &&
            !(transition->before == kAgcResourceUsageHostRead &&
              transition->after == kAgcResourceUsageHostWrite);
        low_transition->before = before;
        low_transition->after = transition->after == kAgcResourceUsageHostWrite ?
            AGC_GFX1013_RESOURCE_USAGE_HOST_READ : after;
        low_transition->completion_address = 0u;
        low_transition->completion_value = 0u;
    }
    *resource_out = resource;
    *transition_flags = flags;
    *dependency_label = label;
    *dependency_value = value;
    return AGC_OK;
}

static int agcBatchBufferRangeState(AgcBuffer buffer,
    const AgcRuntimeBatchTransitionState *states, uint32_t state_count,
    uint64_t offset, uint64_t size, uint32_t command_index,
    int require_prior_command, AgcResourceUsage *usage,
    AgcResourceOwner *owner)
{
    const uint64_t end = offset + size;
    uint64_t position = offset;
    int found = 0;

    while (position < end) {
        AgcResourceUsage segment_usage;
        AgcResourceOwner segment_owner;
        uint64_t next_boundary;
        uint32_t provenance = UINT32_MAX;
        uint32_t i;

        agcBufferCommittedStateAt(buffer, position, &segment_usage,
            &segment_owner, &next_boundary);
        if (next_boundary > end)
            next_boundary = end;
        for (i = 0u; i < state_count; ++i) {
            uint64_t state_end;
            if (states[i].resource_type != kAgcResourceTypeBuffer ||
                states[i].resource != buffer)
                continue;
            state_end = states[i].buffer_offset + states[i].buffer_size;
            if (states[i].buffer_offset <= position && position < state_end) {
                segment_usage = states[i].usage;
                segment_owner = states[i].owner;
                provenance = states[i].command_index;
            }
            if (states[i].buffer_offset > position &&
                states[i].buffer_offset < next_boundary)
                next_boundary = states[i].buffer_offset;
            if (state_end > position && state_end < next_boundary)
                next_boundary = state_end;
        }
        if (!found) {
            *usage = segment_usage;
            *owner = segment_owner;
            found = 1;
        } else if (*usage != segment_usage || *owner != segment_owner) {
            return 0;
        }
        if (require_prior_command &&
            (provenance == UINT32_MAX || provenance >= command_index))
            return 0;
        position = next_boundary;
    }
    return found;
}

static int agcBatchImageRangeState(AgcImage image,
    const AgcRuntimeBatchTransitionState *states, uint32_t state_count,
    const AgcImageSubresourceRange *range, uint32_t command_index,
    int require_prior_command, AgcResourceUsage *usage,
    AgcResourceOwner *owner)
{
    AgcImageAspectFlags aspect;
    AgcResourceUsage first_usage = kAgcResourceUsageCount;
    AgcResourceOwner first_owner = kAgcResourceOwnerCount;
    int found = 0;

    for (aspect = 1u; aspect <= AGC_IMAGE_ASPECT_STENCIL_BIT;
         aspect <<= 1u) {
        uint32_t layer;
        if ((range->aspect_mask & aspect) == 0u)
            continue;
        for (layer = range->base_array_layer;
             layer < range->base_array_layer + range->array_layer_count;
             ++layer) {
            uint32_t mip;
            for (mip = range->base_mip_level;
                 mip < range->base_mip_level + range->mip_level_count;
                 ++mip) {
                AgcResourceUsage cell_usage;
                AgcResourceOwner cell_owner;
                uint32_t provenance = UINT32_MAX;
                uint32_t i;
                if (image->subresource_states) {
                    agcImageUnpackState(image->subresource_states[
                        agcImageSubresourceIndex(image, aspect, mip, layer)],
                        &cell_usage, &cell_owner);
                } else {
                    cell_usage = image->usage_state;
                    cell_owner = image->owner_state;
                }
                for (i = 0u; i < state_count; ++i) {
                    if (states[i].resource_type == kAgcResourceTypeImage &&
                        states[i].resource == image &&
                        agcImageRangeContainsSubresource(
                            &states[i].image_range, aspect, mip, layer)) {
                        cell_usage = states[i].usage;
                        cell_owner = states[i].owner;
                        provenance = states[i].command_index;
                    }
                }
                if (require_prior_command &&
                    (provenance == UINT32_MAX ||
                     provenance >= command_index))
                    return 0;
                if (!found) {
                    first_usage = cell_usage;
                    first_owner = cell_owner;
                    found = 1;
                } else if (cell_usage != first_usage ||
                    cell_owner != first_owner) {
                    return 0;
                }
            }
        }
    }
    if (!found)
        return 0;
    *usage = first_usage;
    *owner = first_owner;
    return 1;
}

static int32_t agcReserveSubmissionBufferStates(
    AgcCommandBuffer const *command_buffers, uint32_t command_count)
{
    uint32_t command_index;

    for (command_index = 0u; command_index < command_count; ++command_index) {
        uint32_t transition_index;

        for (transition_index = 0u; transition_index <
                command_buffers[command_index]->recorded_transition_count;
             ++transition_index) {
            const AgcRuntimeRecordedTransition *record =
                &command_buffers[command_index]
                    ->recorded_transitions[transition_index];
            AgcBuffer buffer;
            uint32_t count = 0u;
            uint32_t release_count = 0u;
            uint32_t i;

            if (record->resource_type == kAgcResourceTypeImage) {
                AgcImage image = (AgcImage)record->resource;
                for (i = 0u; i < command_count; ++i) {
                    uint32_t j;
                    for (j = 0u; j <
                            command_buffers[i]->recorded_transition_count;
                         ++j) {
                        const AgcRuntimeRecordedTransition *candidate =
                            &command_buffers[i]->recorded_transitions[j];
                        if (candidate->resource_type ==
                                kAgcResourceTypeImage &&
                            candidate->resource == image &&
                            candidate->flags ==
                                AGC_RESOURCE_TRANSITION_RELEASE_BIT)
                            release_count++;
                    }
                }
                if (release_count > AGC_RUNTIME_MAX_RECORDED_TRANSITIONS -
                        image->transfer_count)
                    return AGC_ERROR_OUT_OF_MEMORY;
                {
                    int32_t transfer_result = agcImageEnsureTransferCapacity(
                        image, image->transfer_count + release_count);
                    if (transfer_result != AGC_OK)
                        return transfer_result;
                }
                if (!agcImageRangeIsWhole(image, &record->image_range)) {
                    int32_t image_result =
                        agcImageEnsureSubresourceStates(image);
                    if (image_result != AGC_OK)
                        return image_result;
                }
                continue;
            }
            if (record->resource_type != kAgcResourceTypeBuffer)
                continue;
            buffer = (AgcBuffer)record->resource;
            for (i = 0u; i < command_count; ++i) {
                uint32_t j;
                for (j = 0u; j <
                        command_buffers[i]->recorded_transition_count; ++j) {
                    const AgcRuntimeRecordedTransition *candidate =
                        &command_buffers[i]->recorded_transitions[j];
                    if (candidate->resource_type == kAgcResourceTypeBuffer &&
                        candidate->resource == buffer) {
                        count++;
                        if (candidate->flags ==
                                AGC_RESOURCE_TRANSITION_RELEASE_BIT)
                            release_count++;
                    }
                }
            }
            if (release_count > AGC_RUNTIME_MAX_RECORDED_TRANSITIONS -
                    buffer->transfer_count)
                return AGC_ERROR_OUT_OF_MEMORY;
            {
                int32_t transfer_result = agcBufferEnsureTransferCapacity(
                    buffer, buffer->transfer_count + release_count);
                if (transfer_result != AGC_OK)
                    return transfer_result;
            }
            if (count > (AGC_RUNTIME_MAX_BUFFER_STATE_RANGES -
                    buffer->state_range_count) / 2u)
                return AGC_ERROR_OUT_OF_MEMORY;
            {
                int32_t result = agcBufferEnsureStateCapacity(buffer,
                    buffer->state_range_count + 2u * count);
                if (result != AGC_OK)
                    return result;
            }
        }
    }
    return AGC_OK;
}

static int32_t agcValidateSubmissionTransitions(
    AgcCommandBuffer const *command_buffers, uint32_t command_count)
{
    AgcRuntimeBatchTransitionState *states = NULL;
    size_t state_capacity = 0u;
    uint32_t state_count = 0u;
    uint32_t i;
    int32_t result = AGC_OK;

    for (i = 0u; i < command_count; ++i) {
        if (command_buffers[i]->recorded_transition_count >
            SIZE_MAX - state_capacity) {
            return AGC_ERROR_OUT_OF_MEMORY;
        }
        state_capacity += command_buffers[i]->recorded_transition_count;
    }
    if (state_capacity == 0u)
        return AGC_OK;
    if (state_capacity > SIZE_MAX / sizeof(*states))
        return AGC_ERROR_OUT_OF_MEMORY;
    states = agcAlloc(command_buffers[0]->device,
        state_capacity * sizeof(*states), _Alignof(AgcRuntimeBatchTransitionState));
    if (!states)
        return AGC_ERROR_OUT_OF_MEMORY;
    for (i = 0u; i < command_count && result == AGC_OK; ++i) {
        AgcCommandBuffer command_buffer = command_buffers[i];
        uint32_t transition_index;

        for (transition_index = 0u;
             transition_index < command_buffer->recorded_transition_count;
             ++transition_index) {
            const AgcRuntimeRecordedTransition *record =
                &command_buffer->recorded_transitions[transition_index];
            AgcResourceUsage usage;
            AgcResourceOwner owner;
            uint32_t prior_index;

            if (record->flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) {
                AgcRuntimePendingTransfer *transfer;
                if (record->resource_type == kAgcResourceTypeBuffer) {
                    transfer = agcBufferFindTransfer(
                        (AgcBuffer)record->resource, record->buffer_offset,
                        record->buffer_size);
                } else {
                    transfer = agcImageFindTransfer(
                        (AgcImage)record->resource, &record->image_range);
                }
                if (!transfer || transfer->usage != record->after ||
                    transfer->owner != record->after_owner ||
                    transfer->label != record->dependency_label ||
                    transfer->value != record->dependency_value ||
                    transfer->acquire_command != command_buffer ||
                    transfer->label->last_signal_submission_id == 0u ||
                    transfer->label->last_signal_value < transfer->value) {
                    result = AGC_ERROR_INVALID_STATE;
                    break;
                }
            } else {
                int overlaps;
                int acquired_in_batch = 0;
                uint32_t state_index;

                if (record->flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
                    for (state_index = 0u; state_index < state_count;
                         ++state_index) {
                        const AgcRuntimeBatchTransitionState *prior =
                            &states[state_index];
                        if (prior->flags !=
                                AGC_RESOURCE_TRANSITION_ACQUIRE_BIT ||
                            prior->resource_type != record->resource_type ||
                            prior->resource != record->resource)
                            continue;
                        if (record->resource_type == kAgcResourceTypeBuffer) {
                            acquired_in_batch = agcBufferRangesOverlap(
                                prior->buffer_offset, prior->buffer_size,
                                record->buffer_offset, record->buffer_size);
                        } else {
                            acquired_in_batch = agcImageRangesOverlap(
                                &prior->image_range, &record->image_range);
                        }
                        if (acquired_in_batch)
                            break;
                    }
                }
                if (record->resource_type == kAgcResourceTypeBuffer) {
                    overlaps = agcBufferTransferOverlaps(
                        (AgcBuffer)record->resource, record->buffer_offset,
                        record->buffer_size);
                } else {
                    overlaps = agcImageTransferOverlaps(
                        (AgcImage)record->resource, &record->image_range);
                }
                if (overlaps && !acquired_in_batch) {
                    result = AGC_ERROR_INVALID_STATE;
                    break;
                }
            }
            if (record->flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
                for (prior_index = 0u; prior_index < state_count;
                     ++prior_index) {
                    const AgcRuntimeBatchTransitionState *prior =
                        &states[prior_index];
                    int overlaps;
                    if (prior->flags !=
                            AGC_RESOURCE_TRANSITION_RELEASE_BIT ||
                        prior->resource_type != record->resource_type ||
                        prior->resource != record->resource)
                        continue;
                    if (record->resource_type == kAgcResourceTypeBuffer) {
                        overlaps = agcBufferRangesOverlap(
                            prior->buffer_offset, prior->buffer_size,
                            record->buffer_offset, record->buffer_size);
                    } else {
                        overlaps = agcImageRangesOverlap(&prior->image_range,
                            &record->image_range);
                    }
                    if (overlaps) {
                        result = AGC_ERROR_INVALID_STATE;
                        break;
                    }
                }
                if (result != AGC_OK)
                    break;
            }

            if (record->resource_type == kAgcResourceTypeBuffer) {
                if (!agcBatchBufferRangeState((AgcBuffer)record->resource,
                        states, state_count, record->buffer_offset,
                        record->buffer_size, i,
                        record->flags ==
                            AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT,
                        &usage, &owner))
                    result = AGC_ERROR_INVALID_STATE;
            } else {
                const AgcImage image = (const AgcImage)record->resource;
                if (!agcBatchImageRangeState((AgcImage)image, states,
                        state_count, &record->image_range, i,
                        record->flags ==
                            AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT,
                        &usage, &owner))
                    result = AGC_ERROR_INVALID_STATE;
            }
            if (result != AGC_OK)
                break;
            if (usage != record->before || owner != record->before_owner) {
                result = AGC_ERROR_INVALID_STATE;
                break;
            }
            states[state_count].resource_type = record->resource_type;
            states[state_count].resource = record->resource;
            states[state_count].usage =
                record->flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT ?
                    record->before : record->after;
            states[state_count].owner =
                record->flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT ?
                    record->before_owner : record->after_owner;
            states[state_count].flags = record->flags;
            states[state_count].command_index = i;
            states[state_count].buffer_offset = record->buffer_offset;
            states[state_count].buffer_size = record->buffer_size;
            states[state_count].image_range = record->image_range;
            state_count++;
        }
    }
    if (result == AGC_OK)
        result = agcReserveSubmissionBufferStates(
            command_buffers, command_count);
    agcFree(command_buffers[0]->device, states);
    return result;
}

static void agcCommitCommandTransitions(AgcCommandBuffer command_buffer)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_transition_count; ++i) {
        const AgcRuntimeRecordedTransition *record =
            &command_buffer->recorded_transitions[i];
        if (record->resource_type == kAgcResourceTypeBuffer) {
            AgcBuffer buffer = (AgcBuffer)record->resource;
            if (record->flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
                AgcRuntimePendingTransfer *transfer =
                    &buffer->transfers[buffer->transfer_count++];
                record->dependency_label->recorded_refs++;
                *transfer = (AgcRuntimePendingTransfer){
                    .usage = record->after,
                    .owner = record->after_owner,
                    .label = record->dependency_label,
                    .value = record->dependency_value,
                    .buffer_offset = record->buffer_offset,
                    .buffer_size = record->buffer_size };
            } else {
                agcBufferCommitRangeState(buffer, record->buffer_offset,
                    record->buffer_size, record->after,
                    record->after_owner);
                if (record->flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) {
                    AgcRuntimePendingTransfer *transfer =
                        agcBufferFindTransfer(buffer, record->buffer_offset,
                            record->buffer_size);
                    if (transfer) {
                        transfer->label->recorded_refs--;
                        agcRemoveTransfer(buffer->transfers,
                            &buffer->transfer_count, transfer);
                    }
                }
            }
        } else {
            AgcImage image = (AgcImage)record->resource;
            if (record->flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
                AgcRuntimePendingTransfer *transfer =
                    &image->transfers[image->transfer_count++];
                record->dependency_label->recorded_refs++;
                *transfer = (AgcRuntimePendingTransfer){
                    .usage = record->after,
                    .owner = record->after_owner,
                    .label = record->dependency_label,
                    .value = record->dependency_value,
                    .image_range = record->image_range };
            } else {
                agcImageCommitRangeState(image, &record->image_range,
                    record->after, record->after_owner);
                if (record->flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) {
                    AgcRuntimePendingTransfer *transfer =
                        agcImageFindTransfer(image, &record->image_range);
                    if (transfer) {
                        transfer->label->recorded_refs--;
                        agcRemoveTransfer(image->transfers,
                            &image->transfer_count, transfer);
                    }
                }
            }
        }
    }
}

static void agcCommitCommandLabelSignals(AgcQueue queue,
    AgcCommandBuffer command_buffer, uint64_t submission_id)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_label_signal_count; ++i) {
        const AgcRuntimeRecordedLabelSignal *signal =
            &command_buffer->recorded_label_signals[i];
        AgcGpuLabel label = signal->label;

        if (label->last_signal_queue != queue) {
            if (label->last_signal_queue)
                label->last_signal_queue->label_refs--;
            label->last_signal_queue = queue;
            queue->label_refs++;
        }
        label->last_signal_value = signal->value;
        label->last_signal_queue_type = (uint32_t)queue->type;
        label->last_signal_submission_id = submission_id;
#ifndef OPENAGC_PROSPERO
        *(uint32_t *)agcAllocationCpuAddress(label->allocation) = signal->value;
#endif
    }
}

static int32_t agcValidateCommandLabelWaits(const AgcQueue queue,
    const AgcCommandBuffer command_buffer)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_label_wait_count; ++i) {
        const AgcRuntimeRecordedLabelWait *wait =
            &command_buffer->recorded_label_waits[i];
        const AgcGpuLabel label = wait->label;

        if (!label || label->magic != AGC_MAGIC_GPU_LABEL ||
            label->device != queue->device)
            return AGC_ERROR_INVALID_ARGUMENT;
        if (label->last_signal_submission_id == 0u)
            return AGC_ERROR_INVALID_STATE;
        if (label->last_signal_value < wait->value)
            return AGC_ERROR_INVALID_STATE;
    }
    return AGC_OK;
}

static uint32_t agcCommandLatestLabelSignalValue(
    const AgcCommandBuffer command_buffer, const AgcGpuLabel label)
{
    uint32_t value = label->last_signal_value;
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_label_signal_count; ++i)
        if (command_buffer->recorded_label_signals[i].label == label)
            value = command_buffer->recorded_label_signals[i].value;
    return value;
}

static int32_t agcValidateBatchLabelSignalOrder(
    AgcCommandBuffer const *command_buffers, uint32_t command_count)
{
    uint32_t command_index;

    for (command_index = 0u; command_index < command_count; ++command_index) {
        AgcCommandBuffer command_buffer = command_buffers[command_index];
        uint32_t signal_index;

        for (signal_index = 0u;
             signal_index < command_buffer->recorded_label_signal_count;
             ++signal_index) {
            const AgcRuntimeRecordedLabelSignal *signal =
                &command_buffer->recorded_label_signals[signal_index];
            uint32_t previous = signal->label->last_signal_value;
            uint32_t i;

            for (i = 0u; i <= command_index; ++i) {
                AgcCommandBuffer prior_command = command_buffers[i];
                uint32_t limit = i == command_index ? signal_index :
                    prior_command->recorded_label_signal_count;
                uint32_t j;

                for (j = 0u; j < limit; ++j)
                    if (prior_command->recorded_label_signals[j].label ==
                        signal->label)
                        previous = prior_command->recorded_label_signals[j].value;
            }
            if (signal->value <= previous)
                return AGC_ERROR_INVALID_STATE;
        }
    }
    return AGC_OK;
}

static int agcSubmitInfoValid(const AgcSubmitInfo *submit_info)
{
    if (!submit_info || submit_info->flags != 0u ||
        !agcReservedZero(submit_info->reserved, 4u))
        return 0;
    if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
        submit_info->struct_size == AGC_SUBMIT_INFO_V1_SIZE)
        return 1;
    return submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_2 &&
        submit_info->struct_size == sizeof(*submit_info) &&
        agcReservedZero(submit_info->reserved_v2, 2u) &&
        ((submit_info->wait_count == 0u && !submit_info->waits) ||
         (submit_info->wait_count != 0u && submit_info->waits)) &&
        ((submit_info->signal_count == 0u && !submit_info->signals) ||
         (submit_info->signal_count != 0u && submit_info->signals)) &&
        submit_info->wait_count <= AGC_RUNTIME_MAX_RECORDED_TRANSITIONS &&
        submit_info->signal_count <= AGC_RUNTIME_MAX_RECORDED_TRANSITIONS;
}

static int32_t agcValidateSubmitLabelLists(const AgcQueue queue,
    const AgcCommandBuffer command_buffer, const AgcSubmitInfo *submit_info)
{
    uint32_t i;

    if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_1)
        return AGC_OK;
    for (i = 0u; i < submit_info->wait_count; ++i) {
        const AgcGpuLabelPoint *point = &submit_info->waits[i];
        const AgcGpuLabel label = point->label;

        if (!label || point->reserved != 0u ||
            label->magic != AGC_MAGIC_GPU_LABEL ||
            label->device != queue->device)
            return AGC_ERROR_INVALID_ARGUMENT;
        if (label->last_signal_submission_id == 0u ||
            label->last_signal_value < point->value)
            return AGC_ERROR_INVALID_STATE;
    }
    for (i = 0u; i < submit_info->signal_count; ++i) {
        const AgcGpuLabelPoint *point = &submit_info->signals[i];
        const AgcGpuLabel label = point->label;
        uint32_t j;

        if (!label || point->reserved != 0u ||
            label->magic != AGC_MAGIC_GPU_LABEL ||
            label->device != queue->device || point->value == 0u ||
            point->value <= label->last_signal_value)
            return AGC_ERROR_INVALID_ARGUMENT;
        for (j = 0u; j < i; ++j) {
            if (submit_info->signals[j].label == label &&
                point->value <= submit_info->signals[j].value)
                return AGC_ERROR_INVALID_ARGUMENT;
        }
        for (j = 0u; j < command_buffer->recorded_label_signal_count; ++j) {
            const AgcRuntimeRecordedLabelSignal *signal =
                &command_buffer->recorded_label_signals[j];
            if (signal->label == label)
                return AGC_ERROR_INVALID_STATE;
        }
    }
    return AGC_OK;
}

static void agcCommitSubmitLabelSignals(AgcQueue queue,
    const AgcGpuLabelPoint *signals, uint32_t signal_count,
    uint64_t submission_id)
{
    uint32_t i;

    for (i = 0u; i < signal_count; ++i) {
        AgcGpuLabel label = signals[i].label;

        if (label->last_signal_queue != queue) {
            if (label->last_signal_queue)
                label->last_signal_queue->label_refs--;
            label->last_signal_queue = queue;
            queue->label_refs++;
        }
        label->last_signal_value = signals[i].value;
        label->last_signal_queue_type = (uint32_t)queue->type;
        label->last_signal_submission_id = submission_id;
#ifndef OPENAGC_PROSPERO
        *(uint32_t *)agcAllocationCpuAddress(label->allocation) =
            signals[i].value;
#endif
    }
}

static int32_t agcRetainSubmitLabels(AgcCommandBuffer command_buffer,
    const AgcSubmitInfo *submit_info)
{
    uint32_t i;

    if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_1)
        return AGC_OK;
    for (i = 0u; i < submit_info->wait_count; ++i) {
        if (!agcCommandRetainGpuLabel(command_buffer,
                submit_info->waits[i].label))
            return AGC_ERROR_OUT_OF_MEMORY;
    }
    for (i = 0u; i < submit_info->signal_count; ++i) {
        if (!agcCommandRetainGpuLabel(command_buffer,
                submit_info->signals[i].label))
            return AGC_ERROR_OUT_OF_MEMORY;
    }
    return AGC_OK;
}

static void agcReleaseSubmitLabels(AgcCommandBuffer command_buffer,
    uint32_t original_label_count)
{
    while (command_buffer->recorded_label_count > original_label_count) {
        command_buffer->recorded_label_count--;
        command_buffer->recorded_labels[command_buffer->recorded_label_count]
            ->recorded_refs--;
    }
}

static int32_t agcInjectSubmitLabelWaits(AgcCommandBuffer command_buffer,
    const AgcSubmitInfo *submit_info)
{
    uint32_t original_dwords = agcCbUsedDwords(&command_buffer->cursor);
    uint32_t wait_dwords = submit_info->wait_count * 7u;
    uint32_t i;

    if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_1 ||
        submit_info->wait_count == 0u)
        return AGC_OK;
    memmove(command_buffer->storage + wait_dwords, command_buffer->storage,
        (size_t)original_dwords * sizeof(uint32_t));
    command_buffer->cursor.cursor_up = (uintptr_t)command_buffer->storage;
    for (i = 0u; i < submit_info->wait_count; ++i) {
        const AgcGpuLabelPoint *point = &submit_info->waits[i];
        if (!sceAgcDcbWaitRegMem(&command_buffer->cursor, 0u,
                AGC_RUNTIME_WAIT_COMPARE_GREATER_EQUAL, 0u, 0u,
                agcAllocationGpuAddress(point->label->allocation), point->value,
                UINT32_MAX, UINT32_MAX))
            return AGC_ERROR_INTERNAL;
    }
    command_buffer->cursor.cursor_up = (uintptr_t)(command_buffer->storage +
        wait_dwords + original_dwords);
    return AGC_OK;
}

static int32_t agcInjectSubmitLabelSignals(AgcCommandBuffer command_buffer,
    const AgcSubmitInfo *submit_info)
{
    uint32_t i;

    if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_1 ||
        submit_info->signal_count == 0u)
        return AGC_OK;
    for (i = 0u; i < submit_info->signal_count; ++i) {
        AgcGfx1013EopFenceState state;
        const AgcGpuLabelPoint *point = &submit_info->signals[i];
        int32_t result;

        state.address = agcAllocationGpuAddress(point->label->allocation);
        state.value = point->value;
        result = agcGfx1013SignalEopFence(&command_buffer->cursor, &state);
        if (result != AGC_OK)
            return result == AGC_ERROR_BUFFER_TOO_SMALL ?
                AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    }
    return AGC_OK;
}

static int32_t agcInjectSubmitLabelLists(AgcCommandBuffer command_buffer,
    const AgcSubmitInfo *submit_info)
{
    int32_t result = agcInjectSubmitLabelWaits(command_buffer, submit_info);

    if (result != AGC_OK)
        return result;
    return agcInjectSubmitLabelSignals(command_buffer, submit_info);
}

int32_t PS5_SYSV_ABI agcCmdTransitionResources(
    AgcCommandBuffer command_buffer, uint32_t transition_count,
    const AgcResourceTransition *transitions)
{
    uint32_t dword_count = 0u;
    uint32_t release_count = 0u;
    uint32_t acquire_count = 0u;
    uint32_t i;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!transitions || transition_count == 0u || transition_count >
            AGC_RUNTIME_MAX_RECORDED_TRANSITIONS)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCmdTransitionResources",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "transition list must be nonempty and within the runtime transition limit");
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCmdTransitionResources",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "resource transitions require a Recording command buffer");
    if (command_buffer->recorded_transition_count >
            AGC_RUNTIME_MAX_RECORDED_TRANSITIONS - transition_count)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCmdTransitionResources",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "transition journal capacity is exhausted");
    for (i = 0u; i < transition_count; ++i) {
        AgcGfx1013ResourceTransition low_transition;
        uint32_t transition_dwords = 0u;
        uint32_t emit_low_transition;
        uint32_t flags;
        AgcGpuLabel label;
        uint32_t value;
        void *resource;
        uint32_t j;
        int32_t result = agcRuntimeValidateTransition(command_buffer,
            &transitions[i], &resource, &low_transition, &emit_low_transition,
            &flags, &label, &value);
        if (result != AGC_OK)
            return agcDebugReport(command_buffer->device,
                AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                result == AGC_ERROR_NOT_SUPPORTED ?
                    AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT :
                    AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
                result, "agcCmdTransitionResources",
                AGC_OBJECT_TYPE_COMMAND_BUFFER,
                command_buffer->allocation->debug_name,
                "transition resource, range, usage, owner, dependency, or declared prior state is invalid");
        /* Preflight does not mutate the command's tentative state. Reject a
         * duplicate resource in one call; callers can issue ordered calls for
         * disjoint or chained ranges while retaining per-call atomicity. */
        for (j = 0u; j < i; ++j) {
            if (transitions[j].resource_type == transitions[i].resource_type &&
                ((transitions[i].resource_type == kAgcResourceTypeBuffer &&
                  transitions[j].buffer == transitions[i].buffer) ||
                 (transitions[i].resource_type == kAgcResourceTypeImage &&
                  transitions[j].image == transitions[i].image)))
                return agcDebugReport(command_buffer->device,
                    AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                    AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
                    AGC_ERROR_NOT_SUPPORTED, "agcCmdTransitionResources",
                    AGC_OBJECT_TYPE_COMMAND_BUFFER,
                    command_buffer->allocation->debug_name,
                    "one transition call cannot contain duplicate resources; issue ordered calls for disjoint or chained ranges");
        }
        if (emit_low_transition) {
            result = agcGfx1013GetResourceTransitionDwords(&low_transition,
                &transition_dwords);
            if (result != AGC_OK || transition_dwords > UINT32_MAX - dword_count)
                return agcDebugReport(command_buffer->device,
                    AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                    result == AGC_OK ?
                        AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT :
                        AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
                    result != AGC_OK ? result :
                        AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
                    "agcCmdTransitionResources",
                    AGC_OBJECT_TYPE_COMMAND_BUFFER,
                    command_buffer->allocation->debug_name,
                    "transition packet requirements exceed the supported command capacity");
            dword_count += transition_dwords;
        }
        if (flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) {
            if (dword_count > UINT32_MAX - 7u)
                return agcDebugReport(command_buffer->device,
                    AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                    AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
                    AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
                    "agcCmdTransitionResources",
                    AGC_OBJECT_TYPE_COMMAND_BUFFER,
                    command_buffer->allocation->debug_name,
                    "ownership acquire packet requirements overflow command capacity");
            dword_count += 7u;
            acquire_count++;
        } else if (flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
            release_count++;
        }
        (void)label;
        (void)value;
        (void)resource;
    }
    if (agcCbRemainingDwords(&command_buffer->cursor) < dword_count)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
            "agcCmdTransitionResources", AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has insufficient dwords for the transition packets");
    if (command_buffer->recorded_buffer_count + transition_count >
            AGC_RUNTIME_MAX_RECORDED_RESOURCES ||
        command_buffer->recorded_image_count + transition_count >
            AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return AGC_ERROR_OUT_OF_MEMORY;
    /* Every v2 release or acquire retains its explicit dependency label.
     * Reserve their bookkeeping before any packet or resource mutation. */
    if (release_count + acquire_count > AGC_RUNTIME_MAX_RECORDED_RESOURCES -
            command_buffer->recorded_label_count ||
        release_count > AGC_RUNTIME_MAX_RECORDED_TRANSITIONS -
            command_buffer->recorded_label_signal_count ||
        acquire_count > AGC_RUNTIME_MAX_RECORDED_TRANSITIONS -
            command_buffer->recorded_label_wait_count)
        return AGC_ERROR_OUT_OF_MEMORY;
    for (i = 0u; i < transition_count; ++i) {
        if (transitions[i].resource_type == kAgcResourceTypeBuffer) {
            AgcBuffer buffer = transitions[i].buffer;
            uint32_t prior = 0u;
            uint32_t j;

            for (j = 0u; j < command_buffer->recorded_transition_count; ++j) {
                if (command_buffer->recorded_transitions[j].resource_type ==
                        kAgcResourceTypeBuffer &&
                    command_buffer->recorded_transitions[j].resource == buffer)
                    prior++;
            }
            if (prior > (AGC_RUNTIME_MAX_BUFFER_STATE_RANGES -
                    buffer->state_range_count) / 2u)
                return AGC_ERROR_OUT_OF_MEMORY;
            {
                int32_t result = agcBufferEnsureStateCapacity(buffer,
                    buffer->state_range_count + 2u * (prior + 1u));
                if (result != AGC_OK)
                    return result;
            }
        } else if (!agcImageRangeIsWhole(transitions[i].image,
                &transitions[i].image_range)) {
            int32_t result =
                agcImageEnsureSubresourceStates(transitions[i].image);
            if (result != AGC_OK)
                return result;
        }
    }
    for (i = 0u; i < transition_count; ++i) {
        AgcGfx1013ResourceTransition low_transition;
        uint32_t emit_low_transition;
        uint32_t flags;
        AgcGpuLabel label;
        uint32_t value;
        void *resource;
        int32_t result = agcRuntimeValidateTransition(command_buffer,
            &transitions[i], &resource, &low_transition, &emit_low_transition,
            &flags, &label, &value);
        if (result != AGC_OK)
            return AGC_ERROR_INTERNAL;
        if ((flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT ||
             flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) &&
            !agcCommandRetainGpuLabel(command_buffer, label))
            return AGC_ERROR_INTERNAL;
        if (flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT &&
            !sceAgcDcbWaitRegMem(&command_buffer->cursor, 0u,
                AGC_RUNTIME_WAIT_COMPARE_GREATER_EQUAL, 0u, 0u,
                agcAllocationGpuAddress(label->allocation), value, UINT32_MAX,
                UINT32_MAX))
            return AGC_ERROR_INTERNAL;
        if (emit_low_transition && (result = agcGfx1013TransitionResource(
                &command_buffer->cursor, &low_transition)) != AGC_OK)
            return AGC_ERROR_INTERNAL;
        if (transitions[i].resource_type == kAgcResourceTypeBuffer) {
            if (!agcCommandRetainBuffer(command_buffer, (AgcBuffer)resource))
                return AGC_ERROR_INTERNAL;
        } else if (!agcCommandRetainImage(command_buffer, (AgcImage)resource)) {
            return AGC_ERROR_INTERNAL;
        }
        command_buffer->recorded_transitions[
            command_buffer->recorded_transition_count++] =
            (AgcRuntimeRecordedTransition){ transitions[i].resource_type,
                resource, transitions[i].before, transitions[i].before_owner,
                transitions[i].after, transitions[i].after_owner, flags, label,
                value, transitions[i].buffer_offset,
                transitions[i].buffer_size, transitions[i].image_range };
        agcCaptureRecordTransition(command_buffer, &transitions[i], resource,
            flags, label, value);
        if (flags == AGC_RESOURCE_TRANSITION_RELEASE_BIT) {
            command_buffer->recorded_label_signals[
                command_buffer->recorded_label_signal_count++] =
                (AgcRuntimeRecordedLabelSignal){ label, value };
        } else if (flags == AGC_RESOURCE_TRANSITION_ACQUIRE_BIT) {
            AgcRuntimePendingTransfer *transfer;
            command_buffer->recorded_label_waits[
                command_buffer->recorded_label_wait_count++] =
                (AgcRuntimeRecordedLabelWait){ label, value };
            if (transitions[i].resource_type == kAgcResourceTypeBuffer) {
                transfer = agcBufferFindTransfer((AgcBuffer)resource,
                    transitions[i].buffer_offset, transitions[i].buffer_size);
            } else {
                transfer = agcImageFindTransfer((AgcImage)resource,
                    &transitions[i].image_range);
            }
            if (!transfer)
                return AGC_ERROR_INTERNAL;
            transfer->acquire_command = command_buffer;
        }
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdCopyBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer source, uint64_t source_offset, AgcBuffer destination,
    uint64_t destination_offset, uint64_t size)
{
    const uint64_t maximum_packet_bytes = UINT64_C(0x1ffffc);
    AgcResourceUsage source_usage;
    AgcResourceUsage destination_usage;
    AgcResourceOwner source_owner;
    AgcResourceOwner destination_owner;
    uint64_t packet_count;
    uint64_t required_dwords;
    uint32_t required_buffers;
    uint32_t i;
    int source_recorded = 0;
    int destination_recorded = 0;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !source || source->magic != AGC_MAGIC_BUFFER || !destination ||
        destination->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(command_buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (source->device != command_buffer->device ||
        destination->device != command_buffer->device || source->deferred ||
        destination->deferred || command_buffer->state !=
            AGC_COMMAND_BUFFER_STATE_RECORDING)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCmdCopyBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "buffer copy requires live same-device resources and a Recording command buffer");
    if (size == 0u || ((source_offset | destination_offset | size) & 3u) != 0u ||
        source_offset > source->size || size > source->size - source_offset ||
        destination_offset > destination->size ||
        size > destination->size - destination_offset ||
        (source == destination &&
         (source_offset < destination_offset + size &&
          destination_offset < source_offset + size)))
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCmdCopyBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "buffer copy ranges must be nonempty, four-byte aligned, in bounds, and nonoverlapping");
    if (!agcCommandBufferRangeState(command_buffer, source, source_offset,
            size, &source_usage, &source_owner) ||
        !agcCommandBufferRangeState(command_buffer, destination,
            destination_offset, size, &destination_usage,
            &destination_owner) ||
        source_usage != kAgcResourceUsageCopySource ||
        destination_usage != kAgcResourceUsageCopyDestination ||
        source_owner != agcRuntimeCommandOwner(command_buffer) ||
        destination_owner != agcRuntimeCommandOwner(command_buffer)) {
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcCmdCopyBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "buffer copy requires source and destination ranges to be transitioned to copy usages for this queue");
    }
    packet_count = size / maximum_packet_bytes +
        (size % maximum_packet_bytes != 0u);
    if (packet_count > UINT32_MAX / 7u)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED, "agcCmdCopyBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "buffer copy packet count exceeds command capacity");
    required_dwords = packet_count * 7u;
    if (required_dwords > agcCbRemainingDwords(&command_buffer->cursor))
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED, "agcCmdCopyBuffer",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has insufficient dwords for the buffer copy");
    for (i = 0u; i < command_buffer->recorded_buffer_count; ++i) {
        source_recorded |= command_buffer->recorded_buffers[i] == source;
        destination_recorded |= command_buffer->recorded_buffers[i] == destination;
    }
    required_buffers = (uint32_t)!source_recorded +
        (uint32_t)!destination_recorded;
    if (command_buffer->recorded_buffer_count >
        AGC_RUNTIME_MAX_RECORDED_RESOURCES - required_buffers)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcGfx1013CopyBuffer(&command_buffer->cursor,
        agcBufferGpuAddress(source) + source_offset,
        agcBufferGpuAddress(destination) + destination_offset,
        size);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    if (!agcCommandRetainBuffer(command_buffer, source) ||
        !agcCommandRetainBuffer(command_buffer, destination))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

static int32_t agcCommandValidateBufferWrite(
    AgcCommandBuffer command_buffer, AgcBuffer destination,
    uint64_t destination_offset, uint64_t size, uint64_t required_dwords,
    const char *function_name)
{
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    uint32_t i;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!destination || destination->magic != AGC_MAGIC_BUFFER ||
        destination->device != command_buffer->device ||
        destination->deferred || command_buffer->state !=
            AGC_COMMAND_BUFFER_STATE_RECORDING ||
        (destination->usage & AGC_BUFFER_USAGE_TRANSFER_DST_BIT) == 0u ||
        size == 0u || ((destination_offset | size) & 3u) != 0u ||
        destination_offset > destination->size ||
        size > destination->size - destination_offset)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, function_name,
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "buffer write requires a live transfer-destination buffer and a nonempty, four-byte-aligned in-range destination");
    if (!agcCommandBufferRangeState(command_buffer, destination,
            destination_offset, size, &usage, &owner) ||
        usage != kAgcResourceUsageCopyDestination ||
        owner != agcRuntimeCommandOwner(command_buffer))
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
            AGC_ERROR_INVALID_STATE, function_name,
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "buffer write requires the complete range in queue-owned CopyDestination state");
    if (required_dwords > agcCbRemainingDwords(&command_buffer->cursor))
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED, function_name,
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has insufficient dwords for the complete buffer write");
    for (i = 0u; i < command_buffer->recorded_buffer_count; ++i)
        if (command_buffer->recorded_buffers[i] == destination)
            return AGC_OK;
    if (command_buffer->recorded_buffer_count >=
        AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return AGC_ERROR_OUT_OF_MEMORY;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdUpdateBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer destination, uint64_t destination_offset, const void *data,
    uint64_t size)
{
    const uint64_t maximum_packet_dwords = UINT64_C(0x3ffd);
    uint64_t data_dwords;
    uint64_t packet_count;
    uint64_t required_dwords;
    uint64_t emitted_dwords = 0u;
    int32_t result;

    if (!data)
        return AGC_ERROR_INVALID_ARGUMENT;
    data_dwords = size / sizeof(uint32_t);
    packet_count = data_dwords / maximum_packet_dwords +
        (data_dwords % maximum_packet_dwords != 0u);
    if (size == 0u || (size & 3u) != 0u ||
        packet_count > (UINT64_MAX - data_dwords) / 4u)
        return AGC_ERROR_INVALID_ARGUMENT;
    required_dwords = data_dwords + packet_count * 4u;
    result = agcCommandValidateBufferWrite(command_buffer, destination,
        destination_offset, size, required_dwords, "agcCmdUpdateBuffer");
    if (result != AGC_OK)
        return result;
    while (emitted_dwords < data_dwords) {
        uint32_t chunk = data_dwords - emitted_dwords > maximum_packet_dwords ?
            (uint32_t)maximum_packet_dwords :
            (uint32_t)(data_dwords - emitted_dwords);
        const uint32_t *source = (const uint32_t *)((const uint8_t *)data +
            emitted_dwords * sizeof(uint32_t));
        if (!sceAgcDcbWriteData(&command_buffer->cursor, 2u, 0u,
                agcBufferGpuAddress(destination) + destination_offset +
                    emitted_dwords * sizeof(uint32_t),
                source, chunk, 1u, 1u))
            return AGC_ERROR_INTERNAL;
        emitted_dwords += chunk;
    }
    if (!agcCommandRetainBuffer(command_buffer, destination))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdFillBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer destination, uint64_t destination_offset, uint64_t size,
    uint32_t value)
{
    const uint64_t maximum_chunk_size = UINT64_C(0x1ffffc);
    const uint64_t packet_dwords = 7u;
    uint64_t packet_count = size / maximum_chunk_size +
        (size % maximum_chunk_size != 0u);
    uint64_t required_dwords;
    uint64_t emitted_bytes = 0u;
    int32_t result;

    if (size == 0u || (size & 3u) != 0u ||
        packet_count > UINT64_MAX / packet_dwords)
        return AGC_ERROR_INVALID_ARGUMENT;
    required_dwords = packet_count * packet_dwords;
    result = agcCommandValidateBufferWrite(command_buffer, destination,
        destination_offset, size, required_dwords, "agcCmdFillBuffer");
    if (result != AGC_OK)
        return result;
    while (emitted_bytes < size) {
        uint64_t address = agcBufferGpuAddress(destination) +
            destination_offset + emitted_bytes;
        uint32_t chunk = size - emitted_bytes > maximum_chunk_size ?
            (uint32_t)maximum_chunk_size : (uint32_t)(size - emitted_bytes);
        uint32_t *packet = agcCbAllocDwords(&command_buffer->cursor,
            (uint32_t)packet_dwords);
        if (!packet)
            return AGC_ERROR_INTERNAL;
        packet[0] = agcPm4Header3(AGC_PM4_OP_DMA_DATA,
            (uint32_t)packet_dwords);
        /* Immediate source, memory destination, and completion ordering. */
        packet[1] = UINT32_C(0xc0000000);
        packet[2] = value;
        packet[3] = 0u;
        packet[4] = (uint32_t)address;
        packet[5] = (uint32_t)(address >> 32u);
        packet[6] = chunk;
        emitted_bytes += chunk;
    }
    if (!agcCommandRetainBuffer(command_buffer, destination))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

static int32_t agcCommandValidateOcclusionQueryRange(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset,
    uint64_t size, const char *function_name)
{
    AgcResourceUsage usage;
    AgcResourceOwner owner;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        buffer->device != command_buffer->device || buffer->deferred ||
        command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics ||
        (buffer->usage & AGC_BUFFER_USAGE_QUERY_BIT) == 0u || size == 0u ||
        (offset & 7u) != 0u || offset > buffer->size ||
        size > buffer->size - offset) {
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, function_name,
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "occlusion queries require an aligned in-range typed query buffer on a recording graphics command buffer");
    }
    if (!agcCommandCanRetainBuffer(command_buffer, buffer))
        return AGC_ERROR_OUT_OF_MEMORY;
    if (!agcCommandBufferRangeState(command_buffer, buffer, offset, size,
            &usage, &owner))
        return AGC_ERROR_INVALID_STATE;
    if (usage != kAgcResourceUsageQueryWrite ||
        owner != kAgcResourceOwnerGraphics) {
        AgcResourceTransition transition = AGC_RESOURCE_TRANSITION_INIT;
        int32_t transition_result;

        transition.before = usage;
        transition.after = kAgcResourceUsageQueryWrite;
        transition.before_owner = owner;
        transition.after_owner = kAgcResourceOwnerGraphics;
        transition.buffer = buffer;
        transition.buffer_offset = offset;
        transition.buffer_size = size;
        transition_result = agcCmdTransitionResources(
            command_buffer, 1u, &transition);
        if (transition_result != AGC_OK)
            return agcDebugReport(command_buffer->device,
                AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
                transition_result, function_name,
                AGC_OBJECT_TYPE_COMMAND_BUFFER,
                command_buffer->allocation->debug_name,
                "occlusion query record could not be acquired into graphics-owned QueryWrite state");
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdResetOcclusionQueries(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset,
    uint32_t query_count)
{
    static const uint32_t zeros[
        AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE / sizeof(uint32_t)] = {0};
    const uint32_t record_dwords =
        AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE / sizeof(uint32_t);
    const uint32_t packet_dwords = record_dwords + 4u;
    uint64_t size;
    uint64_t required_dwords;
    uint32_t query;
    int32_t result;

    if (query_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    size = (uint64_t)query_count * AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE;
    result = agcCommandValidateOcclusionQueryRange(command_buffer, buffer,
        offset, size, "agcCmdResetOcclusionQueries");
    if (result != AGC_OK)
        return result;
    required_dwords = (uint64_t)query_count * packet_dwords;
    if (required_dwords > agcCbRemainingDwords(&command_buffer->cursor))
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED,
            "agcCmdResetOcclusionQueries", AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has insufficient dwords for complete query reset packets");
    for (query = 0u; query < query_count; ++query) {
        const uint64_t address = agcBufferGpuAddress(buffer) + offset +
            (uint64_t)query * AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE;
        if (!sceAgcDcbWriteData(&command_buffer->cursor, 2u, 0u, address,
                zeros, record_dwords, 0u, 1u))
            return AGC_ERROR_INTERNAL;
    }
    if (!agcCommandRetainBuffer(command_buffer, buffer))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdBeginOcclusionQuery(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset,
    uint32_t precise)
{
    uint32_t words[AGC_RUNTIME_GRAPHICS_DEFAULT_DWORDS +
        AGC_GFX1013_OCCLUSION_QUERY_OP_DWORDS];
    SceAgcCb scratch;
    int32_t result;

    if (precise > 1u)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcCommandValidateOcclusionQueryRange(command_buffer, buffer,
        offset, AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE,
        "agcCmdBeginOcclusionQuery");
    if (result != AGC_OK)
        return result;
    agcCbInit(&scratch, words, sizeof(words));
    if (!command_buffer->graphics_defaults_emitted) {
        result = agcGfx1013ApplyGraphicsDefaultsV8(&scratch, NULL);
        if (result != AGC_OK)
            return result == AGC_ERROR_BUFFER_TOO_SMALL ?
                AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    }
    result = agcGfx1013BeginOcclusionQuery(&scratch,
        agcBufferGpuAddress(buffer) + offset, precise);
    if (result != AGC_OK)
        return result;
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result != AGC_OK)
        return result;
    command_buffer->graphics_defaults_emitted = 1u;
    if (!agcCommandRetainBuffer(command_buffer, buffer))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdEndOcclusionQuery(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset)
{
    uint32_t words[AGC_GFX1013_OCCLUSION_QUERY_OP_DWORDS + 32u];
    AgcGfx1013EopFenceState availability;
    SceAgcCb scratch;
    uint64_t address;
    int32_t result;

    result = agcCommandValidateOcclusionQueryRange(command_buffer, buffer,
        offset, AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE,
        "agcCmdEndOcclusionQuery");
    if (result != AGC_OK)
        return result;
    address = agcBufferGpuAddress(buffer) + offset;
    availability.address = address + AGC_GFX1013_OCCLUSION_QUERY_STRIDE;
    availability.value = 1u;
    agcCbInit(&scratch, words, sizeof(words));
    result = agcGfx1013EndOcclusionQuery(&scratch, address + sizeof(uint64_t));
    if (result == AGC_OK)
        result = agcGfx1013SignalEopFence(&scratch, &availability);
    if (result != AGC_OK)
        return result;
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result != AGC_OK)
        return result;
    if (!agcCommandRetainBuffer(command_buffer, buffer))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

static int agcImageCopyLayoutsMatch(AgcImage source, AgcImage destination)
{
    return source->desc.width == destination->desc.width &&
        source->desc.height == destination->desc.height &&
        source->desc.depth == destination->desc.depth &&
        source->desc.mip_levels == destination->desc.mip_levels &&
        source->desc.array_layers == destination->desc.array_layers &&
        source->desc.format == destination->desc.format &&
        source->desc.sample_count == destination->desc.sample_count &&
        source->layout.allocation_size == destination->layout.allocation_size &&
        source->layout.alignment == destination->layout.alignment &&
        source->layout.plane_count == destination->layout.plane_count &&
        source->layout.subresource_count ==
            destination->layout.subresource_count &&
        source->layout.block_width == destination->layout.block_width &&
        source->layout.block_height == destination->layout.block_height &&
        source->layout.bytes_per_block == destination->layout.bytes_per_block &&
        source->layout.first_mip_in_tail ==
            destination->layout.first_mip_in_tail &&
        source->layout.metadata_offset == destination->layout.metadata_offset &&
        source->layout.metadata_size == destination->layout.metadata_size;
}

int32_t PS5_SYSV_ABI agcCmdCopyImage(AgcCommandBuffer command_buffer,
    AgcImage source, AgcImage destination)
{
    const uint64_t maximum_packet_bytes = UINT64_C(0x1ffffc);
    AgcResourceUsage source_usage;
    AgcResourceUsage destination_usage;
    AgcResourceOwner source_owner;
    AgcResourceOwner destination_owner;
    uint64_t size;
    uint64_t packet_count;
    uint64_t required_dwords;
    uint32_t required_images;
    uint32_t i;
    int source_recorded = 0;
    int destination_recorded = 0;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !source || source->magic != AGC_MAGIC_IMAGE || !destination ||
        destination->magic != AGC_MAGIC_IMAGE || source == destination ||
        !agcDeviceValid(command_buffer->device) ||
        source->device != command_buffer->device ||
        destination->device != command_buffer->device || source->deferred ||
        destination->deferred || command_buffer->state !=
            AGC_COMMAND_BUFFER_STATE_RECORDING ||
        !agcImageCopyLayoutsMatch(source, destination)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    size = source->layout.allocation_size;
    if (size == 0u || (size & 3u) != 0u)
        return AGC_ERROR_NOT_SUPPORTED;
    agcCommandTransitionState(command_buffer, kAgcResourceTypeImage, source,
        &source_usage, &source_owner);
    agcCommandTransitionState(command_buffer, kAgcResourceTypeImage,
        destination, &destination_usage, &destination_owner);
    if (source_usage != kAgcResourceUsageCopySource ||
        destination_usage != kAgcResourceUsageCopyDestination ||
        source_owner != agcRuntimeCommandOwner(command_buffer) ||
        destination_owner != agcRuntimeCommandOwner(command_buffer)) {
        return AGC_ERROR_INVALID_STATE;
    }
    packet_count = size / maximum_packet_bytes +
        (size % maximum_packet_bytes != 0u);
    if (packet_count > UINT32_MAX / 7u)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    required_dwords = packet_count * 7u;
    if (required_dwords > agcCbRemainingDwords(&command_buffer->cursor))
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    for (i = 0u; i < command_buffer->recorded_image_count; ++i) {
        source_recorded |= command_buffer->recorded_images[i] == source;
        destination_recorded |=
            command_buffer->recorded_images[i] == destination;
    }
    required_images = (uint32_t)!source_recorded +
        (uint32_t)!destination_recorded;
    if (command_buffer->recorded_image_count >
        AGC_RUNTIME_MAX_RECORDED_RESOURCES - required_images)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcGfx1013CopyBuffer(&command_buffer->cursor,
        agcImageGpuAddress(source), agcImageGpuAddress(destination), size);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    if (!agcCommandRetainImage(command_buffer, source) ||
        !agcCommandRetainImage(command_buffer, destination))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

typedef struct AgcRuntimeImageCopyGeometry {
    uint32_t block_width;
    uint32_t block_height;
    uint32_t bytes_per_block;
    uint32_t row_count;
    uint32_t slice_count;
    uint64_t row_bytes;
    uint64_t buffer_row_pitch;
    uint64_t buffer_slice_pitch;
    uint64_t buffer_footprint;
} AgcRuntimeImageCopyGeometry;

static int agcRuntimeRangesOverlap(uint64_t first_offset,
    uint64_t first_size, uint64_t second_offset, uint64_t second_size)
{
    if (first_offset <= second_offset)
        return second_offset - first_offset < first_size;
    return first_offset - second_offset < second_size;
}

static int agcCommandCanRetainImage(AgcCommandBuffer command_buffer,
    AgcImage image)
{
    uint32_t i;
    for (i = 0u; i < command_buffer->recorded_image_count; ++i) {
        if (command_buffer->recorded_images[i] == image)
            return 1;
    }
    return command_buffer->recorded_image_count <
        AGC_RUNTIME_MAX_RECORDED_RESOURCES;
}

static int32_t agcRuntimeValidateImageCopyRegion(AgcImage image,
    const AgcImageSubresourceLayers *subresource, const AgcOffset3D *offset,
    const AgcExtent3D *extent, uint32_t buffer_row_length,
    uint32_t buffer_image_height, AgcRuntimeImageCopyGeometry *geometry)
{
    AgcRuntimeFormatInfo format;
    uint32_t mip_width;
    uint32_t mip_height;
    uint32_t mip_depth;
    uint64_t copy_width_blocks;
    uint64_t row_length_blocks;
    uint64_t image_height_blocks;
    uint64_t footprint;
    uint64_t row_tail;
    int explicit_row_length = buffer_row_length != 0u;
    int explicit_image_height = buffer_image_height != 0u;

    if (!image || !subresource || !offset || !extent || !geometry ||
        !agcGetRuntimeFormatInfo(image->desc.format, &format) ||
        format.depth_stencil || format.plane_count != 1u ||
        image->desc.sample_count != 1u || image->layout.metadata_size != 0u ||
        subresource->aspect_mask != AGC_IMAGE_ASPECT_COLOR_BIT ||
        subresource->mip_level >= image->desc.mip_levels ||
        subresource->array_layer_count == 0u || offset->x < 0 ||
        offset->y < 0 || offset->z < 0 || extent->width == 0u ||
        extent->height == 0u || extent->depth == 0u)
        return AGC_ERROR_NOT_SUPPORTED;
    mip_width = image->desc.width >> subresource->mip_level;
    mip_height = image->desc.height >> subresource->mip_level;
    mip_depth = image->desc.depth >> subresource->mip_level;
    if (mip_width == 0u) mip_width = 1u;
    if (mip_height == 0u) mip_height = 1u;
    if (mip_depth == 0u) mip_depth = 1u;
    if ((uint32_t)offset->x > mip_width ||
        extent->width > mip_width - (uint32_t)offset->x ||
        (uint32_t)offset->y > mip_height ||
        extent->height > mip_height - (uint32_t)offset->y ||
        (uint32_t)offset->z > mip_depth ||
        extent->depth > mip_depth - (uint32_t)offset->z)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (image->desc.depth > 1u) {
        if (subresource->base_array_layer != 0u ||
            subresource->array_layer_count != 1u)
            return AGC_ERROR_INVALID_ARGUMENT;
    } else if (offset->z != 0 || extent->depth != 1u ||
        subresource->base_array_layer >= image->desc.array_layers ||
        subresource->array_layer_count > image->desc.array_layers -
            subresource->base_array_layer) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (((uint32_t)offset->x % format.block_width) != 0u ||
        ((uint32_t)offset->y % format.block_height) != 0u ||
        (extent->width % format.block_width != 0u &&
         (uint32_t)offset->x + extent->width != mip_width) ||
        (extent->height % format.block_height != 0u &&
         (uint32_t)offset->y + extent->height != mip_height))
        return AGC_ERROR_INVALID_ALIGNMENT;
    if (buffer_row_length == 0u)
        buffer_row_length = extent->width;
    if (buffer_image_height == 0u)
        buffer_image_height = extent->height;
    if (buffer_row_length < extent->width ||
        buffer_image_height < extent->height ||
        (explicit_row_length &&
         buffer_row_length % format.block_width != 0u) ||
        (explicit_image_height &&
         buffer_image_height % format.block_height != 0u))
        return AGC_ERROR_INVALID_ARGUMENT;
    copy_width_blocks = (extent->width + format.block_width - 1u) /
        format.block_width;
    row_length_blocks = ((uint64_t)buffer_row_length +
        format.block_width - 1u) / format.block_width;
    image_height_blocks = ((uint64_t)buffer_image_height +
        format.block_height - 1u) / format.block_height;
    memset(geometry, 0, sizeof(*geometry));
    geometry->block_width = format.block_width;
    geometry->block_height = format.block_height;
    geometry->bytes_per_block = format.bytes[0];
    geometry->row_count = (extent->height + format.block_height - 1u) /
        format.block_height;
    geometry->slice_count = image->desc.depth > 1u ? extent->depth :
        subresource->array_layer_count;
    if (!agcMulU64(copy_width_blocks, format.bytes[0],
            &geometry->row_bytes) ||
        !agcMulU64(row_length_blocks, format.bytes[0],
            &geometry->buffer_row_pitch) ||
        !agcMulU64(geometry->buffer_row_pitch, image_height_blocks,
            &geometry->buffer_slice_pitch) ||
        !agcMulU64((uint64_t)geometry->slice_count - 1u,
            geometry->buffer_slice_pitch, &footprint) ||
        !agcMulU64((uint64_t)geometry->row_count - 1u,
            geometry->buffer_row_pitch, &row_tail) ||
        !agcAddU64(footprint, row_tail, &footprint) ||
        !agcAddU64(footprint, geometry->row_bytes,
            &geometry->buffer_footprint))
        return AGC_ERROR_INVALID_ARGUMENT;
    if ((geometry->row_bytes & 3u) != 0u)
        return AGC_ERROR_NOT_SUPPORTED;
    return AGC_OK;
}

static int32_t agcRuntimeImageCopyRowAddress(AgcImage image,
    const AgcImageSubresourceLayers *subresource, const AgcOffset3D *offset,
    const AgcRuntimeImageCopyGeometry *geometry, uint32_t slice,
    uint32_t row, uint64_t *address)
{
    AgcImageSubresourceLayout layout = AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT;
    uint32_t layer = image->desc.depth > 1u ? 0u :
        subresource->base_array_layer + slice;
    uint32_t z = image->desc.depth > 1u ? (uint32_t)offset->z + slice : 0u;
    uint64_t relative;
    uint64_t absolute;
    uint64_t value;
    uint64_t row_offset;
    uint64_t column_offset;
    int32_t result = agcGetImageSubresourceLayout(image->device,
        &image->desc, subresource->mip_level, layer, 0u, &layout);
    if (result != AGC_OK)
        return result;
    if (!agcMulU64((uint64_t)(uint32_t)offset->y /
            geometry->block_height + row, layout.row_pitch, &row_offset) ||
        !agcMulU64((uint64_t)(uint32_t)offset->x /
            geometry->block_width, geometry->bytes_per_block,
            &column_offset) ||
        !agcMulU64(z, layout.slice_pitch, &relative) ||
        !agcAddU64(relative, row_offset, &relative) ||
        !agcAddU64(relative, column_offset, &relative) ||
        relative > layout.size || geometry->row_bytes > layout.size - relative ||
        !agcAddU64(layout.offset, relative, &absolute) ||
        !agcAddU64(agcImageGpuAddress(image), absolute, &value) ||
        (value & 3u) != 0u)
        return AGC_ERROR_INVALID_ALIGNMENT;
    *address = value;
    return AGC_OK;
}

static int agcRuntimeImageCopyState(AgcCommandBuffer command_buffer,
    AgcImage image, const AgcImageSubresourceLayers *subresource,
    AgcResourceUsage required)
{
    AgcImageSubresourceRange range = { subresource->aspect_mask,
        subresource->mip_level, 1u, subresource->base_array_layer,
        subresource->array_layer_count, 0u };
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    return agcCommandImageRangeState(command_buffer, image, &range,
        &usage, &owner) && usage == required &&
        owner == agcRuntimeCommandOwner(command_buffer);
}

int32_t PS5_SYSV_ABI agcCmdCopyImageRegions(
    AgcCommandBuffer command_buffer, AgcImage source, AgcImage destination,
    uint32_t region_count, const AgcImageCopyRegion *regions)
{
    uint64_t total_rows = 0u;
    uint32_t region;
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !source || source->magic != AGC_MAGIC_IMAGE || !destination ||
        destination->magic != AGC_MAGIC_IMAGE || source == destination ||
        source->device != command_buffer->device ||
        destination->device != command_buffer->device || source->deferred ||
        destination->deferred || command_buffer->state !=
            AGC_COMMAND_BUFFER_STATE_RECORDING || region_count == 0u ||
        !regions || source->desc.format != destination->desc.format ||
        (source->allocation == destination->allocation &&
         agcRuntimeRangesOverlap(source->memory_offset,
             source->layout.allocation_size, destination->memory_offset,
             destination->layout.allocation_size)) ||
        (source->desc.usage & AGC_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u ||
        (destination->desc.usage & AGC_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (region = 0u; region < region_count; ++region) {
        const AgcImageCopyRegion *copy = &regions[region];
        AgcRuntimeImageCopyGeometry source_geometry;
        AgcRuntimeImageCopyGeometry destination_geometry;
        uint32_t slice;
        uint32_t row;
        uint64_t region_rows;
        int32_t result;
        if (!agcHeaderValid(copy->struct_size, sizeof(*copy), copy->version) ||
            copy->flags != 0u || copy->reserved0 != 0u ||
            copy->reserved1 != 0u || copy->reserved2 != 0u ||
            copy->reserved3 != 0u || !agcReservedZero(copy->reserved, 4u) ||
            copy->source_subresource.array_layer_count !=
                copy->destination_subresource.array_layer_count)
            return AGC_ERROR_INVALID_ARGUMENT;
        result = agcRuntimeValidateImageCopyRegion(source,
            &copy->source_subresource, &copy->source_offset, &copy->extent,
            copy->extent.width, copy->extent.height, &source_geometry);
        if (result == AGC_OK)
            result = agcRuntimeValidateImageCopyRegion(destination,
                &copy->destination_subresource, &copy->destination_offset,
                &copy->extent, copy->extent.width, copy->extent.height,
                &destination_geometry);
        if (result != AGC_OK)
            return result;
        if (source_geometry.row_bytes != destination_geometry.row_bytes ||
            source_geometry.row_count != destination_geometry.row_count ||
            source_geometry.slice_count != destination_geometry.slice_count ||
            !agcRuntimeImageCopyState(command_buffer, source,
                &copy->source_subresource, kAgcResourceUsageCopySource) ||
            !agcRuntimeImageCopyState(command_buffer, destination,
                &copy->destination_subresource,
                kAgcResourceUsageCopyDestination))
            return AGC_ERROR_INVALID_STATE;
        for (slice = 0u; slice < source_geometry.slice_count; ++slice) {
            for (row = 0u; row < source_geometry.row_count; ++row) {
                uint64_t source_address;
                uint64_t destination_address;
                result = agcRuntimeImageCopyRowAddress(source,
                    &copy->source_subresource, &copy->source_offset,
                    &source_geometry, slice, row, &source_address);
                if (result == AGC_OK)
                    result = agcRuntimeImageCopyRowAddress(destination,
                        &copy->destination_subresource,
                        &copy->destination_offset, &destination_geometry,
                        slice, row, &destination_address);
                if (result != AGC_OK)
                    return result;
            }
        }
        if (!agcMulU64(source_geometry.slice_count,
                source_geometry.row_count, &region_rows) ||
            !agcAddU64(total_rows, region_rows, &total_rows))
            return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    }
    if (total_rows > UINT32_MAX / 7u || total_rows * 7u >
            agcCbRemainingDwords(&command_buffer->cursor))
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    {
        uint32_t required_images = 0u;
        uint32_t i;
        int source_recorded = 0;
        int destination_recorded = 0;
        for (i = 0u; i < command_buffer->recorded_image_count; ++i) {
            source_recorded |= command_buffer->recorded_images[i] == source;
            destination_recorded |=
                command_buffer->recorded_images[i] == destination;
        }
        required_images = (uint32_t)!source_recorded +
            (uint32_t)!destination_recorded;
        if (command_buffer->recorded_image_count >
            AGC_RUNTIME_MAX_RECORDED_RESOURCES - required_images)
            return AGC_ERROR_OUT_OF_MEMORY;
    }
    for (region = 0u; region < region_count; ++region) {
        const AgcImageCopyRegion *copy = &regions[region];
        AgcRuntimeImageCopyGeometry geometry;
        uint32_t slice;
        uint32_t row;
        int32_t result = agcRuntimeValidateImageCopyRegion(source,
            &copy->source_subresource, &copy->source_offset, &copy->extent,
            copy->extent.width, copy->extent.height, &geometry);
        if (result != AGC_OK)
            return AGC_ERROR_INTERNAL;
        for (slice = 0u; slice < geometry.slice_count; ++slice) {
            for (row = 0u; row < geometry.row_count; ++row) {
                uint64_t source_address;
                uint64_t destination_address;
                (void)agcRuntimeImageCopyRowAddress(source,
                    &copy->source_subresource, &copy->source_offset,
                    &geometry, slice, row, &source_address);
                (void)agcRuntimeImageCopyRowAddress(destination,
                    &copy->destination_subresource,
                    &copy->destination_offset, &geometry, slice, row,
                    &destination_address);
                result = agcGfx1013CopyBuffer(&command_buffer->cursor,
                    source_address, destination_address, geometry.row_bytes);
                if (result != AGC_OK)
                    return AGC_ERROR_INTERNAL;
            }
        }
    }
    if (!agcCommandRetainImage(command_buffer, source) ||
        !agcCommandRetainImage(command_buffer, destination))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

static int32_t agcCmdCopyBufferImage(AgcCommandBuffer command_buffer,
    AgcBuffer buffer, AgcImage image, uint32_t region_count,
    const AgcBufferImageCopyRegion *regions, int buffer_is_source)
{
    uint64_t total_rows = 0u;
    uint32_t region;
    AgcResourceUsage buffer_required = buffer_is_source ?
        kAgcResourceUsageCopySource : kAgcResourceUsageCopyDestination;
    AgcResourceUsage image_required = buffer_is_source ?
        kAgcResourceUsageCopyDestination : kAgcResourceUsageCopySource;
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !buffer || buffer->magic != AGC_MAGIC_BUFFER || !image ||
        image->magic != AGC_MAGIC_IMAGE ||
        buffer->device != command_buffer->device ||
        image->device != command_buffer->device || buffer->deferred ||
        image->deferred || command_buffer->state !=
            AGC_COMMAND_BUFFER_STATE_RECORDING || region_count == 0u ||
        !regions || (buffer->usage & (buffer_is_source ?
            AGC_BUFFER_USAGE_TRANSFER_SRC_BIT :
            AGC_BUFFER_USAGE_TRANSFER_DST_BIT)) == 0u ||
        (image->desc.usage & (buffer_is_source ?
            AGC_IMAGE_USAGE_TRANSFER_DST_BIT :
            AGC_IMAGE_USAGE_TRANSFER_SRC_BIT)) == 0u ||
        (buffer->allocation == image->allocation &&
         agcRuntimeRangesOverlap(buffer->memory_offset, buffer->size,
             image->memory_offset, image->layout.allocation_size)))
        return AGC_ERROR_INVALID_ARGUMENT;
    for (region = 0u; region < region_count; ++region) {
        const AgcBufferImageCopyRegion *copy = &regions[region];
        AgcRuntimeImageCopyGeometry geometry;
        AgcResourceUsage usage;
        AgcResourceOwner owner;
        uint32_t slice;
        uint32_t row;
        uint64_t region_rows;
        int32_t result;
        if (!agcHeaderValid(copy->struct_size, sizeof(*copy), copy->version) ||
            copy->flags != 0u || copy->reserved0 != 0u ||
            copy->reserved1 != 0u || copy->reserved2 != 0u ||
            !agcReservedZero(copy->reserved, 4u))
            return AGC_ERROR_INVALID_ARGUMENT;
        result = agcRuntimeValidateImageCopyRegion(image,
            &copy->image_subresource, &copy->image_offset,
            &copy->image_extent, copy->buffer_row_length,
            copy->buffer_image_height, &geometry);
        if (result != AGC_OK)
            return result;
        if ((copy->buffer_offset & 3u) != 0u ||
            copy->buffer_offset > buffer->size ||
            geometry.buffer_footprint > buffer->size - copy->buffer_offset)
            return AGC_ERROR_INVALID_ARGUMENT;
        if (!agcCommandBufferRangeState(command_buffer, buffer,
                copy->buffer_offset, geometry.buffer_footprint,
                &usage, &owner) || usage != buffer_required ||
            owner != agcRuntimeCommandOwner(command_buffer))
            return AGC_ERROR_INVALID_STATE;
        if (!agcRuntimeImageCopyState(command_buffer, image,
                &copy->image_subresource, image_required))
            return AGC_ERROR_INVALID_STATE;
        for (slice = 0u; slice < geometry.slice_count; ++slice) {
            for (row = 0u; row < geometry.row_count; ++row) {
                uint64_t image_address;
                uint64_t buffer_relative;
                result = agcRuntimeImageCopyRowAddress(image,
                    &copy->image_subresource, &copy->image_offset, &geometry,
                    slice, row, &image_address);
                if (!agcMulU64(slice, geometry.buffer_slice_pitch,
                        &buffer_relative) ||
                    !agcAddU64(buffer_relative,
                        (uint64_t)row * geometry.buffer_row_pitch,
                        &buffer_relative) ||
                    !agcAddU64(buffer_relative, copy->buffer_offset,
                        &buffer_relative) ||
                    !agcAddU64(agcBufferGpuAddress(buffer), buffer_relative,
                        &buffer_relative) || (buffer_relative & 3u) != 0u)
                    return AGC_ERROR_INVALID_ALIGNMENT;
                if (result != AGC_OK)
                    return result;
            }
        }
        if (!agcMulU64(geometry.slice_count, geometry.row_count,
                &region_rows) ||
            !agcAddU64(total_rows, region_rows, &total_rows))
            return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    }
    if (total_rows > UINT32_MAX / 7u || total_rows * 7u >
            agcCbRemainingDwords(&command_buffer->cursor))
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    if (!agcCommandCanRetainBuffer(command_buffer, buffer) ||
        !agcCommandCanRetainImage(command_buffer, image))
        return AGC_ERROR_OUT_OF_MEMORY;
    for (region = 0u; region < region_count; ++region) {
        const AgcBufferImageCopyRegion *copy = &regions[region];
        AgcRuntimeImageCopyGeometry geometry;
        uint32_t slice;
        uint32_t row;
        int32_t result = agcRuntimeValidateImageCopyRegion(image,
            &copy->image_subresource, &copy->image_offset,
            &copy->image_extent, copy->buffer_row_length,
            copy->buffer_image_height, &geometry);
        if (result != AGC_OK)
            return AGC_ERROR_INTERNAL;
        for (slice = 0u; slice < geometry.slice_count; ++slice) {
            for (row = 0u; row < geometry.row_count; ++row) {
                uint64_t image_address;
                uint64_t buffer_address;
                uint64_t buffer_row_offset;
                uint64_t buffer_slice_offset;
                if (!agcMulU64(slice, geometry.buffer_slice_pitch,
                        &buffer_slice_offset) ||
                    !agcMulU64(row, geometry.buffer_row_pitch,
                        &buffer_row_offset) ||
                    !agcAddU64(buffer_slice_offset, buffer_row_offset,
                        &buffer_address) ||
                    !agcAddU64(buffer_address, copy->buffer_offset,
                        &buffer_address) ||
                    !agcAddU64(buffer_address, agcBufferGpuAddress(buffer),
                        &buffer_address))
                    return AGC_ERROR_INTERNAL;
                (void)agcRuntimeImageCopyRowAddress(image,
                    &copy->image_subresource, &copy->image_offset, &geometry,
                    slice, row, &image_address);
                result = agcGfx1013CopyBuffer(&command_buffer->cursor,
                    buffer_is_source ? buffer_address : image_address,
                    buffer_is_source ? image_address : buffer_address,
                    geometry.row_bytes);
                if (result != AGC_OK)
                    return AGC_ERROR_INTERNAL;
            }
        }
    }
    if (!agcCommandRetainBuffer(command_buffer, buffer) ||
        !agcCommandRetainImage(command_buffer, image))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdCopyBufferToImage(
    AgcCommandBuffer command_buffer, AgcBuffer source, AgcImage destination,
    uint32_t region_count, const AgcBufferImageCopyRegion *regions)
{
    return agcCmdCopyBufferImage(command_buffer, source, destination,
        region_count, regions, 1);
}

int32_t PS5_SYSV_ABI agcCmdCopyImageToBuffer(
    AgcCommandBuffer command_buffer, AgcImage source, AgcBuffer destination,
    uint32_t region_count, const AgcBufferImageCopyRegion *regions)
{
    return agcCmdCopyBufferImage(command_buffer, destination, source,
        region_count, regions, 0);
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

static int agcCommandRetainGpuLabel(AgcCommandBuffer command_buffer,
    AgcGpuLabel label)
{
    uint32_t i;

    for (i = 0u; i < command_buffer->recorded_label_count; ++i) {
        if (command_buffer->recorded_labels[i] == label)
            return 1;
    }
    if (command_buffer->recorded_label_count >=
        AGC_RUNTIME_MAX_RECORDED_RESOURCES)
        return 0;
    command_buffer->recorded_labels[
        command_buffer->recorded_label_count++] = label;
    label->recorded_refs++;
    return 1;
}

static int agcRuntimeDescriptorUsageMatches(
    const AgcShaderDescriptorMapping *mapping, AgcResourceUsage usage)
{
    const uint32_t access = AGC_SHADER_DESCRIPTOR_ACCESS(mapping->array_size);

    switch (mapping->type) {
    case AGC_SHADER_DESCRIPTOR_SAMPLED_IMAGE:
    case AGC_SHADER_DESCRIPTOR_COMBINED_IMAGE_SAMPLER:
    case AGC_SHADER_DESCRIPTOR_INPUT_ATTACHMENT:
    case AGC_SHADER_DESCRIPTOR_UNIFORM_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER:
        return usage == kAgcResourceUsageShaderRead;
    case AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE:
    case AGC_SHADER_DESCRIPTOR_STORAGE_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER:
        /* Current reflection describes descriptor type but not per-binding
         * access qualifiers. Require an explicit shader-readable or writable
         * state; future read/write access metadata can narrow this safely. */
        if (access == AGC_SHADER_DESCRIPTOR_ACCESS_READ_BIT)
            return usage == kAgcResourceUsageShaderRead;
        if (access == AGC_SHADER_DESCRIPTOR_ACCESS_WRITE_BIT)
            return usage == kAgcResourceUsageShaderWrite;
        return usage == kAgcResourceUsageShaderRead ||
            usage == kAgcResourceUsageShaderWrite;
    default:
        return 1;
    }
}

static int32_t agcCommandValidateDescriptorBufferState(
    AgcCommandBuffer command_buffer, const AgcBuffer buffer,
    uint64_t offset, uint64_t size,
    const AgcShaderDescriptorMapping *mapping)
{
    AgcResourceUsage usage;
    AgcResourceOwner owner;

    if (!agcCommandBufferRangeState(command_buffer, (AgcBuffer)buffer,
            offset, size, &usage, &owner) ||
        owner != agcRuntimeCommandOwner(command_buffer) ||
        !agcRuntimeDescriptorUsageMatches(mapping, usage))
        return AGC_ERROR_INVALID_STATE;
    return AGC_OK;
}

static int32_t agcCommandValidateDescriptorImageState(
    AgcCommandBuffer command_buffer, const AgcImageView view,
    const AgcShaderDescriptorMapping *mapping)
{
    AgcImage image = view->image;
    AgcImageSubresourceRange range = {
        agcRuntimeImageAspectMask(image), view->desc.base_mip_level,
        view->desc.mip_level_count, view->desc.base_array_layer,
        view->desc.array_layer_count, 0u };
    AgcResourceUsage usage;
    AgcResourceOwner owner;

    if (!agcCommandImageRangeState(command_buffer, image, &range,
            &usage, &owner) ||
        owner != agcRuntimeCommandOwner(command_buffer) ||
        !agcRuntimeDescriptorUsageMatches(mapping, usage))
        return AGC_ERROR_INVALID_STATE;
    return AGC_OK;
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
        if (write->buffer || write->image_view)
            return AGC_ERROR_RESOURCE_INVALID;
        if (!write->sampler)
            return AGC_OK;
        if (write->sampler->magic != AGC_MAGIC_SAMPLER ||
            write->sampler->device != command_buffer->device)
            return AGC_ERROR_RESOURCE_INVALID;
        memcpy(encoded->bytes,
            agcAllocationCpuAddress(write->sampler->allocation), size);
        encoded->sampler = write->sampler;
        return AGC_OK;
    case AGC_SHADER_DESCRIPTOR_SAMPLED_IMAGE:
    case AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE:
    case AGC_SHADER_DESCRIPTOR_INPUT_ATTACHMENT:
        if (write->buffer || write->sampler)
            return AGC_ERROR_RESOURCE_INVALID;
        if (!write->image_view)
            return AGC_OK;
        if (write->image_view->magic != AGC_MAGIC_IMAGE_VIEW ||
            write->image_view->device != command_buffer->device)
            return AGC_ERROR_RESOURCE_INVALID;
        if ((mapping->type == AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE &&
             (write->image_view->image->desc.usage &
              AGC_IMAGE_USAGE_STORAGE_BIT) == 0u) ||
            (mapping->type != AGC_SHADER_DESCRIPTOR_STORAGE_IMAGE &&
            (write->image_view->image->desc.usage &
             AGC_IMAGE_USAGE_SAMPLED_BIT) == 0u))
            return AGC_ERROR_RESOURCE_INVALID;
        result = agcCommandValidateDescriptorImageState(command_buffer,
            write->image_view, mapping);
        if (result != AGC_OK)
            return result;
        memcpy(encoded->bytes,
            agcAllocationCpuAddress(write->image_view->allocation), size);
        encoded->view = write->image_view;
        return AGC_OK;
    case AGC_SHADER_DESCRIPTOR_COMBINED_IMAGE_SAMPLER:
        if (write->buffer)
            return AGC_ERROR_RESOURCE_INVALID;
        if (write->image_view) {
            if (write->image_view->magic != AGC_MAGIC_IMAGE_VIEW ||
                write->image_view->device != command_buffer->device ||
                (write->image_view->image->desc.usage &
                 AGC_IMAGE_USAGE_SAMPLED_BIT) == 0u)
                return AGC_ERROR_RESOURCE_INVALID;
            result = agcCommandValidateDescriptorImageState(command_buffer,
                write->image_view, mapping);
            if (result != AGC_OK)
                return result;
            memcpy(encoded->bytes,
                agcAllocationCpuAddress(write->image_view->allocation),
                sizeof(AgcGfx1013ImageDescriptor));
            encoded->view = write->image_view;
        }
        if (write->sampler) {
            if (write->sampler->magic != AGC_MAGIC_SAMPLER ||
                write->sampler->device != command_buffer->device)
                return AGC_ERROR_RESOURCE_INVALID;
            memcpy(encoded->bytes + sizeof(AgcGfx1013ImageDescriptor),
                agcAllocationCpuAddress(write->sampler->allocation),
                sizeof(AgcSamplerDescriptor));
            encoded->sampler = write->sampler;
        }
        return AGC_OK;
    case AGC_SHADER_DESCRIPTOR_UNIFORM_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_STORAGE_TEXEL_BUFFER:
    case AGC_SHADER_DESCRIPTOR_UNIFORM_BUFFER:
    case AGC_SHADER_DESCRIPTOR_STORAGE_BUFFER:
        if (write->image_view || write->sampler)
            return AGC_ERROR_RESOURCE_INVALID;
        if (!write->buffer) {
            if (write->buffer_offset || write->buffer_range ||
                write->buffer_stride)
                return AGC_ERROR_RESOURCE_INVALID;
            return AGC_OK;
        }
        if (write->buffer->magic != AGC_MAGIC_BUFFER ||
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
        result = agcCommandValidateDescriptorBufferState(command_buffer,
            write->buffer, write->buffer_offset, range, mapping);
        if (result != AGC_OK)
            return result;
        address = agcBufferGpuAddress(write->buffer) +
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
        !agcDeviceValid(command_buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!writes || write_count == 0u ||
        write_count > AGC_RUNTIME_MAX_DESCRIPTOR_WRITES)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcCmdBindDescriptors",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "descriptor-write list must be nonempty and within the runtime limit");
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        (!command_buffer->graphics_pipeline &&
         !command_buffer->compute_pipeline))
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcCmdBindDescriptors",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "descriptor binding requires a Recording command buffer with a bound pipeline");
    if (command_buffer->descriptors_bound)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT,
            AGC_ERROR_NOT_SUPPORTED, "agcCmdBindDescriptors",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "descriptor tables are immutable after their first bind in this command buffer");
    layout = agcCommandResourceLayout(command_buffer);
    mappings = agcCommandDescriptorMappings(command_buffer, &mapping_count);
    if (!layout || write_count != layout->descriptor_element_count ||
        mapping_count == 0u || !command_buffer->resource_allocation)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
            AGC_ERROR_VALIDATION_FAILED, "agcCmdBindDescriptors",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "descriptor writes must exactly cover the reflected pipeline layout");
    for (i = 0u; i < write_count; ++i) {
        const AgcShaderDescriptorMapping *mapping = NULL;
        if (!agcHeaderValid(writes[i].struct_size, sizeof(writes[i]),
                writes[i].version) || writes[i].reserved0 != 0u ||
            !agcReservedZero(writes[i].reserved, 3u))
            return agcDebugReport(command_buffer->device,
                AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
                AGC_ERROR_INVALID_ARGUMENT, "agcCmdBindDescriptors",
                AGC_OBJECT_TYPE_COMMAND_BUFFER,
                command_buffer->allocation->debug_name,
                "descriptor write has an invalid version or reserved field");
        for (j = 0u; j < mapping_count; ++j) {
            if (mappings[j].set == writes[i].set &&
                mappings[j].binding == writes[i].binding) {
                mapping = &mappings[j];
                break;
            }
        }
        if (!mapping || mapping->type != writes[i].type ||
            writes[i].array_element >=
                AGC_SHADER_DESCRIPTOR_ARRAY_SIZE(mapping->array_size))
            return agcDebugReport(command_buffer->device,
                AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                AGC_ERROR_VALIDATION_FAILED, "agcCmdBindDescriptors",
                AGC_OBJECT_TYPE_COMMAND_BUFFER,
                command_buffer->allocation->debug_name,
                "descriptor set, binding, type, or array element does not match shader reflection");
        for (j = 0u; j < i; ++j) {
            if (writes[j].set == writes[i].set &&
                writes[j].binding == writes[i].binding &&
                writes[j].array_element == writes[i].array_element)
                return agcDebugReport(command_buffer->device,
                    AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                    AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT,
                    AGC_ERROR_VALIDATION_FAILED, "agcCmdBindDescriptors",
                    AGC_OBJECT_TYPE_COMMAND_BUFFER,
                    command_buffer->allocation->debug_name,
                    "descriptor writes contain a duplicate set, binding, and array element");
        }
        result = agcCommandEncodeDescriptor(
            command_buffer, &writes[i], mapping, &encoded[i]);
        if (result != AGC_OK)
            return agcDebugReport(command_buffer->device,
                AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                result == AGC_ERROR_NOT_SUPPORTED ?
                    AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT :
                    AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
                result, "agcCmdBindDescriptors",
                AGC_OBJECT_TYPE_COMMAND_BUFFER,
                command_buffer->allocation->debug_name,
                "descriptor resource is invalid, deferred, misaligned, out of range, or incompatible with its reflected type");
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
            agcBufferGpuAddress(bindings[i].buffer) +
                bindings[i].offset,
            bindings[i].stride, (uint32_t)(range / bindings[i].stride));
        if (result != AGC_OK)
            return result;
        seen |= 1u << bindings[i].binding;
    }
    for (i = 0u; i < binding_count; ++i) {
        AgcResourceUsage usage;
        AgcResourceOwner owner;

        if (!agcCommandBufferRangeState(command_buffer, bindings[i].buffer,
                bindings[i].offset,
                bindings[i].buffer->size - bindings[i].offset,
                &usage, &owner) ||
            usage != kAgcResourceUsageShaderRead ||
            owner != kAgcResourceOwnerGraphics)
            return AGC_ERROR_INVALID_STATE;
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
         (1u << kAgcShaderStagePs) |
         (command_buffer->graphics_pipeline->hull_shader ?
          (1u << kAgcShaderStageHs) : 0u)) :
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
    for (stage = 0u; stage < kAgcShaderStageCount; ++stage) {
        if ((stage_mask & (1u << stage)) == 0u)
            continue;
        memcpy((uint8_t *)agcAllocationCpuAddress(
                command_buffer->resource_allocation) +
                layout->push_constant_offset +
                (uint64_t)stage * layout->push_constant_stride + offset,
            data, size);
    }
    result = agcFlushRuntimeAllocation(command_buffer->resource_allocation,
        layout->push_constant_offset,
        (uint64_t)layout->push_constant_stride * kAgcShaderStageCount);
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
        *value = (uint32_t)(gpu_base + layout->push_constant_offset +
            (uint64_t)shader->stage * layout->push_constant_stride);
        return AGC_OK;
    case AGC_SHADER_USER_SGPR_INLINE_PUSH_CONSTANT:
        if (!cpu_base || sgpr->index >= 64u ||
            (command_buffer->push_constant_masks[shader->stage] &
             (UINT64_C(1) << sgpr->index)) == 0u)
            return AGC_ERROR_RESOURCE_NOT_BOUND;
        memcpy(value, cpu_base + layout->push_constant_offset +
            (uint64_t)shader->stage * layout->push_constant_stride +
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

static int32_t agcCommandEmitGraphicsIndirectUserData(SceAgcCb *cb,
    AgcCommandBuffer command_buffer, AgcShader shader, uint32_t draw_index)
{
    uint32_t i;

    for (i = 0u; i < shader->reflection.user_sgpr_count; ++i) {
        const AgcShaderUserSgpr *sgpr = &shader->reflection.user_sgprs[i];
        AgcRegisterValue reg;
        int32_t result;
        if (sgpr->kind == AGC_SHADER_USER_SGPR_BASE_VERTEX ||
            sgpr->kind == AGC_SHADER_USER_SGPR_START_INSTANCE)
            continue;
        if (sgpr->kind == AGC_SHADER_USER_SGPR_PUSH_CONSTANT_POINTER &&
            agcCommandRequiredPushMask(&shader->reflection) == 0u)
            continue;
        reg.offset = sgpr->register_offset;
        result = agcCommandUserSgprValue(command_buffer, shader, sgpr,
            0, 0u, draw_index, &reg.value);
        if (result != AGC_OK)
            return result;
        if (!sceAgcCbSetShRegistersDirect(cb, &reg, 1u))
            return AGC_ERROR_BUFFER_TOO_SMALL;
    }
    return AGC_OK;
}

static int32_t agcCommandIndirectLocations(AgcShader shader,
    uint32_t *base_vertex_location, uint32_t *start_instance_location,
    uint32_t *draw_index_present)
{
    uint32_t i;
    uint32_t base = UINT32_MAX;
    uint32_t instance = UINT32_MAX;
    uint32_t draw_index = 0u;

    for (i = 0u; i < shader->reflection.user_sgpr_count; ++i) {
        const AgcShaderUserSgpr *sgpr = &shader->reflection.user_sgprs[i];
        if (sgpr->dword_count != 1u)
            continue;
        if (sgpr->kind == AGC_SHADER_USER_SGPR_BASE_VERTEX)
            base = sgpr->register_offset;
        else if (sgpr->kind == AGC_SHADER_USER_SGPR_START_INSTANCE)
            instance = sgpr->register_offset;
        else if (sgpr->kind == AGC_SHADER_USER_SGPR_DRAW_INDEX)
            draw_index = 1u;
    }
    if (base == UINT32_MAX || instance == UINT32_MAX)
        return AGC_ERROR_NOT_SUPPORTED;
    if (!((base >= AGC_REG_SPI_SHADER_USER_DATA_GS_0 &&
           base <= AGC_REG_SPI_SHADER_USER_DATA_GS_31 &&
           instance >= AGC_REG_SPI_SHADER_USER_DATA_GS_0 &&
           instance <= AGC_REG_SPI_SHADER_USER_DATA_GS_31) ||
          (base >= AGC_REG_SPI_SHADER_USER_DATA_HS_0 &&
           base <= AGC_REG_SPI_SHADER_USER_DATA_HS_31 &&
           instance >= AGC_REG_SPI_SHADER_USER_DATA_HS_0 &&
           instance <= AGC_REG_SPI_SHADER_USER_DATA_HS_31)))
        return AGC_ERROR_VALIDATION_FAILED;
    *base_vertex_location = base;
    *start_instance_location = instance;
    *draw_index_present = draw_index;
    return AGC_OK;
}

static int32_t agcCommandIndirectModifier(uint32_t base_vertex_location,
    uint32_t start_instance_location, uint64_t *modifier)
{
    uint32_t register_base;
    uint64_t value;

    if (base_vertex_location >= AGC_REG_SPI_SHADER_USER_DATA_GS_0 &&
        base_vertex_location <= AGC_REG_SPI_SHADER_USER_DATA_GS_31 &&
        start_instance_location >= AGC_REG_SPI_SHADER_USER_DATA_GS_0 &&
        start_instance_location <= AGC_REG_SPI_SHADER_USER_DATA_GS_31) {
        register_base = AGC_REG_SPI_SHADER_USER_DATA_GS_0;
        value = 0u;
    } else if (base_vertex_location >= AGC_REG_SPI_SHADER_USER_DATA_HS_0 &&
               base_vertex_location <= AGC_REG_SPI_SHADER_USER_DATA_HS_31 &&
               start_instance_location >=
                   AGC_REG_SPI_SHADER_USER_DATA_HS_0 &&
               start_instance_location <=
                   AGC_REG_SPI_SHADER_USER_DATA_HS_31) {
        register_base = AGC_REG_SPI_SHADER_USER_DATA_HS_0;
        value = UINT64_C(3) << 29u;
    } else {
        return AGC_ERROR_VALIDATION_FAILED;
    }
    value |= UINT64_C(1) | (UINT64_C(1) << 2u);
    value |= (uint64_t)(base_vertex_location - register_base) << 9u;
    value |= (uint64_t)(start_instance_location - register_base) << 19u;
    *modifier = value;
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

int32_t PS5_SYSV_ABI agcCmdSetViewportScissors(
    AgcCommandBuffer command_buffer, uint32_t count,
    const AgcViewport *viewports, const AgcScissor *scissors)
{
    uint32_t words[
        AGC_GFX1013_VIEWPORT_ARRAY_DWORDS(AGC_RUNTIME_MAX_VIEWPORTS)];
    AgcGfx1013ViewportArrayState state;
    SceAgcCb scratch;
    uint32_t i;
    int32_t result;

    result = agcCommandDynamicStateValid(
        command_buffer, AGC_DYNAMIC_STATE_VIEWPORT_BIT);
    if (result == AGC_OK)
        result = agcCommandDynamicStateValid(
            command_buffer, AGC_DYNAMIC_STATE_SCISSOR_BIT);
    if (result != AGC_OK)
        return result;
    if (count == 0u || count > AGC_RUNTIME_MAX_VIEWPORTS ||
        !viewports || !scissors)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset(&state, 0, sizeof(state));
    state.count = count;
    for (i = 0u; i < count; ++i) {
        const AgcViewport *viewport = &viewports[i];
        const AgcScissor *scissor = &scissors[i];
        if (!agcHeaderValid(viewport->struct_size, sizeof(*viewport),
                viewport->version) ||
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
            viewport->min_depth > viewport->max_depth ||
            !agcHeaderValid(scissor->struct_size, sizeof(*scissor),
                scissor->version) ||
            !agcReservedZero(scissor->reserved, 2u) ||
            scissor->x < 0 || scissor->y < 0 ||
            scissor->width == 0u || scissor->height == 0u ||
            (uint32_t)scissor->x > 0x7fffu - scissor->width ||
            (uint32_t)scissor->y > 0x7fffu - scissor->height)
            return AGC_ERROR_INVALID_ARGUMENT;
        state.viewports[i] = (AgcGfx1013Viewport){
            viewport->x, viewport->y, viewport->width, viewport->height,
            viewport->min_depth, viewport->max_depth};
        state.scissors[i] = (AgcGfx1013ScissorState){
            (uint32_t)scissor->x, (uint32_t)scissor->y,
            (uint32_t)scissor->x + scissor->width,
            (uint32_t)scissor->y + scissor->height};
    }
    agcCbInit(&scratch, words, sizeof(words));
    result = agcGfx1013SetViewportArray(&scratch, &state);
    if (result == AGC_OK)
        result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result == AGC_OK)
        command_buffer->dynamic_state_set_mask |=
            AGC_DYNAMIC_STATE_VIEWPORT_BIT | AGC_DYNAMIC_STATE_SCISSOR_BIT;
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

int32_t PS5_SYSV_ABI agcCmdSetLineWidth(
    AgcCommandBuffer command_buffer, float line_width)
{
    uint32_t words[AGC_GFX1013_PRIMITIVE_SIZE_STATE_DWORDS];
    AgcGfx1013PrimitiveSizeState state = {
        1.0f, 1.0f, 64.0f, line_width};
    SceAgcCb scratch;
    int32_t result = agcCommandDynamicStateValid(
        command_buffer, AGC_DYNAMIC_STATE_LINE_WIDTH_BIT);

    if (result != AGC_OK)
        return result;
    if (!agcRuntimeFloatFinite(line_width) ||
        line_width < 1.0f || line_width > 64.0f)
        return AGC_ERROR_INVALID_ARGUMENT;
    agcCbInit(&scratch, words, sizeof(words));
    result = agcGfx1013SetPrimitiveSizeState(&scratch, &state);
    if (result != AGC_OK)
        return result;
    result = agcCommandCommitScratch(command_buffer, &scratch, words);
    if (result == AGC_OK)
        command_buffer->dynamic_state_set_mask |=
            AGC_DYNAMIC_STATE_LINE_WIDTH_BIT;
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
    {
        AgcResourceUsage usage;
        AgcResourceOwner owner;

        if (!agcCommandBufferRangeState(command_buffer, buffer, offset,
                buffer->size - offset, &usage, &owner) ||
            usage != kAgcResourceUsageShaderRead ||
            owner != kAgcResourceOwnerGraphics)
            return AGC_ERROR_INVALID_STATE;
    }
    if (!command_buffer->index_buffer) {
        command_buffer->index_buffer = buffer;
        buffer->recorded_refs++;
    }
    command_buffer->index_offset = offset;
    command_buffer->index_size = index_size;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdDraw(AgcCommandBuffer command_buffer,
    uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
    uint32_t first_instance)
{
    uint32_t temporary[512];
    SceAgcCb temporary_cb;
    uint32_t dword_count;
    uint32_t *destination;
    AgcShader vertex_stage;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics ||
        !command_buffer->graphics_pipeline)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcCmdDraw",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "draw requires a Recording graphics command buffer with a pipeline bound");
    if (vertex_count == 0u || instance_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->graphics_pipeline->color_attachment_count != 0u &&
        command_buffer->color_target_count !=
            command_buffer->graphics_pipeline->color_attachment_count)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
            AGC_ERROR_RESOURCE_NOT_BOUND, "agcCmdDraw",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "draw is missing one or more pipeline color attachments");
    if (command_buffer->graphics_pipeline->depth_stencil.format !=
            AGC_FORMAT_UNDEFINED && !command_buffer->depth_stencil_target)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
            AGC_ERROR_RESOURCE_NOT_BOUND, "agcCmdDraw",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "draw is missing the pipeline depth/stencil attachment");
    if (command_buffer->graphics_pipeline->hull_shader &&
        vertex_count % command_buffer->graphics_pipeline->
            tessellation_input_control_points != 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    if (!command_buffer->graphics_pipeline->hull_shader &&
        command_buffer->graphics_pipeline->geometry_input_vertices != 0u &&
        !agcPipelineGeometryIndexCountValid(
            command_buffer->graphics_pipeline->primitive_topology,
            vertex_count))
        return AGC_ERROR_VALIDATION_FAILED;
    vertex_stage = command_buffer->graphics_pipeline->hull_shader ?
        command_buffer->graphics_pipeline->hull_shader :
        command_buffer->graphics_pipeline->primitive_shader;
    if ((first_vertex != 0u &&
         (vertex_stage->reflection.system_sgpr_mask &
          AGC_SHADER_SYSTEM_SGPR_BASE_VERTEX_BIT) == 0u) ||
        (first_instance != 0u &&
         (vertex_stage->reflection.system_sgpr_mask &
          AGC_SHADER_SYSTEM_SGPR_START_INSTANCE_BIT) == 0u))
        return AGC_ERROR_NOT_SUPPORTED;
    if (command_buffer->graphics_pipeline->hull_shader) {
        result = agcCommandShaderResourcesReady(command_buffer,
            command_buffer->graphics_pipeline->hull_shader);
        if (result != AGC_OK)
            return result;
    }
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
    agcCbInit(&temporary_cb, temporary, sizeof(temporary));
    if (command_buffer->graphics_pipeline->hull_shader) {
        result = agcCommandEmitGraphicsUserData(&temporary_cb, command_buffer,
            command_buffer->graphics_pipeline->hull_shader,
            (int32_t)first_vertex, first_instance, 0u);
        if (result != AGC_OK)
            return result;
    }
    result = agcCommandEmitGraphicsUserData(&temporary_cb, command_buffer,
        command_buffer->graphics_pipeline->primitive_shader,
        (int32_t)first_vertex, first_instance, 0u);
    if (result != AGC_OK)
        return result;
    result = agcCommandEmitGraphicsUserData(&temporary_cb, command_buffer,
        command_buffer->graphics_pipeline->pixel_shader,
        (int32_t)first_vertex, first_instance, 0u);
    if (result != AGC_OK)
        return result;
    (void)sceAgcDcbSetNumInstances(&temporary_cb, instance_count);
    (void)sceAgcDcbDrawIndexAuto(&temporary_cb, vertex_count, 0x40000000u);
    dword_count = (uint32_t)agcCbUsedDwords(&temporary_cb);
    if (agcCbRemainingDwords(&command_buffer->cursor) < dword_count)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED, "agcCmdDraw",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has insufficient dwords for the draw");
    destination = agcCbAllocDwords(&command_buffer->cursor, dword_count);
    if (!destination)
        return AGC_ERROR_INTERNAL;
    memcpy(destination, temporary, dword_count * sizeof(uint32_t));
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
    AgcShader vertex_stage;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics ||
        !command_buffer->graphics_pipeline || !command_buffer->index_buffer)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcCmdDrawIndexed",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "indexed draw requires a Recording graphics command buffer with pipeline and index buffer bound");
    if (index_count == 0u || instance_count == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->graphics_pipeline->color_attachment_count != 0u &&
        command_buffer->color_target_count !=
            command_buffer->graphics_pipeline->color_attachment_count) {
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
            AGC_ERROR_RESOURCE_NOT_BOUND, "agcCmdDrawIndexed",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "indexed draw is missing one or more pipeline color attachments");
    }
    if (command_buffer->graphics_pipeline->depth_stencil.format !=
            AGC_FORMAT_UNDEFINED && !command_buffer->depth_stencil_target) {
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT,
            AGC_ERROR_RESOURCE_NOT_BOUND, "agcCmdDrawIndexed",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "indexed draw is missing the pipeline depth/stencil attachment");
    }
    if (command_buffer->graphics_pipeline->hull_shader &&
        index_count % command_buffer->graphics_pipeline->
            tessellation_input_control_points != 0u)
        return AGC_ERROR_VALIDATION_FAILED;
    if (!command_buffer->graphics_pipeline->hull_shader &&
        !command_buffer->graphics_pipeline->primitive_restart_enable &&
        command_buffer->graphics_pipeline->geometry_input_vertices != 0u &&
        !agcPipelineGeometryIndexCountValid(
            command_buffer->graphics_pipeline->primitive_topology,
            index_count))
        return AGC_ERROR_VALIDATION_FAILED;
    vertex_stage = command_buffer->graphics_pipeline->hull_shader ?
        command_buffer->graphics_pipeline->hull_shader :
        command_buffer->graphics_pipeline->primitive_shader;
    if ((vertex_offset != 0 &&
        (vertex_stage->reflection.
          system_sgpr_mask & AGC_SHADER_SYSTEM_SGPR_BASE_VERTEX_BIT) == 0u) ||
        (first_instance != 0u &&
         (vertex_stage->reflection.
          system_sgpr_mask &
          AGC_SHADER_SYSTEM_SGPR_START_INSTANCE_BIT) == 0u))
        return AGC_ERROR_NOT_SUPPORTED;
    if (command_buffer->graphics_pipeline->hull_shader) {
        result = agcCommandShaderResourcesReady(command_buffer,
            command_buffer->graphics_pipeline->hull_shader);
        if (result != AGC_OK)
            return result;
    }
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
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_RESOURCE_INVALID, "agcCmdDrawIndexed",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "indexed draw byte range overruns the bound index buffer");
    }
    agcCbInit(&temporary_cb, temporary, sizeof(temporary));
    if (command_buffer->graphics_pipeline->primitive_restart_enable) {
        const AgcRegisterValue restart_index = {
            AGC_REG_VGT_MULTI_PRIM_IB_RESET_INDX,
            command_buffer->index_size == kAgcIndexSize16 ?
                UINT16_MAX : UINT32_MAX,
        };
        if (!sceAgcCbSetCxRegistersDirect(
                &temporary_cb, &restart_index, 1u))
            return AGC_ERROR_INTERNAL;
    }
    if (command_buffer->graphics_pipeline->hull_shader) {
        result = agcCommandEmitGraphicsUserData(&temporary_cb, command_buffer,
            command_buffer->graphics_pipeline->hull_shader,
            vertex_offset, first_instance, 0u);
        if (result != AGC_OK)
            return result;
    }
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
        agcBufferGpuAddress(command_buffer->index_buffer) +
            command_buffer->index_offset + byte_offset,
        0u);
    dword_count = (uint32_t)agcCbUsedDwords(&temporary_cb);
    if (agcCbRemainingDwords(&command_buffer->cursor) < dword_count)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED, "agcCmdDrawIndexed",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has insufficient dwords for the indexed draw");
    destination = agcCbAllocDwords(&command_buffer->cursor, dword_count);
    if (!destination)
        return AGC_ERROR_INTERNAL;
    memcpy(destination, temporary, dword_count * sizeof(uint32_t));
    return AGC_OK;
}

static int32_t agcCommandValidateIndirectGraphics(
    AgcCommandBuffer command_buffer, AgcBuffer argument_buffer,
    uint64_t offset, uint32_t draw_count, uint32_t stride, uint32_t indexed,
    uint32_t *draw_index_present, uint64_t *modifier)
{
    const uint32_t record_size = indexed ? 20u : 16u;
    AgcShader vertex_stage;
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    uint64_t final_offset;
    uint64_t accessed_size;
    uint32_t base_vertex_location;
    uint32_t start_instance_location;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !argument_buffer ||
        argument_buffer->magic != AGC_MAGIC_BUFFER ||
        argument_buffer->device != command_buffer->device ||
        argument_buffer->deferred ||
        (argument_buffer->usage & AGC_BUFFER_USAGE_INDIRECT_BIT) == 0u ||
        draw_count == 0u || (offset & 3u) != 0u ||
        stride < record_size || (stride & 3u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        command_buffer->queue_type != kAgcQueueGraphics ||
        !command_buffer->graphics_pipeline ||
        (indexed && !command_buffer->index_buffer))
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->graphics_pipeline->color_attachment_count != 0u &&
        command_buffer->color_target_count !=
            command_buffer->graphics_pipeline->color_attachment_count)
        return AGC_ERROR_RESOURCE_NOT_BOUND;
    if (command_buffer->graphics_pipeline->depth_stencil.format !=
            AGC_FORMAT_UNDEFINED && !command_buffer->depth_stencil_target)
        return AGC_ERROR_RESOURCE_NOT_BOUND;
    if ((command_buffer->dynamic_state_set_mask &
         command_buffer->graphics_pipeline->dynamic_state_mask) !=
        command_buffer->graphics_pipeline->dynamic_state_mask)
        return AGC_ERROR_INVALID_STATE;
    final_offset = offset;
    if (draw_count > 1u) {
        const uint64_t extra_count = (uint64_t)draw_count - 1u;
        if (extra_count > (UINT64_MAX - final_offset) / stride)
            return AGC_ERROR_RESOURCE_INVALID;
        final_offset += extra_count * stride;
    }
    if (final_offset > argument_buffer->size ||
        record_size > argument_buffer->size - final_offset)
        return AGC_ERROR_RESOURCE_INVALID;
    accessed_size = final_offset - offset + record_size;
    if (!agcCommandBufferRangeState(command_buffer, argument_buffer,
            offset, accessed_size, &usage, &owner) ||
        usage != kAgcResourceUsageShaderRead ||
        owner != kAgcResourceOwnerGraphics)
        return AGC_ERROR_INVALID_STATE;
    if (!agcCommandCanRetainBuffer(command_buffer, argument_buffer))
        return AGC_ERROR_OUT_OF_MEMORY;
    if (command_buffer->graphics_pipeline->hull_shader) {
        result = agcCommandShaderResourcesReady(command_buffer,
            command_buffer->graphics_pipeline->hull_shader);
        if (result != AGC_OK)
            return result;
    }
    result = agcCommandShaderResourcesReady(command_buffer,
        command_buffer->graphics_pipeline->primitive_shader);
    if (result != AGC_OK)
        return result;
    result = agcCommandShaderResourcesReady(command_buffer,
        command_buffer->graphics_pipeline->pixel_shader);
    if (result != AGC_OK)
        return result;
    vertex_stage = command_buffer->graphics_pipeline->hull_shader ?
        command_buffer->graphics_pipeline->hull_shader :
        command_buffer->graphics_pipeline->primitive_shader;
    result = agcCommandIndirectLocations(vertex_stage,
        &base_vertex_location, &start_instance_location,
        draw_index_present);
    if (result != AGC_OK)
        return result;
    return agcCommandIndirectModifier(base_vertex_location,
        start_instance_location, modifier);
}

static int32_t agcCommandEmitIndirectGraphicsPacket(SceAgcCb *cb,
    AgcCommandBuffer command_buffer, uint64_t argument_address,
    uint32_t draw_count, uint32_t stride, uint32_t draw_index,
    uint32_t indexed, uint64_t modifier)
{
    AgcGraphicsPipeline pipeline = command_buffer->graphics_pipeline;
    uint64_t base_address = argument_address & ~UINT64_C(7);
    uint32_t data_offset = (uint32_t)(argument_address & UINT64_C(7));
    int32_t result;

    if (pipeline->hull_shader) {
        result = agcCommandEmitGraphicsIndirectUserData(cb, command_buffer,
            pipeline->hull_shader, draw_index);
        if (result != AGC_OK)
            return result;
    }
    result = agcCommandEmitGraphicsIndirectUserData(cb, command_buffer,
        pipeline->primitive_shader, draw_index);
    if (result != AGC_OK)
        return result;
    result = agcCommandEmitGraphicsIndirectUserData(cb, command_buffer,
        pipeline->pixel_shader, draw_index);
    if (result != AGC_OK)
        return result;
    if (indexed) {
        uint64_t element_size = command_buffer->index_size ==
            kAgcIndexSize16 ? 2u : 4u;
        uint64_t available = command_buffer->index_buffer->size -
            command_buffer->index_offset;
        if (available / element_size == 0u ||
            available / element_size > UINT32_MAX)
            return AGC_ERROR_RESOURCE_INVALID;
        if (pipeline->primitive_restart_enable) {
            const AgcRegisterValue restart_index = {
                AGC_REG_VGT_MULTI_PRIM_IB_RESET_INDX,
                command_buffer->index_size == kAgcIndexSize16 ?
                    UINT16_MAX : UINT32_MAX,
            };
            if (!sceAgcCbSetCxRegistersDirect(cb, &restart_index, 1u))
                return AGC_ERROR_BUFFER_TOO_SMALL;
        }
        if (!sceAgcDcbSetIndexSize(cb, command_buffer->index_size, 0u) ||
            !sceAgcDcbSetIndexBuffer(cb,
                agcBufferGpuAddress(command_buffer->index_buffer) +
                    command_buffer->index_offset,
                (uint32_t)(available / element_size)))
            return AGC_ERROR_BUFFER_TOO_SMALL;
    }
    if (!sceAgcDcbSetBaseIndirectArgs(cb, 0u, base_address))
        return AGC_ERROR_BUFFER_TOO_SMALL;
    if (!(indexed ? sceAgcDcbDrawIndexIndirectMulti(cb, data_offset, 0u,
              draw_count, NULL, stride, modifier) :
          sceAgcDcbDrawIndirectMulti(cb, data_offset, 0u, draw_count,
              NULL, stride, modifier)))
        return AGC_ERROR_BUFFER_TOO_SMALL;
    return AGC_OK;
}

static int32_t agcCommandDrawIndirectCommon(
    AgcCommandBuffer command_buffer, AgcBuffer argument_buffer,
    uint64_t offset, uint32_t draw_count, uint32_t stride, uint32_t indexed)
{
    uint32_t sizing_words[512];
    SceAgcCb sizing_cb;
    uint32_t draw_index_present;
    uint32_t packet_count;
    uint32_t per_packet_dwords;
    uint64_t required_dwords;
    uint64_t argument_address;
    uint64_t modifier;
    uint32_t i;
    int32_t result;

    result = agcCommandValidateIndirectGraphics(command_buffer,
        argument_buffer, offset, draw_count, stride, indexed,
        &draw_index_present, &modifier);
    if (result != AGC_OK)
        return result;
    argument_address = agcBufferGpuAddress(argument_buffer) + offset;
    packet_count = draw_index_present ? draw_count : 1u;
    agcCbInit(&sizing_cb, sizing_words, sizeof(sizing_words));
    result = agcCommandEmitIndirectGraphicsPacket(&sizing_cb, command_buffer,
        argument_address, draw_index_present ? 1u : draw_count, stride,
        0u, indexed, modifier);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    per_packet_dwords = agcCbUsedDwords(&sizing_cb);
    required_dwords = (uint64_t)per_packet_dwords * packet_count;
    /* The recovered multi-draw builder writes ten dwords but requires a
     * 16-dword reservation at each emission point. Preserve that encoder
     * contract internally without exposing it through the native API. */
    if (required_dwords > UINT32_MAX ||
        agcCbRemainingDwords(&command_buffer->cursor) < required_dwords + 6u)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    for (i = 0u; i < packet_count; ++i) {
        uint64_t address = argument_address;
        if (draw_index_present)
            address += (uint64_t)i * stride;
        result = agcCommandEmitIndirectGraphicsPacket(
            &command_buffer->cursor, command_buffer, address,
            draw_index_present ? 1u : draw_count, stride, i, indexed,
            modifier);
        if (result != AGC_OK)
            return AGC_ERROR_INTERNAL;
    }
    if (!agcCommandRetainBuffer(command_buffer, argument_buffer))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdDrawIndirect(AgcCommandBuffer command_buffer,
    AgcBuffer argument_buffer, uint64_t offset, uint32_t draw_count,
    uint32_t stride)
{
    return agcCommandDrawIndirectCommon(command_buffer, argument_buffer,
        offset, draw_count, stride, 0u);
}

int32_t PS5_SYSV_ABI agcCmdDrawIndexedIndirect(
    AgcCommandBuffer command_buffer, AgcBuffer argument_buffer,
    uint64_t offset, uint32_t draw_count, uint32_t stride)
{
    return agcCommandDrawIndirectCommon(command_buffer, argument_buffer,
        offset, draw_count, stride, 1u);
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
        (command_buffer->queue_type != kAgcQueueGraphics &&
         command_buffer->queue_type != kAgcQueueCompute) ||
        !command_buffer->compute_pipeline)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcCmdDispatch",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "dispatch requires a Recording graphics/compute command buffer with a bound compute pipeline");
    if (group_count_x == 0u || group_count_y == 0u || group_count_z == 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    pipeline = command_buffer->compute_pipeline;
    shader = pipeline->shader;
    result = agcCommandShaderResourcesReady(command_buffer, shader);
    if (result != AGC_OK)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT, result,
            "agcCmdDispatch", AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "dispatch is missing reflected descriptors, push constants, or required resource transitions");
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
    /* FW 5.50's qualified compute path establishes the V8 SH defaults before
     * programming a dispatch. Keep that policy inside the native runtime so
     * applications never need to assemble default-state register packets. */
    result = agcGfx1013ApplyComputeDefaultsV8(&temporary_cb, NULL);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    result = agcGfx1013DispatchCompute(&temporary_cb, &state);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    dword_count = (uint32_t)agcCbUsedDwords(&temporary_cb);
    if (agcCbRemainingDwords(&command_buffer->cursor) < dword_count)
        return agcDebugReport(command_buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED, "agcCmdDispatch",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has insufficient dwords for the compute dispatch");
    destination = agcCbAllocDwords(&command_buffer->cursor, dword_count);
    if (!destination)
        return AGC_ERROR_INTERNAL;
    memcpy(destination, temporary, dword_count * sizeof(uint32_t));
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdDispatchIndirect(
    AgcCommandBuffer command_buffer, AgcBuffer argument_buffer,
    uint64_t offset)
{
    uint32_t temporary[512];
    uint32_t user_data[16];
    AgcRegisterValue static_sh[UINT8_MAX];
    AgcShaderRecord record;
    SceAgcCb temporary_cb;
    AgcComputePipeline pipeline;
    AgcShader shader;
    AgcGfx1013ComputeState state = {0};
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    uint64_t argument_address;
    uint32_t dword_count;
    uint32_t *destination;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !agcDeviceValid(command_buffer->device) || !argument_buffer ||
        argument_buffer->magic != AGC_MAGIC_BUFFER ||
        argument_buffer->device != command_buffer->device ||
        argument_buffer->deferred ||
        (argument_buffer->usage & AGC_BUFFER_USAGE_INDIRECT_BIT) == 0u ||
        (offset & 3u) != 0u || offset > argument_buffer->size ||
        12u > argument_buffer->size - offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING ||
        (command_buffer->queue_type != kAgcQueueGraphics &&
         command_buffer->queue_type != kAgcQueueCompute) ||
        !command_buffer->compute_pipeline)
        return AGC_ERROR_INVALID_STATE;
    if (!agcCommandBufferRangeState(command_buffer, argument_buffer,
            offset, 12u, &usage, &owner) ||
        usage != kAgcResourceUsageShaderRead ||
        owner != agcRuntimeCommandOwner(command_buffer))
        return AGC_ERROR_INVALID_STATE;
    if (!agcCommandCanRetainBuffer(command_buffer, argument_buffer))
        return AGC_ERROR_OUT_OF_MEMORY;
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
    /* The common compute validator requires nonzero direct dimensions even
     * though the indirect packet replaces them at execution time. */
    state.group_count_x = 1u;
    state.group_count_y = 1u;
    state.group_count_z = 1u;
    state.modifier = shader->reflection.wave_size == 32u ?
        AGC_GFX1013_COMPUTE_DISPATCH_WAVE32 :
        AGC_GFX1013_COMPUTE_DISPATCH_WAVE64;
    argument_address = agcBufferGpuAddress(argument_buffer) + offset;
    agcCbInit(&temporary_cb, temporary, sizeof(temporary));
    result = agcGfx1013ApplyComputeDefaultsV8(&temporary_cb, NULL);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    result = agcGfx1013DispatchComputeIndirect(&temporary_cb, &state,
        argument_address & ~UINT64_C(7),
        (uint32_t)(argument_address & UINT64_C(7)));
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
    if (!agcCommandRetainBuffer(command_buffer, argument_buffer))
        return AGC_ERROR_INTERNAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateGpuLabel(
    AgcDevice device, const AgcGpuLabelDesc *desc, AgcGpuLabel *label_out)
{
    AgcGpuLabel label;
    uint32_t *value;
    int32_t result;

    if (!label_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    *label_out = NULL;
    if (!agcDeviceValid(device) || !desc ||
        !agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        desc->flags != 0u || !agcReservedZero(desc->reserved, 4u))
        return AGC_ERROR_INVALID_ARGUMENT;
    label = agcCreateChild(device, sizeof(*label));
    if (!label)
        return AGC_ERROR_OUT_OF_MEMORY;
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        sizeof(*value), 8u, 0u, AGC_OBJECT_TYPE_COUNT, label,
        &label->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, label);
        return result;
    }
    value = agcAllocationCpuAddress(label->allocation);
    *value = desc->initial_value;
    result = agcFlushRuntimeAllocation(label->allocation, 0u,
        sizeof(*value));
    if (result != AGC_OK) {
        agcRuntimeFree(device, label->allocation);
        agcDestroyChild(device, label);
        return result;
    }
    label->magic = AGC_MAGIC_GPU_LABEL;
    label->device = device;
    label->last_signal_value = desc->initial_value;
    label->last_signal_queue_type = UINT32_MAX;
    label->last_wait_result = AGC_ERROR_BUSY;
    agcCaptureRecordObjectCreate(device, label,
        AGC_CAPTURE_OBJECT_GPU_LABEL, desc->initial_value, 0u);
    *label_out = label;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcDestroyGpuLabel(AgcGpuLabel label)
{
    AgcDevice device;

    if (!label || label->magic != AGC_MAGIC_GPU_LABEL ||
        !agcDeviceValid(label->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (label->recorded_refs != 0u)
        return AGC_ERROR_BUSY;
    device = label->device;
    if (label->last_signal_queue)
        label->last_signal_queue->label_refs--;
    agcRuntimeFree(device, label->allocation);
    agcCaptureRecordObjectDestroy(device, label,
        AGC_CAPTURE_OBJECT_GPU_LABEL);
    label->magic = 0u;
    agcDestroyChild(device, label);
    return AGC_OK;
}

static int32_t agcGpuLabelObservedValue(AgcGpuLabel label,
    uint32_t *observed)
{
#ifdef OPENAGC_PROSPERO
    int32_t result;

    result = agcGpuMemoryInvalidate(&label->allocation->block->memory,
        (size_t)label->allocation->offset, sizeof(*observed));
    if (result != AGC_OK)
        return result;
#endif
    *observed = *(const volatile uint32_t *)
        agcAllocationCpuAddress(label->allocation);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetGpuLabelInfo(
    AgcGpuLabel label, AgcGpuLabelInfo *info)
{
    AgcGpuLabelInfo snapshot = AGC_GPU_LABEL_INFO_INIT;
    uint32_t observed;
    uint32_t v1;
    int32_t result;

    if (!label || label->magic != AGC_MAGIC_GPU_LABEL ||
        !agcDeviceValid(label->device) || !info)
        return AGC_ERROR_INVALID_ARGUMENT;
    v1 = info->version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
        info->struct_size == AGC_GPU_LABEL_INFO_V1_SIZE;
    if ((!v1 && (info->version != AGC_RUNTIME_STRUCTURE_VERSION_2 ||
            info->struct_size != sizeof(*info))) || info->reserved0 != 0u ||
        !agcReservedZero(info->reserved, 2u) ||
        (!v1 && !agcReservedZero(info->reserved_v2, 2u)))
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcGpuLabelObservedValue(label, &observed);
    if (result != AGC_OK)
        return result;
    snapshot.scheduled_value = label->last_signal_value;
    snapshot.observed_value = observed;
    snapshot.queue_type = label->last_signal_queue_type;
    snapshot.last_signal_submission_id = label->last_signal_submission_id;
    snapshot.firmware_abi_key =
        label->device->runtime_info.firmware_abi_key;
    snapshot.hardware_family = label->device->runtime_info.hardware_family;
    snapshot.last_wait_value = label->last_wait_value;
    snapshot.last_wait_result = label->last_wait_result;
    snapshot.timeout_count = label->timeout_count;
    snapshot.last_timeout_ns = label->last_timeout_ns;
    memcpy(snapshot.profile_name, label->device->runtime_info.profile_name,
        sizeof(snapshot.profile_name));
    if (v1) {
        snapshot.struct_size = AGC_GPU_LABEL_INFO_V1_SIZE;
        snapshot.version = AGC_RUNTIME_STRUCTURE_VERSION_1;
        memcpy(info, &snapshot, AGC_GPU_LABEL_INFO_V1_SIZE);
    } else {
        *info = snapshot;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetGpuLabelStatus(
    AgcGpuLabel label, uint32_t value)
{
    uint32_t observed;
    int32_t result;

    if (!label || label->magic != AGC_MAGIC_GPU_LABEL ||
        !agcDeviceValid(label->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (value > label->last_signal_value)
        return AGC_ERROR_INVALID_STATE;
    result = agcGpuLabelObservedValue(label, &observed);
    if (result != AGC_OK)
        return result;
    return observed >= value ? AGC_OK : AGC_ERROR_BUSY;
}

int32_t PS5_SYSV_ABI agcWaitGpuLabel(
    AgcGpuLabel label, uint32_t value, uint64_t timeout_ns)
{
    int32_t result;

    if (!label || label->magic != AGC_MAGIC_GPU_LABEL ||
        !agcDeviceValid(label->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (timeout_ns == AGC_RUNTIME_INFINITE_TIMEOUT)
        return AGC_ERROR_INVALID_ARGUMENT;
    label->last_wait_value = value;
    result = agcGetGpuLabelStatus(label, value);
    if (result != AGC_ERROR_BUSY) {
        label->last_wait_result = result;
        return result;
    }
#ifdef OPENAGC_PROSPERO
    {
        uint64_t timeout_microseconds = timeout_ns / 1000u;

        if (timeout_ns % 1000u != 0u)
            timeout_microseconds++;
        if (timeout_microseconds > UINT32_MAX)
            timeout_microseconds = UINT32_MAX;
        result = agcGpuMemoryWait32(&label->allocation->block->memory,
            (size_t)label->allocation->offset, label->last_signal_value,
            (uint32_t)timeout_microseconds);
        if (result == AGC_OK)
            result = agcGetGpuLabelStatus(label, value);
        if (result == AGC_ERROR_BUSY)
            result = AGC_ERROR_TIMEOUT;
    }
#else
    (void)timeout_ns;
    result = AGC_ERROR_TIMEOUT;
#endif
    label->last_wait_result = result;
    if (result == AGC_ERROR_TIMEOUT) {
        label->timeout_count++;
        label->last_timeout_ns = timeout_ns;
    }
    return result;
}

int32_t PS5_SYSV_ABI agcCmdWaitGpuLabel(
    AgcCommandBuffer command_buffer, AgcGpuLabel label, uint32_t value)
{
    uint64_t address;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !label || label->magic != AGC_MAGIC_GPU_LABEL ||
        !agcDeviceValid(command_buffer->device) ||
        label->device != command_buffer->device)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING)
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->recorded_label_wait_count >=
            AGC_RUNTIME_MAX_RECORDED_TRANSITIONS ||
        agcCbRemainingDwords(&command_buffer->cursor) < 7u)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    if (!agcCommandRetainGpuLabel(command_buffer, label))
        return AGC_ERROR_OUT_OF_MEMORY;
    address = agcAllocationGpuAddress(label->allocation);
    if (!sceAgcDcbWaitRegMem(&command_buffer->cursor, 0u,
            AGC_RUNTIME_WAIT_COMPARE_GREATER_EQUAL, 0u, 0u,
            address, value, UINT32_MAX, UINT32_MAX))
        return AGC_ERROR_INTERNAL;
    command_buffer->recorded_label_waits[
        command_buffer->recorded_label_wait_count++] =
        (AgcRuntimeRecordedLabelWait){ label, value };
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCmdSignalGpuLabel(
    AgcCommandBuffer command_buffer, AgcGpuLabel label, uint32_t value)
{
    AgcGfx1013EopFenceState state;
    uint32_t scheduled_value;
    int32_t result;

    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        !label || label->magic != AGC_MAGIC_GPU_LABEL ||
        !agcDeviceValid(command_buffer->device) ||
        label->device != command_buffer->device)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_RECORDING)
        return AGC_ERROR_INVALID_STATE;
    /* Waits use reached-or-passed comparison. Strictly monotonic signals keep
     * every earlier point satisfied; UINT32_MAX is terminal, not wrapping. */
    scheduled_value = agcCommandLatestLabelSignalValue(command_buffer, label);
    if (value <= scheduled_value)
        return AGC_ERROR_INVALID_STATE;
    if (command_buffer->recorded_label_signal_count >=
            AGC_RUNTIME_MAX_RECORDED_TRANSITIONS ||
        agcCbRemainingDwords(&command_buffer->cursor) <
            AGC_GFX1013_EOP_FENCE_DWORDS)
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    if (!agcCommandRetainGpuLabel(command_buffer, label))
        return AGC_ERROR_OUT_OF_MEMORY;
    state.address = agcAllocationGpuAddress(label->allocation);
    state.value = value;
    result = agcGfx1013SignalEopFence(&command_buffer->cursor, &state);
    if (result != AGC_OK)
        return result == AGC_ERROR_BUFFER_TOO_SMALL ?
            AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
    command_buffer->recorded_label_signals[
        command_buffer->recorded_label_signal_count++] =
        (AgcRuntimeRecordedLabelSignal){ label, value };
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcCreateFence(
    AgcDevice device, const AgcFenceDesc *desc, AgcFence *fence_out)
{
    AgcFence fence;
#ifdef OPENAGC_PROSPERO
    uint32_t *completion;
    int32_t result;
#endif

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
    fence->device = device;
    fence->completion_value = 1u;
    fence->observed_completion_value = desc->signaled ? 1u : 0u;
    fence->last_queue_type = UINT32_MAX;
    fence->last_command_buffer_state = AGC_COMMAND_BUFFER_STATE_INITIAL;
    fence->last_wait_result = desc->signaled ? AGC_OK : AGC_ERROR_BUSY;
#ifdef OPENAGC_PROSPERO
    result = agcRuntimeAllocate(device, AGC_MEMORY_HEAP_FLEXIBLE,
        sizeof(*completion), sizeof(*completion), 0u,
        AGC_OBJECT_TYPE_COUNT, fence, &fence->allocation);
    if (result != AGC_OK) {
        agcDestroyChild(device, fence);
        return result;
    }
    completion = agcAllocationCpuAddress(fence->allocation);
    *completion = desc->signaled ? 1u : 0u;
    result = agcFlushRuntimeAllocation(fence->allocation, 0u,
        sizeof(*completion));
    if (result != AGC_OK) {
        agcRuntimeFree(device, fence->allocation);
        agcDestroyChild(device, fence);
        return result;
    }
#endif
    fence->magic = AGC_MAGIC_FENCE;
    fence->signaled = desc->signaled;
    agcCaptureRecordObjectCreate(device, fence, AGC_CAPTURE_OBJECT_FENCE,
        desc->signaled, 0u);
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
    if (fence->allocation)
        agcRuntimeFree(device, fence->allocation);
    agcCaptureRecordObjectDestroy(device, fence, AGC_CAPTURE_OBJECT_FENCE);
    fence->magic = 0u;
    agcDestroyChild(device, fence);
    return AGC_OK;
}

static int32_t agcFencePollCompletion(AgcFence fence)
{
#ifdef OPENAGC_PROSPERO
    uint32_t completion;
    int32_t result;
#endif

    if (fence->signaled)
        return AGC_OK;
#ifdef OPENAGC_PROSPERO
    if (!fence->allocation)
        return AGC_ERROR_INTERNAL;
    result = agcGpuMemoryInvalidate(&fence->allocation->block->memory,
        (size_t)fence->allocation->offset, sizeof(completion));
    if (result != AGC_OK)
        return result;
    completion = *(const volatile uint32_t *)
        agcAllocationCpuAddress(fence->allocation);
    fence->observed_completion_value = completion;
    if (completion != fence->completion_value)
        return AGC_ERROR_BUSY;
#else
    return AGC_ERROR_BUSY;
#endif
    fence->signaled = 1u;
    if (fence->pending_command_buffer_count != 0u) {
        AgcQueue queue = fence->queue;
        uint32_t i;

        for (i = 0u; i < fence->pending_command_buffer_count; ++i) {
            AgcCommandBuffer command_buffer =
                fence->pending_command_buffers[i];

            command_buffer->pending_refs--;
            command_buffer->state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
            command_buffer->completion_fence = NULL;
            queue->pending_count--;
        }
        queue->last_completed_submission_id = fence->submission_id;
        fence->pending_refs--;
        fence->pending_command_buffer_count = 0u;
        fence->queue = NULL;
        fence->last_completed_submission_id = fence->submission_id;
        fence->last_command_buffer_state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetFenceStatus(AgcFence fence)
{
    int32_t result;

    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    result = agcFencePollCompletion(fence);
    return result == AGC_ERROR_BUSY ? AGC_ERROR_BUSY : result;
}

int32_t PS5_SYSV_ABI agcGetFenceInfo(AgcFence fence, AgcFenceInfo *info)
{
    int32_t result;

    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device) || !info ||
        !agcHeaderValid(info->struct_size, sizeof(*info), info->version) ||
        !agcReservedZero(info->reserved, 3u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    result = agcFencePollCompletion(fence);
    if (result != AGC_OK && result != AGC_ERROR_BUSY)
        return result;
    *info = (AgcFenceInfo)AGC_FENCE_INFO_INIT;
    info->state = fence->signaled ? AGC_FENCE_STATE_SIGNALED :
        fence->pending_refs != 0u ? AGC_FENCE_STATE_PENDING :
        AGC_FENCE_STATE_UNSIGNALED;
    info->queue_type = fence->last_queue_type;
    info->command_buffer_state = fence->last_command_buffer_state;
    info->completion_value = fence->completion_value;
    info->observed_completion_value = fence->observed_completion_value;
    info->last_wait_result = fence->last_wait_result;
    info->firmware_abi_key = fence->device->runtime_info.firmware_abi_key;
    info->hardware_family = fence->device->runtime_info.hardware_family;
    info->submission_id = fence->submission_id;
    info->last_completed_submission_id =
        fence->last_completed_submission_id;
    info->timeout_count = fence->timeout_count;
    info->last_timeout_ns = fence->last_timeout_ns;
    memcpy(info->profile_name, fence->device->runtime_info.profile_name,
        sizeof(info->profile_name));
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcResetFence(AgcFence fence)
{
    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (fence->pending_refs != 0u)
        return AGC_ERROR_BUSY;
#ifdef OPENAGC_PROSPERO
    if (fence->allocation) {
        uint32_t *completion = agcAllocationCpuAddress(fence->allocation);
        int32_t result;

        *completion = 0u;
        result = agcFlushRuntimeAllocation(fence->allocation, 0u,
            sizeof(*completion));
        if (result != AGC_OK)
            return result;
    }
#endif
    fence->signaled = 0u;
    fence->observed_completion_value = 0u;
    fence->last_wait_result = AGC_ERROR_BUSY;
    fence->last_command_buffer_state = AGC_COMMAND_BUFFER_STATE_INITIAL;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcWaitFence(AgcFence fence, uint64_t timeout_ns)
{
    int32_t result;

    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (timeout_ns == AGC_RUNTIME_INFINITE_TIMEOUT)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcFencePollCompletion(fence);
    if (result != AGC_ERROR_BUSY) {
        fence->last_wait_result = result;
        agcCaptureRecordFenceResult(fence, result, timeout_ns);
        return result;
    }
#ifdef OPENAGC_PROSPERO
    if (fence->allocation) {
        uint64_t timeout_microseconds = timeout_ns / 1000u;

        if (timeout_ns % 1000u != 0u)
            timeout_microseconds++;
        if (timeout_microseconds > UINT32_MAX)
            timeout_microseconds = UINT32_MAX;
        result = agcGpuMemoryWait32(&fence->allocation->block->memory,
            (size_t)fence->allocation->offset, fence->completion_value,
            (uint32_t)timeout_microseconds);
        if (result != AGC_OK) {
            fence->last_wait_result = result;
            if (result == AGC_ERROR_TIMEOUT) {
                fence->timeout_count++;
                fence->last_timeout_ns = timeout_ns;
            }
            agcCaptureRecordFenceResult(fence, result, timeout_ns);
            return result;
        }
        result = agcFencePollCompletion(fence);
        if (result == AGC_ERROR_BUSY)
            result = AGC_ERROR_TIMEOUT;
        fence->last_wait_result = result;
        if (result == AGC_ERROR_TIMEOUT) {
            fence->timeout_count++;
            fence->last_timeout_ns = timeout_ns;
        }
        agcCaptureRecordFenceResult(fence, result, timeout_ns);
        return result;
    }
    agcCaptureRecordFenceResult(fence, AGC_ERROR_INTERNAL, timeout_ns);
    return AGC_ERROR_INTERNAL;
#else
    fence->last_wait_result = AGC_ERROR_TIMEOUT;
    fence->timeout_count++;
    fence->last_timeout_ns = timeout_ns;
    agcCaptureRecordFenceResult(fence, AGC_ERROR_TIMEOUT, timeout_ns);
    return AGC_ERROR_TIMEOUT;
#endif
}

int32_t PS5_SYSV_ABI agcRecycleCommandBuffers(AgcFence fence,
    uint32_t command_buffer_count,
    AgcCommandBuffer const *command_buffers)
{
    uint32_t i;
    int32_t result;

    if (!fence || fence->magic != AGC_MAGIC_FENCE ||
        !agcDeviceValid(fence->device) || !command_buffers ||
        command_buffer_count == 0u ||
        command_buffer_count > AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (i = 0u; i < command_buffer_count; ++i) {
        AgcCommandBuffer command_buffer = command_buffers[i];
        uint32_t j;

        if (!command_buffer ||
            command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
            command_buffer->device != fence->device)
            return AGC_ERROR_INVALID_ARGUMENT;
        if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_EXECUTABLE &&
            command_buffer->state != AGC_COMMAND_BUFFER_STATE_PENDING)
            return AGC_ERROR_INVALID_STATE;
        if (command_buffer->state == AGC_COMMAND_BUFFER_STATE_PENDING &&
            command_buffer->completion_fence != fence)
            return AGC_ERROR_INVALID_ARGUMENT;
        for (j = 0u; j < i; ++j)
            if (command_buffers[j] == command_buffer)
                return AGC_ERROR_INVALID_ARGUMENT;
    }
    result = agcFencePollCompletion(fence);
    if (result != AGC_OK)
        return result;
    for (i = 0u; i < command_buffer_count; ++i) {
        AgcCommandBuffer command_buffer = command_buffers[i];
        if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_EXECUTABLE ||
            command_buffer->pending_refs != 0u)
            return AGC_ERROR_INVALID_STATE;
    }
    for (i = 0u; i < command_buffer_count; ++i) {
        AgcCommandBuffer command_buffer = command_buffers[i];
        agcReleaseCommandReferences(command_buffer);
        agcCbReset(&command_buffer->cursor, command_buffer->storage,
            (size_t)command_buffer->capacity_dwords * sizeof(uint32_t));
        command_buffer->state = AGC_COMMAND_BUFFER_STATE_INITIAL;
    }
    return AGC_OK;
}

static int32_t agcQueueSubmitBatch(
    AgcQueue queue, const AgcSubmitInfo *submit_info, AgcFence fence)
{
    AgcCommandBuffer command_buffers[AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS];
    void *addresses[AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS];
    uint32_t sizes[AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS];
    uint32_t count = submit_info->command_buffer_count;
    AgcCommandBuffer first;
    AgcCommandBuffer last;
    uint32_t first_label_count = 0u;
    uintptr_t first_cursor = 0u;
    uintptr_t last_cursor = 0u;
    uint32_t *first_snapshot = NULL;
    uint32_t *last_snapshot = NULL;
    size_t first_snapshot_size = 0u;
    size_t last_snapshot_size = 0u;
    uint32_t has_lists = submit_info->version ==
        AGC_RUNTIME_STRUCTURE_VERSION_2 &&
        (submit_info->wait_count != 0u || submit_info->signal_count != 0u);
    uint32_t i;
    int32_t result;
#ifdef OPENAGC_PROSPERO
    AgcGfx1013EopFenceState completion;
#endif

    if ((queue->type != kAgcQueueGraphics && queue->type != kAgcQueueCompute) ||
        count < 2u ||
        count > AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS || !fence ||
        fence->pending_command_buffer_count != 0u)
        return AGC_ERROR_NOT_SUPPORTED;
#ifdef OPENAGC_PROSPERO
    if (!fence->allocation)
        return AGC_ERROR_NOT_SUPPORTED;
#endif
    for (i = 0u; i < count; ++i) {
        AgcCommandBuffer command_buffer = submit_info->command_buffers[i];
        uint32_t j;

        if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
            command_buffer->device != queue->device ||
            command_buffer->queue_type != queue->type ||
            command_buffer->state != AGC_COMMAND_BUFFER_STATE_EXECUTABLE ||
            agcCbUsedDwords(&command_buffer->cursor) == 0u)
            return AGC_ERROR_INVALID_ARGUMENT;
        result = agcValidateCommandLabelWaits(queue, command_buffer);
        if (result != AGC_OK)
            return result;
        for (j = 0u; j < i; ++j) {
            if (command_buffers[j] == command_buffer)
                return AGC_ERROR_INVALID_ARGUMENT;
        }
        command_buffers[i] = command_buffer;
    }
    first = command_buffers[0];
    last = command_buffers[count - 1u];
    result = agcValidateBatchLabelSignalOrder(command_buffers, count);
    if (result != AGC_OK)
        return result;
    result = agcValidateSubmissionTransitions(command_buffers, count);
    if (result != AGC_OK)
        return result;
    result = agcValidateSubmitLabelLists(queue, last, submit_info);
    if (result != AGC_OK)
        return result;
    if (has_lists) {
        uint64_t first_extra = (uint64_t)submit_info->wait_count * 7u;
        uint64_t last_extra = (uint64_t)submit_info->signal_count *
            AGC_GFX1013_EOP_FENCE_DWORDS;

#ifdef OPENAGC_PROSPERO
        last_extra += AGC_GFX1013_EOP_FENCE_DWORDS;
#endif
        if (first_extra > UINT32_MAX - agcCbUsedDwords(&first->cursor) ||
            last_extra > UINT32_MAX - agcCbUsedDwords(&last->cursor) ||
            first_extra + agcCbUsedDwords(&first->cursor) >
                first->capacity_dwords ||
            last_extra + agcCbUsedDwords(&last->cursor) >
                last->capacity_dwords)
            return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
        for (i = 0u; i < count; ++i) {
            uint32_t j;
            for (j = 0u; j < submit_info->signal_count; ++j) {
                uint32_t k;
                for (k = 0u; k < command_buffers[i]->recorded_label_signal_count;
                     ++k) {
                    if (command_buffers[i]->recorded_label_signals[k].label ==
                        submit_info->signals[j].label)
                        return AGC_ERROR_INVALID_STATE;
                }
            }
        }
        first_snapshot_size = (size_t)first->capacity_dwords *
            sizeof(*first_snapshot);
        last_snapshot_size = (size_t)last->capacity_dwords *
            sizeof(*last_snapshot);
        first_snapshot = agcAlloc(queue->device, first_snapshot_size,
            sizeof(uint32_t));
        last_snapshot = agcAlloc(queue->device, last_snapshot_size,
            sizeof(uint32_t));
        if (!first_snapshot || !last_snapshot) {
            agcFree(queue->device, first_snapshot);
            agcFree(queue->device, last_snapshot);
            return AGC_ERROR_OUT_OF_MEMORY;
        }
        memcpy(first_snapshot, first->storage, first_snapshot_size);
        memcpy(last_snapshot, last->storage, last_snapshot_size);
        first_cursor = first->cursor.cursor_up;
        last_cursor = last->cursor.cursor_up;
        first_label_count = first->recorded_label_count;
        result = agcRetainSubmitLabels(first, submit_info);
        if (result != AGC_OK)
            goto rollback_batch_lists;
        result = agcInjectSubmitLabelWaits(first, submit_info);
        if (result != AGC_OK)
            goto rollback_batch_lists;
        result = agcInjectSubmitLabelSignals(last, submit_info);
        if (result != AGC_OK)
            goto rollback_batch_lists;
    }
#ifdef OPENAGC_PROSPERO
    *(uint32_t *)agcAllocationCpuAddress(fence->allocation) = 0u;
    result = agcFlushRuntimeAllocation(fence->allocation, 0u,
        sizeof(uint32_t));
    if (result != AGC_OK)
        return result;
    completion.address = agcAllocationGpuAddress(fence->allocation);
    completion.value = fence->completion_value;
    if (!has_lists)
        last_cursor = last->cursor.cursor_up;
    result = agcGfx1013SignalEopFence(
        &last->cursor, &completion);
    if (result != AGC_OK)
        goto rollback_batch_lists;
#endif
    for (i = 0u; i < count; ++i) {
        uint64_t command_size = (uint64_t)
            agcCbUsedDwords(&command_buffers[i]->cursor) * sizeof(uint32_t);

        result = agcFlushRuntimeAllocation(command_buffers[i]->allocation,
            0u, command_size);
        if (result != AGC_OK) {
            if (has_lists) {
                memcpy(first->storage, first_snapshot, first_snapshot_size);
                memcpy(last->storage, last_snapshot, last_snapshot_size);
                first->cursor.cursor_up = first_cursor;
                last->cursor.cursor_up = last_cursor;
                agcReleaseSubmitLabels(first, first_label_count);
            } else {
                last->cursor.cursor_up = last_cursor;
            }
            agcFree(queue->device, first_snapshot);
            agcFree(queue->device, last_snapshot);
            return result;
        }
        addresses[i] = (void *)(uintptr_t)agcAllocationGpuAddress(
            command_buffers[i]->allocation);
        sizes[i] = (uint32_t)command_size;
    }
    for (i = 0u; i < count; ++i) {
        command_buffers[i]->state = AGC_COMMAND_BUFFER_STATE_PENDING;
        command_buffers[i]->pending_refs++;
#ifdef OPENAGC_PROSPERO
        command_buffers[i]->completion_fence = fence;
#endif
        queue->pending_count++;
    }
    fence->pending_refs++;
    result = sceAgcDriverSubmitMultiCommandBuffersDirect(
        count, addresses, sizes, NULL, NULL);
    if (result == AGC_ERROR_NOT_INITIALIZED)
        result = AGC_ERROR_DEVICE_LOST;
    if (result == AGC_OK) {
        uint64_t submission_id = ++queue->next_submission_id;

        for (i = 0u; i < count; ++i) {
            agcCommitCommandTransitions(command_buffers[i]);
            agcCommitCommandLabelSignals(queue, command_buffers[i],
                submission_id);
        }
        if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_2)
            agcCommitSubmitLabelSignals(queue, submit_info->signals,
                submit_info->signal_count, submission_id);
        fence->submission_id = submission_id;
        fence->last_queue_type = (uint32_t)queue->type;
        fence->last_command_buffer_state = AGC_COMMAND_BUFFER_STATE_PENDING;
        fence->observed_completion_value = 0u;
        fence->last_wait_result = AGC_ERROR_BUSY;
    }
#ifdef OPENAGC_PROSPERO
    if (result == AGC_OK) {
        fence->pending_command_buffer_count = count;
        for (i = 0u; i < count; ++i)
            fence->pending_command_buffers[i] = command_buffers[i];
        fence->queue = queue;
        agcFree(queue->device, first_snapshot);
        agcFree(queue->device, last_snapshot);
        return AGC_OK;
    }
    if (has_lists) {
        memcpy(first->storage, first_snapshot, first_snapshot_size);
        memcpy(last->storage, last_snapshot, last_snapshot_size);
        first->cursor.cursor_up = first_cursor;
        last->cursor.cursor_up = last_cursor;
        agcReleaseSubmitLabels(first, first_label_count);
    } else {
        last->cursor.cursor_up = last_cursor;
    }
#endif
#ifndef OPENAGC_PROSPERO
    if (result != AGC_OK && has_lists) {
        memcpy(first->storage, first_snapshot, first_snapshot_size);
        memcpy(last->storage, last_snapshot, last_snapshot_size);
        first->cursor.cursor_up = first_cursor;
        last->cursor.cursor_up = last_cursor;
        agcReleaseSubmitLabels(first, first_label_count);
    }
#endif
    for (i = 0u; i < count; ++i) {
        command_buffers[i]->pending_refs--;
        command_buffers[i]->state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
#ifdef OPENAGC_PROSPERO
        command_buffers[i]->completion_fence = NULL;
#endif
        queue->pending_count--;
    }
    fence->pending_refs--;
    if (result == AGC_OK) {
        fence->signaled = 1u;
        fence->observed_completion_value = fence->completion_value;
        fence->last_completed_submission_id = fence->submission_id;
        fence->last_command_buffer_state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
        queue->last_completed_submission_id = queue->next_submission_id;
    }
    agcFree(queue->device, first_snapshot);
    agcFree(queue->device, last_snapshot);
    return result;

rollback_batch_lists:
    if (has_lists) {
        memcpy(first->storage, first_snapshot, first_snapshot_size);
        memcpy(last->storage, last_snapshot, last_snapshot_size);
        first->cursor.cursor_up = first_cursor;
        last->cursor.cursor_up = last_cursor;
        agcReleaseSubmitLabels(first, first_label_count);
    }
    agcFree(queue->device, first_snapshot);
    agcFree(queue->device, last_snapshot);
    return result == AGC_ERROR_BUFFER_TOO_SMALL ?
        AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
}

int32_t PS5_SYSV_ABI agcQueueSubmit(
    AgcQueue queue, const AgcSubmitInfo *submit_info, AgcFence fence)
{
    AgcCommandBuffer command_buffer;
    AgcCommandBufferSubmit packet;
    int32_t result;
    uint32_t original_label_count = 0u;
    uintptr_t original_cursor = 0u;
    uint32_t *snapshot = NULL;
    size_t snapshot_size = 0u;
#ifdef OPENAGC_PROSPERO
    AgcGfx1013EopFenceState completion;
#endif

    if (!queue || queue->magic != AGC_MAGIC_QUEUE ||
        !agcDeviceValid(queue->device)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (!agcSubmitInfoValid(submit_info) ||
        submit_info->command_buffer_count == 0u ||
        submit_info->command_buffer_count >
            AGC_RUNTIME_MAX_SUBMIT_COMMAND_BUFFERS ||
        !submit_info->command_buffers) {
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcQueueSubmit",
            AGC_DEBUG_OBJECT_TYPE_NONE, NULL,
            "submission descriptor is invalid or has no command buffers");
    }
    if (fence && (fence->magic != AGC_MAGIC_FENCE ||
            fence->device != queue->device)) {
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcQueueSubmit",
            AGC_DEBUG_OBJECT_TYPE_NONE, NULL,
            "submission fence does not belong to the queue device");
    }
    if (fence && fence->signaled)
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_SYNCHRONIZATION_BIT,
            AGC_ERROR_INVALID_STATE, "agcQueueSubmit",
            AGC_DEBUG_OBJECT_TYPE_NONE, NULL,
            "submission fence must be reset before reuse");
    if (queue->next_submission_id == UINT64_MAX)
        return AGC_ERROR_NOT_SUPPORTED;
    if (submit_info->command_buffer_count > 1u) {
        uint32_t i;
        result = agcQueueSubmitBatch(queue, submit_info, fence);
        if (result == AGC_OK) {
            for (i = 0u; i < submit_info->command_buffer_count; ++i) {
                agcCaptureRecordCommandWords(
                    submit_info->command_buffers[i],
                    AGC_CAPTURE_RECORD_COMMAND_STREAM);
            }
        }
        agcCaptureRecordSubmission(queue, submit_info, fence, result,
            result == AGC_OK ? queue->next_submission_id : 0u);
        return result;
    }
    command_buffer = submit_info->command_buffers[0];
    if (!command_buffer || command_buffer->magic != AGC_MAGIC_COMMAND_BUFFER ||
        command_buffer->device != queue->device ||
        command_buffer->queue_type != queue->type) {
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcQueueSubmit",
            AGC_DEBUG_OBJECT_TYPE_NONE, NULL,
            "command buffer is invalid or belongs to a different device or queue type");
    }
    if (command_buffer->state != AGC_COMMAND_BUFFER_STATE_EXECUTABLE)
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT,
            AGC_ERROR_INVALID_STATE, "agcQueueSubmit",
            AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "submitted command buffer must be Executable");
    result = agcValidateCommandLabelWaits(queue, command_buffer);
    if (result != AGC_OK)
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_SYNCHRONIZATION_BIT, result,
            "agcQueueSubmit", AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer has an unsatisfied GPU-label wait");
    result = agcValidateBatchLabelSignalOrder(&command_buffer, 1u);
    if (result != AGC_OK)
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_SYNCHRONIZATION_BIT, result,
            "agcQueueSubmit", AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "command buffer contains a stale or decreasing GPU-label signal");
    result = agcValidateSubmissionTransitions(&command_buffer, 1u);
    if (result != AGC_OK)
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT, result,
            "agcQueueSubmit", AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "recorded resource transitions do not match committed state");
    result = agcValidateSubmitLabelLists(queue, command_buffer, submit_info);
    if (result != AGC_OK)
        return agcDebugReport(queue->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            AGC_DEBUG_MESSAGE_CATEGORY_SYNCHRONIZATION_BIT, result,
            "agcQueueSubmit", AGC_OBJECT_TYPE_COMMAND_BUFFER,
            command_buffer->allocation->debug_name,
            "submission wait or signal list is invalid");
    if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_2 &&
        (submit_info->wait_count != 0u || submit_info->signal_count != 0u)) {
        uint64_t required_dwords = (uint64_t)submit_info->wait_count * 7u +
            (uint64_t)submit_info->signal_count * AGC_GFX1013_EOP_FENCE_DWORDS;

#ifdef OPENAGC_PROSPERO
        required_dwords += AGC_GFX1013_EOP_FENCE_DWORDS;
#endif
        if (required_dwords > UINT32_MAX -
                agcCbUsedDwords(&command_buffer->cursor) ||
            required_dwords + agcCbUsedDwords(&command_buffer->cursor) >
                command_buffer->capacity_dwords)
            return agcDebugReport(queue->device,
                AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
                AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT,
                AGC_ERROR_COMMAND_SPACE_EXHAUSTED, "agcQueueSubmit",
                AGC_OBJECT_TYPE_COMMAND_BUFFER,
                command_buffer->allocation->debug_name,
                "command buffer lacks space for runtime submission packets");
        snapshot_size = (size_t)command_buffer->capacity_dwords *
            sizeof(*snapshot);
        snapshot = agcAlloc(queue->device, snapshot_size, sizeof(uint32_t));
        if (!snapshot)
            return AGC_ERROR_OUT_OF_MEMORY;
        memcpy(snapshot, command_buffer->storage, snapshot_size);
        original_cursor = command_buffer->cursor.cursor_up;
        original_label_count = command_buffer->recorded_label_count;
        result = agcRetainSubmitLabels(command_buffer, submit_info);
        if (result != AGC_OK)
            goto rollback_submit_lists;
        result = agcInjectSubmitLabelLists(command_buffer, submit_info);
        if (result != AGC_OK)
            goto rollback_submit_lists;
    }
#ifndef OPENAGC_PROSPERO
    /* The generic carrier rejects a zero-dword packet. Preserve the native
     * fence-only submission contract with a runtime-owned no-op carrier. */
    if (agcCbUsedDwords(&command_buffer->cursor) == 0u &&
        !sceAgcCbNop(&command_buffer->cursor, 2u)) {
        return AGC_ERROR_COMMAND_SPACE_EXHAUSTED;
    }
#endif
#ifdef OPENAGC_PROSPERO
    /* Native submission needs an observable GPU completion point so recorded
     * resources cannot be reset or freed while the kernel-owned queue runs. */
    if (!fence)
        return AGC_ERROR_NOT_SUPPORTED;
    if (fence->pending_command_buffer_count != 0u ||
        command_buffer->completion_fence ||
        !fence->allocation)
        return AGC_ERROR_INVALID_STATE;
    *(uint32_t *)agcAllocationCpuAddress(fence->allocation) = 0u;
    result = agcFlushRuntimeAllocation(fence->allocation, 0u,
        sizeof(uint32_t));
    if (result != AGC_OK)
        return result;
    completion.address = agcAllocationGpuAddress(fence->allocation);
    completion.value = fence->completion_value;
    if (!snapshot)
        original_cursor = command_buffer->cursor.cursor_up;
    result = agcGfx1013SignalEopFence(&command_buffer->cursor, &completion);
    if (result != AGC_OK)
        goto rollback_submit_lists;
    command_buffer->completion_fence = fence;
#endif
    {
        uint64_t command_size = (uint64_t)
            agcCbUsedDwords(&command_buffer->cursor) * sizeof(uint32_t);
        int32_t flush_result = agcFlushRuntimeAllocation(
            command_buffer->allocation, 0u, command_size);
        if (flush_result != AGC_OK) {
            if (snapshot) {
                memcpy(command_buffer->storage, snapshot, snapshot_size);
                agcReleaseSubmitLabels(command_buffer, original_label_count);
            }
            command_buffer->cursor.cursor_up = original_cursor;
#ifdef OPENAGC_PROSPERO
            command_buffer->completion_fence = NULL;
#endif
            agcFree(queue->device, snapshot);
            return flush_result;
        }
    }

    packet.command_address = (uintptr_t)agcAllocationGpuAddress(
        command_buffer->allocation);
    packet.dword_count = agcCbUsedDwords(&command_buffer->cursor);
    packet.reserved = 0u;
    agcCaptureRecordCommandWords(command_buffer,
        AGC_CAPTURE_RECORD_COMMAND_STREAM);
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_PENDING;
    command_buffer->pending_refs++;
    queue->pending_count++;
    if (fence)
        fence->pending_refs++;
    if (queue->type == kAgcQueueCompute) {
#ifdef OPENAGC_PROSPERO
        /* Direct DCB compute submission is hardware-proven on FW 5.50.
         * Keep the host backend on its ACB queue path for carrier coverage. */
        result = sceAgcDriverSubmitDcb(&packet);
#else
        result = sceAgcDriverSubmitAcb((uint32_t)queue->backend_handle, &packet);
#endif
    } else {
        result = sceAgcDriverSubmitDcb(&packet);
    }
    if (result == AGC_ERROR_NOT_INITIALIZED)
        result = AGC_ERROR_DEVICE_LOST;
    if (result == AGC_OK) {
        uint64_t submission_id = ++queue->next_submission_id;

        agcCommitCommandTransitions(command_buffer);
        agcCommitCommandLabelSignals(queue, command_buffer, submission_id);
        if (submit_info->version == AGC_RUNTIME_STRUCTURE_VERSION_2)
            agcCommitSubmitLabelSignals(queue, submit_info->signals,
                submit_info->signal_count, submission_id);
        if (fence) {
            fence->submission_id = submission_id;
            fence->last_queue_type = (uint32_t)queue->type;
            fence->last_command_buffer_state =
                AGC_COMMAND_BUFFER_STATE_PENDING;
            fence->observed_completion_value = 0u;
            fence->last_wait_result = AGC_ERROR_BUSY;
        }
    }
    agcCaptureRecordSubmission(queue, submit_info, fence, result,
        result == AGC_OK ? queue->next_submission_id : 0u);
#ifdef OPENAGC_PROSPERO
    if (result == AGC_OK) {
        fence->pending_command_buffer_count = 1u;
        fence->pending_command_buffers[0] = command_buffer;
        fence->queue = queue;
        agcFree(queue->device, snapshot);
        return AGC_OK;
    }
    if (snapshot) {
        memcpy(command_buffer->storage, snapshot, snapshot_size);
        agcReleaseSubmitLabels(command_buffer, original_label_count);
    }
    command_buffer->cursor.cursor_up = original_cursor;
    command_buffer->completion_fence = NULL;
#endif
#ifndef OPENAGC_PROSPERO
    if (result != AGC_OK && snapshot) {
        memcpy(command_buffer->storage, snapshot, snapshot_size);
        command_buffer->cursor.cursor_up = original_cursor;
        agcReleaseSubmitLabels(command_buffer, original_label_count);
    }
#endif
    command_buffer->pending_refs--;
    command_buffer->state = AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
    queue->pending_count--;
    if (fence) {
        fence->pending_refs--;
        if (result == AGC_OK) {
            fence->signaled = 1u;
            fence->observed_completion_value = fence->completion_value;
            fence->last_completed_submission_id = fence->submission_id;
            fence->last_command_buffer_state =
                AGC_COMMAND_BUFFER_STATE_EXECUTABLE;
        }
    }
    if (result == AGC_OK)
        queue->last_completed_submission_id = queue->next_submission_id;
    agcFree(queue->device, snapshot);
    return result;

rollback_submit_lists:
    if (snapshot) {
        memcpy(command_buffer->storage, snapshot, snapshot_size);
        command_buffer->cursor.cursor_up = original_cursor;
        agcReleaseSubmitLabels(command_buffer, original_label_count);
    }
#ifdef OPENAGC_PROSPERO
    command_buffer->completion_fence = NULL;
#endif
    agcFree(queue->device, snapshot);
    return result == AGC_ERROR_BUFFER_TOO_SMALL ?
        AGC_ERROR_COMMAND_SPACE_EXHAUSTED : result;
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
    case AGC_OBJECT_TYPE_MEMORY: {
        const AgcMemory memory = (AgcMemory)object;
        return memory->magic == AGC_MAGIC_MEMORY && !memory->released &&
            memory->device == device ?
            memory->allocation : NULL;
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
    {
        uint32_t capture_type = agcCaptureObjectTypeFromRuntime(type);
        if (capture_type != UINT32_MAX)
            agcCaptureRecordObjectName(device, object, capture_type, name);
    }
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcSetDebugCallback(AgcDevice device,
    const AgcDebugCallbackDesc *desc)
{
    if (!agcDeviceValid(device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!desc) {
        device->debug_callback = NULL;
        device->debug_user_data = NULL;
        device->debug_severity_mask = 0u;
        device->debug_category_mask = 0u;
        return AGC_OK;
    }
    if (!agcHeaderValid(desc->struct_size, sizeof(*desc), desc->version) ||
        !desc->callback || desc->severity_mask == 0u ||
        (desc->severity_mask & ~AGC_DEBUG_MESSAGE_SEVERITY_ALL) != 0u ||
        desc->category_mask == 0u ||
        (desc->category_mask & ~AGC_DEBUG_MESSAGE_CATEGORY_ALL) != 0u ||
        !agcReservedZero(desc->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    device->debug_callback = desc->callback;
    device->debug_user_data = desc->user_data;
    device->debug_severity_mask = desc->severity_mask;
    device->debug_category_mask = desc->category_mask;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetLastDebugMessage(AgcDevice device,
    AgcDebugMessage *message)
{
    if (!agcDeviceValid(device) || !message ||
        !agcHeaderValid(message->struct_size, sizeof(*message),
            message->version) || !agcReservedZero(message->reserved, 4u)) {
        return AGC_ERROR_INVALID_ARGUMENT;
    }
    if (!device->has_debug_message)
        return AGC_ERROR_NOT_FOUND;
    *message = device->last_debug_message;
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
        !agcDeviceValid(buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (buffer->deferred || !data || size == 0u || offset > buffer->size ||
        size > buffer->size - offset ||
        (buffer->create_flags & AGC_BUFFER_CREATE_UPLOAD_BIT) == 0u)
        return agcDebugReport(buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            buffer->deferred ? AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT :
                AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcWriteBuffer",
            AGC_OBJECT_TYPE_BUFFER, buffer->allocation->debug_name,
            "buffer upload requires a live upload buffer and a nonempty in-range byte interval");
    memcpy((uint8_t *)buffer->storage + offset, data, (size_t)size);
    return agcFlushRuntimeAllocation(buffer->allocation,
        buffer->memory_offset + offset, size);
}

int32_t PS5_SYSV_ABI agcReadBuffer(
    AgcBuffer buffer, uint64_t offset, void *data, uint64_t size)
{
    int32_t result;

    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (buffer->deferred || !data || size == 0u || offset > buffer->size ||
        size > buffer->size - offset ||
        (buffer->create_flags & AGC_BUFFER_CREATE_READBACK_BIT) == 0u)
        return agcDebugReport(buffer->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            buffer->deferred ? AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT :
                AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcReadBuffer",
            AGC_OBJECT_TYPE_BUFFER, buffer->allocation->debug_name,
            "buffer readback requires a live readback buffer and a nonempty in-range byte interval");
    result = agcInvalidateRuntimeAllocation(buffer->allocation,
        buffer->memory_offset + offset, size);
    if (result == AGC_OK)
        memcpy(data, (uint8_t *)buffer->storage + offset, (size_t)size);
    return result;
}

int32_t PS5_SYSV_ABI agcGetOcclusionQueryLayout(
    AgcDevice device, AgcOcclusionQueryLayout *layout)
{
    if (!agcDeviceValid(device) || !layout ||
        !agcHeaderValid(layout->struct_size, sizeof(*layout),
            layout->version) || !agcReservedZero(layout->reserved, 5u))
        return AGC_ERROR_INVALID_ARGUMENT;
    layout->record_size = AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE;
    layout->alignment = 8u;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcResetOcclusionQueryResults(AgcBuffer buffer,
    uint64_t offset, uint32_t query_count)
{
    uint64_t size;
    int32_t result;

    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device) || buffer->deferred ||
        (buffer->usage & AGC_BUFFER_USAGE_QUERY_BIT) == 0u ||
        (buffer->create_flags & AGC_BUFFER_CREATE_UPLOAD_BIT) == 0u ||
        query_count == 0u || (offset & 7u) != 0u)
        return AGC_ERROR_INVALID_ARGUMENT;
    size = (uint64_t)query_count * AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE;
    if (offset > buffer->size || size > buffer->size - offset)
        return AGC_ERROR_INVALID_ARGUMENT;
    result = agcBufferEnsureStateCapacity(buffer,
        buffer->state_range_count + 2u);
    if (result != AGC_OK)
        return result;
    memset((uint8_t *)buffer->storage + offset, 0, (size_t)size);
    result = agcFlushRuntimeAllocation(buffer->allocation,
        buffer->memory_offset + offset, size);
    if (result != AGC_OK)
        return result;
    agcBufferCommitRangeState(buffer, offset, size,
        kAgcResourceUsageHostWrite, kAgcResourceOwnerHost);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI agcGetOcclusionQueryResult(AgcBuffer buffer,
    uint64_t offset, uint64_t timeout_ns, AgcOcclusionQueryResult *result)
{
    uint32_t available;
    uint64_t value = 0u;
    uint32_t rb;
    int32_t status;

    if (!buffer || buffer->magic != AGC_MAGIC_BUFFER ||
        !agcDeviceValid(buffer->device) || buffer->deferred || !result ||
        !agcHeaderValid(result->struct_size, sizeof(*result),
            result->version) || result->reserved0 != 0u ||
        !agcReservedZero(result->reserved, 5u) ||
        (buffer->usage & AGC_BUFFER_USAGE_QUERY_BIT) == 0u ||
        (buffer->create_flags & AGC_BUFFER_CREATE_READBACK_BIT) == 0u ||
        (offset & 7u) != 0u || offset > buffer->size ||
        AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE > buffer->size - offset ||
        timeout_ns == AGC_RUNTIME_INFINITE_TIMEOUT)
        return AGC_ERROR_INVALID_ARGUMENT;
    status = agcBufferEnsureStateCapacity(buffer,
        buffer->state_range_count + 2u);
    if (status != AGC_OK)
        return status;
    result->value = 0u;
    result->available = 0u;
    status = agcInvalidateRuntimeAllocation(buffer->allocation,
        buffer->memory_offset + offset,
        AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE);
    if (status != AGC_OK)
        return status;
    memcpy(&available, (uint8_t *)buffer->storage + offset +
        AGC_GFX1013_OCCLUSION_QUERY_STRIDE, sizeof(available));
    if (available != 1u && timeout_ns != 0u) {
#ifdef OPENAGC_PROSPERO
        uint64_t timeout_microseconds = timeout_ns / 1000u;
        size_t wait_offset;

        if (timeout_ns % 1000u != 0u)
            timeout_microseconds++;
        if (timeout_microseconds > UINT32_MAX)
            timeout_microseconds = UINT32_MAX;
        wait_offset = (size_t)(buffer->allocation->offset +
            buffer->memory_offset + offset +
            AGC_GFX1013_OCCLUSION_QUERY_STRIDE);
        status = agcGpuMemoryWait32(&buffer->allocation->block->memory,
            wait_offset, 1u, (uint32_t)timeout_microseconds);
        if (status != AGC_OK)
            return status;
        status = agcInvalidateRuntimeAllocation(buffer->allocation,
            buffer->memory_offset + offset,
            AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE);
        if (status != AGC_OK)
            return status;
        memcpy(&available, (uint8_t *)buffer->storage + offset +
            AGC_GFX1013_OCCLUSION_QUERY_STRIDE, sizeof(available));
#else
        (void)timeout_ns;
        return AGC_ERROR_TIMEOUT;
#endif
    }
    for (rb = 0u; rb < AGC_GFX1013_OCCLUSION_QUERY_MAX_RBS; ++rb) {
        uint64_t begin;
        uint64_t end;
        memcpy(&begin, (uint8_t *)buffer->storage + offset + rb * 16u,
            sizeof(begin));
        memcpy(&end, (uint8_t *)buffer->storage + offset + rb * 16u + 8u,
            sizeof(end));
        if ((begin >> 63u) != 0u && (end >> 63u) != 0u)
            value += (end & INT64_MAX) - (begin & INT64_MAX);
    }
    result->value = value;
    result->available = available == 1u;
    if (result->available)
        agcBufferCommitRangeState(buffer, offset,
            AGC_RUNTIME_OCCLUSION_QUERY_RECORD_SIZE,
            kAgcResourceUsageHostRead, kAgcResourceOwnerHost);
    return result->available ? AGC_OK : AGC_ERROR_BUSY;
}

int32_t PS5_SYSV_ABI agcWriteImage(
    AgcImage image, uint64_t offset, const void *data, uint64_t size)
{
    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        !agcDeviceValid(image->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (image->deferred || !data || size == 0u ||
        offset > image->layout.allocation_size ||
        size > image->layout.allocation_size - offset ||
        (image->desc.usage & AGC_IMAGE_USAGE_TRANSFER_DST_BIT) == 0u)
        return agcDebugReport(image->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            image->deferred ? AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT :
                AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcWriteImage",
            AGC_OBJECT_TYPE_IMAGE, image->allocation->debug_name,
            "image upload requires a live transfer-destination image and a nonempty in-range byte interval");
    memcpy((uint8_t *)agcImageCpuAddress(image) + offset, data, (size_t)size);
    return agcFlushRuntimeAllocation(image->allocation,
        image->memory_offset + offset, size);
}

int32_t PS5_SYSV_ABI agcReadImage(
    AgcImage image, uint64_t offset, void *data, uint64_t size)
{
    int32_t result;

    if (!image || image->magic != AGC_MAGIC_IMAGE ||
        !agcDeviceValid(image->device))
        return AGC_ERROR_INVALID_ARGUMENT;
    if (image->deferred || !data || size == 0u ||
        offset > image->layout.allocation_size ||
        size > image->layout.allocation_size - offset ||
        (image->desc.usage & AGC_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u)
        return agcDebugReport(image->device,
            AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT,
            image->deferred ? AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT :
                AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT,
            AGC_ERROR_INVALID_ARGUMENT, "agcReadImage",
            AGC_OBJECT_TYPE_IMAGE, image->allocation->debug_name,
            "image readback requires a live transfer-source image and a nonempty in-range byte interval");
    result = agcInvalidateRuntimeAllocation(image->allocation,
        image->memory_offset + offset, size);
    if (result == AGC_OK)
        memcpy(data, (uint8_t *)agcImageCpuAddress(image) + offset,
            (size_t)size);
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
    if (buffer->transfer_count != 0u)
        return AGC_ERROR_BUSY;
    if (fence && fence->signaled && buffer->recorded_refs == 0u)
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
    if (image->transfer_count != 0u)
        return AGC_ERROR_BUSY;
    if (fence && fence->signaled && image->dependency_refs == 0u &&
        image->recorded_refs == 0u)
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
        result = agcFencePollCompletion(entry->fence);
        if (result == AGC_ERROR_BUSY) {
            link = &entry->next;
            continue;
        }
        if (result != AGC_OK)
            return result;
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
