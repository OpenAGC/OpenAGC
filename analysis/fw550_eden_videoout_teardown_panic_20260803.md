# FW 5.50 Eden relaunch panic teardown hardening (2026-08-03)

## Evidence boundary

Three consecutive cleanup-first Eden `2048.nro` processes (PIDs 230, 232,
and 234) completed eight native presents, emitted `GAME PASS 8 frames`, and
reached the normal same-app `KillApp` and `All processes exited` lifecycle.
The fourth launcher request timed out after 60 seconds with no response. Its
snapshot klog is empty, so no captured artifact identifies a faulting kernel
instruction or proves whether the console failed before or during that launch.

The incident therefore does not prove a Dynarmic, submission, or VideoOut root
cause. It does expose a concrete unsafe ownership path that must be fixed before
another direct-backend launch.

## Unsafe ownership path

The Prospero VideoOut backend registered caller-owned scanout buffers, deleted
the flip event at close, and closed the VideoOut handle without calling
`sceVideoOutUnregisterBuffers`. Vulkan then destroyed the present chain and
immediately freed the registered swapchain image mappings. This order could
leave VideoOut or the kernel holding stale scanout virtual addresses across
process teardown and immediate relaunch.

The repository's hardware sample already documents the complete order:

1. delete the flip event;
2. unregister buffer slot zero;
3. close VideoOut;
4. delete the equeue;
5. unmap and release the caller-owned memory.

## Fix

`agcVideoOutCloseChecked` now implements that exact order and tracks whether
buffer registration succeeded, including partial-open failure paths. Every
step is checked. `agcDestroyPresentChain` propagates a teardown error before it
releases image dependency references or destroys the present-chain child.
The ABI-compatible void close wrapper terminates the process if checked close
fails, because returning to a caller that cannot observe the error would permit
registered scanout memory to be released. The public hardware sample uses the
checked API and skips scanout unmap/release on failure.

Vulkan-PS5 consumes the checked result. If native present-chain teardown
fails, it retains every registered swapchain image and memory allocation and
emits a mandatory failure diagnostic. A partial-open rollback failure returns
a live chain on error under the documented runtime contract, allowing Vulkan
to quarantine the surface and retain the device. Replacement first unregisters
the retired native chain, so two main-display registrations never overlap.
Native present-fence, compute-queue, graphics-queue, and device destroy results
are also checked instead of being discarded. This is intentionally
fail-closed: leaking the current process on a teardown error is safer than
unmapping memory still owned by VideoOut or `/dev/gc`, and the guarded runner
must block relaunch until a reboot.

The first post-panic hardware canaries established a narrower FW 5.50 rule.
After the eighth successful present, unregistering set zero returns native
VideoOut `RESOURCE_BUSY` (`0x80290009`) because that set remains the active
scanout. Three earlier OpenAGC compute logs recorded the identical unregister
result followed by a successful `sceVideoOutClose`. The checked close path now
accepts only that specific busy result as a request to release ownership by
closing the handle. It does not clear `buffers_registered` until handle close
succeeds. Any other unregister result, or any close failure, still retains the
present chain and caller memory.

The next canary proved checked handle close succeeds, then
`sceKernelDeleteEqueue` returns `0x80020009` (`EBADF`). This is the same
terminal equeue status retained as non-fatal evidence across numerous earlier
stable FW 5.50 1,800-flip qualifications. At that point the flip event is
deleted, the buffer registration is released by successful handle close, and
the equeue descriptor is already invalid. The checked path therefore accepts
only exact `EBADF` as already retired and clears the local descriptor. Any
other delete-equeue error remains a teardown failure.

## Offline validation

- generic OpenAGC runtime: 20,085 assertions, zero failures, including retained
  image ownership after an injected VideoOut close failure;
- Prospero VideoOut ownership source regression: PASS;
- OpenAGC Prospero static-library build: PASS;
- Vulkan-PS5 WSI, lifecycle, and guarded-runner tests: PASS, including injected
  close failure, terminal-surface quarantine, blocked device teardown, and a
  retry after an injected failure between native fence and queue destruction;
- Vulkan-PS5 host and Prospero static-library builds: PASS.

## Hardware gate

Do not infer hardware qualification from the offline fix. After a fresh FW
5.50 boot, use only the direct `/dev/gc` backend, start continuous klog before
the cleanup-first qualification launch, and run one bounded canary. Require
successful unregister/close/queue/device teardown, exact process absence, a
clean scoped klog, and a responsive post-run console before any relaunch. Only
then restart the identical two-run 600-frame Eden gate.
