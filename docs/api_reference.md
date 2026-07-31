# Native runtime API reference

This page indexes the public application contract in `openagc/runtime.h`,
`openagc/capture.h`, `openagc/shader_reflection.h`, and `agc_error.h`. Structure
initializers are part of the versioned ABI: initialize with the matching
`AGC_*_INIT` macro, change documented fields, and leave reserved fields zero.

## Ownership and lifetime

`AgcDevice` is the root owner. Queues, resources, views, samplers, shaders,
pipelines, command buffers, fences, labels, present chains, and captures are
device children. A destroy call returns `AGC_ERROR_BUSY` without mutation when
live children or recorded/submitted references remain. Destroy children in
reverse creation order.

Views retain images; pipelines retain shaders; present chains retain images;
recorded commands retain every referenced object; pending submissions retain
their command buffers and synchronization objects. Reset or destroy an
unpended command buffer to release recorded references. Deferred buffer/image
destruction transfers ownership to the device until the fence completes and
recorded references disappear; do not access the handle afterward.

Descriptor, submit, transition, debug, allocation, and capture-write arrays or
callbacks are borrowed for the duration stated by the call. Shader code,
front-code, and reflection bytes are copied into the shader object at creation.

## Thread safety

All device, queue, object, capture, and child-object calls require external
synchronization. No handle may be used concurrently, including a parent while
another thread mutates one of its children. Validation and capture callbacks
run synchronously inside the initiating call; they must not re-enter the same
device and must synchronize any application-owned output themselves.

Different devices may be driven by different externally synchronized threads.
The API makes no implicit host-thread progress promise. Fence and GPU-label
waits are bounded by the caller-supplied finite timeout.

## Return values

Unless a row says otherwise, functions returning `int32_t` return `AGC_OK` on
success. Common failures are `AGC_ERROR_INVALID_ARGUMENT` (bad pointer, enum,
version, size, range, or reserved field), `AGC_ERROR_INVALID_STATE` (wrong
object/command/resource state), `AGC_ERROR_NOT_SUPPORTED` (unavailable
capability/profile), `AGC_ERROR_OUT_OF_MEMORY`, `AGC_ERROR_BUSY` (live child,
pending work, unsignaled status, or active capture),
`AGC_ERROR_COMMAND_SPACE_EXHAUSTED`, and `AGC_ERROR_TIMEOUT`. Errors fail before
GPU/process mutation unless a documented asynchronous submission already
succeeded. `agcErrorString` returns immutable library-owned text.

## Type index

### Handles and callbacks

| Types | Contract |
| --- | --- |
| `AgcDevice`, `AgcQueue` | Root runtime and device-owned submission queue. |
| `AgcMemory`, `AgcBuffer`, `AgcImage`, `AgcImageView`, `AgcSampler` | Device-owned memory and resources; placed resources retain memory and views retain their image. |
| `AgcShader`, `AgcGraphicsPipeline`, `AgcComputePipeline` | Immutable compiled state; pipelines retain shaders. |
| `AgcCommandBuffer`, `AgcFence`, `AgcGpuLabel` | Externally synchronized recording and bounded completion objects. |
| `AgcPresentChain`, `AgcCapture` | Device-owned VideoOut and diagnostic-stream objects. |
| `AgcAllocationFunction`, `AgcFreeFunction`, `AgcAllocationCallbacks` | Optional device-lifetime allocator callbacks; the application owns callback state. |
| `AgcDebugMessageFunction`, `AgcCaptureWriteFunction` | Synchronous borrowed callbacks; do not re-enter the device. |

### Creation, query, and operation structures

