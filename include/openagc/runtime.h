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
#include "openagc/shader_reflection.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_RUNTIME_API_VERSION 48u
#define AGC_RUNTIME_MAX_VIEWPORTS 16u
#define AGC_RUNTIME_STRUCTURE_VERSION_1 1u
#define AGC_RUNTIME_STRUCTURE_VERSION_2 2u
#define AGC_RUNTIME_STRUCTURE_VERSION_3 3u
#define AGC_RUNTIME_STRUCTURE_VERSION_4 4u
#define AGC_RUNTIME_STRUCTURE_VERSION_5 5u
#define AGC_RUNTIME_PROFILE_NAME_SIZE 48u
#define AGC_RUNTIME_INFINITE_TIMEOUT UINT64_MAX

typedef struct AgcDeviceImpl *AgcDevice;
typedef struct AgcQueueImpl *AgcQueue;
typedef struct AgcMemoryImpl *AgcMemory;
typedef struct AgcBufferImpl *AgcBuffer;
typedef struct AgcImageImpl *AgcImage;
typedef struct AgcImageViewImpl *AgcImageView;
typedef struct AgcSamplerImpl *AgcSampler;
typedef struct AgcShaderImpl *AgcShader;
typedef struct AgcGraphicsPipelineImpl *AgcGraphicsPipeline;
typedef struct AgcComputePipelineImpl *AgcComputePipeline;
typedef struct AgcCommandBufferImpl *AgcCommandBuffer;
typedef struct AgcFenceImpl *AgcFence;
typedef struct AgcGpuLabelImpl *AgcGpuLabel;
typedef struct AgcPresentChainImpl *AgcPresentChain;
typedef struct AgcDepthBias AgcDepthBias;

typedef void *(PS5_SYSV_ABI *AgcAllocationFunction)(
    void *user_data, size_t size, size_t alignment);
typedef void (PS5_SYSV_ABI *AgcFreeFunction)(
    void *user_data, void *memory);

typedef struct AgcAllocationCallbacks {
    void *user_data;
    AgcAllocationFunction allocate;
    AgcFreeFunction free;
} AgcAllocationCallbacks;

/* Applications synchronize each device, queue, and child-object externally.
 * Independent devices may be used concurrently. Devices in one process share
 * the selected physical backend and therefore require the same agc_version.
 * Destroying a parent with live children or recorded references returns
 * AGC_ERROR_BUSY and performs no mutation. */
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

typedef uint32_t AgcMemoryPropertyFlags;
#define AGC_MEMORY_PROPERTY_DEVICE_LOCAL_BIT (1u << 0)
#define AGC_MEMORY_PROPERTY_HOST_VISIBLE_BIT (1u << 1)
#define AGC_MEMORY_PROPERTY_HOST_COHERENT_BIT (1u << 2)
#define AGC_MEMORY_PROPERTY_HOST_CACHED_BIT (1u << 3)

typedef enum AgcMemoryHeap {
    AGC_MEMORY_HEAP_FLEXIBLE = 0,
    AGC_MEMORY_HEAP_GARLIC = 1,
    AGC_MEMORY_HEAP_COUNT = 2
} AgcMemoryHeap;

typedef struct AgcMemoryHeapProperties {
    uint64_t size;
    uint64_t minimum_alignment;
    AgcMemoryPropertyFlags property_flags;
    uint32_t reserved;
} AgcMemoryHeapProperties;

typedef struct AgcDeviceProperties {
    uint32_t struct_size;
    uint32_t version;
    uint32_t max_image_dimension_1d;
    uint32_t max_image_dimension_2d;
    uint32_t max_image_dimension_3d;
    uint32_t max_image_dimension_cube;
    uint32_t max_image_array_layers;
    uint32_t max_color_targets;
    uint32_t subgroup_size;
    uint32_t max_compute_shared_memory_size;
    uint32_t max_compute_workgroup_invocations;
    uint32_t max_compute_workgroup_size[3];
    uint32_t color_sample_counts;
    uint32_t depth_sample_counts;
    uint64_t color_target_format_mask;
    uint32_t depth_stencil_format_mask;
    uint32_t memory_heap_count;
    AgcMemoryHeapProperties memory_heaps[AGC_MEMORY_HEAP_COUNT];
    uint64_t reserved[4];
} AgcDeviceProperties;

#define AGC_DEVICE_PROPERTIES_INIT \
    { sizeof(AgcDeviceProperties), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, {0u, 0u, 0u}, 0u, 0u, \
      0u, 0u, 0u, {{0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u}}, \
      {0u, 0u, 0u, 0u} }

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

typedef enum AgcMemoryCreateFlagBits {
    AGC_MEMORY_CREATE_DEDICATED_BIT = 1u << 0
} AgcMemoryCreateFlagBits;

typedef struct AgcMemoryDesc {
    uint32_t struct_size;
    uint32_t version;
    uint64_t size;
    AgcMemoryHeap heap;
    uint32_t flags;
    uint64_t alignment;
    uint64_t reserved[4];
} AgcMemoryDesc;

