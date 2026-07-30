# Native runtime fence diagnostics qualification — FW 5.50

## Scope

This record covers the API-v5 `agcGetFenceInfo` diagnostic snapshot in the
public `agc_runtime_compute.elf` oracle on a standard PS5 with raw system
software `0x05500008` (FW ABI key `0x0550`).

Artifact SHA-256:

```
bd8545c05a7683bf4fb0c69e7c925317488ba7fd60e455ef7e1ecf715b477c9d
```

The oracle performs the existing reflected compute dispatch and explicit
`undefined -> shader-write -> host-read` transition sequence, waits for its
runtime-owned EOP fence for at most 200 ms, then queries `AgcFenceInfo` before
readback.

## Result

All object/transition/submission calls and the bounded wait returned `AGC_OK`.
`agcGetFenceInfo` returned `AGC_OK` with a signaled fence, submission ID `1`,
last completed submission ID `1`, matching expected/observed completion
markers, a successful last-wait result, and profile name
`prospero-gc-submit16-standard-standard`. The 64-word output readback passed;
reset and all teardown calls returned `AGC_OK`. The websrv HTTP and FTP ports
remained reachable afterward.

## Qualification boundary

This hardware-qualifies the completed-fence diagnostic snapshot for the exact
FW 5.50 public compute path. Host tests additionally cover unsignaled state,
finite-timeout count/deadline reporting, validation, reset behavior, and
generic compute submission ownership. Pending-fence timeout behavior on PS5,
multi-command-buffer submit, wait/signal lists, timelines, and cross-queue
ownership remain unqualified.
