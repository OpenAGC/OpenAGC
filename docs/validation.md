# Native runtime validation and diagnostics

OpenAGC runtime API v23 adds an optional, synchronous validation callback. It
turns existing fail-closed return paths into actionable diagnostics without
changing the safety contract or requiring a logging dependency.

## Enabling the layer

Initialize `AgcDebugCallbackDesc` with `AGC_DEBUG_CALLBACK_DESC_INIT`, set its
`callback` and `user_data`, then call `agcSetDebugCallback`. The default masks
select every severity and category. Applications may narrow them before
installation. Passing `NULL` disables optional messages.

```c
static void PS5_SYSV_ABI debug_message(
    void *user_data, const AgcDebugMessage *message)
{
    (void)user_data;
    fprintf(stderr, "%s: %s (%s)\n", message->function_name,
        message->message, agcErrorString(message->result));
}

AgcDebugCallbackDesc debug = AGC_DEBUG_CALLBACK_DESC_INIT;
debug.callback = debug_message;
if (agcSetDebugCallback(device, &debug) != AGC_OK) {
    /* Handle setup failure. Required runtime validation is still active. */
}
```

The callback is allocation-free inside the runtime and runs synchronously on
the thread making the public API call. It must copy any message data it wants
to retain and must not re-enter the same externally synchronized device.

## Message contract

Every delivered `AgcDebugMessage` contains:

- a monotonically increasing sequence number for delivered messages;
- one severity and one validation category;
- the unchanged public result code;
- the public function name;
- an object type and application debug name when one is available;
- a bounded explanation of the violated rule.

Filtered messages do not consume sequence numbers. `agcGetLastDebugMessage`
copies the most recently delivered message and returns `AGC_ERROR_NOT_FOUND`
before the first delivery. The fixed-size snapshot contains no object pointer,
GPU address, or allocation address, so it is suitable for later capture-stream
serialization without leaking process-specific addresses.

The completed Milestone 5 validation coverage reports:

- invalid descriptor versions, reserved fields, enums, flags, counts, and
  pointers;
- command-buffer state misuse, including reuse after submission without reset;
- misaligned, empty, overlapping, or out-of-range GPU-backed byte intervals;
- missing or mismatched typed transitions and queue ownership;
- shader-reflection, stage-linkage, descriptor, push-constant, vertex-input,
  export/attachment, depth/stencil, and multisample incompatibility;
- integer-target blending and unsupported dual-source behavior;
- unsupported wave, scratch, LDS, workgroup, tessellation, geometry,
  rasterization, and multisample capabilities;
- command-buffer dword and transition-journal exhaustion;
- premature destruction of resources, views, samplers, shaders, pipelines,
  command buffers, fences, queues, and devices.

Messages identify the public entry point, retain the exact `AGC_ERROR_*`
result, and state the corrective contract. API v24+ capture serializes the
same pointer-free messages even when the application callback is disabled.
The host invalid-program matrix exercises every category and verifies that
diagnostic delivery performs no allocation attempt even when the next
application allocator call is forced to fail.

## Release behavior

Disabling the callback suppresses only optional reporting. Invalid programs
still receive the same `AGC_ERROR_*` result and the runtime still rejects the
operation before prohibited object, command, GPU, or process mutation. This
separation allows release builds to omit logging overhead without weakening
required safety checks.
