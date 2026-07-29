# FW 11.60 installed-driver workload attempt

Date: 2026-07-29  
Console: standard PS5, raw firmware `0x11600005`, SoC `0x00840f60`  
Artifact: `samples/hw_test/agc_fw1160_sony_workload.elf`  
SHA-256: `55e1702a94475b97123a6849c599fff430f907a216dcfd8af697434c0bb6cf27`

The process-cleanup payload was the immediately preceding homebrew launch.
ps5debug-NG remained reachable on TCP port 744. The oracle patched GPU
credentials before `dlopen("libSceAgcDriver.sprx")`; the module printed
`Initialized, submit.mode= 1`, and all eight required exports resolved.
The installed driver reported the expected 18/12-dword workload reservations,
and `sceAgcDriverSetupAsyncGraphics(1)` returned `AGC_OK` while reporting
`submit.mode = 1, agrEnabled = 1`.

The ordinary installed-driver `WRITE_DATA` preflight submission returned
`AGC_OK`, but its marker remained zero after the bounded 5,000 ms wait:

```text
installed preflight submit=0x00000000 marker=0x00000000 wait=5000 ms
installed preflight execution: FAIL; workload not attempted
```

The safety gate therefore prevented stream registration and prevented both
workload builders from running. The payload retained the unresolved mapping
for process teardown, printed its failure verdict, and killed itself. This is
the same installed-backend limitation previously observed on FW 5.50: an
installed payload-context submit can report success without executing the DCB.

## Revised interpretation

This attempt does not answer whether Sony-private workload state would make
the nine-dword packets execute. Its single-DCB preflight did not account for
the already hardware-proven exploited-payload rule that the final submitted
graphics descriptor remains deferred. OpenAGC's working direct backend avoids
that condition by appending a GPU-visible 16-dword NOP descriptor.

The revised oracle therefore uses Sony's `sceAgcDriverSubmitMultiDcbs` with two
observable DCBs followed by the same harmless 16-dword NOP trailer. It also
flushes every cache line occupied by the 40-dword workload DCB. The first
attempt remains useful evidence that the safety gate worked, but it is not
evidence that the installed backend is unusable. Do not repeat the original
single-DCB artifact. The console was rebooted before the revised test boundary.