#define AGC_MEMORY_DESC_INIT \
    { sizeof(AgcMemoryDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, \
      AGC_MEMORY_HEAP_FLEXIBLE, 0u, 0u, {0u, 0u, 0u, 0u} }

typedef enum AgcBufferUsageFlagBits {
    AGC_BUFFER_USAGE_VERTEX_BIT = 1u << 0,
    AGC_BUFFER_USAGE_INDEX_BIT = 1u << 1,
    AGC_BUFFER_USAGE_UNIFORM_BIT = 1u << 2,
    AGC_BUFFER_USAGE_STORAGE_BIT = 1u << 3,
    AGC_BUFFER_USAGE_TRANSFER_SRC_BIT = 1u << 4,
    AGC_BUFFER_USAGE_TRANSFER_DST_BIT = 1u << 5,
    AGC_BUFFER_USAGE_INDIRECT_BIT = 1u << 6,
    AGC_BUFFER_USAGE_QUERY_BIT = 1u << 7
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

typedef enum AgcImageTiling {
    AGC_IMAGE_TILING_LINEAR = 0,
    AGC_IMAGE_TILING_OPTIMAL = 1
} AgcImageTiling;

typedef enum AgcImageCreateFlagBits {
    AGC_IMAGE_CREATE_MUTABLE_FORMAT_BIT = 1u << 0
} AgcImageCreateFlagBits;
typedef uint32_t AgcImageCreateFlags;

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
    AgcImageTiling tiling;
    AgcImageCreateFlags flags;
} AgcImageDesc;

#define AGC_IMAGE_DESC_INIT \
    { sizeof(AgcImageDesc), AGC_RUNTIME_STRUCTURE_VERSION_2, 1u, 1u, 1u, \
      1u, 1u, 0u, 1u, 0u, {0u, 0u, 0u, 0u}, \
      AGC_IMAGE_TILING_OPTIMAL, 0u }

#define AGC_PRESENT_CHAIN_MAX_IMAGES 16u
#define AGC_PRESENT_CHAIN_MIN_IMAGES 2u

typedef struct AgcPresentChainDesc {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t image_count;
    const AgcImage *images;
    uint64_t reserved[4];
} AgcPresentChainDesc;

#define AGC_PRESENT_CHAIN_DESC_INIT \
    { sizeof(AgcPresentChainDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, \
      NULL, {0u, 0u, 0u, 0u} }

typedef enum AgcResourceType {
    kAgcResourceTypeBuffer = 0,
    kAgcResourceTypeImage = 1
} AgcResourceType;

typedef enum AgcResourceUsage {
    kAgcResourceUsageUndefined = 0,
    kAgcResourceUsageCopySource = 1,
    kAgcResourceUsageCopyDestination = 2,
    kAgcResourceUsageShaderRead = 3,
    kAgcResourceUsageShaderWrite = 4,
    kAgcResourceUsageColorTarget = 5,
    kAgcResourceUsageDepthStencilRead = 6,
    kAgcResourceUsageDepthStencilWrite = 7,
    kAgcResourceUsageVideoOutScanout = 8,
    kAgcResourceUsageHostRead = 9,
    kAgcResourceUsageHostWrite = 10,
    kAgcResourceUsageQueryWrite = 11,
    kAgcResourceUsageCount
} AgcResourceUsage;

typedef enum AgcResourceOwner {
    kAgcResourceOwnerHost = 0,
    kAgcResourceOwnerGraphics = 1,
    kAgcResourceOwnerCompute = 2,
    kAgcResourceOwnerCount
} AgcResourceOwner;

typedef enum AgcImageAspectFlagBits {
    AGC_IMAGE_ASPECT_COLOR_BIT = 1u << 0,
    AGC_IMAGE_ASPECT_DEPTH_BIT = 1u << 1,
    AGC_IMAGE_ASPECT_STENCIL_BIT = 1u << 2,
    AGC_IMAGE_ASPECT_METADATA_BIT = 1u << 3
} AgcImageAspectFlagBits;
typedef uint32_t AgcImageAspectFlags;

typedef struct AgcImageSubresourceRange {
    AgcImageAspectFlags aspect_mask;
    uint32_t base_mip_level;
    uint32_t mip_level_count;
    uint32_t base_array_layer;
    uint32_t array_layer_count;
    uint32_t reserved0;
} AgcImageSubresourceRange;

#define AGC_IMAGE_SUBRESOURCE_RANGE_INIT \
    { AGC_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u, 0u }

typedef struct AgcOffset3D {
    int32_t x;
    int32_t y;
    int32_t z;
} AgcOffset3D;

typedef struct AgcExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} AgcExtent3D;

typedef struct AgcImageSubresourceLayers {
    AgcImageAspectFlags aspect_mask;
    uint32_t mip_level;
    uint32_t base_array_layer;
    uint32_t array_layer_count;
} AgcImageSubresourceLayers;

typedef struct AgcImageCopyRegion {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved0;
    AgcImageSubresourceLayers source_subresource;
    AgcOffset3D source_offset;
    uint32_t reserved1;
    AgcImageSubresourceLayers destination_subresource;
    AgcOffset3D destination_offset;
    uint32_t reserved2;
    AgcExtent3D extent;
    uint32_t reserved3;
    uint64_t reserved[4];
} AgcImageCopyRegion;

#define AGC_IMAGE_COPY_REGION_INIT \
    { sizeof(AgcImageCopyRegion), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, \
      { AGC_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u }, { 0, 0, 0 }, 0u, \
      { AGC_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u }, { 0, 0, 0 }, 0u, \
      { 1u, 1u, 1u }, 0u, { 0u, 0u, 0u, 0u } }

typedef struct AgcBufferImageCopyRegion {
    uint32_t struct_size;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t buffer_offset;
    uint32_t buffer_row_length;
    uint32_t buffer_image_height;
    AgcImageSubresourceLayers image_subresource;
    AgcOffset3D image_offset;
    uint32_t reserved1;
    AgcExtent3D image_extent;
    uint32_t reserved2;
    uint64_t reserved[4];
} AgcBufferImageCopyRegion;

#define AGC_BUFFER_IMAGE_COPY_REGION_INIT \
    { sizeof(AgcBufferImageCopyRegion), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, 0u, 0u, 0u, \
      { AGC_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u }, { 0, 0, 0 }, 0u, \
      { 1u, 1u, 1u }, 0u, { 0u, 0u, 0u, 0u } }

/* Exactly one of buffer or image is set according to resource_type. Buffer
 * ranges are byte ranges; image ranges name whole subresources. Source and
 * destination ownership must be explicit. v1 permits only same-owner and
 * GPU/host transitions. v2 adds a two-command ownership handoff: record a
 * RELEASE on the source queue, submit it, then record and submit the matching
 * ACQUIRE on the destination queue. Both sides name the same label/value. A
 * v2 batch-dependency record instead consumes the prior state of the same
 * resource from an earlier DCB in one ordered same-queue batch. */
enum {
    AGC_RESOURCE_TRANSITION_RELEASE_BIT = 1u << 0,
    AGC_RESOURCE_TRANSITION_ACQUIRE_BIT = 1u << 1,
    /* A v2 transition whose source state is produced by an earlier command
     * buffer in the same ordered queue batch. It cannot be submitted alone
     * and cannot be combined with ownership release/acquire. */
    AGC_RESOURCE_TRANSITION_BATCH_DEPENDENCY_BIT = 1u << 2
};

#define AGC_RESOURCE_TRANSITION_V1_SIZE 128u

typedef struct AgcResourceTransition {
    uint32_t struct_size;
    uint32_t version;
    AgcResourceType resource_type;
    uint32_t flags;
    AgcResourceUsage before;
    AgcResourceUsage after;
    AgcResourceOwner before_owner;
    AgcResourceOwner after_owner;
    AgcBuffer buffer;
    AgcImage image;
    uint64_t buffer_offset;
    uint64_t buffer_size;
    AgcImageSubresourceRange image_range;
    uint64_t reserved[5];
    /* v2 tail. Ignored for a v1-sized record. */
    AgcGpuLabel dependency_label;
    uint32_t dependency_value;
    uint32_t reserved_v2;
    uint64_t reserved2[2];
} AgcResourceTransition;

#define AGC_RESOURCE_TRANSITION_INIT \
    { AGC_RESOURCE_TRANSITION_V1_SIZE, AGC_RUNTIME_STRUCTURE_VERSION_1, \
      kAgcResourceTypeBuffer, 0u, kAgcResourceUsageUndefined, \
      kAgcResourceUsageUndefined, kAgcResourceOwnerHost, \
      kAgcResourceOwnerHost, NULL, NULL, 0u, 0u, \
      AGC_IMAGE_SUBRESOURCE_RANGE_INIT, {0u, 0u, 0u, 0u, 0u}, \
      NULL, 0u, 0u, {0u, 0u} }

#define AGC_RESOURCE_TRANSITION_V2_INIT \
    { sizeof(AgcResourceTransition), AGC_RUNTIME_STRUCTURE_VERSION_2, \
      kAgcResourceTypeBuffer, 0u, kAgcResourceUsageUndefined, \
      kAgcResourceUsageUndefined, kAgcResourceOwnerHost, \
      kAgcResourceOwnerHost, NULL, NULL, 0u, 0u, \
      AGC_IMAGE_SUBRESOURCE_RANGE_INIT, {0u, 0u, 0u, 0u, 0u}, \
      NULL, 0u, 0u, {0u, 0u} }

/* Read-only snapshot of the state committed by successful queue submission.
 * A uniformly covered pending ownership range reports its destination state
 * and dependency; mixed transfer coverage fails closed as ambiguous.
 * the committed usage/owner remain unchanged until the matching acquire is
 * submitted. Reference counts explain a BUSY destruction result without
 * exposing allocation addresses or backend objects. */
enum {
    AGC_RESOURCE_STATE_TRANSFER_PENDING_BIT = 1u << 0,
    AGC_RESOURCE_STATE_ACQUIRE_RECORDED_BIT = 1u << 1,
    AGC_RESOURCE_STATE_DEFERRED_BIT = 1u << 2
};

typedef struct AgcResourceStateInfo {
    uint32_t struct_size;
    uint32_t version;
    AgcResourceType resource_type;
    uint32_t flags;
    AgcResourceUsage usage;
    AgcResourceOwner owner;
    AgcResourceUsage transfer_usage;
    AgcResourceOwner transfer_owner;
    AgcGpuLabel transfer_label;
    uint32_t transfer_value;
    uint32_t recorded_reference_count;
    uint32_t dependency_reference_count;
    uint32_t reserved0;
    uint64_t reserved[4];
} AgcResourceStateInfo;

#define AGC_RESOURCE_STATE_INFO_INIT \
    { sizeof(AgcResourceStateInfo), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      kAgcResourceTypeBuffer, 0u, kAgcResourceUsageUndefined, \
      kAgcResourceOwnerHost, kAgcResourceUsageUndefined, \
      kAgcResourceOwnerHost, NULL, 0u, 0u, 0u, 0u, \
      {0u, 0u, 0u, 0u} }

/* Occlusion-query storage is a typed buffer contract. Applications query the
 * opaque record size instead of depending on render-backend packet or RB
 * layouts. Results are reduced to one portable 64-bit sample count. */
typedef struct AgcOcclusionQueryLayout {
    uint32_t struct_size;
    uint32_t version;
    uint64_t record_size;
    uint64_t alignment;
    uint64_t reserved[5];
} AgcOcclusionQueryLayout;

#define AGC_OCCLUSION_QUERY_LAYOUT_INIT \
    { sizeof(AgcOcclusionQueryLayout), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, {0u, 0u, 0u, 0u, 0u} }

typedef struct AgcOcclusionQueryResult {
    uint32_t struct_size;
    uint32_t version;
    uint64_t value;
    uint32_t available;
    uint32_t reserved0;
    uint64_t reserved[5];
} AgcOcclusionQueryResult;

#define AGC_OCCLUSION_QUERY_RESULT_INIT \
    { sizeof(AgcOcclusionQueryResult), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, 0u, {0u, 0u, 0u, 0u, 0u} }

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
    uint32_t view_type;
    uint32_t swizzle_r;
    uint32_t swizzle_g;
    uint32_t swizzle_b;
    uint32_t swizzle_a;
    uint32_t flags;
} AgcImageViewDesc;

typedef enum AgcImageViewType {
    AGC_IMAGE_VIEW_TYPE_2D = 0,
    AGC_IMAGE_VIEW_TYPE_2D_ARRAY = 1,
    AGC_IMAGE_VIEW_TYPE_CUBE = 2,
    AGC_IMAGE_VIEW_TYPE_CUBE_ARRAY = 3,
    AGC_IMAGE_VIEW_TYPE_3D = 4
} AgcImageViewType;

typedef enum AgcComponentSwizzle {
    AGC_COMPONENT_SWIZZLE_IDENTITY = 0,
    AGC_COMPONENT_SWIZZLE_ZERO = 1,
    AGC_COMPONENT_SWIZZLE_ONE = 2,
    AGC_COMPONENT_SWIZZLE_R = 3,
    AGC_COMPONENT_SWIZZLE_G = 4,
    AGC_COMPONENT_SWIZZLE_B = 5,
    AGC_COMPONENT_SWIZZLE_A = 6
} AgcComponentSwizzle;

#define AGC_IMAGE_VIEW_DESC_INIT \
    { sizeof(AgcImageViewDesc), AGC_RUNTIME_STRUCTURE_VERSION_2, NULL, 0u, \
      0u, 1u, 0u, 1u, {0u, 0u, 0u, 0u}, AGC_IMAGE_VIEW_TYPE_2D, \
      AGC_COMPONENT_SWIZZLE_IDENTITY, AGC_COMPONENT_SWIZZLE_IDENTITY, \
      AGC_COMPONENT_SWIZZLE_IDENTITY, AGC_COMPONENT_SWIZZLE_IDENTITY, 0u }

