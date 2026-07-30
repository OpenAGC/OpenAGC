/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Firmware-neutral native OpenAGC runtime API for the PS5 GPU. The generic
 * backend is a host validation harness, not a non-PS5 GPU target.
 */

#ifndef OPENAGC_RUNTIME_H
#define OPENAGC_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "agc_error.h"
#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_RUNTIME_API_VERSION 1u
#define AGC_RUNTIME_STRUCTURE_VERSION_1 1u
#define AGC_RUNTIME_PROFILE_NAME_SIZE 48u
#define AGC_RUNTIME_INFINITE_TIMEOUT UINT64_MAX

typedef struct AgcDeviceImpl *AgcDevice;
typedef struct AgcQueueImpl *AgcQueue;
typedef struct AgcBufferImpl *AgcBuffer;
typedef struct AgcImageImpl *AgcImage;
typedef struct AgcImageViewImpl *AgcImageView;
typedef struct AgcSamplerImpl *AgcSampler;
typedef struct AgcShaderImpl *AgcShader;
typedef struct AgcGraphicsPipelineImpl *AgcGraphicsPipeline;
typedef struct AgcComputePipelineImpl *AgcComputePipeline;
typedef struct AgcCommandBufferImpl *AgcCommandBuffer;
typedef struct AgcFenceImpl *AgcFence;

typedef void *(PS5_SYSV_ABI *AgcAllocationFunction)(
    void *user_data, size_t size, size_t alignment);
typedef void (PS5_SYSV_ABI *AgcFreeFunction)(
    void *user_data, void *memory);

typedef struct AgcAllocationCallbacks {
    void *user_data;
    AgcAllocationFunction allocate;
    AgcFreeFunction free;
} AgcAllocationCallbacks;

/* Applications synchronize every device, queue, and child-object call
 * externally. Destroying a parent with live children or recorded references
 * returns AGC_ERROR_BUSY and performs no mutation. */
typedef struct AgcDeviceDesc {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t agc_version;
    uint64_t required_capability_bits;
    const AgcAllocationCallbacks *allocation_callbacks;
    uint64_t reserved[4];
} AgcDeviceDesc;

