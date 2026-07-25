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
| TF ring | `0xc0108139` |
| HS offchip parameter | `0xc008812d` |

The direct submit wrapper uses a 16-byte object with fields at offsets 0, 4,
and 8. Initialization opens `/dev/gc`, issues the context query, and maps a
`0x4000` register aperture at `0xfe0200000`.

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
`yu17wG8L5FI`. OpenAGC resolves that predicate at runtime and fails closed if
it is unavailable. Standard PS5 uses a `0x1000000` CWSR allocation, a
`0xa00000` CWSR working offset, and a `0x100000` GPU-info span. Trinity/PS5 Pro
uses `0x1600000`, `0x1000000`, and `0x180000`, respectively.

Only FW 5.50 has been exercised on real hardware. Other registered builds are
RE-verified aliases and must remain documented as awaiting per-firmware
hardware validation.