/* One color attachment for agcCmdBindColorTargets. For a 3D image,
 * array_layer selects a depth slice of mip_level; otherwise it selects an
 * array layer. The image subresource is retained by the command buffer until
 * it is reset or destroyed. */
typedef struct AgcColorTargetBinding {
    uint32_t struct_size;
    uint32_t version;
    AgcImage image;
    uint32_t mip_level;
    uint32_t array_layer;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved[4];
} AgcColorTargetBinding;

#define AGC_COLOR_TARGET_BINDING_INIT \
    { sizeof(AgcColorTargetBinding), AGC_RUNTIME_STRUCTURE_VERSION_1, NULL, \
      0u, 0u, 0u, 0u, {0u, 0u, 0u, 0u} }

/* One depth/stencil attachment for agcCmdBindDepthStencilTarget. Single-mip
 * depth images are currently supported because their qualified subresource
 * layout is directly addressable by the gfx1013 depth-surface builder. */
typedef struct AgcDepthStencilTargetBinding {
    uint32_t struct_size;
    uint32_t version;
    AgcImage image;
    uint32_t mip_level;
    uint32_t array_layer;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved[4];
} AgcDepthStencilTargetBinding;

#define AGC_DEPTH_STENCIL_TARGET_BINDING_INIT \
    { sizeof(AgcDepthStencilTargetBinding), \
      AGC_RUNTIME_STRUCTURE_VERSION_1, NULL, 0u, 0u, 0u, 0u, \
      {0u, 0u, 0u, 0u} }

typedef enum AgcFilter {
    AGC_FILTER_NEAREST = 0,
    AGC_FILTER_LINEAR = 1
} AgcFilter;

typedef enum AgcAddressMode {
    AGC_ADDRESS_MODE_REPEAT = 0,
    AGC_ADDRESS_MODE_CLAMP_TO_EDGE = 1,
    AGC_ADDRESS_MODE_MIRRORED_REPEAT = 2,
    AGC_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
    AGC_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE = 4
} AgcAddressMode;

typedef enum AgcMipFilter {
    AGC_MIP_FILTER_NONE = 0,
    AGC_MIP_FILTER_NEAREST = 1,
    AGC_MIP_FILTER_LINEAR = 2
} AgcMipFilter;

typedef enum AgcSamplerBorderColor {
    AGC_SAMPLER_BORDER_TRANSPARENT_BLACK = 0,
    AGC_SAMPLER_BORDER_OPAQUE_BLACK = 1,
    AGC_SAMPLER_BORDER_OPAQUE_WHITE = 2,
    AGC_SAMPLER_BORDER_CUSTOM = 3
} AgcSamplerBorderColor;

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
    AgcMipFilter mip_filter;
    uint32_t anisotropy_enable;
    uint32_t max_anisotropy;
    uint32_t compare_enable;
    uint32_t compare_operation;
    AgcSamplerBorderColor border_color;
    uint32_t custom_border_color_index;
    float min_lod;
    float max_lod;
    float lod_bias;
    uint32_t reserved2;
    uint32_t custom_border_color[4];
} AgcSamplerDesc;

#define AGC_SAMPLER_DESC_INIT \
    { sizeof(AgcSamplerDesc), AGC_RUNTIME_STRUCTURE_VERSION_3, \
      AGC_FILTER_NEAREST, AGC_FILTER_NEAREST, AGC_ADDRESS_MODE_REPEAT, \
      AGC_ADDRESS_MODE_REPEAT, AGC_ADDRESS_MODE_REPEAT, 0u, \
      {0u, 0u, 0u, 0u}, AGC_MIP_FILTER_NONE, 0u, 1u, 0u, \
      AGC_COMPARE_OPERATION_ALWAYS, AGC_SAMPLER_BORDER_TRANSPARENT_BLACK, \
      0u, 0.0f, 0.0f, 0.0f, 0u, {0u, 0u, 0u, 0u} }

typedef struct AgcShaderDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcShaderStage stage;
    uint32_t flags;
    const void *code;
    uint64_t code_size;
    uint64_t reserved[4];
    const AgcShaderReflection *reflection;
    const void *front_code;
    uint64_t front_code_size;
} AgcShaderDesc;

#define AGC_SHADER_DESC_INIT \
    { sizeof(AgcShaderDesc), AGC_RUNTIME_STRUCTURE_VERSION_2, \
      kAgcShaderStageCs, 0u, NULL, 0u, {0u, 0u, 0u, 0u}, NULL, NULL, 0u }

typedef enum AgcBlendFactor {
    AGC_BLEND_FACTOR_ZERO = 0,
    AGC_BLEND_FACTOR_ONE,
    AGC_BLEND_FACTOR_SRC_COLOR,
    AGC_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    AGC_BLEND_FACTOR_SRC_ALPHA,
    AGC_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    AGC_BLEND_FACTOR_DST_COLOR,
    AGC_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    AGC_BLEND_FACTOR_DST_ALPHA,
    AGC_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    AGC_BLEND_FACTOR_CONSTANT_COLOR,
    AGC_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
    AGC_BLEND_FACTOR_CONSTANT_ALPHA,
    AGC_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
    AGC_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    AGC_BLEND_FACTOR_SRC1_COLOR,
    AGC_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
    AGC_BLEND_FACTOR_SRC1_ALPHA,
    AGC_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
    AGC_BLEND_FACTOR_COUNT
} AgcBlendFactor;

typedef enum AgcBlendOperation {
    AGC_BLEND_OPERATION_ADD = 0,
    AGC_BLEND_OPERATION_SUBTRACT,
    AGC_BLEND_OPERATION_REVERSE_SUBTRACT,
    AGC_BLEND_OPERATION_MIN,
    AGC_BLEND_OPERATION_MAX,
    AGC_BLEND_OPERATION_COUNT
} AgcBlendOperation;

typedef enum AgcLogicOperation {
    AGC_LOGIC_OPERATION_CLEAR = 0,
    AGC_LOGIC_OPERATION_AND,
    AGC_LOGIC_OPERATION_AND_REVERSE,
    AGC_LOGIC_OPERATION_COPY,
    AGC_LOGIC_OPERATION_AND_INVERTED,
    AGC_LOGIC_OPERATION_NO_OP,
    AGC_LOGIC_OPERATION_XOR,
    AGC_LOGIC_OPERATION_OR,
    AGC_LOGIC_OPERATION_NOR,
    AGC_LOGIC_OPERATION_EQUIVALENT,
    AGC_LOGIC_OPERATION_INVERT,
    AGC_LOGIC_OPERATION_OR_REVERSE,
    AGC_LOGIC_OPERATION_COPY_INVERTED,
    AGC_LOGIC_OPERATION_OR_INVERTED,
    AGC_LOGIC_OPERATION_NAND,
    AGC_LOGIC_OPERATION_SET,
    AGC_LOGIC_OPERATION_COUNT
} AgcLogicOperation;

typedef struct AgcColorBlendAttachmentState {
    uint32_t struct_size;
    uint32_t version;
    uint32_t format;
    uint32_t blend_enable;
    uint32_t write_mask;
    AgcBlendFactor source_color_factor;
    AgcBlendFactor destination_color_factor;
    AgcBlendOperation color_operation;
    AgcBlendFactor source_alpha_factor;
    AgcBlendFactor destination_alpha_factor;
    AgcBlendOperation alpha_operation;
    uint32_t flags;
    uint64_t reserved[2];
} AgcColorBlendAttachmentState;

#define AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT \
    { sizeof(AgcColorBlendAttachmentState), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, 0xfu, AGC_BLEND_FACTOR_ONE, AGC_BLEND_FACTOR_ZERO, \
      AGC_BLEND_OPERATION_ADD, AGC_BLEND_FACTOR_ONE, AGC_BLEND_FACTOR_ZERO, \
      AGC_BLEND_OPERATION_ADD, 0u, {0u, 0u} }

typedef enum AgcPolygonMode {
    AGC_POLYGON_MODE_FILL = 0,
    AGC_POLYGON_MODE_LINE = 1,
    AGC_POLYGON_MODE_POINT = 2,
    AGC_POLYGON_MODE_COUNT
} AgcPolygonMode;

typedef enum AgcCullModeFlagBits {
    AGC_CULL_MODE_NONE = 0,
    AGC_CULL_MODE_FRONT_BIT = 1u << 0,
    AGC_CULL_MODE_BACK_BIT = 1u << 1
} AgcCullModeFlagBits;
typedef uint32_t AgcCullModeFlags;

typedef enum AgcFrontFace {
    AGC_FRONT_FACE_COUNTER_CLOCKWISE = 0,
    AGC_FRONT_FACE_CLOCKWISE = 1
} AgcFrontFace;

typedef enum AgcRasterizationStateFlagBits {
    AGC_RASTERIZATION_DEPTH_CLIP_ENABLE_BIT = 1u << 0,
    AGC_RASTERIZATION_DEPTH_CLIP_DISABLE_BIT = 1u << 1
} AgcRasterizationStateFlagBits;
typedef uint32_t AgcRasterizationStateFlags;

