# FW 11.60 BC sampling qualification — 2026-07-30

## BC1 UNORM first attempt

- Artifact SHA-256:
  `22834911ea4c75eb0dfdf445cc38c7d5a79dd4ec273a574d5dc1ab6a462e5a62`
- The cleanup-first runner uploaded and re-read the exact pinned bytes.
- No fresh file-backed `Graphics result` appeared during the bounded 30-second
  poll, so the runner failed closed.
- Ports 8080, 2121, and 744 remained reachable, and ps5debug-NG found no
  residual `eboot.bin`.

This first attempt was inconclusive rather than a BC1 sampling failure and
matched the known websrv loader-stale symptom previously isolated with
RG32_UINT.

## BC1 UNORM clean-reboot diagnosis

After reboot and ps5debug-NG reinjection, the same old artifact executed to a
bounded fatal verdict:

- Runtime profile `0x1160`, submission, completion fence, marker, driver
  shutdown, memory cleanup, and self-termination all succeeded.
- All three mip/layer regions executed, with `224640` changed pixels.
- The GPU returned the expected eight-color family, including intermediate
  endpoint blends `171/84`.
- The oracle reported `121659` exact mismatches because it computed ideal
  integer thirds `170/85`.

The failure is in the CPU oracle, not BC1 sampling. GFX10.3 follows the
standard six-bit BC interpolation weights `43/21`. The corrected oracle applies
those weights to BC1, BC2, and BC3 and maps the resulting `84/171` values in
the SRGB expectation. The runner now treats `FATAL` plus completed memory
cleanup as a ready verdict, avoiding the misleading timeout report.

Corrected firmware-neutral artifacts were preserved before execution:

- BC1 UNORM:
  `b8ec09a3e15b33b20897e0526e93c6a0828931e358563cf3539d5b964e3a991c`
- BC1 SRGB:
  `acf8b12e84ebf7fc07ffd502ff97b399583f4bdeddca0c477e903342504f0ff2`
- BC2 UNORM:
  `0aa0a688aa337624847f054fc4d9e5a31e0eae67bf391a83c789a88aa885cfcf`
- BC2 SRGB:
  `2abdedf25437b2e8907aafef78ec7c45857abf390735ce790c34ef843762cf90`
- BC3 UNORM:
  `f6b99e4db1e3a7342db0132a8466362aab8deb41365b7460439b7a145fbc69bf`
- BC3 SRGB:
  `c5019c536c32877595c10cc6eadab05774d6b41d671b672dd7edc6fc0ac3cc30`

Retry corrected BC1 UNORM twice before advancing.
