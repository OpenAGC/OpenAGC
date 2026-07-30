# Runtime transition matrix — host — 2026-07-31

## Audit result

The Milestone 4 exit audit found that the low-level matrix had exact fixtures
for representative render, compute, copy, presentation, host, and depth rows,
but did not enumerate every accepted pair. It also found a contract mismatch:
the public runtime described `Undefined` as a discard state and permitted it in
resource-usage validation, while `agcGfx1013ValidateTransition` rejected it as
every transition destination.

Runtime API v15 resolves the mismatch. `Undefined` is now both an initial
source and a zero-packet discard destination. Discarding prior writer contents
does not emit a release because no consumer may observe those contents; it also
does not emit an acquire. Completion addresses remain invalid when no release
is required.

## Exhaustive oracle

The gfx1013 enum has ten states: undefined, render target, compute write, copy
source, copy destination, shader read, present, host read, depth/stencil write,
and depth/stencil read. The host fixture now evaluates all 100 ordered pairs.
For every pair it proves:

- the size query succeeds and returns the derived exact dword count;
- command emission succeeds and advances by exactly that count;
- writer transitions that preserve contents start with `RELEASE_MEM` and keep
  the hardware-proven two-dword NOP trailer;
- depth-writer releases include the metadata flush prefix;
- consumer visibility paths end with the eight-dword `ACQUIRE_MEM` packet;
- every destination-discard row emits zero dwords.

Existing representative fixtures continue to compare every emitted word for
release, acquire, depth metadata, render-to-present, present-to-render,
read-only no-op, and short-buffer atomicity.

At the public layer, a buffer in committed HostRead state records
`HostRead -> Undefined`, submits it on the compute command carrier, commits the
new state only after submission, resets its finite fence and command, and
destroys without stale ownership.

## Verification

- Generic complete suite: 16,402 passed, 0 failed.
- CTest: seven of seven suites pass.
- Generic and Prospero builds: no compiler warnings.

This closes the exhaustive low-level host-matrix and public discard rows. Exact
firmware execution labels for non-discard transition endpoints remain separate
and are tracked in `PLAN.md` and `STATUS.md`.
