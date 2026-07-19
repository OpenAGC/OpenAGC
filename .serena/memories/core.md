# OpenAGC — core

Clean-rewrite PS5 AGC (Advanced Graphics Controller) library. Provides `sceAgc*` / `sceAgcDriver*` ABI compatibility with the Sony PS5 SDK, buildable **without** proprietary SDK headers. MIT licensed, C99-ish, libc-only on host.

## Two backends

- `generic` — pure software host backend, used for tests. Selected via `-DOPENAGC_PLATFORM=generic` (or auto-detected on non-PS5 hosts). Compiles `src/driver_generic.c`. Defines `OPENAGC_GENERIC`.
- `prospero` — native PS5 `/dev/gc` backend. Selected via `-DOPENAGC_PLATFORM=prospero`. Compiles `src/driver_prospero.c` (guarded by `#ifdef OPENAGC_PROSPERO`). Defines `OPENAGC_PROSPERO` and `OPENAGC_REQUIRE_ABI`. Links `kernel` + `SceAgcDriver` from ps5-payload-sdk.

## Source map

- `include/` — public headers (`agc_*.h`, `agcdriver.h`). `agc_types.h` resolves `PS5_SYSV_ABI` via `__PROSPERO__`.
- `src/` — implementation. Core: `cb.c` (cursor), `cb_builders.c` (packet builders), `dcb.c`, `acb.c`, `context_state.c`, `register_defaults.c`, `texture.c`, `shader.c`, `game_compat.c`. Backend split: `driver_generic.c` vs `driver_prospero.c`.
- `tests/` — single-test-binary harness (`test_main.c` + per-area `test_*.c`). 1483 assertions total.
- `samples/hw_test/` — PS5 hardware validation ELFs (`videoout_linear.elf`, `agc_init.elf`, colorbars variants). Built via its own Makefile against ps5-payload-sdk.
- `analysis/` — RE reference notes (markdown + tsv). RE-only, not committed to firmware.

## Critical invariants

- **No firmware blobs.** SPRX/microcode under `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50` are RE references only. Never copy/embed/commit. Constants go in `include/agc_re.h`, `agc_nids.h`, `agc_ioctl.h`.
- **Backend isolation.** Anything touching `/dev/gc`, ioctls, kernel objects belongs ONLY in `driver_prospero.c` and must be `#ifdef OPENAGC_PROSPERO`-guarded. Never put PS5-specific code in `driver_generic.c`.
- **PM4 headers.** Gen5 type-3 packets encode `length_dwords - 2` in bits `29:16`. Always use `agcPm4Header3()` / `agcPm4Header3Sub()` from `agc_pm4.h` — never hand-pack.
- **Cursor model.** `SceAgcCb` / `SceAgcDcb` / `SceAgcAcb` are cursors. `agcCbAllocDwords(cb, n)` advances and returns write ptr; builders must NULL-check and bail.
- **ABI macros.** `PS5_SYSV_ABI` on every public function decl AND def. Don't change signatures without ABI reason.
- **Sibling project boundary.** PS4 `sceGnm*` belongs in `opengnm`, NOT here.

## Roadmap pointers

See `STATUS.md` and `PLAN.md`. Remaining hardware work: PA debug ioctl (EPERM), FRAME_OPEN ioctl (EINVAL, non-blocking), default-state blob validation, full GPU command submission, ongoing game-compat function coverage.

## Memory graph

- `mem:status` — current milestone, test count (1483), HW validation results, credential bypass, game coverage
- `mem:roadmap` — next RE tasks (PA debug, FRAME_OPEN, default-state validation, full GPU submit, game compat) + phase plan + evidence levels
- `mem:packet_model` — PM4 type-3 header, IT_NOP subcommands, cursor model, submit descriptor, verified opcodes
- `mem:implemented_builders` — full list of CB/DCB/ACB/VSH builders, patchers, prospero backend functions
- `mem:sprx_re` — SPRX disassembly: init sequence, internal memory (9 regions), queue create, suspend point, NIDs
- `mem:kernel_re` — kernel /dev/gc driver: ioctl table, handlers, credential check, error codes
- `mem:game_compat` — 3-game coverage (100%), Joe & Mac / PPSA09076 / PPSA03157 analysis
- `mem:ps5_openagc_audit` — what's WRONG in ps5-openagc (FRAME_OPEN, queue ioctls, VMID mask, mem types, region sizes) + what's verified
- `mem:reference_findings` — SharpEmu, RPCSX, opengnm, firmware paths, SDK paths
- `mem:tech_stack`, `mem:conventions`, `mem:suggested_commands`, `mem:task_completion`