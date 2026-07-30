# Runtime whole-image ownership handoff — FW 5.50

Date: 2026-07-31

Endpoint: standard PS5, exact system software `0x05500008` (ABI key `0x0550`)

Artifact SHA-256:

```text
70f15e0a5687e431f532d384f5ffb1062b4883bb99746dcaf3857e3dfc5cf7fd
```

The local ELF and the bytes read back from websrv FTP had the same digest.

The bounded native-runtime diagnostic created an 8x8 RGBA8 storage/sampled
image, established compute `shader-write` state, and submitted the version-2
compute release followed by the matching graphics `shader-read` acquire without
a CPU wait between submissions. The release and acquire submissions both
completed within the 200 ms fence bound.

Every setup, transition, submit, wait, reset, destroy, queue teardown, and
device teardown operation returned `AGC_OK`. The final verdict was:

```text
Native runtime image handoff result: PASS
```

This qualifies the whole-image compute-to-graphics synchronization carrier on
exact FW 5.50. The diagnostic deliberately contains no shader workload, so it
does not by itself qualify sampling or storage writes through this handoff.
