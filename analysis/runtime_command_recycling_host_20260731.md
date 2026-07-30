# Fence-driven command recycling — host — 2026-07-31

## Contract

Runtime API v22 adds `agcRecycleCommandBuffers` as a bounded batch operation
over the existing fence and command-buffer lifecycle. It accepts 1–63 distinct
command buffers from the fence device, validates the complete list before
polling, and polls the binary fence once. An incomplete fence returns
`AGC_ERROR_BUSY`; invalid input or state returns the corresponding validation
error. Every failure leaves command bytes, states, and retained references
unchanged. A pending member must be owned by the supplied fence, preventing a
mixed-submission list from being partially completed during validation.

After completion, every member must be executable with no pending submission
reference. The runtime then releases recorded references, resets every cursor
and storage range, and publishes `Initial` for the entire batch. This is an
explicit reuse boundary rather than a new allocator object.

## Generic evidence

The regression fixture records two command buffers that retain separate GPU
labels, proves an unsignaled fence cannot recycle them, submits them under one
fence, and checks duplicate and invalid-state batches are rejected atomically.
It then recycles both commands, records later timeline values into the same
storage, submits the second cycle, and recycles again before teardown.

The deferred-retirement stress sample now uses the same API once per completed
two-command batch. The complete generic binary passes 16,902 assertions with
zero failures.

## Prospero qualification

The guarded `agc_runtime_retirement_stress.elf` remains the exact-firmware
oracle. Its rebuilt SHA-256 and FW 5.50 result are recorded after the guarded
deployment in `runtime_batch_deferred_retirement_host_20260731.md`.
