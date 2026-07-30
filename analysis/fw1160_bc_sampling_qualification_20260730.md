# FW 11.60 BC sampling qualification — 2026-07-30

## BC1 UNORM first attempt

- Artifact SHA-256:
  `22834911ea4c75eb0dfdf445cc38c7d5a79dd4ec273a574d5dc1ab6a462e5a62`
- The cleanup-first runner uploaded and re-read the exact pinned bytes.
- No fresh file-backed `Graphics result` appeared during the bounded 30-second
  poll, so the runner failed closed.
- Ports 8080, 2121, and 744 remained reachable, and ps5debug-NG found no
  residual `eboot.bin`.

This is inconclusive rather than a BC1 sampling failure and matches the known
websrv loader-stale symptom previously isolated with RG32_UINT. Do not rerun
another graphics payload on this boot. Reboot FW 11.60, reinject ps5debug-NG,
then retry these identical BC1 UNORM bytes before advancing to BC1 SRGB.
