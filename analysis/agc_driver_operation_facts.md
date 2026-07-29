# AGC driver operation facts

`agc_driver_operation_facts.tsv` is the conservative operation-level ledger
for every active four-digit firmware key. Regenerate it with:

```sh
python3 tools/build_agc_driver_operation_ledger.py \
  --output analysis/agc_driver_operation_facts.tsv
```

The generator consumes `agc_register_defaults_facts.tsv`; defaults capability
cannot be inferred from a driver-wrapper fingerprint or dispatcher upper
bound.

The operation columns state what the direct backend may actually issue. The
fingerprint columns identify normalized Sony export-wrapper groups from
`agc_driver_wrapper_fingerprints.tsv`; a shared digest is grouping evidence,
not permission to enable an operation. Internal command words, complete
payload layouts, memory constants, and defaults selection must also be exact.

Current boundary:

- FW `0x0550` is hardware-qualified for its listed direct operations. Its
  one-ID workload submit is an independently tested OpenAGC extension, not an
  ABI-compatible implementation of Sony's multi-argument workload exports.
- FW `0x1160` is exact-RE-qualified and hardware-pending for the listed subset.
  Workloads, suspend query, and default states remain disabled.
- Every active firmware key has exact submit16, internal-memory,
  authenticated-queue, primary-suspend, public TF-ring, HS-offchip, and async
  carrier evidence. Outside FW 5.50 these operations remain hardware-pending;
  other fields record why each operation stays disabled.
- The direct-named Sony suspend, TF-ring, and HS-offchip exports are common
  permission stubs across all 39 profiles. Direct `/dev/gc` support therefore
  depends on separately recovered internal ioctl paths, never export presence.
- All no-argument defaults wrappers select through a runtime hardware table.
  Only FW 5.50 has a hardware-observed selected version (8), so all other
  direct defaults operations remain disabled even when their versioned
  dispatcher accepts version 8, 9, or 12.

FW 3.20 remains the lowest active compatibility target. FW 1.00 and 2.x stay
archival and are intentionally absent from this active-operation ledger.

## Workload packet evidence

The workload APIs have three distinct contracts and must not be conflated:

| Source/API | Active form | Complete form | Evidence level |
|---|---:|---:|---|
| OpenAGC driver convenience API | 3 dwords | 3 dwords | Passed on real FW 5.50 hardware |
| Sony FW 5.50 driver packet builder | 18 dwords maximum | 12 dwords maximum | Exact SPRX disassembly |
| KytyPS5 HLE DCB builder | 18-dword NOP placeholder | 12-dword NOP placeholder | Interface/size model only |
| SharpEmu | Not implemented | Not implemented | No workload evidence |

The Sony builder's active path emits a variable prefix followed by a
nine-dword packet headed by `0xc0071e00`; its maximum reservation is 18
dwords. The complete path emits a three-dword prefix plus the same nine-dword
packet, for 12 dwords. Kyty preserves those reservation sizes and some logical
fields but deliberately writes `IT_NOP`, so it is not a hardware-encoding
reference. OpenAGC's existing eight-dword DCB/ACB builders do not match this
evidence and are not used to promote any firmware workload capability.

The three-dword OpenAGC direct helper is a separate submit-owning extension.
It returned `AGC_OK` for active and complete on FW 5.50 and completed the
bounded hardware sample without a hang. That proves its tested 5.50 scope but
does not establish Sony export ABI compatibility or portability to another
firmware.

The same limits, controls, header, and maximum sizes are now disassembly-
verified across all 39 active driver images in
`agc_driver_workload_facts.tsv`; seven/three wrapper groups are compiler and
surrounding-state variations around one common packet contract.
