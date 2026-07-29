# AGC driver internal-memory facts

`agc_driver_memory_facts.tsv` records the eight named internal-region sizes,
CWSR working offset, allocation-carrier fingerprint, and exact Trinity split
for every active firmware key. Generate it with:

```sh
python3 tools/extract_agc_driver_memory_facts.py /Volumes/Untitled/unp \
  --output analysis/agc_driver_memory_facts.tsv
```

Each row is accepted only when one allocation carrier contains the complete
standard constant set (`0x4000`, `0x3c000`, `0xfc000`, `0x1e0000`,
`0xa00000`, and `0x1000000`). FW 9.00+ rows additionally require both the
`0x1600000` Trinity CWSR branch and the `sceKernelHasTrinityMode` import NID
`yu17wG8L5FI`; a constant/import disagreement aborts generation.

The standard profile is common to all 39 active images. Trinity first appears
in the inspected FW 9.00 group and selects:

| Fact | Standard | Trinity |
|---|---:|---:|
| GPU-info span | `0x100000` | `0x180000` |
| CWSR allocation | `0x1000000` | `0x1600000` |
| CWSR working offset | `0xa00000` | `0x1000000` |

Only FW 5.50 standard-console execution is hardware-qualified. Every other
row is exact-RE-qualified and matching-hardware pending.