typedef struct AgcRasterizationState {
    uint32_t struct_size;
    uint32_t version;
    AgcPolygonMode polygon_mode;
    AgcCullModeFlags cull_mode;
    AgcFrontFace front_face;
    uint32_t depth_clamp_enable;
    uint32_t rasterizer_discard_enable;
    uint32_t depth_bias_enable;
    float line_width;
    AgcRasterizationStateFlags flags;
    uint64_t reserved[3];
} AgcRasterizationState;

#define AGC_RASTERIZATION_STATE_INIT \
    { sizeof(AgcRasterizationState), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      AGC_POLYGON_MODE_FILL, AGC_CULL_MODE_NONE, \
      AGC_FRONT_FACE_COUNTER_CLOCKWISE, 0u, 0u, 0u, 1.0f, 0u, \
      {0u, 0u, 0u} }

typedef enum AgcCompareOperation {
    AGC_COMPARE_OPERATION_NEVER = 0,
    AGC_COMPARE_OPERATION_LESS,
    AGC_COMPARE_OPERATION_EQUAL,
    AGC_COMPARE_OPERATION_LESS_OR_EQUAL,
    AGC_COMPARE_OPERATION_GREATER,
    AGC_COMPARE_OPERATION_NOT_EQUAL,
    AGC_COMPARE_OPERATION_GREATER_OR_EQUAL,
    AGC_COMPARE_OPERATION_ALWAYS,
    AGC_COMPARE_OPERATION_COUNT
} AgcCompareOperation;

typedef enum AgcStencilOperation {
    AGC_STENCIL_OPERATION_KEEP = 0,
    AGC_STENCIL_OPERATION_ZERO,
    AGC_STENCIL_OPERATION_REPLACE,
    AGC_STENCIL_OPERATION_INCREMENT_AND_CLAMP,
    AGC_STENCIL_OPERATION_DECREMENT_AND_CLAMP,
    AGC_STENCIL_OPERATION_INVERT,
    AGC_STENCIL_OPERATION_INCREMENT_AND_WRAP,
    AGC_STENCIL_OPERATION_DECREMENT_AND_WRAP,
    AGC_STENCIL_OPERATION_COUNT
} AgcStencilOperation;

typedef struct AgcStencilFaceState {
    AgcCompareOperation compare_operation;
    AgcStencilOperation fail_operation;
    AgcStencilOperation depth_fail_operation;
    AgcStencilOperation pass_operation;
    uint32_t compare_mask;
    uint32_t write_mask;
    uint32_t reference;
    uint32_t flags;
    uint64_t reserved[2];
} AgcStencilFaceState;

#define AGC_STENCIL_FACE_STATE_INIT \
    { AGC_COMPARE_OPERATION_ALWAYS, AGC_STENCIL_OPERATION_KEEP, \
      AGC_STENCIL_OPERATION_KEEP, AGC_STENCIL_OPERATION_KEEP, \
      0xffu, 0xffu, 0u, 0u, {0u, 0u} }

typedef struct AgcDepthStencilPipelineState {
    uint32_t struct_size;
    uint32_t version;
    uint32_t format;
    uint32_t depth_test_enable;
    uint32_t depth_write_enable;
    AgcCompareOperation depth_compare_operation;
    uint32_t depth_bounds_enable;
    uint32_t stencil_test_enable;
    float min_depth_bounds;
    float max_depth_bounds;
    uint32_t back_face_enable;
    uint32_t flags;
    AgcStencilFaceState front;
    AgcStencilFaceState back;
    uint64_t reserved[2];
} AgcDepthStencilPipelineState;

#define AGC_DEPTH_STENCIL_PIPELINE_STATE_INIT \
    { sizeof(AgcDepthStencilPipelineState), \
      AGC_RUNTIME_STRUCTURE_VERSION_2, 0u, 0u, 0u, \
      AGC_COMPARE_OPERATION_ALWAYS, 0u, 0u, 0.0f, 1.0f, 0u, 0u, \
      AGC_STENCIL_FACE_STATE_INIT, AGC_STENCIL_FACE_STATE_INIT, {0u, 0u} }

typedef struct AgcMultisampleState {
    uint32_t struct_size;
    uint32_t version;
    uint32_t rasterization_samples;
    uint32_t sample_shading_enable;
    float minimum_sample_shading;
    uint32_t alpha_to_coverage_enable;
    uint32_t alpha_to_one_enable;
    uint32_t flags;
    uint64_t reserved[2];
} AgcMultisampleState;

#define AGC_MULTISAMPLE_STATE_INIT \
    { sizeof(AgcMultisampleState), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      1u, 0u, 0.0f, 0u, 0u, 0u, {0u, 0u} }

typedef enum AgcDynamicStateFlagBits {
    AGC_DYNAMIC_STATE_VIEWPORT_BIT = 1u << 0,
    AGC_DYNAMIC_STATE_SCISSOR_BIT = 1u << 1,
    AGC_DYNAMIC_STATE_BLEND_CONSTANTS_BIT = 1u << 2,
    AGC_DYNAMIC_STATE_STENCIL_REFERENCE_BIT = 1u << 3,
    AGC_DYNAMIC_STATE_DEPTH_BIAS_BIT = 1u << 4,
    AGC_DYNAMIC_STATE_LINE_WIDTH_BIT = 1u << 5
} AgcDynamicStateFlagBits;
typedef uint32_t AgcDynamicStateFlags;

typedef enum AgcPrimitiveTopology {
    AGC_PRIMITIVE_TOPOLOGY_POINT_LIST = 0,
    AGC_PRIMITIVE_TOPOLOGY_LINE_LIST,
    AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    AGC_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
    AGC_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
    AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
    AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
    AGC_PRIMITIVE_TOPOLOGY_PATCH_LIST,
    AGC_PRIMITIVE_TOPOLOGY_COUNT
} AgcPrimitiveTopology;

typedef struct AgcGraphicsPipelineDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcShader vertex_shader;
    AgcShader pixel_shader;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t reserved[4];
    AgcShader tessellation_control_shader;
    AgcShader tessellation_evaluation_shader;
    AgcShader geometry_shader;
    const AgcShaderVertexInput *vertex_inputs;
    uint32_t vertex_input_count;
    uint32_t descriptor_mapping_count;
    const AgcShaderDescriptorMapping *descriptor_mappings;
    uint32_t push_constant_range_count;
    uint32_t color_attachment_count;
    const AgcShaderPushConstantRange *push_constant_ranges;
    const AgcColorBlendAttachmentState *color_attachments;
    const AgcRasterizationState *rasterization;
    const AgcDepthStencilPipelineState *depth_stencil;
    const AgcMultisampleState *multisample;
    AgcDynamicStateFlags dynamic_state_mask;
    AgcPrimitiveTopology primitive_topology;
    const AgcDepthBias *static_depth_bias;
    uint32_t logic_operation_enable;
    AgcLogicOperation logic_operation;
    uint32_t primitive_restart_enable;
    uint32_t reserved3;
    uint64_t reserved2[1];
} AgcGraphicsPipelineDesc;

#define AGC_GRAPHICS_PIPELINE_DESC_INIT \
    { sizeof(AgcGraphicsPipelineDesc), AGC_RUNTIME_STRUCTURE_VERSION_5, \
      NULL, NULL, 0u, 0u, {0u, 0u, 0u, 0u}, NULL, NULL, NULL, NULL, \
      0u, 0u, NULL, 0u, 0u, NULL, NULL, NULL, NULL, NULL, 0u, \
      AGC_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, NULL, 0u, \
      AGC_LOGIC_OPERATION_COPY, 0u, 0u, {0u} }

typedef struct AgcComputePipelineDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcShader shader;
    uint32_t local_size_x;
    uint32_t local_size_y;
    uint32_t local_size_z;
    uint32_t flags;
    uint64_t reserved[4];
    uint32_t descriptor_mapping_count;
    uint32_t push_constant_range_count;
    const AgcShaderDescriptorMapping *descriptor_mappings;
    const AgcShaderPushConstantRange *push_constant_ranges;
    uint64_t reserved2[4];
} AgcComputePipelineDesc;

#define AGC_COMPUTE_PIPELINE_DESC_INIT \
    { sizeof(AgcComputePipelineDesc), AGC_RUNTIME_STRUCTURE_VERSION_2, NULL, \
      1u, 1u, 1u, 0u, {0u, 0u, 0u, 0u}, 0u, 0u, NULL, NULL, \
      {0u, 0u, 0u, 0u} }

typedef struct AgcDescriptorWrite {
    uint32_t struct_size;
    uint32_t version;
    uint32_t set;
    uint32_t binding;
    uint32_t array_element;
    AgcShaderDescriptorType type;
    AgcBuffer buffer;
    uint64_t buffer_offset;
    uint64_t buffer_range;
    uint32_t buffer_stride;
    uint32_t reserved0;
    AgcImageView image_view;
    AgcSampler sampler;
    uint64_t reserved[3];
} AgcDescriptorWrite;

#define AGC_DESCRIPTOR_WRITE_INIT \
    { sizeof(AgcDescriptorWrite), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, 0u, AGC_SHADER_DESCRIPTOR_SAMPLER, NULL, 0u, 0u, 0u, 0u, \
      NULL, NULL, {0u, 0u, 0u} }

