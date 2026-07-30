# Runtime resource-state diagnostics — host qualification

Date: 2026-07-31

Runtime API v16 adds one versioned `AgcResourceStateInfo` layout and two typed
queries:

- `agcGetBufferStateInfo`
- `agcGetImageStateInfo`

The snapshot reports the state needed to diagnose transition and lifetime
failures without exposing allocation addresses or backend objects:

- committed usage and owner;
- pending transfer usage, owner, label, and monotonic point;
- whether a matching acquire has been recorded but not submitted;
- recorded-command and dependent-object reference counts; and
- fence-keyed deferred-retirement state.

## Host oracle

`test_runtime_resource_transitions` proves:

1. new buffers and images report `Undefined` and Host ownership;
2. nonzero reserved input fails closed;
3. recording retains a resource but does not publish its destination state;
4. successful submit publishes the committed usage and owner;
5. a source release preserves the committed source state while exposing the
   exact pending destination, label, and value;
6. a recorded acquire is distinguishable from a submitted acquire; and
7. successful acquire clears pending diagnostics and publishes destination
   ownership.

`test_runtime_fence_deferred_free` additionally proves deferred buffers and
images remain queryable, with the deferred flag set, until fence-driven
collection recycles them.

Incremental verification after implementation: `16444 passed, 0 failed`.
Full clean verification is required before the checkpoint commit.
