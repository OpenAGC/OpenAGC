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

1. inject ps5debug-NG and run one cleaned-up baseline graphics payload;
2. require `Graphics memory cleanup: pool=0x00000000 cb=0x00000000`;
3. repeat the baseline enough times to exceed the old cumulative threshold;
4. only then run the uncompressed D32/D16/S8/D16+S8 mirrors;
5. keep HTILE and MSAA blocked until that sequence passes.
