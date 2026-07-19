# Code conventions

## Naming

- **Public Sony-compatible fns:** `sceAgc*`, `sceAgcDriver*`, `sceAgcCb*`, `sceAgcDcb*`, `sceAgcAcb*` — match firmware export names EXACTLY.
- **Internal helpers:** `agc*` / `agcCb*` / `agcPm4*` lowercase camelCase.
- **Types:** `SceAgc*` / `Agc*` PascalCase.
- **Macros/constants:** `AGC_*` / `AGC_PM4_*` UPPER_SNAKE.

## ABI & signatures

- `PS5_SYSV_ABI` on every public function declaration AND definition (see `src/cb_builders.c`).
- Never change calling convention or public signatures without an explicit ABI reason.
- Public functions return `int32_t` AGC error codes (see `include/agc_error.h`). Internal helpers return `NULL` / `0` / `false` on failure — NO verbose try/catch-style error paths.

## Headers & includes

- Public headers in `include/`. Private helpers shared between `src/*.c` stay in `src/` (not `include/`).
- CMake exposes `include/` as PUBLIC, `src/` as PRIVATE.
- Public headers include siblings as `#include "agc_*.h"`. Impl files include their public counterpart first, then `<string.h>`/`<stdint.h>` etc.

## Static asserts

Any struct mirroring firmware ABI gets `_Static_assert(sizeof(T) == expected, "...")` and, where relevant, `_Static_assert(offsetof(T, field) == expected, "...")`.

## PM4 / cursor rules

- Never hand-pack PM4 headers — use `agcPm4Header3()` / `agcPm4Header3Sub()`.
- Builders call `agcCbAllocDwords(cb, n)`, NULL-check the result, bail cleanly.

## Backend split

- `driver_generic.c` = testable host backend. No `/dev/gc`, no ioctls, no kernel objects.
- `driver_prospero.c` = PS5 backend. All `/dev/gc`/ioctl/kernel code lives here, guarded by `#ifdef OPENAGC_PROSPERO`.

## Comments

Add/remove comments when they help development. Do NOT delete existing RE-provenance comments.

## Style

- Compact code: collapse duplicate else branches, avoid unnecessary nesting, share abstractions.
- Idiomatic C. Avoid excessive error handling at every line — pick right boundaries (match existing style).
- No new third-party libs. No C++ features. No compiler-specific extensions beyond `PS5_SYSV_ABI` and `_Static_assert`.

## New symbols checklist

- Declare in appropriate `include/*.h` with `PS5_SYSV_ABI` and Sony-style naming.
- Firmware-ABI struct → add `_Static_assert` size/offset.
- New packet builder → add test in `tests/test_cb.c` or `tests/test_dcb.c` (header encoding + cursor advance).
- Update `STATUS.md` "Implemented Packet Builders" list.