# Runtime GPU-label timeline waits — host qualification (2026-07-31)

Runtime API v19 completes the bounded host-facing behavior of the existing
32-bit `AgcGpuLabel` timeline points without changing qualified GPU packet
forms. `agcGetGpuLabelStatus` reports completion when the observed word is at
or beyond an already-scheduled point. `agcWaitGpuLabel` adds the same monotonic
test with a mandatory finite nanosecond deadline. An unscheduled future point
returns `AGC_ERROR_INVALID_STATE`; `AGC_RUNTIME_INFINITE_TIMEOUT` is rejected.

On Prospero, a pending host wait uses the latest scheduled exact label value in
the existing bounded memory-wait carrier, invalidates the flexible-memory word,
then rechecks `observed >= requested`. This permits an observed later point to
satisfy an earlier point without changing GPU-side `WAIT_REG_MEM` equality
semantics.

`AgcGpuLabelInfo` v2 appends the last host wait point/result, timeout count, and
last timeout deadline. The 104-byte v1 prefix remains accepted and is copied
without writing the v2 tail.

Signal ordering is now transactional at both recording and submission:

- one command rejects repeat or decreasing tentative points;
- an ordered batch simulates earlier DCB signals before later DCB signals;
- a stale command recorded before another submission advances the label is
  rejected at submit time;
- `UINT32_MAX` is terminal and cannot repeat or wrap.

The clean generic suite passes all 7 CTest suites and 16,760 assertions. The
timeline fixture covers v1/v2 diagnostics, initial and future-point status,
finite waits, monotonic earlier-point completion, command-local ordering,
transactional two-DCB rejection and correction, and the terminal point.
Prospero builds without new warnings.

`agc_runtime_timeline_wait.elf` reuses the hardware-qualified release label,
performs a bounded host wait and diagnostic query, then completes the existing
graphics acquire and teardown. Its reproducible SHA-256 is
`b4ab3e83b7a81ae220cfc0ccae5e652aa14b682ef30154d829f2d1e78cfca6ce`.
The guarded `deploy_agc_runtime_timeline_wait` target verifies cleanup,
firmware 0x0550, hash, exact `TIMELINE_WAIT PASS`, teardown, and service health.
Hardware qualification remains pending while the configured console/websrv is
unreachable.
