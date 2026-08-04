# FW 5.50 Sony Export Forwarding

## Experimental result

OpenAGC now treats a complete, already-loaded FW 5.50
`libSceAgcDriver.sprx` as the experimental primary carrier. Discovery is
module-specific and non-mutating. The Sony-linked artifact requires a complete
installed module and fails closed for any discovery or resolution failure.

The Sony-linked artifact is strict: module absence or incomplete resolution
fails initialization and never selects `/dev/gc`. Direct operation exists only
in the separately built `OPENAGC_PREFER_INSTALLED_AGC_DRIVER=OFF` artifact.
On successful selection the runtime emits
`[openagc] backend=sony-installed installed_driver=true direct_gc=false` so a
client gate can prove the carrier choice and reject direct-backend activation.
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

## Required OpenAGC/Vulkan functional parity

The product goal is not exhaustive parity with every private `/dev/gc` ioctl.
It is carrier substitution below OpenAGC's native runtime: selecting
`sony-installed` must preserve every OpenAGC capability currently consumed by
`../Vulkan-PS5` and available through the qualified direct backend.

The Vulkan consumer calls only the firmware-neutral `agc*` API. Its backend
requirements reduce to the following carrier matrix:

| Vulkan-visible OpenAGC behavior | Required Sony/OpenAGC path | Current status |
| --- | --- | --- |
| Device creation | establish credentials plus the installed AGC context, internal state, and defaults | **Blocking gap:** current lifecycle adapters return success without establishing or proving this state |
| Graphics queue submission | `sceAgcDriverSubmitDcb` and `sceAgcDriverSubmitMultiDcbs` | resolved and host-adapted; hardware marker/fence still fails |
| Compute queue submission | `sceAgcDriverSetupAsyncGraphics`, then OpenAGC's qualified DCB compute path | export resolved; depends on the lifecycle fix |
| Multi-command-buffer batches | convert OpenAGC byte sizes to Sony dword sizes and call multi-DCB | adapter implemented; ACB arrays are not used by the Prospero Vulkan runtime |
| Tessellation | OpenAGC-owned ring storage and PM4 plus `sceAgcDriverSetTFRing` | **Blocking gap:** functional Sony export exists across the inspected active corpus but is not forwarded |
| Fences and finite waits | OpenAGC-emitted EOP packets, Sony DCB transport, CPU bounded wait | implementation exists; first Sony hardware fence times out |
| Memory, resources, pipelines, and transitions | remain OpenAGC-owned | carrier-independent by design |
| Presentation | OpenAGC-owned VideoOut path | carrier-independent by design |
| Device destruction/recreation | reset OpenAGC state while preserving valid loader-owned Sony state | adapter exists, but lifecycle semantics are not yet proven |

Prospero compute currently uses DCB submission after async setup; it does not
require the direct backend's authenticated user-special queue or ACB carrier.
Likewise, Vulkan does not consume the private suspend, workload-diagnostic,
Razor, capture, HDR-scopes, or permission-only Direct exports. Those operations
remain separate low-level compatibility work and are not blockers for this
parity target. If a future Vulkan/OpenAGC feature begins consuming one, it
enters the required carrier matrix at that point.

SharpProspero provides one important lifecycle clue: its `AgcDevice.Initialize`
calls `sceAgcInit` and retains the associated state before calling
`sceAgcDriverSubmitDcb`. This agrees with the observed failure of OpenAGC's
preload-only/no-op lifecycle assumption. It does not establish the exact fix:
SharpProspero hard-codes defaults revision 8, exposes a legacy two-argument init
shape, and is not tied to an exact firmware. OpenAGC must recover the matching
module-specific lifecycle and version contract, avoid resolving its own public
wrappers, and retain exact-firmware fail-closed selection.

Acceptance requires the Sony artifact to preserve the same Vulkan-visible
capability set as the direct artifact and pass, after a clean reboot: lifecycle
preflight, DCB/multi-DCB marker execution, bounded fence completion, native
compute and graphics, tessellation factor-ring use, Vulkan compute/graphics,
presentation, teardown, and immediate relaunch. A successful return value
without observable GPU completion is failure. No parity step may open or issue
an ioctl to `/dev/gc` directly after Sony selection.

## Cross-firmware export audit (2026-08-04)

