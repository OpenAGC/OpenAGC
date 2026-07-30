# GPU reached-or-passed timeline waits — host qualification (2026-07-31)

Runtime API v21 aligns GPU-side waits with the monotonic host-label contract.
`agcCmdWaitGpuLabel`, version-2 submit wait lists, and ownership acquires now
encode gfx1013 `WAIT_REG_MEM` compare function 5 (greater-or-equal). A requested
point is valid when that point or any later point has already submitted.

Signals remain strictly increasing. Repeated, decreasing, unscheduled future,
and post-`UINT32_MAX` points still reject before packet or driver mutation.
Ownership acquires still match the exact pending range, destination state,
label, and release value; only the label word's permitted current value changes
from equality to reached-or-passed.

## Host evidence

The generic suite verifies all three public paths:

- a graphics command waits for point 7 after its compute producer submitted
  points 7 and 8;
- a submit-list waits for point 1 after its producer submitted points 1 and 2;
- two disjoint buffer ranges share label points 1 and 2, as do two disjoint
  image subresource ranges, and every exact acquire submits successfully after
  each shared label has advanced to 2.

Packet checks lock compare function 5 for command, submit-list, and ownership
waits. The complete generic executable passes 16,852 assertions.

The partial buffer and image hardware probes now exercise two disjoint ranges
with one shared label, and the timeline-wait probe advances the label to point
2 before waiting for and acquiring point 1. These are Prospero build artifacts,
not hardware evidence until their cleanup-first guarded targets complete on an
exact endpoint.

The three directly relevant artifacts reproduced across two consecutive
builds and are pinned as:

- timeline wait:
  `c30a07d6b8c55b495df9736476376e2e7d8869e17e90f1f7e98a60f85be6976f`
- partial buffer handoff:
  `7b55c694eb8fabcfaf17a7141725a0ca9814ade8afab56213a32f5d8ea07c501`
- partial image handoff:
  `07b17b74865d8c810655196ff6d02c6569b8561c4087f79033d765b16c745f1a`

The API v21 FW 5.50 preflight stopped before upload because `10.0.1.41`
accepted neither websrv port 8080 nor FTP port 2121. No payload ran and no
hardware qualification claim changed.
