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

KytyPS5 reserves 18 and 12 dwords but emits `IT_NOP` emulator metadata.
SharpEmu has no workload builders. Neither is hardware-packet evidence.
