# Runtime Firmware and Backend Selection

## Selection contract

Prospero queries the complete system-software version before OpenAGC opens
`/dev/gc`. The ABI key is the upper 16 bits (for example `0x0550` from
`0x05500008`), and only exact keys in the installed-driver and direct-driver
tables are eligible. Numeric ranges and nearest-version matching are forbidden.

`OPENAGC_PREFER_INSTALLED_AGC_DRIVER` defaults to `ON` for Prospero consumers.
That mode exports `SceAgcDriver` and `--no-as-needed` transitively from
`OpenAGC::openagc`, making the matching module loader-owned and discoverable
before OpenAGC initializes. Make consumers use the equivalent
`-Wl,--no-as-needed -lSceAgcDriver`. Setting the option to `OFF` produces a
direct-only build with no Sony dependency.

The preferred path uses `kernel_dynlib_handle` followed by
`kernel_dynlib_dlsym` on that exact module handle. It never calls `dlopen`,
never uses `RTLD_DEFAULT`, and never unloads the module. Selection has three
outcomes:

- complete preloaded profile: select `sony-installed`;
- genuinely absent module: select the exact `/dev/gc` profile, if available;
- present but incomplete or self-resolving module: fail closed.

Once Sony is selected, errors and fence timeouts are returned to the caller.
There is no runtime switch to `/dev/gc`. Shutdown resets OpenAGC dispatch state
but leaves loader-owned module state intact; a later initialization may select
the same module again.

The generic build continues to select `agcGenericDriverOps` without firmware
or module discovery.

## Runtime ownership

OpenAGC still owns PM4 generation, resources, transitions, EOP fence packets,
bounded waits, and VideoOut. The installed driver only carries DCB, ACB,
multi-DCB, async-graphics, and ABI-safe diagnostics. Module initialization,
internal memory, defaults, and shutdown use loader-owned adapters. Private
queue/suspend carriers, permission stubs, incompatible workload builders, and
nonexistent EOP convenience exports remain unsupported.

That list describes the current implementation, not the completion criterion.
The Sony backend is complete only when carrier selection preserves every
native OpenAGC capability consumed by Vulkan-PS5. Functional
`sceAgcDriverSetTFRing` forwarding is mandatory and now preserves the
tessellation entry point; hardware qualification remains pending. The current
success-only lifecycle adapters are still a blocking gap. Exhaustive
private-ioctl parity is not required unless a native/Vulkan capability depends
on the operation.

`agcDriverDebugBackendName()` reports `sony-installed`, and the existing
`agcGetRuntimeInfo()` profile string incorporates that backend name without a
public ABI change.

## Verification

Host tests cover exact profile lookup, optional and mandatory exports,
self-resolution rejection, preference, absent-module fallback, incompatible
module fail-closed behavior, lifecycle reuse, and byte-to-dword multi-DCB
adaptation. Both Prospero modes build with `-Wall -Wextra -Wpedantic`.

The Prospero Vulkan-PS5 compute target inherits `libSceAgcDriver.sprx` through
`OpenAGC::openagc`; no Vulkan source-level selector is present.

Hardware status remains experimental and unqualified. A prior mutating FW 5.50
`dlopen` probe returned `AGC_OK` without marker execution and contaminated later
direct submission until reboot. The new path is read-only after preload, but a
Sony `AGC_OK` result is still insufficient: marker execution and a bounded
fence are mandatory qualification gates.
