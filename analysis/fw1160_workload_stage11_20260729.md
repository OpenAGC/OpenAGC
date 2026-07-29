# FW 11.60 workload stage-11 failure

Date: 2026-07-29  
Console: standard PS5, raw firmware `0x11600005`  
Candidate revision: `8e1cf1f`

The isolated stage-10 prerequisite passed first: the exact five-argument
`sceKernelSetProcessProperty("Sce.Debug:Gnm", gpu_info, 0x100000, 0, 0)` call
returned `AGC_OK`, `SceGnmDumpArea` naming succeeded, driver shutdown passed,
and the payload self-terminated without PM4 submission.

Stage 11 then initialized the same direct profile and memory layout, including
the 0x200-byte workload table at `SceGnmGpuInfo + 0x3a000`. It installed the
property idempotently and submitted OpenAGC-owned stream 1 using the recovered
18-dword active and 12-dword complete standalone packets. Observed output:

```text
workload active=0x00000000
workload complete=0x00000000
post-workload submit=0x00000000 dwords=5
```

No post-workload marker verdict or shutdown line followed. The foreground HTTP
request timed out after 20 seconds. The console stayed powered on but its UI
was unresponsive. ps5debug-NG enumerated PID 104 as `eboot.elf`, while an exact
debugger attach failed with `AttachDebuggerCommand: ERROR`. Launching the known
process-cleanup ELF through websrv removed the stale process; ps5debug-NG then
reported no matching `eboot.elf`.

This contradicts any claim that the correct process property plus an owned
GPU-info slot and exact packet bytes are sufficient. FW 11.60 workload remains
fail-closed. Before another hardware attempt, recover additional Sony
registered-stream state and lifecycle from the SPRX. Do not rerun the unchanged
stage-11 sequence.
