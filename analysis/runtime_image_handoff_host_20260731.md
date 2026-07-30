# Runtime whole-image ownership handoff — host qualification

Date: 2026-07-31

The generic runtime fixture now covers a complete whole-image queue ownership
handoff from graphics `color-target` use to compute `shader-read` use.

The test creates one linear RGBA8 image with color-target and sampled usage,
publishes graphics ownership, and submits a version-2 release transition. It
then verifies:

- the graphics DCB begins with the qualified `RELEASE_MEM` EOP signal;
- a pending handoff prevents image destruction;
- the compute ACB begins with the exact-value `WAIT_REG_MEM` and follows it
  with `ACQUIRE_MEM` cache invalidation;
- destination ownership is published only after acquire submission; and
- the acquired image can transition to host read and be destroyed normally.

Verification:

```text
openagc_tests: 14959 passed, 0 failed
```

This is host qualification only. It does not add a new PS5 workload artifact
or extend the exact-firmware hardware-qualified matrix.
