# AGC driver GPU-info process-property facts

`agc_driver_process_property_facts.tsv` records the exact GPU-info
`sceKernelSetProcessProperty` carrier in every active Sony driver. Reproduce it
from the external firmware corpus with:

```sh
python3 tools/extract_agc_driver_process_property_facts.py /Volumes/Untitled/unp \
    --output analysis/agc_driver_process_property_facts.tsv
tools/verify_agc_driver_process_property_facts.sh /Volumes/Untitled/unp
```

All 39 FW 3.20 through FW 12.70 drivers import NID `-W4xI5aVI8w` and contain
two call sites with the same five-argument contract:

```c
sceKernelSetProcessProperty(
    "Sce.Debug:Gnm", gpu_info_base, gpu_info_span, 0, 0);
```

The standard-console span is `0x100000`. FW 9.00 and later derive
`0x180000` when the exact `sceKernelHasTrinityMode` predicate is true. Both
trailing arguments are zero. Sony ignores the return value, then names the
same range `"SceGnmDumpArea"` with `sceKernelSetVirtualRangeName`.

This corrects the earlier four-argument interpretation. The failed FW 11.60
OpenAGC experiment passed `(0, span, base, 0)`, putting zero in the required
name pointer and shifting every following argument; the console kernel-panicked.
That call was removed immediately. Never use the four-argument form on any
firmware, including FW 5.50.

The exact five-argument carrier is RE evidence, not direct-backend hardware
qualification. OpenAGC keeps FW 11.60 workloads fail-closed until a guarded
property-only probe proves the corrected call before any workload packet is
submitted.
