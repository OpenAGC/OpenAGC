# Exact-range ownership transfers — host qualification (2026-07-31)

Runtime API v20 generalizes the version-2 graphics/compute ownership protocol
from one whole-resource handoff to multiple disjoint pending ranges per
resource. Buffer transfers carry an exact byte offset and size. Image
transfers carry an exact aspect, mip-level, and array-layer range.

## Contract

- A release preserves committed source state and publishes a pending
  destination state plus its exact label point only after successful submit.
- Pending ranges on one resource cannot overlap. Disjoint ranges remain
  available to ordinary transitions and additional releases.
- An acquire must exactly match one committed range, destination usage/owner,
  label, and value. The label may already have advanced beyond that point. One
  command may reserve each range; resetting that command clears only its
  reservations.
- Acquire submission commits only the selected range and removes only its
  pending record. Pending records retain dependency labels independently of
  the submitted release command's lifetime.
- A uniform query range reports one pending transfer. Mixed pending/nonpending
  coverage or transfers with different metadata returns
  `AGC_ERROR_NOT_SUPPORTED`.
- Submission revalidates committed overlap and exact acquire identity. Two
  independently recorded overlapping releases in one batch reject before the
  driver is called or resource state is changed.
- Destruction, deferred destruction, and whole-image presentation remain busy
  while any pending range exists. HTILE ownership transitions remain outside
  the qualified contract.

The cache packet sequence remains the existing globally qualified gfx1013
release/wait/acquire sequence. Ranges define ownership and state validation;
they do not claim range-scoped hardware cache operations.

## Host evidence

The generic regression creates two pending buffer ranges on one label and two
pending image subresource ranges on another shared label simultaneously. It
verifies exact and ambiguous diagnostic
queries, disjoint usability, overlap rejection, acquire reservation reset,
exact range publication, dependency-label retention, and transactional batch
overlap rejection. The complete generic executable passes 16,852 assertions.

This is host-tested and Prospero-compiled behavior, not new hardware evidence.
Exact FW 5.50 and FW 11.60 execution must use cleanup-first guarded artifacts
and be recorded separately before the partial-range protocol is described as
hardware-qualified.

Two reproducible cleanup-first FW 5.50 probes are pinned for that gate:

- partial buffer handoff:
  `f3d0c76eec898c63d22a9e5a1ddb86728e141f77709cd9ce8984eeb8071fbb0e`
- partial image mip/layer handoff:
  `6b3a56ac8d382b287ecd636be85ac59adc4585955530a9e3bfbb166debf285f4`

Runtime API v21 strengthens each probe to release and acquire two disjoint
ranges through one increasing label. The repinned guarded targets require FW
ABI `0x0550`, exact PASS verdicts, cleanup-first launch, and post-run service
recovery.

## FW 5.50 attempt

The first gate attempt stopped before upload: the configured console at
`10.0.1.41` accepted neither websrv TCP port 8080 nor FTP port 2121. No cleanup
or test ELF was launched, so this records no GPU result and changes no hardware
qualification label. Retry only after etaHEN/websrv exposes both services.
