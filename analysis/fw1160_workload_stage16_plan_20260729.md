# FW 11.60 workload stage-16 plan

## Evidence boundary

Stages 11 through 15 all used the Sony public packet-builder ABI: an active
prefix plus a nine-dword `0xc0071e00` packet, then the matching complete form.
Every candidate stalled at the first `SET_WORKLOAD` packet despite progressively
proving the GPU-info property, registered-stream table, caller-owned cursor
lifecycle, complete cache flush, defaults, async setup, and standard-console
Gn2/Gn3/Gn4 constructor state.

A full instruction-level comparison of the FW 5.50 and FW 11.60 active and
complete exports found no semantic packet difference. The distinct normalized
wrapper fingerprints come from validation and register-allocation code
generation; both versions emit the same controls, stream-slot address, masks,
and nine-dword payload.

That Sony builder form has never been proven through OpenAGC's direct
`/dev/gc` backend on FW 5.50. The workload operation actually qualified on FW
5.50 is OpenAGC's separate three-dword begin/end extension:

```text
begin:    c0011e80 00000001 00000000
complete: c0011e84 00000001 00000000
```

The extension is not a replacement implementation of Sony's multi-argument
exports. Stage 16 tests only whether the hardware-proven direct packet form is
portable within the standard FW 5.50/11.60 compatibility group.

## Isolated gate

`agc_fw1160_stage16.elf` preserves the qualified FW 11.60 sequence:

1. exact standard-console profile, credentials, direct initialization, and
   internal memory;
2. version-12 default states and async setup;
3. an ordinary `WRITE_DATA` preflight with marker `0x1160f016`;
4. one fully flushed 16-dword DCB containing three-dword begin, marker
   `0x1160a016`, three-dword complete, and marker `0x1160c016`;
5. bounded marker wait, memory release, driver shutdown, and forced process
   termination on success.

It deliberately does not register a Sony workload stream or publish the
GPU-info/shadow properties because the three-dword FW 5.50-qualified extension
does not consume that ABI. The public FW 11.60 workload capability remains
disabled regardless of build success.

Artifact SHA-256:
`1d814cb4b19436b0fc9e76937ac7b9f484a25fccd430a125ba0c063dd8be5e96`.
Its `DT_NEEDED` set is limited to VideoOut, libkernel, libc, and libnet; it has
no `libSceAgcDriver.sprx` dependency.

Run it only after a clean reboot, using the guarded target so the cleanup ELF
is the immediately preceding payload:

```sh
make -C samples/hw_test deploy_agc_fw1160_stage16 PS5_HOST=10.0.1.39
```

Require two clean passes before enabling the exact FW 11.60 capability, then
rerun the public FW 5.50 workload path as a regression. A stall must be removed
with the cleanup ELF and stage 16 must not be repeated until its result has
been analyzed.

## Hardware result

Stage 16 was run once on standard FW `0x11600005`, last in a boot session after
two successful graphics and two successful compute qualifications. The
guarded cleanup found no stale process. Exact profile selection, internal
memory, version-12 defaults, and async setup returned `AGC_OK`. The ordinary
preflight submit completed marker `0x1160f016` in 50 ms.

The candidate then printed the expected 16-dword DCB and exact headers
`0xc0011e80` / `0xc0011e84`. Submission returned `AGC_OK`, but both ordered
markers remained zero for the full 5,000 ms wait. The payload reported
`stage 16: direct workload sequence FAIL`; no PASS or normal foreground exit
followed before the websrv timeout.

ps5debug-NG found PID 109 named `eboot.elf`. The established cleanup ELF
removed it, after which the process list was empty and both websrv and TCP 744
were responsive. Do not repeat stage 16 unchanged.

This disproves portability of the FW 5.50-qualified three-dword workload form
to FW 11.60. Together with stages 11-15, both known packet forms now fail at
the first workload operation while ordinary graphics and compute execute
correctly in the same boot. FW 11.60 workload capability remains disabled;
recover an official-driver kernel/context prerequisite before another gate.