| Types | Contract |
| --- | --- |
| `AgcDeviceDesc`, `AgcRuntimeInfo`, `AgcDeviceProperties`, `AgcMemoryHeapProperties` | Required capabilities in; selected profile, qualification, physical limits, formats, and heap properties out. |
| `AgcQueueDesc`, `AgcMemoryDesc`, `AgcBufferDesc`, `AgcImageDesc`, `AgcImageViewDesc`, `AgcSamplerDesc` | Versioned child/resource creation descriptions. |
| `AgcPresentChainDesc` | Images, dimensions, format, and VideoOut policy for a device-owned chain. |
| `AgcShaderDesc`, `AgcGraphicsPipelineDesc`, `AgcComputePipelineDesc` | Immutable shader and normalized pipeline creation contracts. |
| `AgcCommandBufferDesc`, `AgcFenceDesc`, `AgcGpuLabelDesc` | Command capacity/queue type and synchronization initial state. |
| `AgcCaptureDesc`, `AgcCaptureInfo` | Capture callback/flags and active/status/count snapshot. |
| `AgcSubmitInfo`, `AgcGpuLabelPoint` | Ordered command list plus transient wait/signal points; arrays are borrowed during submit. |
| `AgcResourceTransition`, `AgcResourceStateInfo` | Typed usage/owner/range transition and committed-state query. |
| `AgcOcclusionQueryLayout`, `AgcOcclusionQueryResult` | Opaque query-record layout and portable reduced result/availability. |
| `AgcDescriptorWrite`, `AgcVertexBufferBinding`, `AgcColorTargetBinding`, `AgcDepthStencilTargetBinding` | Borrowed command-binding descriptions retained by recorded object reference. |
| `AgcViewport`, `AgcScissor`, `AgcDepthBias` | Dynamic command state. |
| `AgcImageSubresourceRange`, `AgcImageLayout`, `AgcImageSubresourceLayout` | Image selection and overflow-safe computed layout results. |
| `AgcOffset3D`, `AgcExtent3D`, `AgcImageSubresourceLayers`, `AgcImageCopyRegion`, `AgcBufferImageCopyRegion` | Versioned layout-derived image-region and buffer/image transfer geometry. |
| `AgcRasterizationState`, `AgcMultisampleState`, `AgcDepthStencilPipelineState`, `AgcStencilFaceState`, `AgcColorBlendAttachmentState` | Normalized immutable graphics-pipeline state. |
| `AgcFenceInfo`, `AgcGpuLabelInfo` | Submission/profile/marker and last bounded-wait diagnostic snapshots. |
| `AgcAllocationInfo`, `AgcMemoryStats` | Object placement and device allocation/leak/high-water snapshots. |
| `AgcDebugCallbackDesc`, `AgcDebugMessage` | Severity/category filter and fixed-size pointer-free diagnostic payload. |

### Runtime enums and flags

