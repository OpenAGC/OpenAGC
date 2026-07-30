# VideoOut linear-registration patch profiles (2026-07-30)

OpenAGC's application-neutral VideoOut path temporarily bypasses the system
debug-setting check while registering caller-owned linear scanout buffers, then
restores the original instruction immediately. The previous implementation
hardcoded the FW 5.50 instruction and was unsafe on FW 11.60.

## Firmware evidence

The local decrypted SPRX files are reference inputs only and are not committed.
`videoout_linear_patch_facts.tsv` records the relative path, full SPRX SHA-256,
guard fields, branch address, and complete six-byte instruction for all 39
active ABI keys. `tools/verify_videoout_patch_facts.sh /Volumes/Untitled/unp`
regenerates that ledger and requires a byte-for-byte match.

All bodies are reached through the `sceVideoOutRegisterBuffers` export
(`w3BY+tAEiQY`). The extractor verifies that each candidate branch reaches the
matching mode-2 linear-registration rejection containing `0x80290007`. The 39
exact profiles reduce to ten observed instruction groups, but every firmware
key remains an explicit table entry; shared bytes do not imply support for an
unlisted neighbor.

## Implementation boundary

`agcVideoOutFindLinearPatch()` reduces the console's full raw version, such as
`0x11600005`, to the four-digit ABI key `0x1160`. It returns only an exact
evidenced profile among the 39 active keys. The Prospero VideoOut backend
verifies all six original
instruction bytes before changing memory, restores the same six bytes after
registration, and returns `AGC_ERROR_NOT_SUPPORTED` for an unknown key or a
signature mismatch. Host tests lock every offset and signature, full-version
normalization, and rejection below, between, and above evidenced keys.

Only FW 5.50 and FW 11.60 are hardware-qualified. The remaining 37 entries are
SPRX-qualified and hardware-unverified.

FW 11.60 hardware qualification is recorded below. It qualifies the public
linear VideoOut lifecycle while a direct AGC context is active; rendering a
graphics or compute result directly into the presented buffers remains a
separate higher-level gate.

## Bounded hardware gate

`agc_videoout_public_fw1160_logged.elf` uses only the public OpenAGC VideoOut
lifecycle. While the direct AGC context is active it requires V12 defaults and
async setup, registers two caller-owned linear scanout buffers through the
firmware-keyed patch, executes a GPU `WRITE_DATA` marker, performs two bounded
VSYNC flips, closes VideoOut, shuts down the driver, and releases its flexible
and direct memory before self-termination.

The file-backed runner executes the process-cleanup ELF immediately before
each payload and defaults to two repetitions. It pins artifact SHA-256
`43927ea5dfb6d6f8253cf109cfa2b817f766918757979a22cdf419aa89e556e7` and
requires exact profile, registration/restoration, marker, flip, teardown, and
final PASS lines.

## FW 11.60 hardware result

The pinned payload passed twice on standard PS5 FW `0x11600005`. Both launches
used the process-cleanup ELF immediately beforehand, selected ABI key `0x1160`,
accepted V12 defaults and async setup, verified and restored the `+0x9922`
instruction, completed the GPU marker after 50 ms, and completed both bounded
VSYNC flips. Driver shutdown, flexible release, direct unmap, and direct-memory
release all returned zero. The payload self-terminated after each verdict and
websrv port 8080 plus ps5debug-NG port 744 remained reachable.
