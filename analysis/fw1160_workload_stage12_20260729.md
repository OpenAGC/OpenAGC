# FW 11.60 workload stage-12 failure

Date: 2026-07-29  
Console: standard PS5, raw firmware `0x11600005`  
Candidate revision: `f610c55`

Stage 12 tested the caller-owned inline workload lifecycle recovered from the
FW 11.60 `libSceAgc.sprx` DCB wrappers. It configured the proven GPU-info
process property, registered OpenAGC-owned stream 1, and built one 40-dword DCB
containing active → marker A → complete → marker B. DCB packets used control 0
and the exact 18-dword active and 12-dword complete forms. Only one driver
submission was made.

Observed output ended with:

```text
GPU-info process property=0
workload stream register=0
workload memory result=0 address=f02000000
inline workload DCB dwords=40
inline workload submit=0
```

No marker verdict, unregister, memory release, driver shutdown, or process
termination line followed. The foreground HTTP request timed out after 20
seconds and the console remained powered on with an unresponsive UI. Launching
the established process-cleanup ELF removed the stale payload; a subsequent
ps5debug-NG absence check reported no process matching `eboot.elf`.

This disproves the hypothesis that stage 11 stalled solely because active and
complete were submitted as separate buffers. The exact inline cursor lifecycle
is still insufficient for direct `/dev/gc` workload execution on FW 11.60.
The corrected public cursor ABI remains valid SPRX-derived userspace behavior,
but the distinct FW 11.60 submit-owning workload capability stays disabled.

Do not rerun stage 12 unchanged. Before another hardware attempt, recover the
Sony driver/module initialization that precedes workload use, including any
GPU/register enable state or kernel operation beyond the already proven
process property, stream-table address, packet bytes, and cursor lifecycle.
Reboot the console before a future GPU payload.