| Types | Meaning |
| --- | --- |
| `AgcHardwareFamily`, `AgcQualificationClass`, `AgcRuntimeCapabilityIndex` | Selected hardware family, evidence class, and capability-bit indices. |
| `AgcQueueType`, `AgcCommandBufferState`, `AgcFenceState` | Queue compatibility and command/fence lifecycle states. |
| `AgcMemoryHeap`, `AgcMemoryPropertyFlags`, `AgcMemoryCreateFlagBits`, `AgcObjectType` | Runtime heap, portable heap properties, dedicated-allocation policy, and debug/allocation-query object classification. |
| `AgcResourceType`, `AgcResourceUsage`, `AgcResourceOwner` | Typed transition resource, use, and host/graphics/compute ownership. |
| `AgcFormat`, `AgcImageAspectFlagBits`, `AgcImageAspectFlags` | Image/attachment encoding and aspect mask. |
| `AgcImageTiling`, `AgcImageViewType`, `AgcComponentSwizzle` | Linear/optimal layout selection and normalized view type/component mapping. |
| `AgcMipFilter`, `AgcSamplerBorderColor` | Native sampler mip filtering and fixed/custom border selection. |
| `AgcBufferUsageFlagBits`, `AgcBufferUsageFlags`, `AgcBufferCreateFlagBits` | Buffer permitted uses and upload/readback/dedicated policy. |
| `AgcImageUsageFlagBits`, `AgcImageUsageFlags` | Image sampled/storage/target/transfer/scanout uses. |
| `AgcFilter`, `AgcAddressMode` | Sampler filtering and addressing. |
| `AgcBlendFactor`, `AgcBlendOperation`, `AgcLogicOperation`, `AgcCompareOperation`, `AgcStencilOperation` | Blend, logic, compare, and stencil operations. |
| `AgcPolygonMode`, `AgcCullModeFlagBits`, `AgcCullModeFlags`, `AgcFrontFace` | Rasterization selection. |
| `AgcPrimitiveTopology` | Point/list/strip/fan/adjacency/patch topology; graphics descriptor v5 optionally enables fixed-index primitive restart for strip/fan forms. |
| `AgcDynamicStateFlagBits`, `AgcDynamicStateFlags`, `AgcPrimitiveTopology`, `AgcIndexSize` | Dynamic pipeline fields, primitive topology, and index width. |
| `AgcDebugMessageSeverityFlagBits`, `AgcDebugMessageSeverityFlags` | Diagnostic severity mask. |
| `AgcDebugMessageCategoryFlagBits`, `AgcDebugMessageCategoryFlags` | Diagnostic parameter/state/compatibility/capability/capacity/lifetime mask. |
| `AgcCaptureFlagBits`, `AgcCaptureFlags`, `AgcCaptureRecordType`, `AgcCaptureObjectType`, `AgcCaptureHashAlgorithm` | Capture opt-in, framing vocabulary, stable object kind, and hash algorithm. |

### Shader-reflection types

| Types | Meaning |
| --- | --- |
| `AgcShaderStage`, `AgcShaderPrimitiveTopology` | Compiler stage and required primitive topology. |
| `AgcShaderReflection`, `AgcShaderReflectionFlagBits`, `AgcShaderReflectionFlags` | Versioned complete compiler/runtime contract and feature flags. |
| `AgcShaderHashAlgorithm` | Compiler artifact source-hash identification. |
| `AgcShaderDescriptorType`, `AgcShaderDescriptorMapping` | Reflected set/binding/type/array/access and user-SGPR mapping. |
| `AgcShaderUserSgprKind`, `AgcShaderUserSgpr`, `AgcShaderSystemSgprFlagBits`, `AgcShaderSystemSgprFlags` | User and system SGPR assignments. |
| `AgcShaderPushConstantRange` | Stage-visible push-constant byte interval. |
| `AgcShaderVertexFormat`, `AgcShaderVertexInputRate`, `AgcShaderVertexInput` | Vertex location, format, binding, offset, and rate. |
| `AgcShaderColorExportFormat`, `AgcShaderComponentClass`, `AgcShaderColorExport` | Fragment export slot, encoding, component class/count, and mask. |

## Function reference

The “returns” column lists operation-specific results in addition to the common
values above. “Recorded” means the command retains referenced objects until
reset/destruction; “borrowed” means the input is read only during the call.

