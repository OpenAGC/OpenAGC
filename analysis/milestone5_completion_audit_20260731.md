# Milestone 5 completion audit (2026-07-31)

This audit treats every sentence in the authoritative `PLAN.md` Milestone 5
contract as unproven until matched to current implementation and executable
evidence. The audited code is Runtime API v25 at commits `4569cdf` through
`5ecd983`, plus the final roadmap/evidence commit containing this report.

## Validation and diagnostics

| Requirement | Authoritative evidence | Verdict |
| --- | --- | --- |
| Invalid enums and descriptors | `test_runtime_invalid_program_diagnostic_matrix` rejects sampler enums and unknown buffer usage with exact parameter diagnostics. Creation-path tests cover every version/size/reserved/enum family. | Proven |
| Invalid object/command state | The matrix rejects begin-after-submit; lifecycle tests cover every command/fence state transition. | Proven |
| Misaligned or out-of-range GPU-backed intervals | The matrix rejects a one-byte-misaligned copy and buffer/image overruns; resource/layout tests cover overflow arithmetic and subresource bounds. | Proven |
| Descriptor/reflection mismatch | The matrix binds the wrong reflected descriptor type and asserts result, category, function, and corrective text. | Proven |
| Shader-export/attachment incompatibility | Graphics creation tests cover count, slot, class, component, format, sample, linkage, and dual-source mismatches; the matrix locks the compatibility diagnostic boundary. | Proven |
| Missing transitions | The matrix rejects copy before typed source/destination transitions and dispatch with a missing reflected binding. | Proven |
| Buffer/image overruns | Exact upload/readback overrun rows exist in the matrix; layout tests cover computed image allocation limits. | Proven |
| Integer-target blending | The matrix creates a reflected UINT export/UINT attachment with blending and requires fail-closed compatibility diagnostics. | Proven |
| Unsupported capabilities | The matrix rejects an unsupported wave size as `AGC_ERROR_NOT_SUPPORTED` in the capability category; device capability-bit tests cover startup rejection. | Proven |
| Command exhaustion | A two-dword command records a pipeline but rejects dispatch with `AGC_ERROR_COMMAND_SPACE_EXHAUSTED` and an exact capacity message. | Proven |
| Use-after-submit | A pending command rejects begin until bounded completion and reset. | Proven |
| Premature destruction | A buffer retained by a recorded transition returns `AGC_ERROR_BUSY` and names the recorded-reference contract. Parent/child lifetime tests cover all object kinds. | Proven |
| Deterministic | Every invalid row asserts one exact result/category/function/message and a final callback count of 14. Filtering/disable tests preserve results. | Proven |
| Allocation-aware/free delivery | `test_runtime_validation_is_allocation_free` arms the next application allocator attempt to fail, triggers validation/callback delivery, and proves the attempt counter is unchanged and allocations remain balanced. | Proven |
| Assertion-independent | Required checks return errors in production code; `src/runtime.c` contains no C `assert()` path. ABI `_Static_assert` declarations are compile-time layout checks, not runtime validation. | Proven |
| Optional selectable layer with required safety retained | `agcSetDebugCallback` enables/filters/disables synchronous messages; identical invalid calls still fail closed when disabled. | Proven |

Detailed row evidence is preserved in
`analysis/runtime_validation_matrix_host_20260731.md`. The clean assertion
binary covers the complete implementation, not a standalone mock.

## Capture stream

| Required record/behavior | Authoritative evidence | Verdict |
| --- | --- | --- |
| Versioned endian-defined framing | Header magic, v1, fixed sizes, little-endian tag, contiguous sequence, terminal record/byte counts are asserted in `test_runtime_capture_v1_stream`; malformed decoder input fails. | Proven |
| Runtime/profile/capabilities | Runtime record count and exact decoder rendering are asserted. | Proven |
| Object creation and debug names | Runtime test requires matching creation/destruction counts and copied command name. | Proven |
| Stable local IDs, no host pointers | Dense `next_object_id` and distinct dependency/resource IDs are asserted; records serialize IDs only. Two-process decoded reference output is identical. | Proven |
| Resource descriptions | Buffer, image, image view, and sampler records are all counted and decoded. | Proven |
| Shader record versions and hashes | Three runtime shader records plus decoder version/size/FNV hash rendering are asserted. | Proven |
| Shader bytes only by explicit opt-in | Default stream requires zero byte records; opt-in bundle requires exactly one primary and one front-half byte record. | Proven |
| Pipeline descriptions | Runtime stream requires normalized compute and graphics records; decoder fixture parses/renders both fixed and dynamic forms. | Proven |
| Typed transitions | Runtime stream asserts exact buffer byte range and image aspect/mip/layer range; decoder renders both kinds and dependency fields. | Proven |
| Command boundaries and PM4 dwords | Begin/end counts and both exact post-injection command streams are required. The reference decoder names `DISPATCH_DIRECT`. | Proven |
| Submission order, waits, signals | Two real v2 submissions assert stable source/destination label IDs and exact wait/signal values; decoder renders command order, fence, wait, and signal entries. | Proven |
| Fence results | Both bounded fence results are required and the reference frame contains its successful fence record. | Proven |
| Selected readback hashes | Runtime computes a selected readback hash; decoder requires named FNV-1a-64 output. | Proven |
| Validation warnings | An intentionally invalid end call creates one actionable capture warning; decoder renders its function/object/message. | Proven |
| Diagnostic, not replay | Public documentation explicitly forbids hardware replay and describes address-remapping/security as future work. | Proven |

