# AGC driver direct-submission facts

`agc_driver_submission_facts.tsv` ties the DCB, ACB, and multi-DCB exported
wrappers from every active firmware to their private submit carrier. Reproduce
it with:

```sh
python3 tools/extract_agc_driver_submission_facts.py /Volumes/Untitled/unp \
    --output analysis/agc_driver_submission_facts.tsv
tools/verify_agc_driver_submission_facts.sh /Volumes/Untitled/unp
```

All 39 images use one instruction-identical `0xc0108102` carrier. Its complete
16-byte request is a 32-bit queue/type at offset 0, a 32-bit descriptor count
at offset 4, and a 64-bit descriptor-array pointer at offset 8. The public DCB
and multi-DCB wrappers each form one normalized group across every active
firmware. ACB wrappers form three groups as their queue-selection logic evolves,
but all converge on the same submit carrier.

`AgcGcSubmitArgs` and `AgcGcCommandBuffer` lock the request and descriptor
layouts with size and field-offset `_Static_assert`s. FW 5.50 is hardware-
qualified; the other exact profiles remain hardware-pending.
