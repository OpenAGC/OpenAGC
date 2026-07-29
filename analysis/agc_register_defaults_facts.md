# AGC register-default selection facts

`agc_register_defaults_facts.tsv` records both register-default exports from
the `libSceAgc.sprx` paired with every active driver profile. Reproduce it
from the firmware corpus with:

```sh
python3 tools/extract_agc_register_defaults_facts.py /Volumes/Untitled/unp \
    --output analysis/agc_register_defaults_facts.tsv
tools/verify_agc_register_defaults_facts.sh /Volumes/Untitled/unp
```

The versioned `sceAgcGetRegisterDefaults2` dispatcher supports different
version ranges across firmware generations: 0..7 on FW 3.20, 0..8 on FW
4.x, 0..9 on FW 5.02 through FW 8.60, and 0..12 from FW 9.00 onward. These
upper bounds describe accepted dispatcher inputs; they do not identify the
version selected for a particular GPU.

All 39 no-argument `sceAgcGetRegisterDefaults` wrappers are instruction-for-
instruction identical after relocation normalization. Each indexes an
80-byte runtime hardware record and loads a selector from offset `0x44`
before tail-calling the versioned implementation. Consequently, firmware
version alone cannot safely choose a defaults version. FW 5.50 version 8 is
the only selected version in this ledger because that exact pairing passed
the hardware sample. Every other direct profile remains fail-closed for
default-state notification until its runtime selector is observed or an
equivalent hardware-qualified table fact is recovered.

The committed fingerprints group identical dispatchers into six generations
and the runtime selector into one common group. Firmware binaries remain
external RE inputs and are never copied into the repository.
