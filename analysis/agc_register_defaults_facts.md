# AGC register-default selection facts

`agc_register_defaults_facts.tsv` records the versioned and no-argument forms
of both primary and internal register-default exports from the `libSceAgc.sprx`
paired with every active driver profile. Reproduce it
from the firmware corpus with:

```sh
python3 tools/extract_agc_register_defaults_facts.py /Volumes/Untitled/unp \
    --output analysis/agc_register_defaults_facts.tsv
tools/verify_agc_register_defaults_facts.sh /Volumes/Untitled/unp
```

The versioned `sceAgcGetRegisterDefaults2` dispatcher supports different
version ranges across firmware generations: 0..7 on FW 3.20, 0..8 on FW
4.x, 0..9 on FW 5.02 through FW 8.60, and 0..12 from FW 9.00 onward. These
upper bounds describe accepted caller inputs.

All 39 no-argument primary wrappers and all 39 no-argument internal wrappers
are instruction-for-instruction identical after relocation normalization.
Each indexes an
80-byte runtime record and loads a selector from offset `0x44` before
tail-calling the versioned implementation. The earlier analysis incorrectly
described this as a hidden hardware selector. In every active SPRX, the current
one-argument `sceAgcInit(version)` wrapper forwards its EDI argument and the
common initializer stores that value at record offset `0x44`.

There is therefore no firmware- or hardware-selected version to recover. The
exact per-firmware fact is the accepted caller range: V0..V7 on FW 3.20,
V0..V8 on FW 4.x, V0..V9 on FW 5.02 through FW 8.60, and V0..V12 from FW 9.00
onward. OpenAGC now preserves the caller's `sceAgcInit(version)` choice and
rejects values above the exact profile bound instead of treating that bound as
the selected version. FW 5.50 has hardware-qualified V8 operation within its
SPRX-proven V0..V9 range; FW 11.60 has hardware-qualified V12 operation within
its V0..V12 range. Other caller/profile combinations remain hardware-unverified.

The direct blob builder has an explicit layout for every accepted version 0
through 12, including the larger V0-V6 and V11 tables. Host tests prove each
version's computed primary and internal sizes fit its DDID subregions. Calling
`sce_agc_initialize()` alone no longer silently chooses a defaults version;
default-state notification fails closed until `sceAgcInit(version)` records the
caller choice.

The public no-argument `sceAgcGetRegisterDefaults` and
`sceAgcGetRegisterDefaultsInternal` exports now return the structures for that
recorded version. This replaces an older incorrect output-buffer signature
whose implementation always flattened V8 state.

The committed fingerprints group identical dispatchers into six generations
and the runtime-record wrapper into one common group. Firmware binaries remain
external RE inputs and are never copied into the repository.
