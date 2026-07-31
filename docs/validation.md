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

The first API-v23 slice reports device-lifetime violations, invalid command
buffer begin/end/reset states, invalid single-command submission descriptors,
queue/device mismatches, fence reuse, unsatisfied or decreasing GPU-label
dependencies, mismatched committed resource transitions, and submission-time
command-space exhaustion. Later Milestone 5 slices extend the same message
contract across creation, pipeline compatibility, descriptor, range, and
resource-lifetime validation. API v24 capture serializes these pointer-free
messages even when the application callback is disabled.

## Release behavior

Disabling the callback suppresses only optional reporting. Invalid programs
still receive the same `AGC_ERROR_*` result and the runtime still rejects the
operation before prohibited object, command, GPU, or process mutation. This
separation allows release builds to omit logging overhead without weakening
required safety checks.