The forwarded surface was checked by decoded export name and raw NID against
the `libSceAgcDriver.sprx` corpus under `/Volumes/Untitled/unp/`. The corpus
decoder in `/Volumes/Untitled/unp/build_import_export_db.py` and the local SDK
`aerolib.csv` were used so that version-specific export tables were compared by
recovered name rather than by address. This is reference evidence only;
firmware modules and generated proprietary stubs are not committed.

All five mandatory exports exist in every parseable module from FW 1.00
through FW 12.70, including every directly inspected exact firmware profile
selected by OpenAGC. Their NIDs are stable across the checked corpus:

| Export | NID | Availability |
| --- | --- | --- |
| `sceAgcDriverSubmitMultiDcbs` | `6UzEidRZwkg` | all parseable versions |
| `sceAgcDriverSubmitDcb` | `UglJIZjGssM` | all parseable versions |
| `sceAgcDriverSubmitAcb` | `gSRnr79F8tQ` | all parseable versions |
| `sceAgcDriverSetupAsyncGraphics` | `Vlaj1gwmIFA` | all parseable versions |
| `sceAgcDriverGetPaDebugInterfaceVersion` | `Pqxglq1oKec` | all parseable versions |

The optional exports have two historical availability differences:

- `sceAgcDriverSetTargetRingForDiag` is absent on FW 1.00 through FW 2.50
  and present from FW 3.00 onward.
- `sceAgcDriverSdmaCopyLinearBlocking` is absent on FW 1.00 through FW 3.20
  and present from FW 4.00 onward. The FW 3.20 manifest already records this
  slot as unavailable and never attempts to resolve or call it.
- The capture-interface and Razor ACQ exports selected by OpenAGC are present
  throughout the parseable corpus. They remain optional because their
  presence is not required for command submission or runtime operation.

Export-wrapper disassembly found no calling-convention change in the mandatory
surface. `SubmitMultiDcbs` retains the same three-argument register shuffle and
20-byte wrapper, while `SubmitDcb` retains its single-pointer ABI and 15-byte
wrapper. `SubmitAcb` continues to accept an owner handle and packet pointer,
and `SetupAsyncGraphics` continues to accept one 32-bit pipe identifier. The
latter two wrappers change size and internal behavior across firmware, so this
evidence establishes export-boundary ABI shape, not identical implementation
semantics.

The corpus has two extraction exceptions. The available FW 7.00 file is an
encrypted/non-ELF image and could not be audited directly. OpenAGC nevertheless
includes an exact `fw700-installed` profile in the 7.x installed-driver family:
the mandatory export names, NIDs, and wrapper ABI are identical on the adjacent
FW 6.50 and FW 7.01 modules, and every later inspected 7.x module uses the same
surface. This is an explicit family inference, not an exact-SPRX verification.
SharpProspero independently uses name-based `libSceAgcDriver` bindings for
`sceAgcDriverSubmitDcb` and `sceAgcDriverSubmitMultiDcbs`, but it does not carry
FW 7.00-specific proof and is treated only as secondary corroboration. Its
one-argument `sceAgcDriverSubmitAcb` declaration is not used as authority over
OpenAGC's recovered carrier ABI. One duplicated FW 12.60 file has a malformed
dynamic table, but the alternate FW 12.60 module is valid and contains the
complete selected surface.

No currently selected OpenAGC profile is therefore incompatible because a
mandatory Sony export is missing. FW 1.00, FW 2.00, FW 2.50, and FW 3.00 remain
unsupported because OpenAGC deliberately has no exact installed profile for
them, not because the mandatory forwarding exports were absent. Export
presence and register-level ABI shape do not prove cross-firmware packet
layouts, initialization state, or GPU execution behavior; every non-FW-5.50
installed profile, including inferred FW 7.00, remains experimental and
hardware-unverified.

## SharpProspero cross-reference (2026-08-04)

`/Users/bizkut/Downloads/PS5/homebrew/SharpProspero` was compared with the
OpenAGC public driver surface and the private Sony-forwarding manifest.
SharpProspero provides C# `LibraryImport` declarations and a stub-name catalog;
it does not implement or emulate `libSceAgcDriver` and does not attach its AGC
declarations to an exact firmware version. It is therefore useful for an
independent API-shape comparison, but not as firmware-qualification evidence.

The source trees contain 41 overlapping `sceAgcDriver*` names. SharpProspero's
79 declarations include APIs outside OpenAGC's current compatibility surface,
while OpenAGC declares 66 driver APIs including Direct/private carriers that
SharpProspero does not model. At its higher layer, SharpProspero's `AgcDevice`
actually calls `sceAgcDriverSubmitDcb`; its other selected driver declarations
are mostly raw bindings rather than exercised backend implementations.

