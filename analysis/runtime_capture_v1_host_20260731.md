# Native runtime capture v1 — host — 2026-07-31

## Scope

Runtime API v24 introduces the first endian-defined, streaming diagnostic
capture. The library remains libc-only: applications supply a synchronous
write callback, while OpenAGC manually serializes every integer in little-
endian order and never serializes C padding.

The initial stream records:

- the runtime API, selected profile, firmware key, hardware family,
  capabilities, and qualification classes;
- dense capture-local IDs for command buffers, queues, fences, and GPU labels;
- object creation, application debug names, and matching destruction;
- command-buffer begin/end boundaries and application PM4 dwords;
- final post-injection PM4 dwords exactly submitted to the backend;
- submission IDs, queue, command order, typed label waits/signals, and fence;
- bounded fence wait results and validation messages;
- final record and byte counts.

No C object pointer, CPU address, allocation address, or separately queried GPU
address is serialized. PM4 may embed addresses, so the host decoder redacts
address-bearing and unknown register values unless explicitly asked to show
them. Captures are diagnostic only and have no replay entry point.

## Host evidence

The runtime fixture captures an intentionally invalid command state without a
debug callback, then records and submits an empty compute command. It
independently parses the resulting bytes and proves:

- exact magic, format/header size, endian tag, and API version;
- contiguous record versions, sizes, and sequence numbers;
- one runtime record, three matched object lifetimes, one debug name,
  command boundaries, validation, submission, fence result, and end record;
- an empty application command followed by the exact two-dword runtime-owned
  generic NOP in the submitted stream;
- dense IDs `1..3`, exact record/byte counts, and clean allocation teardown.

The independent decoder fixture creates a known SET_CONTEXT_REG plus WRITE_DATA
stream. It proves deterministic text, `CB_TARGET_MASK` naming, packet lengths,
default address redaction, explicit address opt-in, validation rendering, and
malformed-magic rejection. The generic suite passes 17,064 assertions with
zero failures and CTest passes 8/8 before final clean verification.

## Remaining Milestone 5 capture work

Resource descriptions, shader record versions/hashes and opt-in bytes,
pipeline descriptions, typed transition records, and selected readback hashes
remain required before capture exit criteria are complete. The reference-frame
hardware artifact is also still pending.