#define AGC_DEVICE_DESC_INIT \
    { sizeof(AgcDeviceDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 7u, 0u, \
      NULL, {0u, 0u, 0u, 0u} }

typedef enum AgcHardwareFamily {
    AGC_HARDWARE_FAMILY_HOST_TEST = 0,
    AGC_HARDWARE_FAMILY_STANDARD_PS5 = 1,
    AGC_HARDWARE_FAMILY_TRINITY_PS5 = 2
} AgcHardwareFamily;

typedef enum AgcQualificationClass {
    AGC_QUALIFICATION_UNAVAILABLE = 0,
    AGC_QUALIFICATION_HOST_TESTED = 1,
    AGC_QUALIFICATION_PROFILE_QUALIFIED = 2,
    AGC_QUALIFICATION_HARDWARE_QUALIFIED = 3
} AgcQualificationClass;

typedef enum AgcRuntimeCapabilityIndex {
    AGC_RUNTIME_CAP_GRAPHICS_INDEX = 0,
    AGC_RUNTIME_CAP_COMPUTE_INDEX = 1,
    AGC_RUNTIME_CAP_RESOURCE_OBJECTS_INDEX = 2,
    AGC_RUNTIME_CAP_SHADER_OBJECTS_INDEX = 3,
    AGC_RUNTIME_CAP_PIPELINE_OBJECTS_INDEX = 4,
    AGC_RUNTIME_CAP_COMMAND_BUFFERS_INDEX = 5,
    AGC_RUNTIME_CAP_BINARY_FENCES_INDEX = 6,
    AGC_RUNTIME_CAP_ASYNC_COMPUTE_QUEUE_INDEX = 7,
    AGC_RUNTIME_CAPABILITY_COUNT = 8
} AgcRuntimeCapabilityIndex;

#define AGC_RUNTIME_CAP_GRAPHICS \
    (UINT64_C(1) << AGC_RUNTIME_CAP_GRAPHICS_INDEX)
#define AGC_RUNTIME_CAP_COMPUTE \
    (UINT64_C(1) << AGC_RUNTIME_CAP_COMPUTE_INDEX)
#define AGC_RUNTIME_CAP_RESOURCE_OBJECTS \
    (UINT64_C(1) << AGC_RUNTIME_CAP_RESOURCE_OBJECTS_INDEX)
#define AGC_RUNTIME_CAP_SHADER_OBJECTS \
    (UINT64_C(1) << AGC_RUNTIME_CAP_SHADER_OBJECTS_INDEX)
#define AGC_RUNTIME_CAP_PIPELINE_OBJECTS \
    (UINT64_C(1) << AGC_RUNTIME_CAP_PIPELINE_OBJECTS_INDEX)
#define AGC_RUNTIME_CAP_COMMAND_BUFFERS \
    (UINT64_C(1) << AGC_RUNTIME_CAP_COMMAND_BUFFERS_INDEX)
#define AGC_RUNTIME_CAP_BINARY_FENCES \
    (UINT64_C(1) << AGC_RUNTIME_CAP_BINARY_FENCES_INDEX)
#define AGC_RUNTIME_CAP_ASYNC_COMPUTE_QUEUE \
    (UINT64_C(1) << AGC_RUNTIME_CAP_ASYNC_COMPUTE_QUEUE_INDEX)
#define AGC_RUNTIME_CAP_BASELINE \
    (AGC_RUNTIME_CAP_GRAPHICS | AGC_RUNTIME_CAP_COMPUTE | \
     AGC_RUNTIME_CAP_RESOURCE_OBJECTS | AGC_RUNTIME_CAP_SHADER_OBJECTS | \
     AGC_RUNTIME_CAP_PIPELINE_OBJECTS | AGC_RUNTIME_CAP_COMMAND_BUFFERS | \
     AGC_RUNTIME_CAP_BINARY_FENCES | AGC_RUNTIME_CAP_ASYNC_COMPUTE_QUEUE)

typedef struct AgcRuntimeInfo {
    uint32_t struct_size;
    uint32_t version;
    uint32_t runtime_api_version;
    uint32_t firmware_version;
    uint16_t firmware_abi_key;
    uint16_t hardware_family;
    uint32_t agc_version;
    uint64_t capability_bits;
    uint8_t qualification[AGC_RUNTIME_CAPABILITY_COUNT];
    char profile_name[AGC_RUNTIME_PROFILE_NAME_SIZE];
    uint64_t reserved[4];
} AgcRuntimeInfo;

#define AGC_RUNTIME_INFO_INIT \
    { sizeof(AgcRuntimeInfo), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, 0u, \
      0u, 0u, 0u, {0}, {0}, {0u, 0u, 0u, 0u} }

typedef struct AgcQueueDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcQueueType type;
    uint32_t priority;
    uint64_t reserved[4];
} AgcQueueDesc;