The strongest agreement is the DCB submission record. SharpProspero declares a
16-byte sequential structure with the command address at offset 0, the dword
count at offset 8, and a flag/reserved tail. This exactly matches OpenAGC's
statically asserted `AgcCommandBufferSubmit` layout. Both projects also agree
that `sceAgcDriverSubmitDcb` accepts a pointer to that record and that
`sceAgcDriverSubmitMultiDcbs` receives a DCB-address array, a dword-size array,
and a 32-bit count. SharpProspero also agrees with the two input parameters of
`sceAgcDriverSetTFRing`.

Several declarations disagree and must be resolved against SPRX disassembly
before either project is used to change the other:

| Export | SharpProspero declaration | OpenAGC declaration/evidence | Disposition |
| --- | --- | --- | --- |
| `sceAgcDriverSubmitMultiDcbs` | `void(ptr, ptr, u32)` | `int32_t(ptr[], u32[], u32)` | input ABI agrees; OpenAGC preserves the status return observed through the carrier |
| `sceAgcDriverSubmitAcb` | `void(u32 queueId)` | `int32_t(u32 owner, submit*)` | SharpProspero omits the second register argument that the wrappers preserve; do not adopt |
| `sceAgcDriverSubmitCommandBuffer` | `int(ptr context, ptr buffer)` | `int32_t(u32 queue, ptr buffer, u32 dwords)` | unresolved public-wrapper disagreement; not part of the installed Sony manifest |
| `sceAgcDriverNotifyDefaultStates` | six segment/count arguments | one flags argument in OpenAGC | incompatible surfaces; Sony-first uses a loader-owned adapter and does not forward this export |
| `sceAgcDriverSetWorkloadsActive` / `SetWorkloadComplete` | packet-builder arguments | one workload identifier | known incompatible workload builders; deliberately not forwarded |
| `sceAgcDriverSetHsOffchipParam` | `void(void)` | `int32_t(u32, u64, u32)` | unresolved binding disagreement; deliberately not forwarded |

SharpProspero does not declare `sceAgcDriverSetupAsyncGraphics`,
`sceAgcDriverGetPaDebugInterfaceVersion`, the diagnostic/SDMA exports, or the
capture and Razor ACQ functions used by OpenAGC's forwarding manifest. It
therefore cannot establish that the FW 7.00 manifest is complete. Its useful FW
7.00 contribution is limited to corroborating name-based loader compatibility
and the stable core DCB/multi-DCB input shapes. The FW 7.00 profile remains a
6.50/7.01 family inference until a decrypted FW 7.00 module or hardware result
provides exact evidence.

## Qualification artifacts

`samples/hw_test/agc_init.elf` has no Sony `DT_NEEDED` entry and requires the
direct backend. `agc_init_sony.elf` forces the FW 5.50 dependency, requires
`sony-installed`, and gates three two-DCB marker runs, the nine-dword bounded
wait path, async setup, unsupported private helpers, memory release, and clean
OpenAGC shutdown. `make backend_fw550_check` verifies the opposing dependency
sets and the opposing CMake carrier selections locally. The direct sample
links `build-sony-direct/libopenagc.a`; the Sony sample links the strict
`build-prospero/libopenagc.a`. Sharing one archive between these artifacts is
invalid even if their `DT_NEEDED` sets happen to differ.

Hardware order is strict: clean reboot, direct baseline, reboot, Sony marker
and bounded-fence gate, then native compute/graphics and Vulkan compute/
presentation. Never run a direct test after a Sony failure without rebooting.
An `AGC_OK` return without marker execution is a failed gate.

The 2026-08-04 Eden/Flappy carrier run reached the same boundary with a real
Vulkan client. It logged
`backend=sony-installed installed_driver=true direct_gc=false`, completed GPU
authorization and async setup, then timed out its first native queue wait with
`0x80890007`. The target klog reported an active, non-empty graphics queue and
reset it during teardown. This is execution evidence against the current
loader-owned no-op initialization/workload adapters, not evidence for changing
the public submission ABI or enabling `/dev/gc` fallback. The next carrier
slice is recovery of the installed driver's required workload/context setup.

FW 3.20 and the other exact installed profiles are cross-firmware RE evidence
only and remain hardware-unverified. Generated stubs and firmware binaries are
not committed.