Implementation and branch evidence is in
`analysis/runtime_capture_v1_host_20260731.md`,
`analysis/runtime_capture_semantics_host_20260731.md`, and
`analysis/runtime_reference_capture_examples_host_20260731.md`.

## Host decoder

| Requirement | Evidence | Verdict |
| --- | --- | --- |
| Named packets and fields | Fixture requires `SET_CONTEXT_REG`, `WRITE_DATA`, `DISPATCH_DIRECT`, dword counts, and safe named fields. | Proven |
| Register names | Fixture requires `CB_TARGET_MASK`; runtime packet tests cover underlying register vocabulary. | Proven |
| Resource references | Stable resource/shader/pipeline/transition IDs and ranges are rendered and asserted. | Proven |
| Validation warnings | Function, result, object, and corrective message are rendered and asserted. | Proven |
| Default address redaction | Default output requires `<redacted>`; explicit `--show-addresses` requires the reconstructed address. The deterministic reference gate also requires redaction. | Proven |
| Malformed input rejection | Bad magic/truncation plus record size, sequence, array count, and dynamic payload checks raise `CaptureError`. | Proven |
| Deterministic host decode | Fixture decodes identically twice; real reference producer runs in two processes and decoded text must compare byte-for-byte. | Proven |

## Documentation deliverables

| Deliverable | Installed source | Mechanical evidence | Verdict |
| --- | --- | --- | --- |
| Native overview and lifecycle | `docs/native_runtime.md`, `docs/api_reference.md` | Ownership/thread sections required by API-reference gate | Proven |
| Installation/CMake and first triangle/compute | `docs/getting_started.md`, installed `examples/` | Temporary-prefix configure/build/run gate | Proven |
| Resource memory and synchronization | `docs/memory_resources.md`, `docs/native_runtime.md` | Installed-package guide check | Proven |
| Shader/reflection/descriptor/pipeline | `docs/shader_pipelines.md` | Installed-package guide check | Proven |
| Capability/qualification semantics | `docs/capabilities_debugging.md`, runtime guide | Firmware-neutral C-snippet gate | Proven |
| Error, timeout, capture, hardware debugging | `docs/capabilities_debugging.md`, `docs/capture.md`, `docs/validation.md` | Installed-package guide check | Proven |
| Every public native type/function with ownership/thread safety/returns/examples | `docs/api_reference.md` | Header extractor requires 119 types, 87 functions, and all contract sections | Proven |
| No ordinary firmware branch | All documented C blocks and installed compute/triangle sources | Gate rejects firmware/profile fields, Prospero branches, Sony exports, PM4 helpers, and `/dev/gc` | Proven |

See `analysis/runtime_documentation_host_20260731.md` for the guide-level
evidence and package paths.

## Exit criteria

1. **Intentionally invalid programs produce actionable diagnostics — proven.**
   The exact 14-row matrix covers every named failure class without allocation
   or result mutation.
2. **A captured reference frame decodes deterministically on the host —
   proven.** `reference_capture_deterministic` runs a real reflected public
   compute frame twice in separate processes, decodes both with default
   redaction, compares exact text, and checks the semantic chain.
3. **Public examples build from the installed package — proven.**
   `installed_package_examples` installs to an empty prefix, verifies every
   guide, configures only the installed example source/package/artifacts,
   builds, links, and runs compute and indexed triangle on generic.
4. **Documentation contains no firmware branch in ordinary application code —
   proven.** `documented_application_firmware_neutral` scans every C block and
   both installed examples.

## Hardware checkpoint determination

Milestone 5 defines its reference decode and examples gates on the host. The
changes add synchronous validation reporting, serialization callbacks, a host
decoder, package consumers, documentation, and tests. They do not alter PM4
encoding, transition derivation, memory placement, submit carriers, firmware
selection, register defaults, or VideoOut policy. Capture-disabled operation
retains the already-qualified runtime path, and the Prospero target compiles
cleanly with the public ABI.

Therefore no new PS5 run is required to prove Milestone 5. Existing FW 5.50
and FW 11.60 hardware qualifications remain unchanged and are not relabeled as
evidence for capture execution. The next dual-endpoint hardware gate belongs
to Milestone 6’s hash-identical reference-game ELF. A single FW 11.60 console
is sufficient for ongoing development; access to FW 5.50 is needed only for
that later final portability replay.

## Final verification

The completion report and roadmap updates passed the required deleted-tree
checkpoint:

```text
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON   PASS
cmake --build build --parallel                                      PASS
ctest --test-dir build --output-on-failure                          12/12 PASS
build/openagc_tests                                                  17437/0
cmake --build build-prospero --parallel                             PASS
```

Both toolchains retained `-Wall -Wextra -Wpedantic` and emitted no new warning.
The worktree was then checked with `git diff --check`. All requirements and
exit criteria are proven; Milestone 5 is complete.
