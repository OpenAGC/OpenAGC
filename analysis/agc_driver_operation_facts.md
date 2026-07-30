# AGC driver operation facts

`agc_driver_operation_facts.tsv` is the conservative operation-level ledger
for every active four-digit firmware key. Regenerate it with:

```sh
python3 tools/build_agc_driver_operation_ledger.py \
  --output analysis/agc_driver_operation_facts.tsv
```

The generator consumes `agc_register_defaults_facts.tsv`; defaults capability
requires both the dispatcher bound and the proven `sceAgcInit` argument flow
into runtime-record offset `0x44`.

The operation columns state what the direct backend may actually issue. The
fingerprint columns identify normalized Sony export-wrapper groups from
`agc_driver_wrapper_fingerprints.tsv`; a shared digest is grouping evidence,
not permission to enable an operation. Internal command words, complete
payload layouts, memory constants, and defaults selection must also be exact.

Current boundary:

- FW `0x0550` is hardware-qualified for its listed direct operations. Its
  one-ID workload submit is an independently tested OpenAGC extension, not an
  ABI-compatible implementation of Sony's multi-argument workload exports.
- Standard-PS5 FW `0x1160` is hardware-qualified for submit16, internal memory,
authenticated queue lifecycle, private primary/final suspend, public TF-ring,
zero-entry HS-offchip carrier, and async setup. Both public Direct suspend
exports are corpus-proven `0x8a6d0001` permission stubs. Workloads, EOP flip, and
non-empty HS patch lists remain disabled.
- Every active firmware key is runtime-selectable and has exact submit16, internal-memory,
  authenticated-queue, primary-suspend, public TF-ring, HS-offchip, and async
  carrier evidence. Outside the tested FW 5.50 and standard-PS5 FW 11.60
  profiles these operations remain hardware-unverified; other fields record
  why each operation stays disabled.
- The direct-named Sony suspend, TF-ring, and HS-offchip exports are common
  permission stubs across all 39 profiles. Direct `/dev/gc` support therefore
  depends on separately recovered internal ioctl paths, never export presence.
- All no-argument defaults wrappers read the version previously supplied to
  `sceAgcInit`. OpenAGC now does the same: every exact profile exposes its
  SPRX-proven accepted range and the direct backend uses the caller-selected
  version, never the range maximum as an inferred hardware choice. FW 5.50 V8
  and FW 11.60 V12 are hardware-qualified caller/profile combinations; all
  other combinations are hardware-unverified.
- `agc_driver_ring_facts.tsv` semantically verifies the 16-byte TF/HS payloads
  behind each carrier group. FW 12.x adds explicit reserved-dword zeroing;
  OpenAGC's zero-initialized typed arguments satisfy both forms.
- `agc_driver_submission_facts.tsv` verifies that all DCB, ACB, and multi-DCB
  groups converge on one `0xc0108102` carrier with the complete
  `u32@0,u32@4,u64@8` request layout.

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
