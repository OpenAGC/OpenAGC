# AGC driver suspend submission/query facts

`agc_driver_suspend_facts.tsv` maps the private suspend commands and carrier
groups to every active firmware key:

```sh
python3 tools/build_agc_driver_suspend_facts.py \
  --output analysis/agc_driver_suspend_facts.tsv
```

Primary submit is one identical carrier across all 39 images. It stores four
`uint32_t` arguments at offsets `0`, `4`, `8`, and `0xc`, then issues
`0xc010811c`. `AgcGcSuspendArg` has exact size/offset static assertions. FW
5.50 hardware-qualified this path; other keys are RE-exact and hardware
pending.

Final submit uses `0xc0108139`, but later images contain two related carrier
variants with different input/output roles. FW 5.50 remains hardware-qualified
and FW 11.60 retains its exact verified profile; other final-submit profiles
stay disabled until their wrapper role is tied to OpenAGC's four-input helper.

Both named Direct exports are the same 39-byte permission stub across all 39
images and return `0x8A6D0001`. The separate `0x80048127` internal carrier is
present, but the permission wrappers do not expose its result semantics.
Suspend query therefore remains disabled on every direct profile.