typedef struct AgcVertexBufferBinding {
    uint32_t struct_size;
    uint32_t version;
    uint32_t binding;
    uint32_t reserved0;
    AgcBuffer buffer;
    uint64_t offset;
    uint32_t stride;
    uint32_t reserved1;
    uint64_t reserved[3];
} AgcVertexBufferBinding;

#define AGC_VERTEX_BUFFER_BINDING_INIT \
    { sizeof(AgcVertexBufferBinding), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0u, 0u, NULL, 0u, 0u, 0u, {0u, 0u, 0u} }

typedef struct AgcViewport {
    uint32_t struct_size;
    uint32_t version;
    float x;
    float y;
    float width;
    float height;
    float min_depth;
    float max_depth;
    uint64_t reserved[2];
} AgcViewport;

#define AGC_VIEWPORT_INIT \
    { sizeof(AgcViewport), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, {0u, 0u} }

typedef struct AgcScissor {
    uint32_t struct_size;
    uint32_t version;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t reserved[2];
} AgcScissor;

#define AGC_SCISSOR_INIT \
    { sizeof(AgcScissor), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0, 0, 1u, 1u, {0u, 0u} }

struct AgcDepthBias {
    uint32_t struct_size;
    uint32_t version;
    float constant_factor;
    float clamp;
    float slope_factor;
    uint32_t flags;
    uint64_t reserved[2];
};

#define AGC_DEPTH_BIAS_INIT \
    { sizeof(AgcDepthBias), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      0.0f, 0.0f, 0.0f, 0u, {0u, 0u} }

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

/* A GPU-visible 32-bit synchronization word. Graphics and compute producers
 * and consumers may synchronize through submitted signals. GPU and host waits
 * use reached-or-passed semantics; values are strictly monotonic timeline
 * points and UINT32_MAX is terminal. */
typedef struct AgcGpuLabelDesc {
    uint32_t struct_size;
    uint32_t version;
    uint32_t initial_value;
    uint32_t flags;
    uint64_t reserved[4];
} AgcGpuLabelDesc;

typedef enum AgcFenceState {
    AGC_FENCE_STATE_UNSIGNALED = 0,
    AGC_FENCE_STATE_PENDING = 1,
    AGC_FENCE_STATE_SIGNALED = 2
} AgcFenceState;

/* Snapshot of the most recent submission that used a binary fence. A queue
 * type of UINT32_MAX means the fence has not yet been submitted. The snapshot
 * lets bounded wait failures identify their exact profile and marker without
 * exposing backend handles or GPU addresses. */
typedef struct AgcFenceInfo {
    uint32_t struct_size;
    uint32_t version;
    AgcFenceState state;
    uint32_t queue_type;
    uint32_t command_buffer_state;
    uint32_t completion_value;
    uint32_t observed_completion_value;
    int32_t last_wait_result;
    uint16_t firmware_abi_key;
    uint16_t hardware_family;
    uint64_t submission_id;
    uint64_t last_completed_submission_id;
    uint64_t timeout_count;
    uint64_t last_timeout_ns;
    char profile_name[AGC_RUNTIME_PROFILE_NAME_SIZE];
    uint64_t reserved[3];
} AgcFenceInfo;

typedef struct AgcGpuLabelInfo {
    uint32_t struct_size;
    uint32_t version;
    uint32_t scheduled_value;
    uint32_t observed_value;
    uint32_t queue_type;
    uint32_t reserved0;
    uint64_t last_signal_submission_id;
    uint16_t firmware_abi_key;
    uint16_t hardware_family;
    char profile_name[AGC_RUNTIME_PROFILE_NAME_SIZE];
    uint64_t reserved[2];
    /* v2 tail: diagnostics for the most recent bounded host wait. */
    uint32_t last_wait_value;
    int32_t last_wait_result;
    uint64_t timeout_count;
    uint64_t last_timeout_ns;
    uint64_t reserved_v2[2];
} AgcGpuLabelInfo;

#define AGC_GPU_LABEL_INFO_V1_SIZE 104u

/* Native PS5 resource formats. Values shared with gfx1013 image descriptors
 * deliberately retain their hardware encoding. */
typedef enum AgcFormat {
    AGC_FORMAT_UNDEFINED = 0,
    AGC_FORMAT_RGBA8_UNORM = 56,
    AGC_FORMAT_BGRA8_UNORM = 57,
    AGC_FORMAT_RGBA8_SRGB = 58,
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
    AGC_FORMAT_D32_FLOAT_S8_UINT = 260,
    AGC_FORMAT_RGBA16_FLOAT = 512,
    AGC_FORMAT_RGBA32_FLOAT = 513,
    AGC_FORMAT_RGBA16_UINT = 514,
    AGC_FORMAT_RGBA16_SINT = 515,
    AGC_FORMAT_RGBA32_UINT = 516,
    AGC_FORMAT_RGBA32_SINT = 517,
    AGC_FORMAT_R8_UNORM = 518,
    AGC_FORMAT_RG8_UNORM = 519,
    AGC_FORMAT_RGB10A2_UNORM = 520,
    AGC_FORMAT_R16_FLOAT = 521,
    AGC_FORMAT_RG16_FLOAT = 522,
    AGC_FORMAT_R32_FLOAT = 523,
    AGC_FORMAT_RG32_FLOAT = 524,
    AGC_FORMAT_R11G11B10_FLOAT = 525,
    AGC_FORMAT_BGRA8_SRGB = 526,
    AGC_FORMAT_R16_UNORM = 527,
    AGC_FORMAT_R16_SNORM = 528,
    AGC_FORMAT_R16_UINT = 529,
    AGC_FORMAT_R16_SINT = 530,
    AGC_FORMAT_RG16_UNORM = 531,
    AGC_FORMAT_RG16_SNORM = 532,
    AGC_FORMAT_RG16_UINT = 533,
    AGC_FORMAT_RG16_SINT = 534,
    AGC_FORMAT_RGBA16_UNORM = 535,
    AGC_FORMAT_RGBA16_SNORM = 536,
    AGC_FORMAT_R32_UINT = 537,
    AGC_FORMAT_R32_SINT = 538,
    AGC_FORMAT_RG32_UINT = 539,
    AGC_FORMAT_RG32_SINT = 540
} AgcFormat;

typedef enum AgcObjectType {
    AGC_OBJECT_TYPE_BUFFER = 0,
    AGC_OBJECT_TYPE_IMAGE = 1,
    AGC_OBJECT_TYPE_SHADER = 2,
    AGC_OBJECT_TYPE_COMMAND_BUFFER = 3,
    AGC_OBJECT_TYPE_IMAGE_VIEW = 4,
    AGC_OBJECT_TYPE_SAMPLER = 5,
    AGC_OBJECT_TYPE_MEMORY = 6,
    AGC_OBJECT_TYPE_COUNT = 7
} AgcObjectType;

#define AGC_RUNTIME_DEBUG_NAME_SIZE 64u
#define AGC_RUNTIME_DEBUG_FUNCTION_NAME_SIZE 48u
#define AGC_RUNTIME_DEBUG_MESSAGE_SIZE 192u
#define AGC_DEBUG_OBJECT_TYPE_NONE UINT32_MAX

typedef enum AgcDebugMessageSeverityFlagBits {
    AGC_DEBUG_MESSAGE_SEVERITY_INFO_BIT = 1u << 0,
    AGC_DEBUG_MESSAGE_SEVERITY_WARNING_BIT = 1u << 1,
    AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT = 1u << 2
} AgcDebugMessageSeverityFlagBits;
typedef uint32_t AgcDebugMessageSeverityFlags;

#define AGC_DEBUG_MESSAGE_SEVERITY_ALL \
    (AGC_DEBUG_MESSAGE_SEVERITY_INFO_BIT | \
     AGC_DEBUG_MESSAGE_SEVERITY_WARNING_BIT | \
     AGC_DEBUG_MESSAGE_SEVERITY_ERROR_BIT)

typedef enum AgcDebugMessageCategoryFlagBits {
    AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT = 1u << 0,
    AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT = 1u << 1,
    AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT = 1u << 2,
    AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT = 1u << 3,
    AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT = 1u << 4,
    AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT = 1u << 5,
    AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT = 1u << 6,
    AGC_DEBUG_MESSAGE_CATEGORY_SYNCHRONIZATION_BIT = 1u << 7
} AgcDebugMessageCategoryFlagBits;
typedef uint32_t AgcDebugMessageCategoryFlags;

#define AGC_DEBUG_MESSAGE_CATEGORY_ALL \
    (AGC_DEBUG_MESSAGE_CATEGORY_PARAMETER_BIT | \
     AGC_DEBUG_MESSAGE_CATEGORY_OBJECT_STATE_BIT | \
     AGC_DEBUG_MESSAGE_CATEGORY_RESOURCE_STATE_BIT | \
     AGC_DEBUG_MESSAGE_CATEGORY_COMPATIBILITY_BIT | \
     AGC_DEBUG_MESSAGE_CATEGORY_COMMAND_CAPACITY_BIT | \
     AGC_DEBUG_MESSAGE_CATEGORY_LIFETIME_BIT | \
     AGC_DEBUG_MESSAGE_CATEGORY_CAPABILITY_BIT | \
     AGC_DEBUG_MESSAGE_CATEGORY_SYNCHRONIZATION_BIT)

