# Idempotent ordinary transition replay

## Scope

Vulkan command buffers may be recorded concurrently. A command buffer can
therefore record an ordinary resource transition before an earlier queue
submission commits the same transition. At submission time, the committed
resource state can already equal the recorded destination even though it no
longer equals the recorded source.

OpenAGC now accepts this narrow replay case when all of the following hold:

- the transition has no release, acquire, or batch-dependency flags;
- the committed usage equals the recorded `after` usage; and
- the committed owner equals the recorded `after_owner`.

All other source-state or ownership mismatches remain invalid. In particular,
queue ownership transfers and dependency-bearing transitions cannot use this
replay rule.

## Host evidence

The runtime regression records two graphics command buffers from the same
undefined image state, submits the first undefined-to-color transition, then
submits the independently recorded second command. The second submission
succeeds because the committed state already equals its destination. Existing
partial-range, ownership-transfer, stale-label, and failure-cleanup coverage
continues to pass.

Verified on 2026-08-01:

- `openagc_tests`: 17,825 passed, 0 failed.
- complete generic CTest: 19/19 passed.
- Vulkan-PS5 generic CTest: 46/46 passed.
- Vulkan-PS5 sanitizer CTest: 46/46 passed.

FW 5.50 hardware qualification remains pending after the host change.
