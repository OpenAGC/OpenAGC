# AGC driver private-command carrier groups

`agc_driver_command_carriers.tsv` fingerprints the complete internal function
surrounding each known private ioctl command in all 39 active driver SPRXs.
Generate it with:

```sh
python3 tools/fingerprint_agc_driver_command_carriers.py \
  /Volumes/Untitled/unp \
  --output analysis/agc_driver_command_carriers.tsv
```

Unlike an exported-wrapper fingerprint, a carrier fingerprint includes the
payload construction, validation, command immediate, and ioctl call in the
same internal function. Relocation-dependent branch targets and RIP-relative
addresses are normalized; constants, stack offsets, argument registers, and
stores remain part of the digest.

This is stronger grouping evidence, but it remains fail-closed:

- A command found in an image does not prove that the public API reaches it.
- An identical carrier does not prove that driver-owned global objects have
  identical initialization or memory placement.
- Hardware qualification remains specific to the tested firmware/model.
- A capability is promoted only after the carrier group is tied to the named
  wrapper and its typed argument layout.

## Current groups

- Submit16, primary suspend, and privileged TF setup each have one identical
  carrier across all 39 active firmware images.
- Async setup and HS-offchip each have two carrier groups. In both cases the
  differences are bounded: async separates pre-9.00 and 9.00+ ioctl-call
  scaffolding; HS-offchip 12.x explicitly zeroes the reserved fourth dword.
- Public TF setup has three groups: 3.20-10.60, 11.00-11.60, and 12.x. All
  store the same address/size payload and issue `0x80108128`; 12.x explicitly
  initializes the reserved fourth dword.
- Final suspend has three carriers because later images contain two related
  wrappers around `0xc0108139`. This remains a distinct fact from the single
  four-input primary-suspend carrier.
- Queue create has 14 carrier groups and queue destroy has six. Queue support
  therefore still requires group-specific token, address-offset, and public
  reachability recovery; command presence alone does not promote it.