#define AGC_QUEUE_DESC_INIT \
    { sizeof(AgcQueueDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      kAgcQueueGraphics, 0u, {0u, 0u, 0u, 0u} }

typedef enum AgcBufferUsageFlagBits {
    AGC_BUFFER_USAGE_VERTEX_BIT = 1u << 0,
    AGC_BUFFER_USAGE_INDEX_BIT = 1u << 1,
    AGC_BUFFER_USAGE_UNIFORM_BIT = 1u << 2,
    AGC_BUFFER_USAGE_STORAGE_BIT = 1u << 3,
    AGC_BUFFER_USAGE_TRANSFER_SRC_BIT = 1u << 4,
    AGC_BUFFER_USAGE_TRANSFER_DST_BIT = 1u << 5
} AgcBufferUsageFlagBits;
typedef uint32_t AgcBufferUsageFlags;

typedef enum AgcBufferCreateFlagBits {
    AGC_BUFFER_CREATE_UPLOAD_BIT = 1u << 0,
    AGC_BUFFER_CREATE_READBACK_BIT = 1u << 1,
    AGC_BUFFER_CREATE_DEDICATED_BIT = 1u << 2
} AgcBufferCreateFlagBits;

typedef struct AgcBufferDesc {
    uint32_t struct_size;
    uint32_t version;
    uint64_t size;
    AgcBufferUsageFlags usage;
    uint32_t flags;
    uint64_t reserved[4];
} AgcBufferDesc;

#define AGC_BUFFER_DESC_INIT \
    { sizeof(AgcBufferDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, 0u, \
      {0u, 0u, 0u, 0u} }

typedef enum AgcImageUsageFlagBits {
    AGC_IMAGE_USAGE_SAMPLED_BIT = 1u << 0,
    AGC_IMAGE_USAGE_STORAGE_BIT = 1u << 1,
    AGC_IMAGE_USAGE_COLOR_TARGET_BIT = 1u << 2,
    AGC_IMAGE_USAGE_DEPTH_STENCIL_BIT = 1u << 3,
    AGC_IMAGE_USAGE_TRANSFER_SRC_BIT = 1u << 4,
    AGC_IMAGE_USAGE_TRANSFER_DST_BIT = 1u << 5,
    AGC_IMAGE_USAGE_SCANOUT_BIT = 1u << 6,
    AGC_IMAGE_USAGE_CUBE_COMPATIBLE_BIT = 1u << 7,
    AGC_IMAGE_USAGE_HTILE_BIT = 1u << 8
} AgcImageUsageFlagBits;
typedef uint32_t AgcImageUsageFlags;

typedef struct AgcImageDesc {
    uint32_t struct_size;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mip_levels;
    uint32_t array_layers;
    uint32_t format;
    uint32_t sample_count;
    AgcImageUsageFlags usage;
    uint64_t reserved[4];
} AgcImageDesc;

#define AGC_IMAGE_DESC_INIT \
    { sizeof(AgcImageDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 1u, 1u, 1u, \
      1u, 1u, 0u, 1u, 0u, {0u, 0u, 0u, 0u} }

typedef struct AgcImageViewDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcImage image;
    uint32_t format;
    uint32_t base_mip_level;
    uint32_t mip_level_count;
    uint32_t base_array_layer;
    uint32_t array_layer_count;
    uint64_t reserved[4];
} AgcImageViewDesc;

#define AGC_IMAGE_VIEW_DESC_INIT \
    { sizeof(AgcImageViewDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, NULL, 0u, \
      0u, 1u, 0u, 1u, {0u, 0u, 0u, 0u} }

typedef enum AgcFilter {
    AGC_FILTER_NEAREST = 0,
    AGC_FILTER_LINEAR = 1
} AgcFilter;

typedef enum AgcAddressMode {
    AGC_ADDRESS_MODE_REPEAT = 0,
    AGC_ADDRESS_MODE_CLAMP_TO_EDGE = 1
} AgcAddressMode;

typedef struct AgcSamplerDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcFilter min_filter;
    AgcFilter mag_filter;
    AgcAddressMode address_u;
    AgcAddressMode address_v;
    AgcAddressMode address_w;
    uint32_t flags;
    uint64_t reserved[4];
} AgcSamplerDesc;

#define AGC_SAMPLER_DESC_INIT \
    { sizeof(AgcSamplerDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      AGC_FILTER_NEAREST, AGC_FILTER_NEAREST, AGC_ADDRESS_MODE_REPEAT, \
      AGC_ADDRESS_MODE_REPEAT, AGC_ADDRESS_MODE_REPEAT, 0u, \
      {0u, 0u, 0u, 0u} }

typedef struct AgcShaderDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcShaderStage stage;
    uint32_t flags;
    const void *code;
    uint64_t code_size;
    uint64_t reserved[4];
} AgcShaderDesc;

#define AGC_SHADER_DESC_INIT \
    { sizeof(AgcShaderDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      kAgcShaderStageCs, 0u, NULL, 0u, {0u, 0u, 0u, 0u} }

typedef struct AgcGraphicsPipelineDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcShader vertex_shader;
    AgcShader pixel_shader;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved[4];
} AgcGraphicsPipelineDesc;

#define AGC_GRAPHICS_PIPELINE_DESC_INIT \
    { sizeof(AgcGraphicsPipelineDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      NULL, NULL, 0u, 0u, {0u, 0u, 0u, 0u} }

typedef struct AgcComputePipelineDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcShader shader;
    uint32_t local_size_x;
    uint32_t local_size_y;
    uint32_t local_size_z;
    uint32_t flags;
    uint64_t reserved[4];
} AgcComputePipelineDesc;

#define AGC_COMPUTE_PIPELINE_DESC_INIT \
    { sizeof(AgcComputePipelineDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, NULL, \
      1u, 1u, 1u, 0u, {0u, 0u, 0u, 0u} }

typedef enum AgcCommandBufferState {
    AGC_COMMAND_BUFFER_STATE_INITIAL = 0,
    AGC_COMMAND_BUFFER_STATE_RECORDING = 1,
    AGC_COMMAND_BUFFER_STATE_EXECUTABLE = 2,
    AGC_COMMAND_BUFFER_STATE_PENDING = 3
} AgcCommandBufferState;

typedef struct AgcCommandBufferDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcQueueType queue_type;
    uint32_t capacity_dwords;
    uint64_t reserved[4];
} AgcCommandBufferDesc;

#define AGC_COMMAND_BUFFER_DESC_INIT \
    { sizeof(AgcCommandBufferDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      kAgcQueueGraphics, 4096u, {0u, 0u, 0u, 0u} }

typedef struct AgcFenceDesc {
    uint32_t struct_size;
    uint32_t version;
    uint32_t signaled;
    uint32_t flags;
    uint64_t reserved[4];
} AgcFenceDesc;

/* Native PS5 resource formats. Values shared with gfx1013 image descriptors
 * deliberately retain their hardware encoding. */
typedef enum AgcFormat {
    AGC_FORMAT_UNDEFINED = 0,
    AGC_FORMAT_RGBA8_UNORM = 56,
    AGC_FORMAT_BC1_UNORM = 169,
    AGC_FORMAT_BC1_SRGB = 170,
    AGC_FORMAT_BC2_UNORM = 171,
    AGC_FORMAT_BC2_SRGB = 172,
    AGC_FORMAT_BC3_UNORM = 173,
    AGC_FORMAT_BC3_SRGB = 174,
    AGC_FORMAT_BC4_UNORM = 175,
    AGC_FORMAT_BC4_SNORM = 176,
    AGC_FORMAT_BC5_UNORM = 177,
    AGC_FORMAT_BC5_SNORM = 178,
    AGC_FORMAT_BC6_UFLOAT = 179,
    AGC_FORMAT_BC6_SFLOAT = 180,
    AGC_FORMAT_BC7_UNORM = 181,
    AGC_FORMAT_BC7_SRGB = 182,
    AGC_FORMAT_D16_UNORM = 256,
    AGC_FORMAT_D32_FLOAT = 257,
    AGC_FORMAT_S8_UINT = 258,
    AGC_FORMAT_D16_UNORM_S8_UINT = 259,
    AGC_FORMAT_D32_FLOAT_S8_UINT = 260
} AgcFormat;

typedef enum AgcMemoryHeap {
    AGC_MEMORY_HEAP_FLEXIBLE = 0,
    AGC_MEMORY_HEAP_GARLIC = 1,
    AGC_MEMORY_HEAP_COUNT = 2
} AgcMemoryHeap;

typedef enum AgcObjectType {
    AGC_OBJECT_TYPE_BUFFER = 0,
    AGC_OBJECT_TYPE_IMAGE = 1,
    AGC_OBJECT_TYPE_SHADER = 2,
    AGC_OBJECT_TYPE_COMMAND_BUFFER = 3,
    AGC_OBJECT_TYPE_IMAGE_VIEW = 4,
    AGC_OBJECT_TYPE_SAMPLER = 5,
    AGC_OBJECT_TYPE_COUNT = 6
} AgcObjectType;

#define AGC_RUNTIME_DEBUG_NAME_SIZE 64u

typedef struct AgcAllocationInfo {
    uint32_t struct_size;
    uint32_t version;
    uint32_t heap;
    uint32_t dedicated;
    uint64_t allocation_size;
    uint64_t requested_size;
    uint64_t heap_offset;
    uint64_t gpu_address;
    void *cpu_address;
    uint32_t resident;
    uint32_t owner_type;
    char debug_name[AGC_RUNTIME_DEBUG_NAME_SIZE];
    const void *owner;
    uint64_t reserved[3];
} AgcAllocationInfo;

#define AGC_ALLOCATION_INFO_INIT \
    { sizeof(AgcAllocationInfo), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, \
      0u, 0u, 0u, 0u, NULL, 0u, 0u, {0}, NULL, {0u, 0u, 0u} }

typedef struct AgcMemoryStats {
    uint32_t struct_size;
    uint32_t version;
    uint32_t block_count[AGC_MEMORY_HEAP_COUNT];
    uint32_t dedicated_block_count;
    uint64_t live_allocation_count;
    uint64_t live_bytes;
    uint64_t high_water_allocation_count;
    uint64_t high_water_bytes;
    uint64_t deferred_free_count;
    uint64_t reserved[4];
} AgcMemoryStats;

#define AGC_MEMORY_STATS_INIT \
    { sizeof(AgcMemoryStats), AGC_RUNTIME_STRUCTURE_VERSION_1, {0u, 0u}, 0u, \
      0u, 0u, 0u, 0u, 0u, {0u, 0u, 0u, 0u} }

typedef struct AgcImageLayout {
    uint32_t struct_size;
    uint32_t version;
    uint64_t allocation_size;
    uint64_t alignment;
    uint32_t plane_count;
    uint32_t subresource_count;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t bytes_per_block;
    uint32_t first_mip_in_tail;
    uint64_t metadata_offset;
    uint64_t metadata_size;
    uint64_t reserved[4];
} AgcImageLayout;

#define AGC_IMAGE_LAYOUT_INIT \
    { sizeof(AgcImageLayout), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, 0u, \
      0u, 0u, 0u, 0u, 0u, 0u, 0u, {0u, 0u, 0u, 0u} }

typedef struct AgcImageSubresourceLayout {
    uint32_t struct_size;
    uint32_t version;
    uint32_t mip_level;
    uint32_t array_layer;
    uint32_t plane;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint64_t offset;
    uint64_t size;
    uint64_t row_pitch;
    uint64_t slice_pitch;
    uint64_t reserved[4];
} AgcImageSubresourceLayout;

#define AGC_IMAGE_SUBRESOURCE_LAYOUT_INIT \
    { sizeof(AgcImageSubresourceLayout), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, \
      {0u, 0u, 0u, 0u} }

#define AGC_FENCE_DESC_INIT \
    { sizeof(AgcFenceDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, \
      {0u, 0u, 0u, 0u} }

typedef struct AgcSubmitInfo {
    uint32_t struct_size;
    uint32_t version;
    uint32_t command_buffer_count;
    uint32_t flags;
    const AgcCommandBuffer *command_buffers;
    uint64_t reserved[4];
} AgcSubmitInfo;

#define AGC_SUBMIT_INFO_INIT \
    { sizeof(AgcSubmitInfo), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, NULL, \
      {0u, 0u, 0u, 0u} }

/* The v1 application ABI targets the 64-bit PS5 process model. Structure-size
 * assertions make an accidental field, enum, or alignment change fail at
 * compile time; future layouts use a new version and initializer. */
_Static_assert(sizeof(AgcAllocationCallbacks) == 24u,
    "AgcAllocationCallbacks v1 size mismatch");
_Static_assert(sizeof(AgcDeviceDesc) == 64u,
    "AgcDeviceDesc v1 size mismatch");
_Static_assert(sizeof(AgcRuntimeInfo) == 120u,
    "AgcRuntimeInfo v1 size mismatch");
_Static_assert(sizeof(AgcQueueDesc) == 48u,
    "AgcQueueDesc v1 size mismatch");
_Static_assert(sizeof(AgcBufferDesc) == 56u,
    "AgcBufferDesc v1 size mismatch");
_Static_assert(sizeof(AgcImageDesc) == 72u,
    "AgcImageDesc v1 size mismatch");
_Static_assert(sizeof(AgcImageViewDesc) == 72u,
    "AgcImageViewDesc v1 size mismatch");
_Static_assert(sizeof(AgcSamplerDesc) == 64u,
    "AgcSamplerDesc v1 size mismatch");
_Static_assert(sizeof(AgcShaderDesc) == 64u,
    "AgcShaderDesc v1 size mismatch");
_Static_assert(sizeof(AgcGraphicsPipelineDesc) == 64u,
    "AgcGraphicsPipelineDesc v1 size mismatch");
_Static_assert(sizeof(AgcComputePipelineDesc) == 64u,
    "AgcComputePipelineDesc v1 size mismatch");
_Static_assert(sizeof(AgcCommandBufferDesc) == 48u,
    "AgcCommandBufferDesc v1 size mismatch");
_Static_assert(sizeof(AgcFenceDesc) == 48u,
    "AgcFenceDesc v1 size mismatch");
_Static_assert(sizeof(AgcSubmitInfo) == 56u,
    "AgcSubmitInfo v1 size mismatch");
_Static_assert(sizeof(AgcAllocationInfo) == 160u,
    "AgcAllocationInfo v1 size mismatch");
_Static_assert(sizeof(AgcMemoryStats) == 96u,
    "AgcMemoryStats v1 size mismatch");
_Static_assert(sizeof(AgcImageLayout) == 96u,
    "AgcImageLayout v1 size mismatch");
_Static_assert(sizeof(AgcImageSubresourceLayout) == 96u,
    "AgcImageSubresourceLayout v1 size mismatch");

int32_t PS5_SYSV_ABI agcCreateDevice(
    const AgcDeviceDesc *desc, AgcDevice *device_out);
int32_t PS5_SYSV_ABI agcDestroyDevice(AgcDevice device);
int32_t PS5_SYSV_ABI agcGetRuntimeInfo(
    AgcDevice device, AgcRuntimeInfo *info);

int32_t PS5_SYSV_ABI agcCreateQueue(
    AgcDevice device, const AgcQueueDesc *desc, AgcQueue *queue_out);
int32_t PS5_SYSV_ABI agcDestroyQueue(AgcQueue queue);

int32_t PS5_SYSV_ABI agcCreateBuffer(
    AgcDevice device, const AgcBufferDesc *desc, AgcBuffer *buffer_out);
int32_t PS5_SYSV_ABI agcDestroyBuffer(AgcBuffer buffer);
int32_t PS5_SYSV_ABI agcDestroyBufferDeferred(
    AgcBuffer buffer, AgcFence fence);
int32_t PS5_SYSV_ABI agcWriteBuffer(
    AgcBuffer buffer, uint64_t offset, const void *data, uint64_t size);
int32_t PS5_SYSV_ABI agcReadBuffer(
    AgcBuffer buffer, uint64_t offset, void *data, uint64_t size);
int32_t PS5_SYSV_ABI agcCreateImage(
    AgcDevice device, const AgcImageDesc *desc, AgcImage *image_out);
int32_t PS5_SYSV_ABI agcDestroyImage(AgcImage image);
int32_t PS5_SYSV_ABI agcDestroyImageDeferred(
    AgcImage image, AgcFence fence);
int32_t PS5_SYSV_ABI agcGetImageLayout(
    AgcDevice device, const AgcImageDesc *desc, AgcImageLayout *layout);
int32_t PS5_SYSV_ABI agcGetImageSubresourceLayout(AgcDevice device,
    const AgcImageDesc *desc, uint32_t mip_level, uint32_t array_layer,
    uint32_t plane, AgcImageSubresourceLayout *layout);
int32_t PS5_SYSV_ABI agcCreateImageView(
    AgcDevice device, const AgcImageViewDesc *desc, AgcImageView *view_out);
int32_t PS5_SYSV_ABI agcDestroyImageView(AgcImageView view);
int32_t PS5_SYSV_ABI agcCreateSampler(
    AgcDevice device, const AgcSamplerDesc *desc, AgcSampler *sampler_out);
int32_t PS5_SYSV_ABI agcDestroySampler(AgcSampler sampler);
int32_t PS5_SYSV_ABI agcCreateShader(
    AgcDevice device, const AgcShaderDesc *desc, AgcShader *shader_out);
int32_t PS5_SYSV_ABI agcDestroyShader(AgcShader shader);
int32_t PS5_SYSV_ABI agcCreateGraphicsPipeline(AgcDevice device,
    const AgcGraphicsPipelineDesc *desc, AgcGraphicsPipeline *pipeline_out);
int32_t PS5_SYSV_ABI agcDestroyGraphicsPipeline(AgcGraphicsPipeline pipeline);
int32_t PS5_SYSV_ABI agcCreateComputePipeline(AgcDevice device,
    const AgcComputePipelineDesc *desc, AgcComputePipeline *pipeline_out);
int32_t PS5_SYSV_ABI agcDestroyComputePipeline(AgcComputePipeline pipeline);

int32_t PS5_SYSV_ABI agcCreateCommandBuffer(AgcDevice device,
    const AgcCommandBufferDesc *desc, AgcCommandBuffer *command_buffer_out);
int32_t PS5_SYSV_ABI agcDestroyCommandBuffer(AgcCommandBuffer command_buffer);
int32_t PS5_SYSV_ABI agcBeginCommandBuffer(AgcCommandBuffer command_buffer);
int32_t PS5_SYSV_ABI agcEndCommandBuffer(AgcCommandBuffer command_buffer);
int32_t PS5_SYSV_ABI agcResetCommandBuffer(AgcCommandBuffer command_buffer);
int32_t PS5_SYSV_ABI agcGetCommandBufferState(
    AgcCommandBuffer command_buffer, AgcCommandBufferState *state_out);
int32_t PS5_SYSV_ABI agcCmdBindGraphicsPipeline(
    AgcCommandBuffer command_buffer, AgcGraphicsPipeline pipeline);
int32_t PS5_SYSV_ABI agcCmdBindComputePipeline(
    AgcCommandBuffer command_buffer, AgcComputePipeline pipeline);
int32_t PS5_SYSV_ABI agcCmdBindIndexBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer buffer, uint64_t offset, AgcIndexSize index_size);
int32_t PS5_SYSV_ABI agcCmdDrawIndexed(AgcCommandBuffer command_buffer,
    uint32_t index_count, uint32_t instance_count, uint32_t first_index,
    int32_t vertex_offset, uint32_t first_instance);
int32_t PS5_SYSV_ABI agcCmdDispatch(AgcCommandBuffer command_buffer,
    uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

int32_t PS5_SYSV_ABI agcCreateFence(
    AgcDevice device, const AgcFenceDesc *desc, AgcFence *fence_out);
int32_t PS5_SYSV_ABI agcDestroyFence(AgcFence fence);
int32_t PS5_SYSV_ABI agcGetFenceStatus(AgcFence fence);
int32_t PS5_SYSV_ABI agcResetFence(AgcFence fence);
int32_t PS5_SYSV_ABI agcWaitFence(AgcFence fence, uint64_t timeout_ns);
int32_t PS5_SYSV_ABI agcQueueSubmit(
    AgcQueue queue, const AgcSubmitInfo *submit_info, AgcFence fence);

int32_t PS5_SYSV_ABI agcGetObjectAllocationInfo(AgcDevice device,
    AgcObjectType type, const void *object, AgcAllocationInfo *info);
int32_t PS5_SYSV_ABI agcSetObjectDebugName(AgcDevice device,
    AgcObjectType type, void *object, const char *name);
int32_t PS5_SYSV_ABI agcGetMemoryStats(
    AgcDevice device, AgcMemoryStats *stats);
int32_t PS5_SYSV_ABI agcCollectDeferredFrees(AgcDevice device);

#ifdef __cplusplus
}
#endif

#endif /* OPENAGC_RUNTIME_H */
