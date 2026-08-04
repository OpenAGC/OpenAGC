# FW 5.50 Sony Export Forwarding

## Experimental result

OpenAGC now treats a complete, already-loaded FW 5.50
`libSceAgcDriver.sprx` as the experimental primary carrier. Discovery is
module-specific and non-mutating. If the module is absent, the qualified
direct `/dev/gc` backend remains available; if it is present but incomplete,
initialization fails closed and never falls back.

This is an implementation promotion, not a hardware-qualification claim. The
only previous installed-driver hardware attempts returned `AGC_OK` without
executing observable markers. They used mutating module loading and altered
later direct behavior until reboot. The preloaded path must pass the new
qualification artifact before its status changes.

## Forwarded surface

Five exports are mandatory for FW 5.50:

- `sceAgcDriverSubmitMultiDcbs`
- `sceAgcDriverSubmitDcb`
- `sceAgcDriverSubmitAcb`
- `sceAgcDriverSetupAsyncGraphics`
- `sceAgcDriverGetPaDebugInterfaceVersion`

The multi-DCB adapter validates nonzero aligned byte sizes, bounds the array to
`0xfff` entries, converts sizes to dwords, and rejects ACB arrays that the Sony
entry point cannot represent. ABI-safe capture, Razor, SDMA, and diagnostic
exports are optional.

OpenAGC deliberately does not forward permission-only Direct suspend/TF/HS
exports, private queue helpers, the incompatible workload builders,
`sceAgcDriverSubmitToHDRScopesACQ`, or the nonexistent EOP convenience export.
OpenAGC emits its own EOP fence packets and continues to own VideoOut.

The loader-owned initialization, internal-memory, default-state, and shutdown
operations use adapters. Credential repair runs in the Sony initialize adapter
before the first forwarded operation. The module is never unloaded.

## Qualification artifacts

`samples/hw_test/agc_init.elf` has no Sony `DT_NEEDED` entry and requires the
direct backend. `agc_init_sony.elf` forces the FW 5.50 dependency, requires
`sony-installed`, and gates three two-DCB marker runs, the nine-dword bounded
wait path, async setup, unsupported private helpers, memory release, and clean
OpenAGC shutdown. `make backend_fw550_check` verifies the opposing dependency
sets locally.

Hardware order is strict: clean reboot, direct baseline, reboot, Sony marker
and bounded-fence gate, then native compute/graphics and Vulkan compute/
presentation. Never run a direct test after a Sony failure without rebooting.
An `AGC_OK` return without marker execution is a failed gate.

FW 3.20 and the other exact installed profiles are cross-firmware RE evidence
only and remain hardware-unverified. Generated stubs and firmware binaries are
not committed.
