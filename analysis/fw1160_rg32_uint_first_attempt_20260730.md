# FW 11.60 RG32_UINT first attempt — 2026-07-30

## Artifact

- target: `RG32_UINT`
- SHA-256: `da7c0203a288d994986ff37100e3eac5f8cb962fca00a4821c53f54fc9cb9511`
- firmware-neutral verifier: PASS
- expected runtime ABI key: `0x1160`

## Result

The first actual launch produced no file-backed verdict within the bounded
30-second poll. The runner had deleted the preceding R32_UINT result before
launch, so no stale output was accepted. Cleanup then removed any candidate
payload; ps5debug-NG enumerated 190 processes and found only `websrv.elf`, with
no `eboot.elf`.

This attempt is inconclusive rather than an RG32 failure. A control launch of
the already hardware-qualified pinned R32_UINT artifact failed in exactly the
same way on this boot: its bytes uploaded and verified, but websrv produced no
new result file. That control rules out using the current loader state to draw
a conclusion about the `32_GR` export or `COLOR_32_32` target.

The detached cleanup request itself began returning curl status 28 even though
the helper ran and websrv recovered. Commit `719d57f` aligned the guarded
runner with its documented behavior by accepting only status 0 or 28, then
still requiring a live websrv preflight. This transport fix does not turn the
missing payload verdict into a pass.

## Required next hardware action

1. Reboot the FW 11.60 console.
2. Reinject ps5debug-NG and verify ports 8080, 2121, 744, and 3232.
3. Confirm no residual `eboot.elf`.
4. Run the same hash-named RG32_UINT ELF through the cleanup-first target.
5. Require two complete, identical-byte passes before qualification.

Do not rebuild or replace the pinned ELF. FW 5.50 replay remains pending.
