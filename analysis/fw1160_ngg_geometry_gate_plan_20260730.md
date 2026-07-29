# FW 11.60 NGG geometry gate plan (2026-07-30)

## Scope

FW 5.50 independently hardware-qualified three Wave32 NGG geometry variants:

1. amplified triangle output;
2. line input/output topology;
3. multiple geometry-shader invocations.

FW 11.60 already qualifies the ordinary Wave32 NGG+PS path and direct,
indexed, and indirect submission. This tier changes only the compiled NGG
shader pair and its matching topology/oracle. It does not use tessellation,
HTILE, MSAA, or workload packets.

## Exact artifacts

| Profile | Variant | ELF | SHA-256 |
| --- | --- | --- | --- |
| `0x1160` logged | amplify | `agc_graphics_amplify_fw1160_logged.elf` | `dccbb0eafb2f4d432a5724a6eb74124b8c1ba36b4798be3bd042d2acf5617fab` |
| `0x1160` logged | lines | `agc_graphics_lines_fw1160_logged.elf` | `55b41bb1c50fdfcb715237c2dc43f914b5ad317a636194a87499fafd7502c77e` |
| `0x1160` logged | invocations | `agc_graphics_invocations_fw1160_logged.elf` | `7e053f12f3c59332d593988172176c787b670919c51dc829db2b4564818ba750` |
| `0x0550` headless mirror | amplify | `agc_graphics_amplify_fw550_headless.elf` | `b9da4c124f7351393659f3800e1c14399c9016b43d1642f5a103a252714478c5` |
| `0x0550` headless mirror | lines | `agc_graphics_lines_fw550_headless.elf` | `ff0d32041e7cccf6ab76e01e202270d52619b74a92299a84ac260328c52631ca` |
| `0x0550` headless mirror | invocations | `agc_graphics_invocations_fw550_headless.elf` | `ae89683462f661d9e546c2f593f494239735f57d7228ffdff0111371f0691900` |

All six artifacts cross-build without warnings from the current source and
current `libopenagc.a`.

## Fail-closed identity and oracle

The sample now writes an explicit `Graphics variant:` line after redirecting
stdout to the file-backed result. `run_fw1160_graphics.sh` accepts
`EXPECTED_VARIANT` and rejects a generic baseline result when a variant was
requested. Existing checks still require exact ABI selection, bounded fence,
the target-specific FP16 coverage/value oracle, driver shutdown PASS, final
graphics PASS, and absence of failure text.

Run amplify twice, lines twice, then invocations twice. Every payload must be
immediately preceded by the process-cleanup ELF; ps5debug-NG must find no
residual `eboot` after each run. Stop on the first fence timeout, oracle
mismatch, residual process, or console responsiveness loss.

The current-source FW 5.50 mirrors remain a separate regression requirement
when that console becomes reachable.
