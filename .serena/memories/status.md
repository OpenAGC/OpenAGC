# Current Status

## Test count
Host generic backend: **1483 passed, 0 failed** (any drop = regression).
Build: `cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure` (or `make && make test`).

## Milestone
RE foundation + native prospero backend implemented and hardware-validated (FW 5.50, exploited PS5 @ 10.0.1.41). All userspace packet builders, patchers, LOD stats helpers, FW 5.50 ioctl/submit/queue RE layer, shader record parser, register-defaults blob builder/parser, and `driver_prospero.c` are implemented and build.

## Hardware validation (FW 5.50)
- videoout_linear.elf — PASS (4K, 60fps flip loop)
- agc_init.elf — PARTIAL PASS:
  - [1] sce_agc_initialize — PASS (fd=7, CONTEXT_QUERY OK, mmap @0xfe0200000)
  - [2] sce_agc_initialize_internal_memory — PASS (9 regions, type=0x33)
  - [3] sceAgcDriverNotifyDefaultStates — PASS
  - [4] sceAgcDriverGetPaDebugInterfaceVersion — **FAIL EPERM** (separate kernel permission check, not cr_sceAuthId)
  - [5] sceAgcDriverSubmitDcb(NOP) — PASS
  - [6] sceAgcDriverSetupAsyncGraphics(1) — PASS (ioctl 0x80048126)
  - [7] _sceAgcDriverCreateUserSpecialQueue — PASS (with credential bypass)
  - [8] sceAgcDriverSuspendPointSubmitDirect — PASS (with credential bypass)
  - [9] _sceAgcDriverDestroyUserSpecialQueue — PASS
  - [10] BeginWorkload/EndWorkload — PASS

## Credential bypass (cr_sceAuthId)
Kernel handler 0xffffffffd8f66bb0 calls 0xffffffffd8e70400 which checks `proc->creds` at `[ucred+0x58]` (cr_sceAuthId). Check: `((cr_sceAuthId & 0xff0f000000000000) + 0xb7ff000000000000) >> 49 == 0`. Setting **cr_sceAuthId = 0x4801000000000000** satisfies this (64-bit overflow → 0). Same check + magic triple (0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c) gates queue create and suspend point. Magic triple selects config table 0xd9d5b360 → slot (2,3,5) at ctx offset 0x158.

## Game compatibility — 100% coverage
| Game | Title ID | AGC imports | Implemented |
|------|----------|-------------|-------------|
| Joe & Mac Caveman Ninja | PPSA02801 | 70 | 70 |
| PPSA09076 (backport) | PPSA09076 | 69 | 69 |
| PPSA03157 | PPSA03157 | 58 | 58 |
Total unique AGC functions: 72. All implemented.

## Hardware-discovered bugs fixed
- PS5 memory types differ from PS4: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2 (type=1 fails on exploited PS5)
- PS5 VideoOut requires userId=0xFF (not 0)
- PS5 VideoOut requires tiled mode (linear needs debug setting)
- PS5 direct memory: garlic searchEnd=0x300000000, alignment=0x200000
- `__ORBIS__` → `__PROSPERO__`

See `mem:roadmap` for next tasks, `mem:hw_validation` for details, `mem:game_compat` for per-game analysis.