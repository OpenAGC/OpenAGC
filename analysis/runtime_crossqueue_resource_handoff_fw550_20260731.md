# FW 5.50 native runtime v2 resource-handoff oracle

Date: 2026-07-31

Target: standard PS5, system software raw `0x05500008`, normalized ABI key
`0x0550`, runtime profile `prospero-gc-submit16-standard-standard`.

Artifact SHA-256:

```
a8becfe1cf68a988c997fe506849bf549365a7ff6c472efe7b2504e6e2c41797
```

The public native-runtime sample `agc_runtime_eop.elf` used no application
shader, descriptor, draw, or dispatch state. It established a whole 64-byte
storage buffer as compute `shader-write`, waited for that setup fence, then:

1. Recorded and submitted a v2 `AGC_RESOURCE_TRANSITION_RELEASE_BIT` from
   compute `shader-write` ownership to graphics `shader-read` ownership with
   label value 1.
2. Without a CPU wait for that release, recorded and submitted the matching
   graphics v2 `AGC_RESOURCE_TRANSITION_ACQUIRE_BIT`.
3. Bounded-waited the graphics fence and then the compute-release fence.
4. Reset both command buffers and destroyed both fences, the label, buffer,
   queues, and device.

Observed result: every API call returned `AGC_OK`; the sample ended with
`Native runtime cross-queue handoff result: PASS`.

This qualifies only the exact whole-buffer, compute `shader-write` to graphics
`shader-read` v2 release/acquire row on FW 5.50 standard PS5. It does not
qualify shader execution, partial ranges, images, depth/color metadata,
submit wait/signal lists, timeline rollover, or FW 11.60.