typedef struct AgcDebugMessage {
    uint32_t struct_size;
    uint32_t version;
    uint64_t sequence;
    AgcDebugMessageSeverityFlags severity;
    AgcDebugMessageCategoryFlags category;
    int32_t result;
    uint32_t object_type;
    char function_name[AGC_RUNTIME_DEBUG_FUNCTION_NAME_SIZE];
    char object_name[AGC_RUNTIME_DEBUG_NAME_SIZE];
    char message[AGC_RUNTIME_DEBUG_MESSAGE_SIZE];
    uint64_t reserved[4];
} AgcDebugMessage;

#define AGC_DEBUG_MESSAGE_INIT \
    { sizeof(AgcDebugMessage), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, 0u, \
      AGC_OK, AGC_DEBUG_OBJECT_TYPE_NONE, {0}, {0}, {0}, \
      {0u, 0u, 0u, 0u} }

typedef void (PS5_SYSV_ABI *AgcDebugMessageFunction)(
    void *user_data, const AgcDebugMessage *message);

typedef struct AgcDebugCallbackDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcDebugMessageSeverityFlags severity_mask;
    AgcDebugMessageCategoryFlags category_mask;
    AgcDebugMessageFunction callback;
    void *user_data;
    uint64_t reserved[4];
} AgcDebugCallbackDesc;

