# FW 11.60 installed-driver workload oracle

## Purpose

Direct `/dev/gc` stage 13 proved that FW 11.60 default states, async setup,
the `Sce.Debug:Gnm` process property, stream registration, ordinary DCB
submission, marker visibility, packet bytes, slot address, and inline cursor
lifecycle are all present before the first `SET_WORKLOAD` packet stalls. The
remaining hypothesis is private GPU or queue state established by the Sony
driver module.

`samples/hw_test/agc_fw1160_sony_workload.c` is an opt-in oracle for that
hypothesis. It does not make the installed driver OpenAGC's primary backend and
does not initialize or fall back to the direct backend.

## Ordering and isolation

The payload enforces this order:

1. Require runtime firmware key `0x1160` and reject Trinity hardware.
2. Apply the GPU credential bypass before loading the module.
3. `dlopen("libSceAgcDriver.sprx", RTLD_NOW | RTLD_LOCAL)` so the console's
   matching module initializes only after credentials are active.
4. Resolve every required export from that module-specific handle.
5. Require the module's packet-size exports to return 18 and 12 dwords.
6. Call the installed async setup export.
7. Submit an ordinary installed-driver `WRITE_DATA` marker and require it to
   execute before registering or building a workload packet.
8. Register stream 1 through the installed module, call its exact active and
   complete builders into one DCB, and submit through its `SubmitDcb` export.

The preflight gate is important: FW 5.50 testing previously showed that an
installed payload-context submit could return `AGC_OK` without executing a
marker. If FW 11.60 behaves the same way, the oracle stops before workload use
and records that the installed backend cannot answer the state hypothesis in
this environment.

The ELF has no `libSceAgcDriver.sprx` `DT_NEEDED` entry; preloading would run
module initialization before `main` can patch credentials. The Make verifier
also requires the exact runtime module name in `.rodata`.

## Recovered export signatures

The FW 11.60 wrappers at `0x0af0`, `0x0d10`, and `0x0e70` establish the
module-specific calls used by the oracle:

```c
int32_t RegisterWorkloadStream(uint32_t stream_id,
    const void *descriptor_32_bytes);
int32_t SetWorkloadsActive(uint32_t *packet, uint32_t control,
    uint32_t stream_id, const uint32_t *workload_ids,
    uint32_t workload_count);
int32_t SetWorkloadComplete(uint32_t *packet, uint32_t control,
    uint32_t stream_id, uint32_t workload_id);
```

DCB control is zero. The module's builders supply their private stream-slot
address, which is the exact state under investigation; OpenAGC does not replace
or patch the resulting packets.

## Safety gate and deployment

Build and verify without touching hardware:

```sh
make -C samples/hw_test fw1160_sony_workload_check
```

Hardware deployment is deliberately opt-in and runs the process-cleanup ELF
immediately beforehand:

```sh
make -C samples/hw_test deploy_agc_fw1160_sony_workload \
  PS5_HOST=10.0.1.39 ALLOW_SONY_DRIVER_ORACLE=YES
```

If an `AGC_OK` submission does not reach its marker, the payload does not
unregister the stream or unmap GPU-visible memory. It prints the unresolved
state and lets process teardown release resources. A clean console reboot is
required after every oracle attempt, whether it passes or fails. Never launch
a direct `/dev/gc` payload later in the same boot session.

Status: build-qualified, hardware pending.
