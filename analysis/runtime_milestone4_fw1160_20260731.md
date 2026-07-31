# Runtime Milestone 4 guarded matrix — FW 11.60 — 2026-07-31

## Environment and guard

The endpoint was a standard PS5 reporting raw system software `0x11600005`
and normalized ABI key `0x1160`. Every target used
`samples/hw_test/run_runtime_guarded.sh`, which authenticated the local
SHA-256, launched the process-cleanup ELF first, required the exact firmware
and PASS verdict, rejected AGC errors or incomplete teardown, and rechecked
websrv HTTP/FTP afterward.

The artifacts contain no `libSceAgc*.sprx` dependency, firmware-specific
compile key, or workload-extension call. They use the ordinary
firmware-neutral native runtime, whose profile selected the FW 11.60 direct
submit/default-state policy internally. The independent FW 11.60 workload
extension remains disabled and outside this matrix.

## Passing identical-byte artifacts

| Contract | SHA-256 | FW 11.60 | FW 5.50 replay |
| --- | --- | --- | --- |
| API v22 batch recycling and deferred retirement, 32 cycles | `837183c7d4ad463a50baa993755b55d071845ba479454ede0441901efc66b17d` | PASS | PASS |
| reached-or-passed timeline wait | `cb1bd3285de6003ad9cf1f2f07533c3151c3d85d05f1966a57493c73d3f30c1e` | PASS | PASS |
| two disjoint buffer ownership ranges | `2b78f787d8ab15ee972382102d566afaab2e06742b27953936aea3530e33ba80` | PASS | PASS |
| two disjoint image mip/layer ownership ranges | `249125fd245037c720f409c9830a105a872310498217f15835536403189a8e2c` | PASS | PASS |
| presentation stage 0: registration | `f83de2ab8251375db4dd040e34c6abc3d7557eb8b6d503c1cef6a256e4b89de4` | PASS | PASS |
| presentation stage 1: initial scanout transition | `adceba2bb3da402fbb29660d1a9a30597585366f80082581ebd925b21ae1ece0` | PASS | PASS |
| presentation stage 2: first bounded flip | `f384e65fe0a7d3cd3cdb26980cd07b003dabeceb1a4489441a906b65943691fb` | PASS | PASS |
| presentation stage 3: render/scanout round trip | `30a08d185ddef730ebdebccf672c80bbd1781e555747020a2d299cbf22d4d9c8` | PASS | PASS |
| presentation stage 4: final bounded flip | `a24956e489cc9abff3b562199a65ceeded8a8b38a138b453210de04e86e7b6fa` | PASS | PASS |

The unchanged partial-range artifacts already had FW 5.50 qualification. The
seven artifacts whose current bytes changed were replayed through their exact
FW 5.50 guarded targets after the FW 11.60 run. All final public destroy calls
returned `AGC_OK`; no guarded PASS left an unreachable service or residual
process.

## Loader-return correction

The first FW 11.60 stage-0 attempt completed every API operation, printed
`PRESENT_STAGE_0 PASS`, and destroyed the device, but the foreground websrv
request did not return before the transport deadline. This was not accepted as
a guarded pass. Presentation and retirement probes now flush their verdict and
self-terminate after complete teardown, matching the already-proven timeline
and partial-handoff carrier behavior. Each affected artifact reproduced
byte-for-byte across two builds before deployment. Stage 0 and every later
gate then returned through the runner normally.

## Qualification boundary

This matrix closes the optional Milestone 4 extension qualification on the
exact standard-PS5 FW 11.60 profile and demonstrates identical application
bytes across the FW 5.50 and FW 11.60 endpoints. Host tests remain the broader
negative and transactional contract. It does not enable or qualify the
separate FW 11.60 workload extension.