### Device, diagnostics, and memory

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreateDevice` | Creates the root; copies descriptor/callback table. | capability, allocation, initialization errors | [compute](../examples/first_compute.c) |
| `agcDestroyDevice` | Requires no live children/deferred objects. | `AGC_ERROR_BUSY` if ownership remains | [cleanup](getting_started.md#first-compute-submission) |
| `agcGetRuntimeInfo` | Writes a snapshot; no ownership transfer. | structure/version validation | [capabilities](getting_started.md#capability-and-error-policy) |
| `agcGetDeviceProperties` | Returns firmware-neutral image/compute limits, format/sample masks, and heap profiles; a null device is valid for pre-device discovery. | structure/version/device validation | [capabilities](getting_started.md#capability-and-error-policy) |
| `agcGetObjectAllocationInfo` | Queries a same-device resource/object. | invalid object/type | [memory](memory_resources.md#diagnostics-and-retirement) |
| `agcSetObjectDebugName` | Copies the name into a supported same-device object. | invalid type/object, allocation | [capture](capture.md#application-setup) |
| `agcGetMemoryStats` | Writes current/high-water/deferred statistics. | structure/version validation | [memory](memory_resources.md#diagnostics-and-retirement) |
| `agcCollectDeferredFrees` | Polls and releases eligible deferred allocations. | backend/invalidation failure | [retirement](memory_resources.md#diagnostics-and-retirement) |
| `agcSetDebugCallback` | Borrows callback state until disabled/device destruction. | invalid mask/callback | [validation](validation.md#enabling-the-layer) |
| `agcGetLastDebugMessage` | Copies the last delivered message snapshot. | no-message state, bad structure | [validation](validation.md#message-contract) |
| `agcErrorString` | Returns immutable static text for a result code. | never returns owned memory | [errors](getting_started.md#capability-and-error-policy) |

### Queues and submission

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreateQueue` | Creates a device child of the selected type. | unsupported queue/profile, allocation | [compute](../examples/first_compute.c) |
| `agcDestroyQueue` | Requires no pending submission. | `AGC_ERROR_BUSY` | [cleanup](../examples/first_compute.c) |
| `agcQueueSubmit` | Borrows arrays; retains commands, fence, waits/signals until recycle/reset. | state/ownership/dependency/capacity/backend errors | [triangle](../examples/first_triangle.c) |

