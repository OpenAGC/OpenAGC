# FW 5.50 runtime-profile hardware regression

Date: 2026-07-26

Target: standard PS5 running system software `5.500.008`, raw version
`0x05500008`.

Runtime profile selection uses the upper four hex digits as the ABI key
(`raw & 0xffff0000`), so this console selects `0x0550`. The complete raw value
is retained for diagnostics. Unknown build suffixes therefore do not require
separate aliases, while unregistered major/minor firmware keys still fail closed.

The `samples/hw_test/agc_init.elf` regression queries OpenAGC's selected
runtime profile immediately after initialization and rejects any value that
does not match the independently recovered FW 5.50 standard-console ABI.

## Hardware-observed profile

| Field | Value |
|---|---:|
| Backend | `prospero-gc-submit16` |
| ABI family | `standard` |
| Model | `standard-ps5` |
| Submit ioctl | `0xC0108102` |
| Authenticated special queue | enabled |
| TF ring | enabled |
| EOP ring offset | `0x39000` |
| GPU-info span | `0x100000` |
| CWSR working offset | `0xA00000` |
| CWSR allocation | `0x1000000` |

## Regression result

- Runtime profile assertion: PASS.
- AGC initialization and nine internal-memory allocations: PASS.
- Default-state notification: PASS.
- PA-debug permission stub returned expected `0x8A6D0001`: PASS.
- Multi-DCB submission produced both ordered markers in one earlier run, but
  repeated final runs produce `0xD001CAFE` and `0x00000000`. Explicit CPU
  cache-line invalidation did not change the result. This remains an open,
  separate submission regression; descriptor zero executes and the ioctl
  returns `AGC_OK`.
- Async graphics setup: PASS.
- User special queue create/destroy: PASS.
- Suspend-point submit/query: PASS.
- Workload begin/end: PASS.
- Payload exit status: failure because the multi-DCB assertion remains strict.

The raw build value is retained for diagnostics. Registry selection uses its
four-digit `0x0550` ABI key, not an exact `0x05500008` alias.
