# AGC driver authenticated special-queue facts

`agc_driver_queue_facts.tsv` records the complete special-queue setup tuple
for all 39 active firmware keys. Generation requires one wrapper carrier that
contains all four authentication values, the EOP ring offset, and both ACQRB
offsets in the same function:

```sh
python3 tools/extract_agc_driver_queue_facts.py /Volumes/Untitled/unp \
  --output analysis/agc_driver_queue_facts.tsv
```

The tuple is stable across the active corpus:

| Fact | Value |
|---|---:|
| Create ioctl | `0xc0408121` (`0x40`-byte payload) |
| Destroy ioctl | `0xc00c810e` (`0x0c`-byte payload) |
| Authentication | `0xaf1e80b7`, `0x8b4cdd90`, `0x99f68d6c`, `0xe5fcc174` |
| EOP ring offset | `0x39000` |
| ACQRB read pointer | `0x1c8000` |
| ACQRB metadata | `0x1cc000` |
| Pipe ID | `0x0c` |
| Ring size | `0x1000` |

The setup wrappers form several normalized groups as surrounding logging and
state management changes, while the complete tuple remains invariant. Typed
`AgcGcQueueCreateArg` and `AgcGcQueueDestroyArg` assertions lock the kernel
payload offsets. FW 5.50 is hardware-qualified; other keys are exact-RE-
qualified and matching-hardware pending.
