# VideoOut linear-registration patch profiles (2026-07-30)

OpenAGC's application-neutral VideoOut path temporarily bypasses the system
debug-setting check while registering caller-owned linear scanout buffers, then
restores the original instruction immediately. The previous implementation
hardcoded the FW 5.50 instruction and was unsafe on FW 11.60.

## Firmware evidence

The local decrypted SPRX files are reference inputs only and are not committed:

| FW ABI key | SPRX SHA-256 | registration validator | patch branch | original bytes |
|---|---|---:|---:|---|
| `0x0550` | `d77c0063198fd5ad74e3733852fa7a5c27c57f560b39cee110b9d1651ee6afaa` | `0x79e0` | `0x7e61` | `0f 84 15 02 00 00` |
| `0x1160` | `7d8628957d087962f851d779a0a58ee7149691bcbe00f3884cf1b0a67dd6f7a3` | `0x93f0` | `0x9922` | `0f 84 3c 02 00 00` |

Both bodies are reached through the `sceVideoOutRegisterBuffers` export
(`w3BY+tAEiQY`). FW 5.50 checks the port fields at `+0x758` and `+0x700`
before the branch. FW 11.60's evolved body checks `+0x774` and `+0x704`.
Taking either branch reaches the matching linear-registration rejection that
returns `0x80290007`.

## Implementation boundary

`agcVideoOutFindLinearPatch()` reduces the console's full raw version, such as
`0x11600005`, to the four-digit ABI key `0x1160`. It returns only an exact
evidenced profile. The Prospero VideoOut backend verifies all six original
instruction bytes before changing memory, restores the same six bytes after
registration, and returns `AGC_ERROR_NOT_SUPPORTED` for an unknown key or a
signature mismatch. Host tests lock both offsets, signatures, normalization,
and rejection of an unevidenced firmware key.

Hardware qualification on FW 11.60 remains required before presentation is
advertised as parity-complete.
