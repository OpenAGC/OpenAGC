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
| API v22 batch recycling and deferred retirement, 32 cycles | `a3d04e6472c2cdd0ea09624cd3536dd5eb53345fa063aa5cee937636290852fb` | PASS |
| reached-or-passed timeline wait | `c30a07d6b8c55b495df9736476376e2e7d8869e17e90f1f7e98a60f85be6976f` | PASS |
| two disjoint buffer ownership ranges | `2b78f787d8ab15ee972382102d566afaab2e06742b27953936aea3530e33ba80` | PASS |
| two disjoint image mip/layer ownership ranges | `249125fd245037c720f409c9830a105a872310498217f15835536403189a8e2c` | PASS |
| presentation stage 0: registration | `70c9f44db94154a8105b4379ccb86f213736047001f6d09ba72a97688542c714` | PASS |
| presentation stage 1: initial scanout transition | `84e709cb15a18b3262e81b732d4258ae52e42913ad063fd9dfe332745c9e6643` | PASS |
| presentation stage 2: first bounded flip | `d67bbade7d48a918cb2636ac1e42f0c176dca99848a3a00f244019f5686ece11` | PASS |
| presentation stage 3: render/scanout round trip | `d5ee2434129f16d068110208671bba72d95637681db22d2226919d487a4452f6` | PASS |
| presentation stage 4: final bounded flip | `108416ea85233ff5768062a8bc5b062376a2ee2ced909b5358e2a7e050e658bf` | PASS |

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

These results qualify the listed contracts only for the exact FW 5.50 standard
PS5 profile. Host tests remain the broader negative/transactional contract.
FW 11.60 retains its previously proven firmware-neutral baseline, but these
optional Milestone 4 extensions have not been replayed there.
