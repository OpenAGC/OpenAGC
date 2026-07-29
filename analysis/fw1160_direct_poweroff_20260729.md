# FW 11.60 direct-backend power-off investigation

## Incident

On 2026-07-29 a standard PS5 reported system software `0x11600005`. The first
probe that passed firmware/model selection froze the UI and the console powered
itself off. Websrv stopped responding before the buffered operation log was
delivered, so the last completed operation cannot be claimed from runtime
evidence. Full FW 11.60 deployment remains disabled.

## Recovered initialization mismatches

Comparing the complete FW 5.50 and FW 11.60 `libSceAgcDriver` carriers exposed
four OpenAGC mismatches before the first submit:

1. The fixed `0xfe0200000` GC aperture is mapped with protection `0x22` on both
   firmwares. OpenAGC used ordinary CPU read/write protection `0x03`.
2. The call after `mmap` is NID `DGMG3JshrZU`,
   `sceKernelSetVirtualRangeName(addr, 0x4000, "SceGnmDingDong")`. OpenAGC had
   misidentified it as two-argument POSIX `mlock` and later called `munlock`.
3. `sceKernelMapNamedSystemFlexibleMemory` receives address hint
   `0xfe0300000` for `SceGnmGpuInfo` and `0xf00000000` for every other internal
   region. OpenAGC passed `NULL` for all nine allocations.
4. Both Sony carriers clear the complete `0xfc000` DDID allocation before
   publishing it. OpenAGC only initialized its 64-byte trailer subregion.

The hardware sample also performed a diagnostic aperture map/unmap immediately
before real initialization. That mutating preflight has been removed so the
backend owns the only mapping for its complete lifecycle.

These are exact static fixes, not a renewed FW 11.60 support claim. Any of the
first two mismatches can affect initialization itself; the address and DDID
mismatches can affect later GPU consumers.

The corrected path subsequently passed the complete FW 5.50 `agc_init`
hardware probe (`20260729T111638Z-98955`). All nine mappings landed at Sony's
expected hinted address sequence, and the full submission/queue/suspend
lifecycle completed. This proves the correction did not regress the qualified
firmware; it does not identify which mismatch caused the FW 11.60 power-off.

## Safe requalification boundary

`agc_fw1160_stage0.elf` reads only firmware and `hw.sce_main_socid`.
`agc_fw1160_stage1.elf` performs corrected `/dev/gc` initialization followed
immediately by shutdown. Neither stage allocates AGC internal memory or submits
GPU work. The process-cleanup ELF must be the immediately preceding websrv
launch. A later stage must not combine memory, submission, queue, suspend, or
display operations; each needs a separate two-pass gate.

`agc_fw1160_stage3.elf` is the first GPU gate: one direct DCB containing one
`WRITE_DATA` marker. FW 11.60 uses the same normalized submit16/multi-DCB
carrier group as FW 5.50. The standard compatibility group therefore shares
the exploited-payload CLOSE transition plus trailing NOP IB completion policy;
FW 5.50 is its hardware proof point rather than a separate submit ABI. The
stage invokes the direct backend entry because normal FW 11.60 runtime
selection deliberately remains blocked until qualification completes.

The following gates add exactly one new operation over qualified prerequisites:
stage 4 adds async setup, stage 5 adds authenticated queue create/destroy, and
stage 6 adds primary suspend submission while the qualified queue is active.
Stage 7 adds final suspend, stage 8 binds an aligned 16 KiB TF ring, and stage
9 exercises the HS-offchip ioctl/payload boundary with a valid aligned pointer
and zero entries; stage 9 does not claim non-empty patch-list execution.
Each gate still requires two clean runs with process cleanup immediately before
the payload.

The cleanup payload's `refusing stale eboot count/status=1` message means its
process scan found zero stale `eboot.elf` instances (`matches + 1`), not one
stale process. Successful stage payloads flush their PASS line and terminate
themselves with `SIGKILL` so websrv releases the foreground app and restores
the UI without manual intervention.

## Staged hardware result

On the same standard FW 11.60 console (`0x11600005`, SoC `0x00840f60`):

- Stage 0 passed twice and classified the hardware as standard PS5.
- Stage 1 passed twice. Profile configuration, corrected direct initialization,
  and direct shutdown each returned `AGC_OK`; the UI remained responsive and
  the console stayed powered on.
- Stage 2 passed twice. All nine internal allocations landed at the same exact
  hinted addresses as FW 5.50/Sony's carrier, DDID initialization completed,
  and shutdown returned `AGC_OK` without a freeze or power-off.
- Stage 3 passed twice. The shared standard-group CLOSE/trailing-NOP policy
  submitted one five-dword `WRITE_DATA` DCB; the GPU wrote `0x1160cafe` after
  50 ms and 0 ms respectively. Flexible-memory release and direct shutdown
  returned success in both runs.
- Stage 4 async setup returned `AGC_OK` twice and direct shutdown succeeded.
  The original probe process remained foreground after returning from `main`,
  leaving the UI black until the app was killed manually; this was a homebrew
  process-lifecycle issue, not a GPU failure. Later staged probes self-terminate
  after flushing their result.
- Stage 5 passed twice. Async setup, authenticated queue create (handle 0),
  queue destroy, and shutdown all returned success.
- Stage 6 passed twice. With the qualified async/queue prerequisites active,
  primary suspend submission returned `AGC_OK`; queue destroy and shutdown
  also succeeded. ps5debug-NG reported no `eboot.elf` after the monitored
  stage 5 and stage 6 runs.
- Stage 7 passed twice. Final suspend returned `AGC_OK` after the qualified
  primary-suspend sequence; queue and driver teardown remained clean.
- Stage 8 passed twice. An aligned mapped 16 KiB TF ring was accepted, the GC
  context shut down, and the ring memory released successfully.
- Stage 9 passed twice. The HS-offchip carrier accepted an aligned list pointer
  with zero entries, then context teardown and memory release succeeded. This
  qualifies the request/layout boundary, not non-empty patch-list execution.
- ps5debug-NG reported no `eboot.elf` after every monitored stage 7-9 run.
- Websrv retained each foreground HTTP pipe until the 20-second client timeout,
  but the cleanup check before the next run found no stale `eboot.elf`.

This hardware result validates corrected initialization, internal memory, and
basic graphics-ring submission through clean teardown. It strongly localizes
the original power-off to the corrected pre-submit mapping defects, though it
cannot distinguish which individual defect was causal. Basic submit, async,
authenticated queue lifecycle, and primary suspend are now qualified. Final
suspend and TF-ring are also qualified. The HS-offchip zero-entry carrier is
qualified, while non-empty patch-list semantics remain hardware-pending.
