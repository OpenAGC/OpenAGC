# AGC driver workload packet-builder facts

`agc_driver_workload_facts.tsv` records the four Sony workload exports and
their shared private size helper for every active driver image. Reproduce it
from the external firmware corpus with:

```sh
python3 tools/extract_agc_driver_workload_facts.py /Volumes/Untitled/unp \
    --output analysis/agc_driver_workload_facts.tsv
tools/verify_agc_driver_workload_facts.sh /Volumes/Untitled/unp
```

All 39 firmware keys share the same packet contract despite seven normalized
active-builder groups, three complete-builder groups, and two equivalent size-
helper compiler forms:

- stream IDs are 1 through 31;
- workload IDs are 0 through 63;
- active accepts 1 through 63 distinct workload IDs and builds their 64-bit
  mask;
- the final hardware packet is always nine dwords with header `0xc0071e00`;
- active and complete use prefix controls `0xcc000000` and `0xcd000000`, then
  packet controls `0x267` and `0x275` respectively;
- the exported maximum sizes are 18 dwords for active and 12 for complete.

This proves a common Sony ABI contract, not compatibility with OpenAGC's
existing APIs. The Sony exports take a command-buffer base/cursor, stream ID,
and workload collection or ID, and depend on registered-stream private state.
OpenAGC's public one-ID driver calls are a separate submit-owning extension
that passed on FW 5.50 only. The existing eight-dword DCB/ACB builders also do
not match the nine-dword Sony packet. Therefore Sony-compatible workload
capability remains disabled on every firmware; only the independently tested
FW 5.50 extension retains its narrowly scoped direct capability.

## Registered-stream state

FW 5.50 and FW 11.60 initialize the workload address table from internal
GPU-info region 2. In both standard-console layouts that region is the
`0x200`-byte span at `SceGnmGpuInfo + 0x3a000`; the base must be 16-byte
aligned and the module rejects a span smaller than `0x100`. The 32 entries are
GPU-visible 64-bit slots, so stream `n` uses `gpu_info_gpu_va + 0x3a000 + n*8`.
Module startup clears the backing GPU-info allocation and reserves stream 0 as
the 32-byte `"System"` metadata record. Public registration accepts stream IDs
1 through 31, copies exactly 32 descriptor bytes into a userspace metadata
table, and sets a registration bit. Unregistration clears that descriptor and
bit; it does not allocate, map, or release a kernel object. The get-info export
copies at most 32 descriptor bytes and separately returns the corresponding
64-bit GPU slot value.

This removes the earlier uncertainty about an external Sony-owned allocation:
the direct backend already owns the required `SceGnmGpuInfo` allocation. A
bounded adapter can carve and clear the exact subregion itself. It must still
emit the private prefix and final hardware packet below; using the slot address
alone is insufficient.

Hardware qualification showed that ownership of the virtual span is not by
itself sufficient. On standard-PS5 FW 11.60 the exact 18/12-dword adapter
returned `AGC_OK` for active and complete, but the ordered following marker
timed out at zero after five seconds. A follow-up attempt to reproduce the
SPRX's separately observed GPU-info process-property step caused a kernel panic
before a payload verdict was returned. The experiment used an incorrect
four-argument order and was removed. Corpus-wide RE now proves the actual call
as `("Sce.Debug:Gnm", gpu_info_base, gpu_info_span, 0, 0)`; see
`agc_driver_process_property_facts.md`. The corrected property-only stage 10
then passed on standard-PS5 FW `0x11600005`, including clean shutdown and
self-termination without PM4 submission. The standard FW 11.60 profile now
selects the exact adapter and installs that property idempotently before its
first workload call. Stage 11 separately gates active/complete execution with
an ordered `WRITE_DATA` marker; Trinity and all other unqualified profiles
remain fail-closed.

## Exact FW 11.60 builder layout

The private prefix helper emits nine dwords for the active call's eight-byte
mask and three dwords for the complete call's empty payload. With its boolean
control argument clear (the standalone-buffer form), active is:

| Dword | Value |
|---:|---|
| 0 | `0xc0027904` |
| 1 | `0x00000342` |
| 2 | `0xcc000000 | stream_id` |
| 3 | low 32 bits of the active workload mask |
| 4 | `0xc0033704` |
| 5 | `0x06010000` |
| 6 | `0x0000c343` |
| 7 | high 32 bits of the active workload mask |
| 8 | zero padding |
| 9 | `0xc0071e00` |
| 10 | `0x40000267` |
| 11:12 | GPU address of the selected 64-bit stream slot |
| 13:14 | the active workload mask |
| 15:17 | zero |

Complete uses the same standalone-buffer control form:

| Dword | Value |
|---:|---|
| 0 | `0xc0017904` |
| 1 | `0x00000342` |
| 2 | `0xcd000000 | (workload_id << 5) | stream_id` |
| 3 | `0xc0071e00` |
| 4 | `0x40000275` |
| 5:6 | GPU address of the selected 64-bit stream slot |
| 7:8 | `~(1ULL << workload_id)` |
| 9:11 | zero |

When the boolean control argument is set, bit 2 is clear in both prefix packet
headers and bit 30 is clear in the final packet control. Active accepts a set
of distinct IDs and ORs `1ULL << id` for each. Complete accepts one ID. The
OpenAGC one-ID adapter can therefore use one registered private stream and a
single-bit mask without narrowing the recovered packet contract. It does not
yet reproduce the loader-owned GPU-info mapping state, as the hardware failures
above demonstrate.

KytyPS5 reserves 18 and 12 dwords but emits `IT_NOP` emulator metadata.
SharpEmu has no workload builders. Neither is hardware-packet evidence.
