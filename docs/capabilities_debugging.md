# Capabilities, errors, timeouts, and hardware debugging

This guide separates application policy from qualification work. Applications
select features through the native capability contract. Hardware operators use
the guarded qualification runners and exact artifact evidence; that firmware
knowledge never enters ordinary rendering code.

## Firmware-neutral capability decisions

Put startup-critical features in
`AgcDeviceDesc.required_capability_bits`. Device creation fails with
`AGC_ERROR_NOT_SUPPORTED` before backend mutation if the selected runtime
cannot provide all of them. After creation, call `agcGetRuntimeInfo` once and
cache its immutable snapshot.

`AgcRuntimeInfo.capability_bits` says an operation is available. The parallel
`qualification[index]` entry says how strongly that operation has been proven:

| Class | Meaning | Application policy |
| --- | --- | --- |
| `AGC_QUALIFICATION_UNAVAILABLE` | Capability is absent. | Disable the optional feature or fail startup. |
| `AGC_QUALIFICATION_HOST_TESTED` | Generic validation/encoding behavior passed. | Suitable for host development; do not claim console qualification. |
| `AGC_QUALIFICATION_PROFILE_QUALIFIED` | Exact profile/SPRX evidence exists but the public operation lacks a hardware pass. | Opt in only under the project’s documented risk policy. |
| `AGC_QUALIFICATION_HARDWARE_QUALIFIED` | The public runtime operation passed an exact-profile console oracle. | Use within the precise scope recorded in `STATUS.md`. |

The capability bit is the program decision. `profile_name`,
`firmware_version`, `firmware_abi_key`, and `hardware_family` are diagnostic
labels for logs and support reports. Do not switch queues, cache policy,
register defaults, packet forms, or memory layout from those fields. The
runtime owns that selection and rejects unknown profiles before GPU mutation.

Example optional-feature policy:

```c
AgcRuntimeInfo info = AGC_RUNTIME_INFO_INIT;

if (agcGetRuntimeInfo(device, &info) != AGC_OK)
    return APP_STARTUP_FAILED;
if ((info.capability_bits & AGC_RUNTIME_CAP_ASYNC_COMPUTE_QUEUE) != 0u) {
    /* Create an ordinary compute queue; no firmware selection here. */
}
```

Qualification is per public operation, not inherited from a low-level carrier
or a different workload that passed on the same console.

## Error handling policy

Handle result codes by class while retaining the exact code and public
function name in diagnostics:

| Result class | Meaning and response |
| --- | --- |
| `AGC_ERROR_INVALID_ARGUMENT`, `AGC_ERROR_INVALID_ALIGNMENT`, `AGC_ERROR_BUFFER_TOO_SMALL` | Application contract bug. Preserve the validation message and fix the caller; retrying unchanged input is wrong. |
| `AGC_ERROR_INVALID_STATE`, `AGC_ERROR_BUSY`, `AGC_ERROR_RESOURCE_INVALID`, `AGC_ERROR_RESOURCE_NOT_BOUND` | Ownership/lifecycle/resource-state bug or unfinished work. Query state/diagnostics and correct ordering; do not destroy or reuse pending objects. |
| `AGC_ERROR_NOT_SUPPORTED` | Capability/profile limitation. Use an already-designed optional fallback or fail before frame mutation. |
| `AGC_ERROR_COMMAND_SPACE_EXHAUSTED`, `AGC_ERROR_CB_OVERFLOW` | The fixed recording capacity is insufficient. Reset safely and allocate a larger command buffer for a later attempt. |
| `AGC_ERROR_OUT_OF_MEMORY` | Allocation failed. Collect eligible deferred frees, reduce optional memory, or fail cleanly; inspect `agcGetMemoryStats`. |
| `AGC_ERROR_TIMEOUT` | Bounded progress was not observed. Capture diagnostics and enter the application’s recovery/fail-stop policy. |
| `AGC_ERROR_SUBMIT_FAILED`, `AGC_ERROR_SUBMIT_NOT_ALLOWED` | Submission did not enter the accepted runtime path. Preserve validation/backend diagnostics; do not assume any requested work completed. |
| `AGC_ERROR_DEVICE_LOST`, `AGC_ERROR_INTERNAL` | Runtime/backend integrity cannot be assumed. Stop issuing GPU work, preserve evidence, and tear down only through documented safe paths. |

