# FW 11.60 R16_UINT first attempt — 2026-07-30

## Retired artifact

The first immutable artifact was SHA-256
`8136a9a22005e1f9087b9d24402bac47ee66463a13c351500652276e34fa34b4`.
It was launched once through the cleanup-first guarded FW 11.60 target and
must not be rerun.

## Result

The console selected standard profile `0x1160`, initialized normally, submitted
the 2,461-dword DCB, completed its fence at 0 us, and executed the post-draw
marker. Coverage was healthy: 255,744 pixels in exact `768x665` bounds. The
native R16_UINT values were only `0x0000..0x00ff`, however, with 254,635 exact
oracle mismatches. The gate failed before writing its final verdict, so the
runner timed out even though the partial file log contained the diagnosis.
Ports 8080, 744, and 3232 remained reachable afterward.

## Root cause and correction

The new psbc export selector correctly emitted UINT16_ABGR, but an older
pipeline default still set Mesa/ACO's `color_is_int8` mask for every color
attachment. ACO consequently clamped each unsigned export to 255 before
packing it. Sibling psbc commit `c624c5c` clears that mask for explicit
UINT16_ABGR and SINT16_ABGR exports. Its full test suite passes.

The shader and ELF were rebuilt after that compiler correction. The new,
unexecuted artifact is SHA-256
`aabefd4d05f8d7ea7f56f917ae79c23f60eccf801627f06a053451e74ae8bf18`.
Only the new hash may be used for the next cleanup-first attempt.
