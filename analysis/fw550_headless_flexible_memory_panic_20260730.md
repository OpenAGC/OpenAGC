# FW 5.50 headless flexible-memory exhaustion (2026-07-30)

## Incident

FW 5.50 kernel-panicked while starting the first D32 mirror after an ordered
sequence of two standalone copies, three draw variants, three NGG variants,
and five tessellation/TES-to-NGG variants. Every preceding graphics payload
reported driver shutdown PASS and self-terminated. No HTILE or MSAA payload
was launched.

The D32 artifact had also been rebuilt with sibling `openagc-psbc` commit
`b381c96a9f11409279170c253bfb4d8fdd0f0d9f`. That rebuild changed only byte
`0x5b` of the embedded primitive-back shader record, advertising the 11 CX
entries already serialized between offsets `0xe0` and `0x138`.

## Root cause

The shader byte is not the command-stream delta. The hardware sample parser
derives the CX count from the serialized block boundaries and was already
emitting all 11 entries with the previously qualified record. The changed
record byte is consumed only by validation and unused compatibility queries in
this path. The register values, shader code, and emitted draw state are
otherwise identical.

The actual cumulative defect was sample-owned flexible-memory teardown:

- each headless `agc_graphics` launch maps a 65,536-byte command buffer;
- ordinary graphics and tessellation launches each map a 19,922,944-byte
  graphics pool in the retained hardware logs;
- neither mapping was passed to `sceKernelReleaseFlexibleMemory`;
- successful gates terminate with `SIGKILL`, so `atexit` cannot compensate;
- the 11 successful graphics launches immediately before D32 therefore left
  about 219 MiB of graphics pools plus command mappings unreleased before the
  additional depth allocation.

The standalone copy gate is not part of the leak: it uses `AgcGpuMemory` and
frees all three allocations before shutdown. `agc_compute` had the same
unreleased flexible command/pool pattern as `agc_graphics`, so it is fixed in
the same change.

## Fix

`agc_graphics` now tracks the exact sample-owned command and graphics-pool
mapping sizes. Normal completion shuts down the driver, releases both flexible
mappings, releases any closed VideoOut direct mapping, and treats a release
failure as a failed verdict. The `atexit` path performs the same idempotent
flexible cleanup for ordinary early returns. It does not release direct memory
while a VideoOut handle may still own it.

`agc_compute` now tracks and releases its 64 KiB command mapping and 16 MiB
compute pool after driver shutdown, and includes both results in its verdict.

Normal depth builds now consume the committed hardware-qualified shader
records. `make regen_shaders_depth` is the explicit opt-in regeneration path,
preventing an unrelated sibling compiler update from silently changing a
hardware artifact during a regression build.

## Validation boundary

Prospero cross-builds complete without warnings, the committed depth record is
restored, and the host CTest suite passes. The panic powered the FW 5.50
console down, so hardware proof of the teardown must start from a fresh boot:

1. inject ps5debug-NG and run `make cleanup_stress_fw550`;
2. require the file-backed verdict to report zero results for pool release,
   command-buffer release, direct unmap, and direct-memory release;
3. complete all 14 launches, exceeding the old 11-launch cumulative threshold;
4. only then run the uncompressed D32/D16/S8/D16+S8 mirrors;
5. keep HTILE and MSAA blocked until that sequence passes.

The committed runner invokes the process-cleanup ELF immediately before each
payload and pins the corrected current-source FW 5.50 artifact SHA-256 to
`34c1bdd31fc7dc3bb795be0a8bfff761c0d3b1ed185253209b3b9880f7bff0b6`.
Two forced local relinks reproduced those bytes exactly, dependency inspection
found no AGC SPRX, and a hash-named copy was preserved in the host-only
`OpenAGC-hw-artifacts` directory before its first launch.

The first FW 5.50 stress attempt stopped at iteration 1 with no verdict. It did
not reach AGC initialization: the launcher used
`openagc_fw0550_cleanup_stress`, while the `8bb77e4c...` ELF tried to `freopen`
its result under `openagc_fw550_cleanup_stress`. The absent parent made the ELF
exit immediately after credential setup. Websrv, FTP, and ps5debug-NG stayed
reachable; the process-cleanup ELF ran, and ps5debug-NG proved that no
`eboot.bin` remained. The runner now requires its result path to equal the
firmware-derived launch directory before any network access, with a host test
that rejects the former mismatch. The corrected `34c1bdd...` ELF was uploaded
as `openagc_fw0550_cleanup_stress/eboot.elf`, downloaded again, and verified
byte-for-byte before retry. The original `141bac67...` artifact was superseded
before FW 5.50 execution by the current-source relink.
The matching FW 11.60 artifact is pinned to
`55478106b4cdbef50c5d37d15e5a327b3b5dc0b6e2da9f6dc2b48953ea2b8d2e`.
FW 11.60 is a useful runner and teardown safety check, but cannot replace the
FW 5.50 regression because the incident occurred on FW 5.50. That console is
currently unavailable, so the FW 5.50 verdict remains pending.

FW 11.60 hardware completed a one-launch canary and then the full 14/14 stress
sequence with the pinned artifact. Every launch selected ABI key `0x1160`,
reached the GPU fence, shut the driver down, returned zero for all four memory
cleanup operations, and reported PASS. Websrv and ps5debug-NG remained
reachable afterward. This confirms the runner and shared teardown on FW 11.60;
it does not qualify the unavailable FW 5.50 console.
