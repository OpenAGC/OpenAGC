# Runtime API v23 validation debug layer — host — 2026-07-31

## Contract

The first Milestone 5 slice adds one optional, synchronous debug callback per
device. It uses fixed-size versioned structures, performs no allocation while
emitting a message, preserves the original public error code, and leaves all
required safety validation active when disabled.

`AgcDebugMessage` is 368 bytes with its text field at offset 144.
`AgcDebugCallbackDesc` is 64 bytes. Static assertions and public tests lock
both layouts. Messages contain bounded function, object-name, and explanation
strings but no pointer or GPU address.

## Initial diagnostic coverage

The initial hooks cover:

- premature device destruction with live children;
- command-buffer begin, end, and pending-reset state violations;
- invalid single-command submission descriptors and queue/device mismatch;
- signaled-fence reuse;
- unsatisfied waits and stale/decreasing label signals;
- submission transitions that disagree with committed resource state;
- invalid submit wait/signal lists;
- insufficient command storage for runtime-owned submission packets.

The callback masks severity and category independently. Filtered messages do
not consume the deterministic per-device sequence. A versioned query returns
the last delivered snapshot for tooling and the future capture stream.

## Verification

The focused host fixture installs, filters, queries, disables, and re-enables
the optional layer around intentionally invalid command-buffer and lifetime
operations. It proves that callback selection never changes the public safety
result, includes the application debug name, and does not consume sequence
numbers for filtered messages.

The clean generic build plus the final mask-negative fixture passes 16,946
assertions with zero failures. CTest
passes 7/7, and the Prospero library builds with `-Wall -Wextra -Wpedantic`
without a new warning.
