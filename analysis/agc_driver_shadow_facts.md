# AGC driver register-shadow constructor facts

`tools/extract_agc_driver_shadow_facts.py` inspects every active
`libSceAgcDriver.sprx` named by `analysis/agc_firmware_versions.tsv`. The
generated evidence table is `analysis/agc_driver_shadow_facts.tsv`; regenerate
and compare it with:

```sh
sh tools/verify_agc_driver_shadow_facts.sh /Volumes/Untitled/unp
```

The firmware images are external reverse-engineering inputs and are not part of
this repository.

## Corpus result

All 39 active images from ABI key `0x0320` through `0x1270` use the same
standard-console direct-memory and descriptor contract:

- virtual-address hint `0xfe0000000`;
- allocation size and alignment `0x200000`;
- direct-memory type `0x0c` and protection `0x33`;
- two `0x19000`-byte shadow ranges at offsets `0x8000` and `0x21000`;
- descriptor words `{0, 0x3bf, 0x2000, 0x2281, 0x2400, 0x2843}`.

The process-property generations divide at an evidenced boundary:

| Exact active keys | Standard console | Trinity branch |
| --- | --- | --- |
| `0x0320`–`0x0550` | Gn2 + Gn3 | absent from inspected carrier |
| `0x0600`–`0x0860` | Gn2 + Gn3 + Gn4 | absent from inspected carrier |
| `0x0900`–`0x1270` | Gn2 + Gn3 + Gn4 | reduced Gn2-only branch |

Gn4's `SceAgcRegShadowCopy` and `SceAgcRegShadowInfo` range names appear at
the same FW 6.00 boundary. The Trinity predicate NID appears at FW 9.00 and
remains present through FW 12.70.

The extractor maps sectionless ELF virtual addresses through `PT_LOAD`
segments, fingerprints the allocation, descriptor-constructor, and property
call carriers, verifies the allocation immediates and exact descriptor bytes,
and checks that all present Gn property calls share one PLT target. A mismatch
or missing invariant fails regeneration instead of silently classifying the
image.

## Scope of the evidence

This proves constructor ABI state for the exact active firmware keys. It does
not prove that replaying the state is required for direct `/dev/gc` workloads,
nor does it hardware-qualify workload, graphics, or compute behavior on an
untested firmware. FW 5.50 remains workload-qualified without OpenAGC replaying
this constructor state. FW 11.60 stage 15 is the first isolated hardware test
of the standard Gn2/Gn3/Gn4 replay path.

Unknown firmware keys still fail closed. The runtime profile records only
which property generations exist for an already accepted exact key; it does
not infer support from a numeric version range.
