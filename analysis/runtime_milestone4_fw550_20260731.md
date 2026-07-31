# Runtime Milestone 4 guarded matrix — FW 5.50 — 2026-07-31

## Environment and guard

The endpoint was a standard PS5 reporting raw system software `0x05500008`
and normalized ABI key `0x0550`. Every target used
`samples/hw_test/run_runtime_guarded.sh`, which authenticated the local
SHA-256, launched the process-cleanup ELF first, required the exact firmware
and PASS verdict, rejected AGC errors or incomplete teardown, and rechecked
websrv HTTP/FTP afterward.

## Passing artifacts

| Contract | SHA-256 | Result |
| --- | --- | --- |
| API v22 batch recycling and deferred retirement, 32 cycles | `837183c7d4ad463a50baa993755b55d071845ba479454ede0441901efc66b17d` | PASS |
| reached-or-passed timeline wait | `cb1bd3285de6003ad9cf1f2f07533c3151c3d85d05f1966a57493c73d3f30c1e` | PASS |
| two disjoint buffer ownership ranges | `2b78f787d8ab15ee972382102d566afaab2e06742b27953936aea3530e33ba80` | PASS |
| two disjoint image mip/layer ownership ranges | `249125fd245037c720f409c9830a105a872310498217f15835536403189a8e2c` | PASS |
| presentation stage 0: registration | `f83de2ab8251375db4dd040e34c6abc3d7557eb8b6d503c1cef6a256e4b89de4` | PASS |
| presentation stage 1: initial scanout transition | `adceba2bb3da402fbb29660d1a9a30597585366f80082581ebd925b21ae1ece0` | PASS |
| presentation stage 2: first bounded flip | `f384e65fe0a7d3cd3cdb26980cd07b003dabeceb1a4489441a906b65943691fb` | PASS |
| presentation stage 3: render/scanout round trip | `30a08d185ddef730ebdebccf672c80bbd1781e555747020a2d299cbf22d4d9c8` | PASS |
| presentation stage 4: final bounded flip | `a24956e489cc9abff3b562199a65ceeded8a8b38a138b453210de04e86e7b6fa` | PASS |

All final public destroy calls returned `AGC_OK`. No guarded PASS left an
unreachable service or residual process.

## Corrected capacity failure

The first two-range buffer attempt used the earlier 32-dword command capacity.
Both acquire transitions recorded, but submit-time insertion of the
runtime-owned EOP fence returned `AGC_ERROR_COMMAND_SPACE_EXHAUSTED` before
driver mutation. The console stayed responsive. Partial-range variants now
use 64 dwords; both rebuilt artifacts reproduced byte-for-byte across two
builds and passed through the same guard.

## Qualification boundary

These results qualify the listed contracts for the exact FW 5.50 standard PS5
profile. Host tests remain the broader negative/transactional contract. The
same bytes also passed on exact FW 11.60; see
`runtime_milestone4_fw1160_20260731.md`.
