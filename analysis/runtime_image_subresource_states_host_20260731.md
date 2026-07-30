# Runtime image subresource states — host qualification (2026-07-31)

Runtime API v18 makes `AgcImageSubresourceRange` an effective state boundary
instead of accepting complete-image ranges only. Transitions may select any
valid subset of the image's color, depth, or stencil aspects and any bounded
mip/layer rectangle. Partial HTILE transitions and partial cross-queue
release/acquire remain unsupported.

Committed images retain the existing uniform usage/owner pair until the first
partial transition needs to publish. The runtime then allocates a compact
two-byte state cell per aspect/mip/layer, commits only after successful queue
submission, and frees the table when every cell becomes equal again. Recording
and ordered multi-command-buffer batch validation overlay intersecting ranges
without publishing tentative state.

State consumers now use their actual footprint:

- color targets validate one color mip/layer;
- depth/stencil targets validate the selected mip/layer and present aspects;
- sampled, storage, combined, and input descriptors validate their image-view
  mip/layer range;
- whole-image copies and VideoOut presentation require a uniform complete
  image and fail closed when fragmented.

`agcGetImageSubresourceStateInfo` returns a snapshot only when the requested
range is uniform. `agcGetImageStateInfo` preserves its whole-image meaning and
returns `AGC_ERROR_NOT_SUPPORTED` for fragmented state.

Clean generic verification passes all 7 CTest suites and 16,680 assertions
with no failures. Coverage transitions one color mip/layer independently,
checks an untouched neighbor, merges the state back to uniform, and separately
proves depth and stencil aspects can carry different states. The Prospero
library and six guarded runtime artifacts compile without new warnings.

The rebuilt FW 5.50 guarded artifacts are pinned in
`samples/hw_test/Makefile`. The first stage-0 launch attempt stopped before
upload because `10.0.1.41` had no reachable websrv FTP/HTTP/debug service; no
hardware payload executed. Endpoint qualification therefore remains pending
until etaHEN/websrv is reachable.
