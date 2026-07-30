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
  label, and value. One command may reserve each range; resetting that command
  clears only its reservations.
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

The generic regression creates two pending buffer ranges and two pending image
subresource ranges simultaneously. It verifies exact and ambiguous diagnostic
queries, disjoint usability, overlap rejection, acquire reservation reset,
exact range publication, dependency-label retention, and transactional batch
overlap rejection. The complete generic executable passes 16,851 assertions.

This is host-tested and Prospero-compiled behavior, not new hardware evidence.
Exact FW 5.50 and FW 11.60 execution must use cleanup-first guarded artifacts
and be recorded separately before the partial-range protocol is described as
hardware-qualified.

Two reproducible cleanup-first FW 5.50 probes are pinned for that gate:

- partial buffer handoff:
  `02894e64e638c5632d4833c0790a7ed68a51a1d8bcd9d4ffb50bd79995acf371`
- partial image mip/layer handoff:
  `238dfc0da0062587c63344293260e9880c3783067d6cdd365149584e7c282b55`

Each artifact was rebuilt twice with identical SHA-256 output. Their guarded
targets require FW ABI `0x0550`, exact PASS verdicts, cleanup-first launch, and
post-run service recovery.

## FW 5.50 attempt

The first gate attempt stopped before upload: the configured console at
`10.0.1.41` accepted neither websrv TCP port 8080 nor FTP port 2121. No cleanup
or test ELF was launched, so this records no GPU result and changes no hardware
qualification label. Retry only after etaHEN/websrv exposes both services.
