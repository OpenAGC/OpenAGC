# Runtime Firmware and Backend Selection

## Goal

Choose a native driver backend before any backend opens `/dev/gc`, loads an
installed AGC module, or mutates console GPU state. Selection must never infer
that a nearby or newer firmware uses the FW 5.50 private ABI.

## PS5 system-version ABI

The payload SDK exports `sceKernelGetProsperoSystemSwVersion` through its
libkernel stubs but does not declare the argument type in its public headers.
Local PS5SDK and etaHEN references agree on this layout:

```text
offset  size  field
0x00    0x08  reserved
0x08    0x1c  version string
0x24    0x04  numeric version
0x28    0x08  reserved
total   0x30
```

`driver_registry.c` has size and offset static assertions for this private
query structure. Numeric versions use BCD-like bytes: FW 5.50 is
`0x05500000`; normalization produces major 5, minor 50, patch 0 while retaining
the complete raw value for registry matching.

## Selection rules

- The generic build selects `agcGenericDriverOps` without a firmware query.
- The Prospero build starts with no selected operations table.
- `sce_agc_initialize` performs the version query and registry lookup before
  calling any backend initialization callback.
- Registry lookup requires an exact raw-version alias and all requested
  capability flags.
- The only native registry entry currently eligible is FW 5.50
  (`0x05500000`) mapped to `prospero-fw550-direct`.
- Detection failure, FW 5.51, FW 11.60, and every other unregistered value
  return `AGC_ERROR_NOT_SUPPORTED` with no selected backend.
- The Sony export table is not automatically probed or selected. Its FW 5.50
  payload-context submits returned success without executing GPU markers, and
  probing it changed `/dev/gc` behavior across later processes. It may become
  eligible only after a non-destructive capability method is independently
  validated.

This means there is no Sony-then-direct fallback path. Adding another direct
backend requires a new explicit raw-version alias entry backed by independent
firmware RE and hardware validation.

## Verification

Host tests inject a fake detector and registry to cover:

- BCD normalization and raw-value retention
- multiple explicit aliases for one backend
- required-capability filtering
- nearby unknown firmware rejection
- detector failure and invalid arguments

Verification on 2026-07-26:

- clean generic build: PASS
- generic tests: 2066 passed, 0 failed
- Prospero cross-build: PASS, no compiler warnings
- `agc_init.elf` link with the libkernel query: PASS
- FW 5.50 hardware detection and direct-backend entry: PASS; direct `/dev/gc`
  diagnostics executed
- full hardware init/submit in that run: INCONCLUSIVE, returning `0x8089000B`
  in the console session previously contaminated by the Sony module probe;
  reboot is required before repeating the direct-backend smoke test
