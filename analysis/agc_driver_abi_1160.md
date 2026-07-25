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

FW 4.00 is the oldest inspected driver with the PID/direct-submit request used
by the current backend. FW 1.00 through 3.20 need a separate backend around
the older `0xc0108102` submission path. The critical direct-backend requests
and standard-console allocation profile remain present in every inspected
full driver from FW 4.00 through 12.70.

Later drivers contain a hardware-model branch that allocates `0x1600000`
bytes for CWSR on PS5 Pro instead of the standard PS5's `0x1000000`. The
current backend is therefore explicitly a standard-PS5 backend. PS5 Pro must
not be claimed until the model predicate and its complete allocation profile
are implemented.

Only FW 5.50 has been exercised on real hardware. Other registered builds are
RE-verified aliases and must remain documented as awaiting per-firmware
hardware validation.
