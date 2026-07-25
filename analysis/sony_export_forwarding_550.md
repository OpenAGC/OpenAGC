# FW 5.50 Sony Export Forwarding

## Result

OpenAGC can load the installed `libSceAgcDriver.sprx` through a
module-specific `dlopen` handle and populate a private `AgcDriverOps` candidate
with `dlsym`. Resolution never uses `RTLD_DEFAULT`, and every resolved callback
is checked against the matching OpenAGC wrapper to reject self-resolution.

The candidate is intentionally not selected by default yet. Real-hardware
probing on FW 5.50 proved that exports resolve and are called, but the installed
module's payload-context submission callbacks returned `AGC_OK` without
executing marker DCBs. The validated `prospero-fw550-direct` backend therefore
remains the default until runtime selection has a non-destructive capability
probe that proves GPU execution.

## ABI-compatible forwarding

The following firmware entry points match the corresponding OpenAGC operation
signatures or have a bounded adapter:

- `sceAgcDriverSubmitDcb`
- `sceAgcDriverSubmitAcb`
- `sceAgcDriverSetupAsyncGraphics`
- `sceAgcDriverGetPaDebugInterfaceVersion`
- `sceAgcDriverSubmitMultiDcbs`, adapted from dword sizes to the operations
  table's byte-sized direct-submit contract
- suspend-point direct/query exports, when present
- capture and diagnostic no-argument exports, when present

Loading the installed driver executes its `module_start`, so OpenAGC's private
initialization and internal-memory operations use no-op success adapters.
`sceAgcDriverNotifyDefaultStates` also uses an adapter because module startup
already performs the firmware's default-state handshake.

## Excluded ABI-incompatible operations

FW 5.50 disassembly shows that the similarly named firmware symbols are not
safe drop-in targets for several OpenAGC operations:

- Firmware `sceAgcDriverNotifyDefaultStates` consumes six arguments: three
  register-group arrays and three counts. OpenAGC's helper consumes one flags
  argument.
- Firmware `sceAgcDriverSetWorkloadsActive` and
  `sceAgcDriverSetWorkloadComplete` are multi-argument packet builders, not the
  one-ID convenience operations currently modeled by OpenAGC.
- `_sceAgcDriverCreateUserSpecialQueue` and
  `_sceAgcDriverDestroyUserSpecialQueue` are internal routines and are not
  exported under underscored or non-underscored names on FW 5.50.
- Firmware `sceAgcDriverSubmitMultiCommandBuffersDirect` is a permission stub
  returning `0x8A6D0001`; the usable public array entry point is
  `sceAgcDriverSubmitMultiDcbs`.

Missing or incompatible callbacks remain `NULL`; the common dispatcher returns
`AGC_ERROR_NOT_SUPPORTED` rather than guessing a calling convention.

## Hardware evidence

The instrumented `agc_init.elf` loaded the installed FW 5.50 module and reported
the `sony-installed` candidate. Firmware logging confirmed calls to async setup,
the PA-debug permission stub, and the direct multi-submit permission stub.
Module-specific `SubmitDcb` and adapted `SubmitMultiDcbs` returned `AGC_OK`, but
neither GPU marker changed. Loading companion `libSceAgc.sprx` was also rejected
by the retail module with `FS Table offset has shifted. This is not survivable.`

The probe is not non-destructive on FW 5.50 in this payload environment. After
loading and exercising the installed driver, a later payload correctly selected
`prospero-fw550-direct` but its previously validated two-DCB test executed only
the first marker. The changed state persisted across payload processes. Do not
probe the installed module and then fall back to the direct backend in the same
console session; reboot before direct-backend hardware validation.

These results prove export resolution and collision-free forwarding, but not a
usable Sony GPU-submission backend in the exploited payload environment. Future
selection must occur before any backend mutates driver state and must not
attempt a direct fallback after an installed-module probe has started.

## Host coverage

`tests/test_sony_exports.c` uses an injectable fake loader to cover:

- complete mandatory export resolution
- byte-to-dword multi-DCB adaptation
- missing mandatory exports and module cleanup
- OpenAGC wrapper recursion rejection
- missing optional operations
- invalid loader contracts
