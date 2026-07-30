# Firmware-neutral R16 UNORM qualification

Date: 2026-07-30  
Hardware: standard PS5, raw firmware `0x11600005`  
Backend: direct `/dev/gc`, no `libSceAgcDriver` dependency

## Selected tuple and offline evidence

`R16_FLOAT` and `RG16_FLOAT` were already qualified, so the next genuinely
unqualified 16-bit color tuple is `R16_UNORM`. It retains the proven gfx1013
data format `0x02`, standard component swap, two-byte pixel width, and
FP16_ABGR shader export while selecting CB number type UNORM (`0`). The local
rpcsx format table independently maps its `(kDataFormat16,
kNumericFormatUNorm)` pair to `VK_FORMAT_R16_UNORM`.

The public enum was appended to preserve every existing numeric value. Exact
host fixtures lock `CB_COLOR0_INFO=0x00028008`, the typed state, a one-dword
short-buffer rejection, unaligned-address rejection, and preservation of the
caller output for a one-past-last format query. The format-table length has a
compile-time assertion. Every one of the 39 active firmware-profile fixtures
also resolves and initializes the same tuple after exact runtime selection.

The SPRX-derived operation ledger, defaults selectors, submission payloads,
exported-wrapper fingerprints, and VideoOut patch facts were reproduced from
`/Volumes/Untitled/unp` for all active profiles. Those checks establish the
firmware-dependent carriers used around the common gfx1013 packet stream; they
do not substitute for render-target hardware execution on those profiles.

## Preserved artifact and guarded replay

The headless artifact is built without `AGC_EXPECT_FIRMWARE_ABI_KEY` and
selects its defaults version from the detected four-digit runtime key. It
contains no dynamic dependency on `libSceAgc.sprx` or
`libSceAgcDriver.sprx`:

```text
c0a5ad4732bf13c41f96560cb2dbfa3c39dffb9a47958ccbd2bef5754523220a  agc_graphics_r16_unorm_portable.elf
```

The FW 11.60 and future FW 5.50 Make targets reference the same artifact and
hash. The runner rejects changed local bytes before console access, verifies
the uploaded bytes by downloading and hashing them, creates the neutral
file-backed verdict directory, deletes any stale verdict, and launches the
process-cleanup ELF immediately before every payload.

Two discovery attempts do not count as qualification. The first found that
the neutral result directory was not created and therefore yielded no
file-backed verdict. The second produced a complete fail-closed diagnostic:
the sample's absent-macro branch had defaulted its test expectation to
`0x0550`, rejected the live `0x1160` profile before internal-memory setup or
GPU submission, shut the driver down, and exited. The corrected artifact
removed that implicit pin and derives both the printed key and defaults choice
from the runtime profile. Websrv and ps5debug-NG stayed responsive throughout.

## FW 11.60 result

The corrected artifact passed twice through the cleanup-first guarded target.
Both runs reproduced:

| Oracle | Result |
| --- | --- |
| Runtime profile | `0x1160`, standard PS5, PASS |
| CB tuple | format `0x02`, number `0`, swap `0` |
| DCB and submit | 2,470 dwords, `AGC_OK` |
| Completion fence | immediate (`0 us`) |
| Changed/complete pixels | `255217 / 255217` |
| Bounds | `x=384..1151`, `y=436..1100` (`768x665`) |
| Native conversion range | `0x0000..0xffff` |
| Distinct sampled values | at least 8 |
| Native FNV64 | `0x4f17d5e6b1c0d45b` |
| Driver and memory teardown | all zero / PASS |
| Final verdict | PASS |

The changed count is lower than the FP16 gate because the diagnostic sentinel
`0x7e00` is itself a legal UNORM16 value; samples that quantize to that exact
value are conservatively excluded. The exact bounds, broad native range,
diversity, completion count, and reproduced hash provide the semantic oracle.

This result hardware-qualifies `R16_UNORM` only on standard FW 11.60. FW 5.50
must run the exact preserved bytes before endpoint portability is claimed.
The remaining 37 active profiles are SPRX-qualified and host-tested but remain
hardware-unverified.
