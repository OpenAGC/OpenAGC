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

## Consequence

This attempt does not answer whether Sony-private workload state would make
the nine-dword packets execute, because the installed backend cannot pass an
ordinary command-buffer execution oracle in the websrv homebrew-loader
context. `libSceAgcDriver` is therefore not a usable replacement backend and
cannot be used to qualify the workload path here.

Do not repeat this oracle unchanged. The next investigation must compare the
installed module's `submit.mode = 1` initialization and submission routing
with the working direct `/dev/gc` submit16 path, or recover the missing
GPU-side `SET_WORKLOAD` queue/register state offline. The console must be
rebooted before any further direct `/dev/gc` GPU test.
