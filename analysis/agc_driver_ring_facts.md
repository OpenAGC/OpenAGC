# AGC driver TF-ring and HS-offchip facts

`agc_driver_ring_facts.tsv` records the semantic payload layouts behind the
public TF-ring and HS-offchip carriers for every active firmware. Reproduce it
with:

```sh
python3 tools/extract_agc_driver_ring_facts.py /Volumes/Untitled/unp \
    --output analysis/agc_driver_ring_facts.tsv
tools/verify_agc_driver_ring_facts.sh /Volumes/Untitled/unp
```

All 39 public TF-ring carriers issue `0x80108128` with a 16-byte payload
containing a 64-bit address at offset 0 and a 32-bit size at offset 8. They
require 256-byte address alignment and four-byte size alignment. All public
HS-offchip carriers issue `0xc010812c` with a 64-bit list pointer at offset 0
and a 32-bit entry count at offset 8.

FW 12.00 and later wrappers explicitly zero the reserved dword at offset
`0xc`; earlier wrappers leave it unspecified. OpenAGC zero-initializes both
typed structures, which satisfies the later contract without changing the
earlier fields. The normalized implementation groups are three TF carriers
and two HS carriers.

The similarly named direct exports are not carriers: every active image has
the same permission stub returning `0x8a6d0001`. OpenAGC enables only the
recovered public internal paths and never treats those direct export names as
ioctl evidence. FW 5.50 is hardware-qualified; every other exact profile is
still hardware-pending.