`agcErrorString` supplies a stable symbolic label. The optional validation
callback adds the function, category, severity, object identity, and corrective
message without changing the fail-closed result. It is not a substitute for
checking every return code.

## Bounded timeout workflow

Never pass `AGC_RUNTIME_INFINITE_TIMEOUT`. Choose a finite deadline from the
application’s latency budget and keep a separate higher-level watchdog.

After `agcWaitFence` returns `AGC_ERROR_TIMEOUT`:

1. Stop recycling the fence’s command buffers and stop destroying referenced
   resources.
2. Query `agcGetFenceInfo` and log its state, queue/command state, submission
   IDs, expected/observed completion marker, timeout count/deadline, profile,
   ABI key, and hardware family.
3. Query relevant `agcGetGpuLabelInfo` objects for scheduled/observed values,
   producing submission, and last bounded-wait result.
4. Query `agcGetLastDebugMessage` and `agcGetMemoryStats`.
5. End an already-active diagnostic capture if doing so is safe; do not begin
   a new capture by mutating the stalled workload.
6. Follow the application’s fail-stop/restart policy. A timeout is not proof
   that the GPU will never complete, so unchanged resource reuse is unsafe.

The nonblocking `agcGetFenceStatus` and `agcGetGpuLabelStatus` return
`AGC_ERROR_BUSY` while incomplete. Poll with an application deadline; an
unbounded poll loop merely recreates an infinite wait.

## Capture and validation evidence

Enable the validation callback before constructing the workload and keep its
callback allocation-free. Use debug names on application resources and command
buffers. For a selected frame, begin capture before object/command creation and
keep it active through fence results, readback hashes, and reverse-order
destruction.

Decode on the host with addresses redacted:

```sh
python3 tools/decode_openagc_capture.py frame.oagc >frame.txt
```

Only use `--show-addresses` during private local diagnosis. Captures can also
contain opt-in shader bytes and application names; review them before sharing.
A capture is not a replay file and must never be sent directly to hardware.

## Guarded console qualification

Hardware qualification is an operator workflow, not an application fallback:

- Build one pinned artifact and record its SHA-256 before deployment.
- Use the sample’s guarded Make/runner target. It verifies console reachability,
  expected profile, cleanup-first execution, bounded completion, workload
  oracle, teardown, residual processes, and kernel faults.
- Never launch archived stalled probes or installed-driver oracle artifacts
  outside their documented safety gate.
- Record host-tested, profile-qualified, and exact-firmware hardware-qualified
  results separately in `STATUS.md` and an `analysis/` evidence report.
- A pass on one console/profile does not prove a second profile. Reuse the same
  artifact bytes for endpoint portability qualification.

The generic backend is the primary development verifier. It validates object
contracts and command encoding without claiming shader/raster execution.
Console runs are reserved for planned qualification checkpoints.

## Minimum support bundle

Collect these items for a reproducible failure report:

- OpenAGC commit and artifact SHA-256.
- `AgcRuntimeInfo` profile, API version, capability bits, and qualification
  array.
- Exact public function/result name and the last `AgcDebugMessage`.
- Fence/label diagnostic snapshots and chosen finite deadlines.
- Address-redacted decoded capture, when available.
- Guarded runner output, cleanup verdict, residual-process result, and kernel-
  fault check for console failures.
- Memory statistics before the workload and after safe cleanup.

Do not include proprietary firmware modules, decrypted SPRX files, credentials,
raw process addresses, or unrelated private application data.
