# PS5 AGC driver ABI families and FW 11.60 recovery

OpenAGC's public `sceAgc*` API is firmware-independent. Native `/dev/gc`
access is selected through explicit backend aliases for firmware builds whose
private driver ABI was independently inspected. Numeric firmware ranges are
not treated as evidence.

## Sources and method

- Primary FW 11.60 inputs: decrypted `libSceAgcDriver.sprx` and
  `libSceAgc.sprx` from the local firmware archive.
- Cross-version inputs: decrypted drivers from FW 1.00 through 12.70.
- Firmware files are reference inputs only and are not copied into OpenAGC.
- Request words, immediate sizes, queue tokens, and offsets were recovered
  from x86-64 disassembly and compared as complete per-firmware sets.
- `tools/verify_agc_driver_fw1160.sh` anchors the facts below to exact export
  NIDs/sizes and internal instruction addresses in both `libSceAgcDriver` and
  `libSceAgc`. It verifies field stores for typed payloads rather than accepting
  an incidental command constant elsewhere in the image.

FW 11.60 exposes 167 driver functions; 140 map to the FW 5.50 NID corpus.
`libSceAgc.sprx` exposes 247 functions; 216 map. Critical public driver calls,
including submission, default-state notification, async setup, queue control,
suspend points, and capture control, are present.

## FW 11.60 direct backend facts

| Operation | Request word |
|---|---:|
| context query | `0xc004812e` |
| 16-byte submit | `0xc0108102` |
| PID/direct submit | `0xc010813b` |
| queue create | `0xc0408121` |
| queue destroy | `0xc00c810e` |
| async graphics setup | `0x80048126` |
| public TF ring | `0x80108128` |
| privileged TF ring | `0xc0108120` |
| HS offchip parameter | `0xc010812c` |
| primary suspend | `0xc010811c` |
| final suspend | `0xc0108139` |

`0xc0108139` is the final-suspend operation, not TF-ring. `0xc008812d` is a
distinct operation and is not the HS-offchip setter.

The direct submit wrapper uses a 16-byte object with fields at offsets 0, 4,
and 8. Initialization opens `/dev/gc`, issues the context query, and maps a
`0x4000` register aperture at `0xfe0200000`.

The typed layouts shared with FW 5.50 and locked by `_Static_assert` in
`include/agc_ioctl.h` are:

| Payload | Size | Fields |
|---|---:|---|
| `AgcGcSubmitArgs` | `0x10` | `u32@0`, `u32@4`, `u64@8` |
| `AgcGcSuspendArg` | `0x10` | four `u32` values at `0,4,8,0xc` |
| `AgcGcSetTFRingArg` | `0x10` | `u64@0`, `u32@8`, reserved `u32@0xc` |
| `AgcGcSetHsOffchipArg` | `0x10` | `u64@0`, `u32@8`, reserved `u32@0xc` |
| `AgcGcQueueCreateArg` | `0x40` | tokens `@0..0xc`, addresses `@0x10..0x38` |
| `AgcGcQueueDestroyArg` | `0x0c` | three `u32` tokens at `0,4,8` |

## Direct-operation status

FW 11.60 is statically qualified (hardware pending) for submit16, standard and
Trinity internal-memory sizing, authenticated queue create/destroy, primary
and final suspend submission, public TF-ring setup, HS-offchip setup, and async
graphics setup. Its workload wrappers build a larger, different packet
contract than OpenAGC's one-ID workload helper, so workload calls fail closed.
The one-ID helper remains enabled only on FW 5.50 because its independent
three-dword submission path passed the real-console qualification sample; it
must not be inferred from the Sony export's ABI on other firmware.
Both public/direct suspend-query exports are permission stubs. The internal
`0x80048127` helper's result semantics are not exposed by those wrappers, so
suspend query remains disabled. `libSceAgc` exposes a versioned 0..12 defaults
dispatcher, but its no-argument API reads the selected version from a runtime
hardware table. Static analysis of the driver wrapper does not establish the
selected 11.60 value, so default-state construction also remains disabled.

The Sony 5.50 and 11.60 workload-active and workload-complete exports emit
nine-dword `0xc0071e00` packets and consume multiple arguments. That contract
differs from OpenAGC's three-dword, one-ID convenience packet. The latter is a
hardware-qualified FW 5.50 extension, not a Sony-compatible export, and stays
disabled for 11.60.

The standard-console internal allocations are unchanged from FW 5.50:

| Region | Size |
|---|---:|
| GpuInfo | `0x100000` |
| TrapCode | `0x4000` |
| TrapData | `0x4000` |
| Ddid | `0xfc000` |
| EopFifo | `0x3c000` |
| ShadowReg | `0x4000` |
| Cwsr | `0x1000000` |
| Misc | `0x4000` |
| ACQRB | `0x1e0000` |

Queue constants are also unchanged: tokens `0xaf1e80b7`, `0x8b4cdd90`,
`0x99f68d6c`, and `0xe5fcc174`; EOP ring offset `0x39000`; ACQRB read-pointer
offset `0x1c8000`; and ACQRB metadata offset `0x1cc000`.

## Compatibility boundary

OpenAGC's hardware-validated path uses the 16-byte `0xc0108102` submit request,
not the later PID request. Its `{queue_type, count, descriptor_pointer}` layout
is identical in representative FW 1.00, 2.50, 3.20, and 11.60 drivers. Exact
inspected FW 1.00-3.20 builds therefore have dedicated submit16 profiles.
FW 1.00 predates the later authenticated special-queue layout, while FW 1.x
and 2.x lack the TF-ring request; those optional operations fail explicitly.

FW 9.00 and later inspected drivers import `sceKernelHasTrinityMode` at NID
`yu17wG8L5FI`. The FW 11.60 libkernel implementation reads the four-byte
`hw.sce_main_socid` sysctl and identifies Trinity when
`(socid & ~0x1f) == 0x00840fc0`. OpenAGC first resolves the predicate at
runtime, then reproduces this exact sysctl predicate when the protected export
is not visible to a websrv payload; it fails closed if neither query is
available. Standard PS5 uses a `0x1000000` CWSR allocation, a
`0xa00000` CWSR working offset, and a `0x100000` GPU-info span. Trinity/PS5 Pro
uses `0x1600000`, `0x1000000`, and `0x180000`, respectively.

Only FW 5.50 has been exercised on real hardware. Other registered builds are
RE-verified aliases and must remain documented as awaiting per-firmware
hardware validation.