#define AGC_DEBUG_CALLBACK_DESC_INIT \
    { sizeof(AgcDebugCallbackDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      AGC_DEBUG_MESSAGE_SEVERITY_ALL, AGC_DEBUG_MESSAGE_CATEGORY_ALL, \
      NULL, NULL, {0u, 0u, 0u, 0u} }

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

#define AGC_GPU_LABEL_DESC_INIT \
    { sizeof(AgcGpuLabelDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, \
      {0u, 0u, 0u, 0u} }

#define AGC_FENCE_INFO_INIT \
    { sizeof(AgcFenceInfo), AGC_RUNTIME_STRUCTURE_VERSION_1, \
      AGC_FENCE_STATE_UNSIGNALED, UINT32_MAX, \
      AGC_COMMAND_BUFFER_STATE_INITIAL, 0u, 0u, AGC_ERROR_BUSY, 0u, 0u, \
      0u, 0u, 0u, 0u, {0}, {0u, 0u, 0u} }

#define AGC_GPU_LABEL_INFO_INIT \
    { sizeof(AgcGpuLabelInfo), AGC_RUNTIME_STRUCTURE_VERSION_2, 0u, 0u, \
      UINT32_MAX, 0u, 0u, 0u, 0u, {0}, {0u, 0u}, 0u, AGC_ERROR_BUSY, \
      0u, 0u, {0u, 0u} }

typedef struct AgcGpuLabelPoint {
    AgcGpuLabel label;
    uint32_t value;
    uint32_t reserved;
} AgcGpuLabelPoint;

#define AGC_GPU_LABEL_POINT_INIT { NULL, 0u, 0u }

#define AGC_SUBMIT_INFO_V1_SIZE 56u

typedef struct AgcSubmitInfo {
    uint32_t struct_size;
    uint32_t version;
    uint32_t command_buffer_count;
    uint32_t flags;
    const AgcCommandBuffer *command_buffers;
    uint64_t reserved[4];
    /* v2 tail: transient submission dependencies. The runtime retains every
     * referenced label until the submitted command buffer is reset. */
    const AgcGpuLabelPoint *waits;
    const AgcGpuLabelPoint *signals;
    uint32_t wait_count;
    uint32_t signal_count;
    uint64_t reserved_v2[2];
} AgcSubmitInfo;

#define AGC_SUBMIT_INFO_INIT \
    { AGC_SUBMIT_INFO_V1_SIZE, AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, NULL, \
      {0u, 0u, 0u, 0u}, NULL, NULL, 0u, 0u, {0u, 0u} }

#define AGC_SUBMIT_INFO_V2_INIT \
    { sizeof(AgcSubmitInfo), AGC_RUNTIME_STRUCTURE_VERSION_2, 0u, 0u, NULL, \
      {0u, 0u, 0u, 0u}, NULL, NULL, 0u, 0u, {0u, 0u} }

/* The application ABI targets the 64-bit PS5 process model. Structure-size
 * assertions make an accidental field, enum, or alignment change fail at
 * compile time; future layouts use a new version and initializer. */
_Static_assert(sizeof(AgcAllocationCallbacks) == 24u,
    "AgcAllocationCallbacks v1 size mismatch");
_Static_assert(sizeof(AgcDeviceDesc) == 64u,
    "AgcDeviceDesc v1 size mismatch");
_Static_assert(sizeof(AgcRuntimeInfo) == 120u,
    "AgcRuntimeInfo v1 size mismatch");
_Static_assert(sizeof(AgcMemoryHeapProperties) == 24u,
    "AgcMemoryHeapProperties size mismatch");
_Static_assert(sizeof(AgcDeviceProperties) == 160u,
    "AgcDeviceProperties v1 size mismatch");
_Static_assert(sizeof(AgcQueueDesc) == 48u,
    "AgcQueueDesc v1 size mismatch");
_Static_assert(sizeof(AgcBufferDesc) == 56u,
    "AgcBufferDesc v1 size mismatch");
_Static_assert(sizeof(AgcMemoryDesc) == 64u,
    "AgcMemoryDesc v1 size mismatch");
_Static_assert(offsetof(AgcImageDesc, tiling) == 72u,
    "AgcImageDesc v1 prefix size mismatch");
_Static_assert(sizeof(AgcImageDesc) == 80u,
    "AgcImageDesc v2 size mismatch");
_Static_assert(sizeof(AgcImageSubresourceRange) == 24u,
    "AgcImageSubresourceRange v1 size mismatch");
_Static_assert(sizeof(AgcOffset3D) == 12u,
    "AgcOffset3D size mismatch");
_Static_assert(sizeof(AgcExtent3D) == 12u,
    "AgcExtent3D size mismatch");
_Static_assert(sizeof(AgcImageSubresourceLayers) == 16u,
    "AgcImageSubresourceLayers size mismatch");
_Static_assert(sizeof(AgcImageCopyRegion) == 128u,
    "AgcImageCopyRegion v1 size mismatch");
_Static_assert(sizeof(AgcBufferImageCopyRegion) == 112u,
    "AgcBufferImageCopyRegion v1 size mismatch");
_Static_assert(offsetof(AgcResourceTransition, dependency_label) ==
    AGC_RESOURCE_TRANSITION_V1_SIZE,
    "AgcResourceTransition v1 prefix size mismatch");
_Static_assert(sizeof(AgcResourceTransition) == 160u,
    "AgcResourceTransition v2 size mismatch");
_Static_assert(offsetof(AgcImageViewDesc, view_type) == 72u,
    "AgcImageViewDesc v1 prefix size mismatch");
_Static_assert(sizeof(AgcImageViewDesc) == 96u,
    "AgcImageViewDesc v2 size mismatch");
_Static_assert(sizeof(AgcColorTargetBinding) == 64u,
    "AgcColorTargetBinding v1 size mismatch");
_Static_assert(sizeof(AgcDepthStencilTargetBinding) == 64u,
    "AgcDepthStencilTargetBinding v1 size mismatch");
_Static_assert(offsetof(AgcSamplerDesc, mip_filter) == 64u,
    "AgcSamplerDesc v1 prefix size mismatch");
_Static_assert(offsetof(AgcSamplerDesc, custom_border_color) == 108u,
    "AgcSamplerDesc v2 prefix size mismatch");
_Static_assert(sizeof(AgcSamplerDesc) == 128u,
    "AgcSamplerDesc v3 size mismatch");
_Static_assert(sizeof(AgcShaderDesc) == 88u,
    "AgcShaderDesc v2 size mismatch");
_Static_assert(sizeof(AgcColorBlendAttachmentState) == 64u,
    "AgcColorBlendAttachmentState v1 size mismatch");
_Static_assert(sizeof(AgcRasterizationState) == 64u,
    "AgcRasterizationState v1 size mismatch");
_Static_assert(sizeof(AgcStencilFaceState) == 48u,
    "AgcStencilFaceState v1 size mismatch");
_Static_assert(sizeof(AgcDepthStencilPipelineState) == 160u,
    "AgcDepthStencilPipelineState v2 size mismatch");
_Static_assert(sizeof(AgcMultisampleState) == 48u,
    "AgcMultisampleState v1 size mismatch");
_Static_assert(sizeof(AgcGraphicsPipelineDesc) == 200u,
    "AgcGraphicsPipelineDesc v2 size mismatch");
_Static_assert(sizeof(AgcComputePipelineDesc) == 120u,
    "AgcComputePipelineDesc v2 size mismatch");
_Static_assert(sizeof(AgcDescriptorWrite) == 96u,
    "AgcDescriptorWrite v1 size mismatch");
_Static_assert(sizeof(AgcVertexBufferBinding) == 64u,
    "AgcVertexBufferBinding v1 size mismatch");
_Static_assert(sizeof(AgcViewport) == 48u,
    "AgcViewport v1 size mismatch");
_Static_assert(sizeof(AgcScissor) == 40u,
    "AgcScissor v1 size mismatch");
_Static_assert(sizeof(AgcDepthBias) == 40u,
    "AgcDepthBias v1 size mismatch");
_Static_assert(sizeof(AgcCommandBufferDesc) == 48u,
    "AgcCommandBufferDesc v1 size mismatch");
_Static_assert(sizeof(AgcFenceDesc) == 48u,
    "AgcFenceDesc v1 size mismatch");
_Static_assert(sizeof(AgcGpuLabelDesc) == 48u,
    "AgcGpuLabelDesc v1 size mismatch");
_Static_assert(sizeof(AgcGpuLabelPoint) == 16u,
    "AgcGpuLabelPoint v1 size mismatch");
_Static_assert(offsetof(AgcSubmitInfo, waits) == AGC_SUBMIT_INFO_V1_SIZE,
    "AgcSubmitInfo v1 prefix size mismatch");
_Static_assert(sizeof(AgcSubmitInfo) == 96u,
    "AgcSubmitInfo v2 size mismatch");
_Static_assert(sizeof(AgcAllocationInfo) == 160u,
    "AgcAllocationInfo v1 size mismatch");
_Static_assert(sizeof(AgcMemoryStats) == 96u,
    "AgcMemoryStats v1 size mismatch");
_Static_assert(sizeof(AgcImageLayout) == 96u,
    "AgcImageLayout v1 size mismatch");
_Static_assert(sizeof(AgcImageSubresourceLayout) == 96u,
    "AgcImageSubresourceLayout v1 size mismatch");
_Static_assert(sizeof(AgcFenceInfo) == 144u,
    "AgcFenceInfo v1 size mismatch");
_Static_assert(offsetof(AgcGpuLabelInfo, last_wait_value) ==
    AGC_GPU_LABEL_INFO_V1_SIZE,
    "AgcGpuLabelInfo v1 prefix size mismatch");
_Static_assert(sizeof(AgcGpuLabelInfo) == 144u,
    "AgcGpuLabelInfo v2 size mismatch");
_Static_assert(sizeof(AgcResourceStateInfo) == 88u,
    "AgcResourceStateInfo v1 size mismatch");
_Static_assert(sizeof(AgcOcclusionQueryLayout) == 64u,
    "AgcOcclusionQueryLayout v1 size mismatch");
_Static_assert(sizeof(AgcOcclusionQueryResult) == 64u,
    "AgcOcclusionQueryResult v1 size mismatch");

int32_t PS5_SYSV_ABI agcCreateDevice(
    const AgcDeviceDesc *desc, AgcDevice *device_out);
int32_t PS5_SYSV_ABI agcDestroyDevice(AgcDevice device);
int32_t PS5_SYSV_ABI agcGetRuntimeInfo(
    AgcDevice device, AgcRuntimeInfo *info);
int32_t PS5_SYSV_ABI agcGetDeviceProperties(
    AgcDevice device, AgcDeviceProperties *properties);

int32_t PS5_SYSV_ABI agcCreateQueue(
    AgcDevice device, const AgcQueueDesc *desc, AgcQueue *queue_out);
int32_t PS5_SYSV_ABI agcDestroyQueue(AgcQueue queue);

/* Explicit allocations back placed Vulkan-style resources. Destroying a
 * memory handle releases the application reference; storage remains alive
 * until every placed resource is destroyed. */
int32_t PS5_SYSV_ABI agcCreateMemory(
    AgcDevice device, const AgcMemoryDesc *desc, AgcMemory *memory_out);
int32_t PS5_SYSV_ABI agcDestroyMemory(AgcMemory memory);
int32_t PS5_SYSV_ABI agcMapMemory(
    AgcMemory memory, uint64_t offset, uint64_t size, void **data_out);
int32_t PS5_SYSV_ABI agcUnmapMemory(AgcMemory memory);
int32_t PS5_SYSV_ABI agcFlushMemory(
    AgcMemory memory, uint64_t offset, uint64_t size);
int32_t PS5_SYSV_ABI agcInvalidateMemory(
    AgcMemory memory, uint64_t offset, uint64_t size);

int32_t PS5_SYSV_ABI agcCreateBuffer(
    AgcDevice device, const AgcBufferDesc *desc, AgcBuffer *buffer_out);
int32_t PS5_SYSV_ABI agcCreatePlacedBuffer(AgcDevice device,
    const AgcBufferDesc *desc, AgcMemory memory, uint64_t memory_offset,
    AgcBuffer *buffer_out);
int32_t PS5_SYSV_ABI agcDestroyBuffer(AgcBuffer buffer);
int32_t PS5_SYSV_ABI agcGetBufferStateInfo(
    AgcBuffer buffer, AgcResourceStateInfo *info);
/* Queries one byte range. A whole-buffer query returns NOT_SUPPORTED when
 * committed ranges differ; query each application-owned range explicitly. */
int32_t PS5_SYSV_ABI agcGetBufferRangeStateInfo(AgcBuffer buffer,
    uint64_t offset, uint64_t size, AgcResourceStateInfo *info);
/* Queries the effective range state including transitions already recorded in
 * this command buffer. This keeps translator-side state mirrors unnecessary. */
int32_t PS5_SYSV_ABI agcGetCommandBufferRangeStateInfo(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset,
    uint64_t size, AgcResourceStateInfo *info);
/* Image counterpart of agcGetCommandBufferRangeStateInfo. The returned state
 * includes every transition already recorded for the queried subresources. */
int32_t PS5_SYSV_ABI agcGetCommandBufferImageSubresourceStateInfo(
    AgcCommandBuffer command_buffer, AgcImage image,
    const AgcImageSubresourceRange *range, AgcResourceStateInfo *info);
/* Queues retirement against a finite-wait fence. Existing command references
 * may remain, but allocation reuse waits for both fence completion and release
 * of those references by command reset/destruction. */
int32_t PS5_SYSV_ABI agcDestroyBufferDeferred(
    AgcBuffer buffer, AgcFence fence);
int32_t PS5_SYSV_ABI agcWriteBuffer(
    AgcBuffer buffer, uint64_t offset, const void *data, uint64_t size);
int32_t PS5_SYSV_ABI agcReadBuffer(
    AgcBuffer buffer, uint64_t offset, void *data, uint64_t size);
int32_t PS5_SYSV_ABI agcGetOcclusionQueryLayout(
    AgcDevice device, AgcOcclusionQueryLayout *layout);
/* A zero timeout polls. Infinite waits are rejected; every blocking query
 * read therefore has a caller-selected finite upper bound. */
int32_t PS5_SYSV_ABI agcGetOcclusionQueryResult(AgcBuffer buffer,
    uint64_t offset, uint64_t timeout_ns, AgcOcclusionQueryResult *result);
int32_t PS5_SYSV_ABI agcResetOcclusionQueryResults(AgcBuffer buffer,
    uint64_t offset, uint32_t query_count);
int32_t PS5_SYSV_ABI agcCreateImage(
    AgcDevice device, const AgcImageDesc *desc, AgcImage *image_out);
int32_t PS5_SYSV_ABI agcCreatePlacedImage(AgcDevice device,
    const AgcImageDesc *desc, AgcMemory memory, uint64_t memory_offset,
    AgcImage *image_out);
int32_t PS5_SYSV_ABI agcDestroyImage(AgcImage image);
int32_t PS5_SYSV_ABI agcGetImageStateInfo(
    AgcImage image, AgcResourceStateInfo *info);
int32_t PS5_SYSV_ABI agcGetImageSubresourceStateInfo(AgcImage image,
    const AgcImageSubresourceRange *range, AgcResourceStateInfo *info);
/* Existing view/present-chain/command references delay collection safely. */
int32_t PS5_SYSV_ABI agcDestroyImageDeferred(
    AgcImage image, AgcFence fence);
/* Creates a main-display chain from dedicated runtime images. Images must use
 * AGC_IMAGE_USAGE_SCANOUT_BIT; dimensions and pitch are validated against the
 * firmware-neutral default VideoOut mode. The chain retains every image. */
int32_t PS5_SYSV_ABI agcCreatePresentChain(AgcDevice device,
    const AgcPresentChainDesc *desc, AgcPresentChain *present_chain_out);
int32_t PS5_SYSV_ABI agcDestroyPresentChain(
    AgcPresentChain present_chain);
/* Waits for a finite readiness fence, then presents one image with a bounded
 * VSYNC wait. The image must be in graphics-owned VideoOutScanout state. */
int32_t PS5_SYSV_ABI agcPresent(AgcPresentChain present_chain,
    uint32_t image_index, uint64_t frame_id, AgcFence ready_fence,
    uint64_t timeout_ns);
/* Transfer raw image-allocation bytes. The caller obtains portable
 * subresource ranges from agcGetImageSubresourceLayout. */
int32_t PS5_SYSV_ABI agcWriteImage(
    AgcImage image, uint64_t offset, const void *data, uint64_t size);
int32_t PS5_SYSV_ABI agcReadImage(
    AgcImage image, uint64_t offset, void *data, uint64_t size);
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
int32_t PS5_SYSV_ABI agcGetShaderReflection(
    AgcShader shader, AgcShaderReflection *reflection);
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
int32_t PS5_SYSV_ABI agcCmdBindColorTargets(
    AgcCommandBuffer command_buffer, uint32_t target_count,
    const AgcColorTargetBinding *targets);
int32_t PS5_SYSV_ABI agcCmdBindDepthStencilTarget(
    AgcCommandBuffer command_buffer,
    const AgcDepthStencilTargetBinding *target);
int32_t PS5_SYSV_ABI agcCmdTransitionResources(
    AgcCommandBuffer command_buffer, uint32_t transition_count,
    const AgcResourceTransition *transitions);
/* Copies a four-byte-aligned buffer range after explicit CopySource and
 * CopyDestination transitions establish ownership on this command buffer's
 * queue. Overlapping source/destination ranges are rejected. */
int32_t PS5_SYSV_ABI agcCmdCopyBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer source, uint64_t source_offset, AgcBuffer destination,
    uint64_t destination_offset, uint64_t size);
/* Embeds four-byte-aligned data or a repeated 32-bit value into the command
 * stream and writes it to a CopyDestination buffer range. The complete
 * command-space and retention requirements are preflighted before emission. */
int32_t PS5_SYSV_ABI agcCmdUpdateBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer destination, uint64_t destination_offset, const void *data,
    uint64_t size);
