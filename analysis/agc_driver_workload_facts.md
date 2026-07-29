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

The Sony exports take a command-buffer base/cursor, stream ID, and workload
collection or ID, and depend on registered-stream private state. OpenAGC's
public DCB and ACB cursor wrappers now reproduce that contract: DCB passes
control 0, ACB passes control 1, active reserves 18 dwords, complete 12, and
inactive emits the nine-dword zero-mask prefix. OpenAGC's public one-ID driver
calls remain a separate submit-owning extension that passed on FW 5.50 only;
correct cursor builders do not justify enabling that convenience operation on
FW 11.60 or other unqualified profiles.

## Cursor wrapper ABI

FW 11.60 `libSceAgc.sprx` independently proves the public cursor contracts.
The DCB wrappers at `0x7e90`, `0x7f50`, and `0x8000` pass control `0` to the
driver builders; the ACB wrappers at `0x1fe0`, `0x2090`, and `0x2130` pass
control `1`. Their exact C-level argument shapes are:

```c
uint32_t *SetWorkloadsActive(SceAgcCb *cb, uint32_t stream_id,
    const uint32_t *workload_ids, uint32_t workload_count);
uint32_t *SetWorkloadComplete(SceAgcCb *cb, uint32_t stream_id,
    uint32_t workload_id);
uint32_t *SetWorkloadStreamInactive(SceAgcCb *cb, uint32_t stream_id);
```

Each wrapper queries the exact required size, grows the cursor if necessary,
calls the driver builder at the current cursor, advances only on success, and
returns the packet start. The inactive driver builder emits the same nine-dword
active prefix with a zero 64-bit mask and no final `SET_WORKLOAD` packet.

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
self-termination without PM4 submission. Stage 11 then installed the property
idempotently and submitted exact active ID 1 and complete ID 1 packets. Both
calls returned `AGC_OK`, but the process and UI stalled before the ordered
marker loop could report its five-second verdict. The missing prerequisite is
therefore registered-stream state or lifecycle beyond the proven property,
address table, and packet bytes. FW 11.60, Trinity, and all other unqualified
profiles remain fail-closed; do not rerun stage 11 unchanged.

Stage 12 then used the exact caller-owned DCB lifecycle recovered from
`libSceAgc`: active, marker, complete, and marker in one submission. The
submit returned `AGC_OK`, but the first marker was never observed and the UI
stalled. Cleanup removed the payload. This rules out separate submissions as
the sole cause; do not rerun stage 12 unchanged.

The FW 11.60 workload initializer at driver vaddr `0xa50` was subsequently
traced end to end. It performs no workload-specific ioctl: it selects GPU-info
region 2, validates a span of at least `0x100` bytes and 16-byte alignment,
reserves stream 0, clears the 32-entry metadata array, writes `"System"`, and
initializes a userspace mutex. The active and complete builders independently
prove that the packet carries the address of `table_base + stream_id * 8`, not
the value stored in that slot. OpenAGC already matches those facts; see
`agc_driver_workload_init_1160.md`.

Stage 13 restored the normal sequence used by the FW 5.50-qualified path after
a clean reboot. Register defaults, async setup, process property, stream
registration, and an ordinary preflight marker all passed; the marker completed
in 50 ms. The unchanged inline workload submit returned `AGC_OK` and then
stalled without reaching either marker. This rules out those surrounding
prerequisites and must not be rerun unchanged.

The first opt-in installed-driver oracle documented in
`fw1160_sony_workload_oracle.md` was run once after a clean reboot. The matching
module loaded, its exact workload exports and sizes resolved, and async setup
returned `AGC_OK`. Its ordinary `WRITE_DATA` preflight also returned `AGC_OK`,
but the marker remained zero after 5,000 ms. The safety gate prevented any
workload packet from being emitted. That single-DCB preflight omitted the
hardware-proven NOP trailer needed to advance the final graphics descriptor in
this payload context, so the result is inconclusive. The revised oracle uses
Sony's multi-DCB export with two observable DCBs plus a 16-dword NOP trailer and
flushes the full 40-dword workload buffer. Do not retry the original artifact;
see `fw1160_sony_workload_attempt_20260729.md`.

The revised oracle was then run after another clean reboot. Sony's multi-DCB
export returned `AGC_OK`, but neither observable preflight marker executed
after 5,000 ms. The NOP trailer and complete-range flush rule out the original
framing defects; the workload gate again prevented any `SET_WORKLOAD` packet.
The installed module cannot serve as a GPU-execution oracle under websrv. Do
not repeat either installed-driver artifact. Continue recovery against the
working direct `/dev/gc` submission path.

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
cursor wrappers use one registered private stream and the caller's workload
set without narrowing the recovered packet contract. They append into the
caller's DCB/ACB exactly as Sony does. The failed one-ID adapter instead
submitted active and complete as separate driver-owned DCBs; that distinct
lifecycle remains disabled.

KytyPS5 reserves 18 and 12 dwords but emits `IT_NOP` emulator metadata.
SharpEmu has no workload builders. Neither is hardware-packet evidence.
