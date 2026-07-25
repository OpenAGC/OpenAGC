# Legacy submit16 and Trinity runtime profiles

## Legacy submission

Representative FW 1.00, 2.50, and 3.20 `libSceAgcDriver.sprx` disassembly
shows the same `0xc0108102` wrapper used by OpenAGC's FW 5.50 hardware-tested
path. The 16-byte argument is:

| Offset | Field |
|---:|---|
| `0x00` | queue type (`uint32_t`) |
| `0x04` | command-buffer count (`uint32_t`) |
| `0x08` | command-buffer descriptor pointer (`uint64_t`) |

The wrapper returns success when the ioctl returns zero. All inspected early
families retain context query, queue create/destroy, async setup, HS-offchip,
and the standard internal allocation sizes.

Early optional behavior differs:

- FW 1.00 uses EOP offset `0x38000`, predates the later four queue
  authentication constants, and has no TF-ring request. Its core submit path
  is enabled, while the unrecovered special-queue helper and TF ring return
  `AGC_ERROR_NOT_SUPPORTED`.
- FW 2.00/2.50 uses EOP offset `0x39000` and the later authentication constants,
  but still has no TF-ring request.
- FW 3.00/3.20 adds the TF-ring request and otherwise matches the authenticated
  submit16 profile.

## Trinity / PS5 Pro

The import at PLT entry `0xaf40` in FW 11.60 maps through relocation symbol
index 210 to NID `yu17wG8L5FI`, identified as
`sceKernelHasTrinityMode`. The firmware calls it without arguments and branches
on a nonzero return value.

| Profile value | Standard PS5 | Trinity / PS5 Pro |
|---|---:|---:|
| GPU-info span | `0x100000` | `0x180000` |
| CWSR working offset | `0xa00000` | `0x1000000` |
| CWSR allocation | `0x1000000` | `0x1600000` |

The import first appears in the inspected FW 9.00 family. OpenAGC resolves it
through `sceKernelDlsym` for FW 9+ and fails closed if resolution fails. This
avoids inferring the console model from firmware version.

## Validation status

- ABI-specific SPRX verifier passes FW 1.00, 2.50, 3.20, and 11.60.
- Host profile tests cover exact aliases, optional request guards, standard
  sizing, Trinity sizing, and unknown-version rejection.
- Generic and Prospero builds pass.
- FW 5.50 standard PS5 remains the only hardware-tested firmware/model pair.
