# Runtime buffer byte-range states — host qualification

Date: 2026-07-31

Runtime API v17 extends `AgcResourceTransition` buffer offsets and sizes from a
whole-buffer-only contract to bounded same-queue byte ranges.

## State model

Each buffer owns a bounded sorted set of contiguous half-open intervals.
Adjacent intervals with the same usage and owner merge after commit. Recording
uses command-local overlays; submission replays those overlays against the
current committed intervals before any driver call. Capacity is reserved
before packet mutation, so successful driver submission cannot encounter a
state-table allocation failure during commit.

The low-level gfx1013 barrier remains the qualified whole-cache operation. The
range is an application validation and ownership boundary; applications still
never select cache-control bits.

## Host oracle

Generic coverage proves:

1. a middle-range transition remains invisible until successful submit;
2. submit splits the original state into left, middle, and right intervals;
3. exact range queries return their usage/owner and mixed queries fail closed;
4. returning the middle interval to its neighbors merges the whole buffer;
5. out-of-bounds ranges reject before command mutation;
6. copies accept only their exact transitioned source/destination bytes;
7. storage descriptors reject a wider range and accept the exact range;
8. vertex and index bindings accept transitioned tails and reject bytes before
   their binding offset;
9. ordered multi-command-buffer dependencies simulate partial intervals,
   reject reversed order, and commit atomically; and
10. 32 alternating eight-byte intervals grow, query, and merge back to one
    whole-buffer state without leaking dynamic state storage; and
11. injected interval-capacity allocation failure leaves command bytes and
    committed state unchanged, then retries and frees through callbacks; and
12. partial cross-queue ownership transfer remains explicitly unsupported.

Incremental result: `16634 passed, 0 failed`.

Partial image subresources and partial cross-queue ownership are separate
remaining Milestone 4 gates.