int32_t PS5_SYSV_ABI agcCmdFillBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer destination, uint64_t destination_offset, uint64_t size,
    uint32_t value);
/* Query records require AGC_BUFFER_USAGE_QUERY_BIT. These operations acquire
 * each record into graphics-owned QueryWrite state internally. Reset zeroes
 * complete records. End writes availability through a cache-flushing EOP. */
int32_t PS5_SYSV_ABI agcCmdResetOcclusionQueries(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset,
    uint32_t query_count);
int32_t PS5_SYSV_ABI agcCmdBeginOcclusionQuery(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset,
    uint32_t precise);
int32_t PS5_SYSV_ABI agcCmdEndOcclusionQuery(
    AgcCommandBuffer command_buffer, AgcBuffer buffer, uint64_t offset);
/* Copies the complete allocation between two distinct images with identical
 * dimensions, format, sample count, mip/layer shape, and computed layout.
 * Both images require explicit CopySource/CopyDestination state on the command
 * queue. Partial subresources and layout conversion remain fail-closed. */
int32_t PS5_SYSV_ABI agcCmdCopyImage(AgcCommandBuffer command_buffer,
    AgcImage source, AgcImage destination);
/* Region copies derive every byte address from native subresource layouts.
 * Color and BC mip/layer/3D rows are supported when their physical rows are
 * DMA-addressable. Depth/HTILE/MSAA linearization remains fail-closed. */
int32_t PS5_SYSV_ABI agcCmdCopyImageRegions(
    AgcCommandBuffer command_buffer, AgcImage source, AgcImage destination,
    uint32_t region_count, const AgcImageCopyRegion *regions);
int32_t PS5_SYSV_ABI agcCmdCopyBufferToImage(
    AgcCommandBuffer command_buffer, AgcBuffer source, AgcImage destination,
    uint32_t region_count, const AgcBufferImageCopyRegion *regions);
int32_t PS5_SYSV_ABI agcCmdCopyImageToBuffer(
    AgcCommandBuffer command_buffer, AgcImage source, AgcBuffer destination,
    uint32_t region_count, const AgcBufferImageCopyRegion *regions);
/* Descriptor resources must already have a compatible explicit typed state
 * on this command buffer's queue. Read-only descriptors require ShaderRead;
 * storage descriptors accept ShaderRead or ShaderWrite until reflection
 * carries per-binding read/write access qualifiers. A write with the typed
 * resource handles unset encodes a zero/null descriptor; unused typed fields,
 * offsets, ranges, and strides must also be zero. */
int32_t PS5_SYSV_ABI agcCmdBindDescriptors(AgcCommandBuffer command_buffer,
    uint32_t write_count, const AgcDescriptorWrite *writes);
int32_t PS5_SYSV_ABI agcCmdBindVertexBuffers(AgcCommandBuffer command_buffer,
    uint32_t binding_count, const AgcVertexBufferBinding *bindings);
int32_t PS5_SYSV_ABI agcCmdPushConstants(AgcCommandBuffer command_buffer,
    uint32_t stage_mask, uint32_t offset, uint32_t size, const void *data);
int32_t PS5_SYSV_ABI agcCmdSetViewport(
    AgcCommandBuffer command_buffer, const AgcViewport *viewport);
int32_t PS5_SYSV_ABI agcCmdSetScissor(
    AgcCommandBuffer command_buffer, const AgcScissor *scissor);
int32_t PS5_SYSV_ABI agcCmdSetViewportScissors(
    AgcCommandBuffer command_buffer, uint32_t count,
    const AgcViewport *viewports, const AgcScissor *scissors);
int32_t PS5_SYSV_ABI agcCmdSetBlendConstants(
    AgcCommandBuffer command_buffer, const float constants[4]);
int32_t PS5_SYSV_ABI agcCmdSetStencilReference(
    AgcCommandBuffer command_buffer, uint32_t front, uint32_t back);
int32_t PS5_SYSV_ABI agcCmdSetDepthBias(
    AgcCommandBuffer command_buffer, const AgcDepthBias *depth_bias);
int32_t PS5_SYSV_ABI agcCmdSetLineWidth(
    AgcCommandBuffer command_buffer, float line_width);
int32_t PS5_SYSV_ABI agcCmdBindIndexBuffer(AgcCommandBuffer command_buffer,
    AgcBuffer buffer, uint64_t offset, AgcIndexSize index_size);
int32_t PS5_SYSV_ABI agcCmdDraw(AgcCommandBuffer command_buffer,
    uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex,
    uint32_t first_instance);
int32_t PS5_SYSV_ABI agcCmdDrawIndexed(AgcCommandBuffer command_buffer,
    uint32_t index_count, uint32_t instance_count, uint32_t first_index,
    int32_t vertex_offset, uint32_t first_instance);
int32_t PS5_SYSV_ABI agcCmdDrawIndirect(AgcCommandBuffer command_buffer,
    AgcBuffer argument_buffer, uint64_t offset, uint32_t draw_count,
    uint32_t stride);
int32_t PS5_SYSV_ABI agcCmdDrawIndexedIndirect(
    AgcCommandBuffer command_buffer, AgcBuffer argument_buffer,
    uint64_t offset, uint32_t draw_count, uint32_t stride);
int32_t PS5_SYSV_ABI agcCmdDispatch(AgcCommandBuffer command_buffer,
    uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);
int32_t PS5_SYSV_ABI agcCmdDispatchIndirect(
    AgcCommandBuffer command_buffer, AgcBuffer argument_buffer,
    uint64_t offset);

int32_t PS5_SYSV_ABI agcCreateFence(
    AgcDevice device, const AgcFenceDesc *desc, AgcFence *fence_out);
int32_t PS5_SYSV_ABI agcDestroyFence(AgcFence fence);
int32_t PS5_SYSV_ABI agcGetFenceStatus(AgcFence fence);
int32_t PS5_SYSV_ABI agcGetFenceInfo(AgcFence fence, AgcFenceInfo *info);
int32_t PS5_SYSV_ABI agcResetFence(AgcFence fence);
int32_t PS5_SYSV_ABI agcWaitFence(AgcFence fence, uint64_t timeout_ns);
/* Atomically returns a completed submission's command storage to Initial
 * state. The fence is polled once; BUSY leaves every command unchanged. */
int32_t PS5_SYSV_ABI agcRecycleCommandBuffers(AgcFence fence,
    uint32_t command_buffer_count,
    AgcCommandBuffer const *command_buffers);
int32_t PS5_SYSV_ABI agcCreateGpuLabel(
    AgcDevice device, const AgcGpuLabelDesc *desc, AgcGpuLabel *label_out);
int32_t PS5_SYSV_ABI agcDestroyGpuLabel(AgcGpuLabel label);
int32_t PS5_SYSV_ABI agcGetGpuLabelInfo(
    AgcGpuLabel label, AgcGpuLabelInfo *info);
int32_t PS5_SYSV_ABI agcGetGpuLabelStatus(
    AgcGpuLabel label, uint32_t value);
int32_t PS5_SYSV_ABI agcWaitGpuLabel(
    AgcGpuLabel label, uint32_t value, uint64_t timeout_ns);
int32_t PS5_SYSV_ABI agcCmdWaitGpuLabel(
    AgcCommandBuffer command_buffer, AgcGpuLabel label, uint32_t value);
int32_t PS5_SYSV_ABI agcCmdSignalGpuLabel(
    AgcCommandBuffer command_buffer, AgcGpuLabel label, uint32_t value);
int32_t PS5_SYSV_ABI agcQueueSubmit(
    AgcQueue queue, const AgcSubmitInfo *submit_info, AgcFence fence);

int32_t PS5_SYSV_ABI agcGetObjectAllocationInfo(AgcDevice device,
    AgcObjectType type, const void *object, AgcAllocationInfo *info);
int32_t PS5_SYSV_ABI agcSetObjectDebugName(AgcDevice device,
    AgcObjectType type, void *object, const char *name);
/* Installs one synchronous, allocation-free validation callback. Passing NULL
 * disables the optional layer; required safety validation remains active. */
int32_t PS5_SYSV_ABI agcSetDebugCallback(AgcDevice device,
    const AgcDebugCallbackDesc *desc);
int32_t PS5_SYSV_ABI agcGetLastDebugMessage(AgcDevice device,
    AgcDebugMessage *message);
int32_t PS5_SYSV_ABI agcGetMemoryStats(
    AgcDevice device, AgcMemoryStats *stats);
int32_t PS5_SYSV_ABI agcCollectDeferredFrees(AgcDevice device);

#ifdef __cplusplus
}
#endif

#endif /* OPENAGC_RUNTIME_H */
