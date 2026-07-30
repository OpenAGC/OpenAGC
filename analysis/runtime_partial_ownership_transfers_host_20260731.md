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

This behavior is host-tested and Prospero-compiled. The exact FW 5.50 result
below additionally qualifies the two-disjoint-range carriers; FW 11.60 remains
unqualified for this optional extension.

Two reproducible cleanup-first FW 5.50 probes are pinned for that gate:

- partial buffer handoff:
  `2b78f787d8ab15ee972382102d566afaab2e06742b27953936aea3530e33ba80`
- partial image mip/layer handoff:
  `249125fd245037c720f409c9830a105a872310498217f15835536403189a8e2c`

Runtime API v21 strengthens each probe to release and acquire two disjoint
ranges through one increasing label. The repinned guarded targets require FW
ABI `0x0550`, exact PASS verdicts, cleanup-first launch, and post-run service
recovery.

## FW 5.50 qualification

The first gate attempt stopped before upload: the configured console at
`10.0.1.41` accepted neither websrv TCP port 8080 nor FTP port 2121. No cleanup
or test ELF was launched.

After service recovery, the first buffer artifact reached both release records
but its 32-dword command capacity could not fit two acquire waits plus the
runtime-owned EOP fence. Submission returned
`AGC_ERROR_COMMAND_SPACE_EXHAUSTED` before driver mutation. The console and
websrv stayed healthy. The partial variants now allocate 64 dwords; both
corrected artifacts reproduced across two builds.

The corrected buffer and image targets then passed on exact FW 5.50. Each
submitted two disjoint compute releases at label points 1 and 2 followed by
two graphics acquires without a CPU wait. Both bounded fences completed and
all labels, resources, command buffers, queues, fences, and devices destroyed
with `AGC_OK`.
