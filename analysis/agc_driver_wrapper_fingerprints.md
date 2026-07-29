# AGC driver wrapper fingerprint groups

`agc_driver_wrapper_fingerprints.tsv` records normalized exported-wrapper
fingerprints for every active firmware key (`0x0320` through `0x1270`) in the
local SPRX corpus. It is generated with:

```sh
python3 tools/fingerprint_agc_driver_wrappers.py /Volumes/Untitled/unp \
  --aerolib /Users/bizkut/Downloads/PS5/sdk/sce_stubs/aerolib.csv \
  --output analysis/agc_driver_wrapper_fingerprints.tsv
```

Regenerate the table into a temporary file, compare it byte-for-byte, require
all 39 active firmware keys, and reject missing tracked exports with:

```sh
tools/verify_agc_driver_wrapper_fingerprints.sh /Volumes/Untitled/unp
```

The normalizer retains opcodes, register operands, structure offsets, and
immediate constants. It removes function-placement-dependent control-flow
targets and RIP-relative data addresses before hashing. A matching fingerprint
therefore means the exported wrapper instruction contract is identical after
relocation normalization; it does not by itself prove the called internal
routine or kernel handler. Capability promotion still requires the command,
payload layout, internal path, and memory/default-state facts.

## Results that affect profiles

- `sceAgcDriverSubmitDcb` and `sceAgcDriverSubmitMultiDcbs` each form one group
  across all 39 active profiles. This supports the existing submit16 baseline.
- `sceAgcDriverSubmitAcb` has three groups: `0x0320`-`0x0403`,
  `0x0450`-`0x1060`, and `0x1100`-`0x1270`. A single family-wide installed ACB
  assumption is therefore invalid.
- Workload-active wrappers split into seven groups and workload-complete into
  three. FW 11.60 is not compatible with the FW 5.50 convenience workload
  implementation and remains disabled.
- Public TF-ring wrappers split into four groups, while the direct export is
  the same permission stub on all 39 profiles. Direct-export presence must not
  be advertised as TF-ring capability.
- Public HS-offchip wrappers split into four groups, while the direct export is
  likewise the common permission stub.
- Both direct suspend exports share the same 39-byte permission-stub
  fingerprint on all profiles. The direct `/dev/gc` primary/final suspend
  capability instead depends on the separately verified internal ioctl path.
- Default-state wrappers split into six groups and async-graphics wrappers into
  seven. No firmware inherits FW 5.50 defaults version 8 merely from belonging
  to the standard family.

FW 5.50 remains hardware-qualified. FW 11.60 is statically qualified and
hardware pending for only the operations listed in `agc_driver_abi_1160.md`.
All other newly selected profiles remain submit-only until their internal
wrapper facts are promoted group by group.