### Buffers and images

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreateMemory` | Creates an explicit flexible or garlic allocation for placed resources. | size/alignment/heap/allocation errors | [memory](memory_resources.md) |
| `agcDestroyMemory` | Releases the application reference; placed resources retain storage until their destruction. | invalid/released handle | [memory](memory_resources.md) |
| `agcMapMemory` | Returns a CPU pointer for one live nonempty allocation range. | invalid/released/range errors | [memory](memory_resources.md) |
| `agcUnmapMemory` | Ends an application mapping epoch; persistent backend mapping remains internal. | invalid/released handle | [memory](memory_resources.md) |
| `agcFlushMemory` | Makes one mapped write range available to the device. | invalid/released/range errors | [visibility](memory_resources.md#visibility-and-staging) |
| `agcInvalidateMemory` | Makes one device-written range visible to the host. | invalid/released/range errors | [visibility](memory_resources.md#visibility-and-staging) |
| `agcCreateBuffer` | Creates device-owned storage from a validated layout. | usage/size/capability/allocation errors | [compute](../examples/first_compute.c) |
| `agcCreatePlacedBuffer` | Creates a buffer bound to an aligned in-range explicit-memory interval; aliasing is permitted. | device/heap/alignment/range errors | [memory](memory_resources.md) |
| `agcDestroyBuffer` | Requires no view/recorded/pending references. | `AGC_ERROR_BUSY` | [cleanup](../examples/first_compute.c) |
| `agcDestroyBufferDeferred` | Consumes application use of the handle; retires after fence/references. | invalid fence/device/state | [retirement](memory_resources.md#diagnostics-and-retirement) |
| `agcWriteBuffer` | Copies into an upload-visible in-range interval. | usage, range, alignment/state errors | [triangle upload](../examples/first_triangle.c) |
| `agcReadBuffer` | Invalidates and copies a host-readable committed interval. | usage/range/state errors | [compute](../examples/first_compute.c) |
| `agcGetBufferStateInfo` | Queries uniform whole-buffer committed state. | `AGC_ERROR_NOT_SUPPORTED` for mixed ranges | [states](memory_resources.md#visibility-and-staging) |
| `agcGetBufferRangeStateInfo` | Queries one exact nonempty byte interval. | range/mixed-state errors | [states](memory_resources.md#visibility-and-staging) |
| `agcGetCommandBufferRangeStateInfo` | Queries effective range state including transitions already recorded in one command buffer. | recording/device/range/mixed-state errors | [states](memory_resources.md#visibility-and-staging) |
| `agcGetOcclusionQueryLayout` | Returns the opaque record size/alignment without exposing RB or packet layout. | output/device/version errors | [queries](memory_resources.md#occlusion-query-storage) |
| `agcResetOcclusionQueryResults` | Host-clears complete query records and publishes HostWrite state. | usage/range/state errors | [queries](memory_resources.md#occlusion-query-storage) |
| `agcGetOcclusionQueryResult` | Finite-waits or polls, invalidates, and reduces all RB snapshots to one value. | busy/timeout/usage/range errors | [queries](memory_resources.md#occlusion-query-storage) |
| `agcCreateImage` | Creates a device-owned computed image allocation. | layout/format/usage/capability/allocation errors | [triangle](../examples/first_triangle.c) |
| `agcCreatePlacedImage` | Creates an image bound to a queried-layout-aligned garlic-memory interval. | device/heap/layout/alignment/range errors | [image layout](memory_resources.md#image-layouts) |
| `agcDestroyImage` | Requires no view/chain/recorded/pending references. | `AGC_ERROR_BUSY` | [triangle cleanup](../examples/first_triangle.c) |
| `agcDestroyImageDeferred` | Retires after fence completion and reference release. | invalid fence/device/state | [retirement](memory_resources.md#diagnostics-and-retirement) |
| `agcWriteImage` | Copies raw allocation bytes to an upload-visible range. | usage/range/state errors | [layout](memory_resources.md#image-layouts) |
| `agcReadImage` | Invalidates and copies a host-readable allocation range. | usage/range/state errors | [layout](memory_resources.md#image-layouts) |
| `agcGetImageLayout` | Pure validated layout query; creates no object. | overflow/format/shape errors | [layout](memory_resources.md#image-layouts) |
| `agcGetImageSubresourceLayout` | Returns one mip/layer/plane offset and pitches. | subresource/range/format errors | [layout](memory_resources.md#image-layouts) |
| `agcGetImageStateInfo` | Queries uniform whole-image committed state. | mixed-state/not-supported | [states](memory_resources.md#visibility-and-staging) |
| `agcGetImageSubresourceStateInfo` | Queries an exact aspect/mip/layer range. | invalid/mixed range | [states](memory_resources.md#visibility-and-staging) |

### Views, samplers, shaders, and pipelines

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreateImageView` | Creates a child retaining its same-device image. | format/range/usage/allocation errors | [descriptors](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcDestroyImageView` | Requires no recorded descriptor reference. | `AGC_ERROR_BUSY` | [descriptors](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcCreateSampler` | Creates immutable normalized sampler state. | enum/capability/allocation errors | [descriptors](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcDestroySampler` | Requires no recorded descriptor reference. | `AGC_ERROR_BUSY` | [descriptors](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcCreateShader` | Copies code/front-code/reflection into immutable state. | record/reflection/stage/version/allocation errors | [compute](../examples/first_compute.c) |
| `agcDestroyShader` | Requires no live pipeline/recorded reference. | `AGC_ERROR_BUSY` | [triangle cleanup](../examples/first_triangle.c) |
| `agcGetShaderReflection` | Copies the shader reflection snapshot. | bad output/version | [reflection](shader_pipelines.md#shared-reflection-abi) |
| `agcCreateGraphicsPipeline` | Creates immutable normalized linked graphics state; retains shaders. | linkage/export/attachment/format/capability errors | [triangle](../examples/first_triangle.c) |
| `agcDestroyGraphicsPipeline` | Requires no recorded command reference. | `AGC_ERROR_BUSY` | [triangle cleanup](../examples/first_triangle.c) |
| `agcCreateComputePipeline` | Creates immutable reflected compute state; retains shader. | mapping/push/wave/LDS/capability errors | [compute](../examples/first_compute.c) |
| `agcDestroyComputePipeline` | Requires no recorded command reference. | `AGC_ERROR_BUSY` | [compute cleanup](../examples/first_compute.c) |

### Command-buffer lifecycle and binding

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreateCommandBuffer` | Creates device child with fixed queue type/capacity. | queue/capacity/allocation errors | [triangle](../examples/first_triangle.c) |
| `agcDestroyCommandBuffer` | Requires not pending; releases recorded references. | `AGC_ERROR_BUSY` | [cleanup](../examples/first_triangle.c) |
| `agcBeginCommandBuffer` | Initial → Recording. | invalid/use-after-submit state | [compute](../examples/first_compute.c) |
| `agcEndCommandBuffer` | Recording → Executable after completeness validation. | missing state/capacity errors | [compute](../examples/first_compute.c) |
| `agcResetCommandBuffer` | Nonpending → Initial; releases recorded references. | `AGC_ERROR_BUSY` if pending | [cleanup](../examples/first_compute.c) |
| `agcRecycleCommandBuffers` | Completed fence plus commands → reset atomically. | busy/device/submission mismatch | [lifecycle](native_runtime.md#command-buffer-states) |
| `agcGetCommandBufferState` | Copies current lifecycle state. | invalid output/object | [lifecycle](native_runtime.md#command-buffer-states) |
| `agcCmdBindGraphicsPipeline` | Records/retains compatible graphics pipeline. | queue/state/capacity errors | [triangle](../examples/first_triangle.c) |
| `agcCmdBindComputePipeline` | Records/retains compatible compute pipeline. | queue/state/capacity errors | [compute](../examples/first_compute.c) |
| `agcCmdBindColorTargets` | Records/retains transitioned images matching pipeline exports. | state/format/count/ownership errors | [triangle](../examples/first_triangle.c) |
| `agcCmdBindDepthStencilTarget` | Records/retains a transitioned compatible depth image or unbind. | aspect/format/state errors | [pipelines](shader_pipelines.md#pipeline-validation) |
| `agcCmdBindDescriptors` | Records reflected writes and retains resources/views/samplers. | reflection/type/range/state/ownership errors | [compute](../examples/first_compute.c) |
| `agcCmdBindVertexBuffers` | Records reflected binding ranges and retains buffers. | binding/stride/range/state errors | [triangle](../examples/first_triangle.c) |
| `agcCmdBindIndexBuffer` | Records/retains an aligned transitioned index buffer. | width/alignment/range/state errors | [triangle](../examples/first_triangle.c) |
| `agcCmdPushConstants` | Copies bytes into recorded command state. | stage/range/reflection errors | [compute](../examples/first_compute.c) |
| `agcCmdSetViewport` | Records dynamic viewport. | pipeline/dynamic/finite-value errors | [triangle](../examples/first_triangle.c) |
| `agcCmdSetScissor` | Records dynamic scissor. | pipeline/dynamic/range errors | [triangle](../examples/first_triangle.c) |
| `agcCmdSetViewportScissors` | Records one to sixteen paired viewport/scissor entries. | pipeline/dynamic/count/value errors | [triangle](../examples/first_triangle.c) |
| `agcCmdSetBlendConstants` | Copies four finite dynamic constants. | pipeline/dynamic/value errors | [binding](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcCmdSetStencilReference` | Records front/back dynamic references. | pipeline/dynamic/state errors | [binding](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcCmdSetDepthBias` | Copies dynamic bias state. | pipeline/dynamic/value errors | [binding](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcCmdSetLineWidth` | Records a qualified dynamic line width. | pipeline/dynamic/value errors | [binding](shader_pipelines.md#resource-and-dynamic-binding) |

### Commands, transitions, and synchronization packets

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCmdTransitionResources` | Records typed range state and retains resources/dependency label. | usage/owner/range/dependency/capacity errors | [compute](../examples/first_compute.c) |
| `agcCmdCopyBuffer` | Records nonoverlapping aligned ranges in CopySource/CopyDestination. | usage/range/alignment/state errors | [transitions](native_runtime.md#explicit-resource-transitions) |
| `agcCmdUpdateBuffer` | Embeds aligned source bytes and writes a retained CopyDestination range. | usage/range/alignment/state/capacity errors | [transitions](native_runtime.md#explicit-resource-transitions) |
| `agcCmdFillBuffer` | Embeds a repeated 32-bit value and fills a retained CopyDestination range. | usage/range/alignment/state/capacity errors | [transitions](native_runtime.md#explicit-resource-transitions) |
| `agcCmdCopyImage` | Records whole compatible image-allocation copy. | shape/format/layout/state errors | [transitions](native_runtime.md#explicit-resource-transitions) |
| `agcCmdCopyImageRegions` | Records color/BC mip, layer, offset, and extent row copies from queried layouts. | format/block/range/state/capacity errors | [transitions](native_runtime.md#explicit-resource-transitions) |
| `agcCmdCopyBufferToImage` | Records strided buffer rows into color/BC image subresources. | usage/footprint/block/state/capacity errors | [transitions](native_runtime.md#explicit-resource-transitions) |
| `agcCmdCopyImageToBuffer` | Records color/BC image subresource rows into a strided buffer footprint. | usage/footprint/block/state/capacity errors | [transitions](native_runtime.md#explicit-resource-transitions) |
| `agcCmdResetOcclusionQueries` | Acquires and clears one or more retained typed query records. | queue/usage/range/capacity errors | [queries](memory_resources.md#occlusion-query-storage) |
| `agcCmdBeginOcclusionQuery` | Records one normal or precise query snapshot after internal QueryWrite acquire. | queue/usage/range/capacity errors | [queries](memory_resources.md#occlusion-query-storage) |
| `agcCmdEndOcclusionQuery` | Records the closing snapshot and cache-flushing availability release. | queue/usage/range/capacity errors | [queries](memory_resources.md#occlusion-query-storage) |
| `agcCmdDraw` | Records a non-indexed draw after complete compatible graphics state. | missing binding/topology/capacity errors | [triangle](../examples/first_triangle.c) |
| `agcCmdDrawIndexed` | Records an indexed draw after complete compatible graphics state. | missing binding/range/topology/capacity errors | [triangle](../examples/first_triangle.c) |
| `agcCmdDrawIndirect` | Records one or more 16-byte draw records from a retained typed indirect buffer. | usage/state/stride/range/reflection/capacity errors | [binding](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcCmdDrawIndexedIndirect` | Records one or more 20-byte indexed-draw records with a bound retained index buffer. | usage/state/stride/range/reflection/capacity errors | [binding](shader_pipelines.md#resource-and-dynamic-binding) |
| `agcCmdDispatch` | Records dispatch after complete reflected compute state. | missing descriptor/push/range/capacity errors | [compute](../examples/first_compute.c) |
| `agcCmdDispatchIndirect` | Records one 12-byte dispatch record from a retained typed indirect buffer. | usage/state/alignment/range/capacity errors | [compute](../examples/first_compute.c) |
| `agcCmdWaitGpuLabel` | Records a wait for an already scheduled monotonic point. | ordering/value/queue/capacity errors | [sync](native_runtime.md#ownership-and-synchronization) |
| `agcCmdSignalGpuLabel` | Records an increasing nonterminal signal and retains label. | stale/decreasing/wrap/capacity errors | [sync](native_runtime.md#ownership-and-synchronization) |

### Fences and GPU labels

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreateFence` | Creates a device child in requested binary state. | descriptor/allocation errors | [compute](../examples/first_compute.c) |
| `agcDestroyFence` | Requires no pending submission/deferred dependence. | `AGC_ERROR_BUSY` | [cleanup](../examples/first_compute.c) |
| `agcGetFenceStatus` | Nonblocking status poll. | `AGC_OK` if signaled; `AGC_ERROR_BUSY` otherwise | [fences](native_runtime.md#fences-and-errors) |
| `agcGetFenceInfo` | Copies submission/profile/marker/wait snapshot. | bad structure/object | [debug](getting_started.md#capability-and-error-policy) |
| `agcResetFence` | Signaled/unused → unsignaled. | pending/busy state | [fences](native_runtime.md#fences-and-errors) |
| `agcWaitFence` | Bounded host wait; never accepts an infinite sentinel. | `AGC_OK`, `AGC_ERROR_TIMEOUT`, backend failure | [compute](../examples/first_compute.c) |
| `agcCreateGpuLabel` | Creates device-owned monotonic GPU-visible word. | descriptor/allocation errors | [sync](native_runtime.md#ownership-and-synchronization) |
| `agcDestroyGpuLabel` | Requires no recorded/pending/deferred reference. | `AGC_ERROR_BUSY` | [sync](native_runtime.md#ownership-and-synchronization) |
| `agcGetGpuLabelStatus` | Nonblocking reached-or-passed poll. | `AGC_OK` if reached; `AGC_ERROR_BUSY` otherwise | [sync](native_runtime.md#ownership-and-synchronization) |
| `agcGetGpuLabelInfo` | Copies scheduled/observed/producer/wait snapshot. | bad structure/object | [debug](getting_started.md#capability-and-error-policy) |
| `agcWaitGpuLabel` | Bounded host reached-or-passed wait. | `AGC_OK`, `AGC_ERROR_TIMEOUT`, terminal/value errors | [sync](native_runtime.md#ownership-and-synchronization) |

### Presentation

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreatePresentChain` | Creates a chain retaining dedicated 1920×1080 linear `RGBA8_UNORM` or `BGRA8_SRGB` scanout images. | capability/shape/format/VideoOut errors | [presentation](native_runtime.md#current-qualification-boundary) |
| `agcDestroyPresentChain` | Releases retained images after no active present. | `AGC_ERROR_BUSY`, VideoOut failure | [presentation](native_runtime.md#current-qualification-boundary) |
| `agcPresent` | Waits finite ready fence/VSYNC and presents Host/graphics-qualified scanout image. | timeout/state/index/VideoOut errors | [presentation](native_runtime.md#current-qualification-boundary) |

### Capture

| Function | Ownership/state | Returns | Example |
| --- | --- | --- | --- |
| `agcCreateCapture` | Creates device child borrowing write callback state for capture lifetime. | invalid callback/flags/allocation | [setup](capture.md#application-setup) |
| `agcDestroyCapture` | Requires inactive capture. | `AGC_ERROR_BUSY` | [setup](capture.md#application-setup) |
| `agcBeginCapture` | Makes this device’s inactive capture active and emits header/runtime state. | busy/write/status error | [setup](capture.md#application-setup) |
| `agcEndCapture` | Emits terminal counts and makes capture inactive. | inactive/write/status error | [setup](capture.md#application-setup) |
| `agcGetCaptureInfo` | Copies active/status/record/byte/ID counters. | bad structure/object | [format](capture.md#binary-format) |
| `agcCaptureRecordReadbackHash` | Invalidates and hashes a selected same-device readback resource range. | object/type/range/usage/state/write errors | [capture](capture.md#application-setup) |

## Examples

- [First compute](../examples/first_compute.c) is the shortest complete
  reflection → resource → transition → dispatch → bounded fence lifecycle.
- [First indexed triangle](../examples/first_triangle.c) is the shortest
  reflection → graphics pipeline → upload → target → indexed draw lifecycle.
- [Capture reference frame](../tests/capture_reference_frame.c) demonstrates
  capture creation, object names, semantic records, readback hashing, and
  reverse-order destruction while capture remains active.
- [Getting started](getting_started.md) explains installed-package CMake use;
  [native runtime](native_runtime.md), [memory/resources](memory_resources.md),
  [shader/pipelines](shader_pipelines.md), [validation](validation.md), and
  [capture](capture.md) provide the task-oriented guides.
