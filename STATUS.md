# openagc Status

## GFX1013 occlusion-query snapshots (2026-07-28)

OpenAGC now owns application-neutral gfx1013 occlusion begin/end helpers. They
enable perfect ZPASS counting before the address-bearing begin snapshot,
snapshot the end counter before disabling increments, and validate GPU address
and aggregate command capacity atomically. Public constants describe the conservative
16-render-backend, 256-byte per-query storage layout; exact packet and rejection
tests keep PM4 details out of Vulkan clients. Hardware qualification is pending.

## Application-neutral GPU authorization (2026-07-28)

The Prospero `sce_agc_initialize` path now prepares the payload process for
`/dev/gc` before opening the device. It uses the payload SDK's ucred helper and,
for the primary FW 5.50 profile, repairs any detached thread ucred through the
same offsets proven by the hardware samples. Failures return
`AGC_ERROR_NOT_INITIALIZED`; firmware-specific kernel layout remains private to
`driver_prospero.c`. This removes the prior hidden requirement that every
OpenAGC/Vulkan consumer include `samples/hw_test/gpu_credentials.h`. The generic
suite remains unchanged and passing, and the Prospero archive cross-builds with
strict warnings enabled. Real-console requalification remains pending.

## Compute descriptor resource tables (2026-07-27)

`AgcGfx1013ComputeState` now carries application-neutral resource-table
bindings. `agcGfx1013ValidateCompute` requires every compiler-emitted
descriptor-set placeholder to resolve to a unique, aligned gfx1013 table
address. `agcGfx1013DispatchCompute` includes the patch packets in its capacity
preflight and emits them after the shader/user state and immediately before
`DISPATCH_DIRECT`, so a compiled compute shader cannot overwrite its own table
binding. The host packet fixture verifies ordering, register selection, address
encoding, and the missing-binding rejection. CMake/CTest passes on the generic
backend and the Prospero static library cross-builds cleanly.

## Reusable gfx1013 capability query (2026-07-27)

The public `agc_capabilities.h` API now provides a versioned, application-neutral
gfx1013 capability snapshot. It centralizes qualified image dimensions, MRT and
Wave32/compute limits, color/depth format masks, sample counts, and the flexible
and direct-memory profiles used by higher-level APIs such as Vulkan-PS5. The
generic host suite covers invalid arguments and every exposed capability class.

## Product scope and Subnautica evidence cleanup (2026-07-27)

OpenAGC is a source-level GPU API for PS5 homebrew. Retail-game compatibility
is not a product goal or release gate. Retail binaries are retained only as
bounded ABI/NID evidence, packet-builder usage examples, and cross-checks for
structures, calling conventions, and firmware variants.

The pinned Subnautica `PPSA02453` executable is 27,750,350 bytes with SHA-256
`8d5cd4b6417363a0568ea8d3c28ebdbad01e9725edaf39c614d303b352dcaf07`.
Its 63 imports resolve to 58 direct definitions and five versioned forwarding
wrappers. The strict analyzer now rejects header-only coverage. The manifest
correctly classifies `sceAgcDriverSetHsOffchipParam` and
`sceAgcDriverSetTFRing` as direct implementations, not forwarding wrappers.
This is static ABI evidence and makes no claim that Subnautica runs through
OpenAGC or on FW `0x0550`.

## Standalone rotating-cube homebrew (2026-07-27)

`examples/cube` is the first complete application outside `samples/hw_test`.
It configures only against an installed OpenAGC package through
`find_package(OpenAGC)`, then owns its flexible and garlic allocations, shader
record parsing and upload, resource table, DCBs, VideoOut buffers, and cleanup.
No repository-private header, in-tree library target, or sample-local PM4 setup
is consumed.

The application uses three independent frame slots. Each owns a linear BGRA8
render target, command buffer, continuously updated 24-vertex/36-index cube,
buffer descriptor, and EOP completion fence. Reuse and completion waits are
bounded to two seconds. Completed flexible-memory images are copied to three
registered garlic buffers and presented through VideoOut. The finite 3,600-frame
loop restores its launch-context state, closes VideoOut, releases direct memory,
unmaps flexible memory, and exits on both success and application errors.

The GLSL vertex/pass-through-geometry/pixel shaders compile into a gfx1013
Wave32 NGG pair and descriptor-free pixel record. A staged Prospero install and
separate consumer configure/build completed without warnings and linked only
the installed `libopenagc.a`.

The FW `0x05500008` curl/websrv gate now passes. Two consecutive executions
accepted the 2,467-dword first-frame DCB, reached every EOP fence, presented all
3,600 frames, and completed teardown without a hang, reset, panic, UI crash, or
stuck process. The display and live Chiaki capture showed the rotating colored
cube on dark gray. The 1920x1080 projection scales X and Y within 0.27%, so the
changing tall/wide silhouette is perspective foreshortening rather than aspect
stretch. See `analysis/fw550_standalone_cube_qualification_20260727.md`.

## Minimal public graphics example (2026-07-27)

`samples/triangle` is the small homebrew-facing counterpart to the FW 5.50
qualification harness. Its `openagcTriangleRecord` helper atomically composes
the public gfx1013 frame prologue, Wave32 baseline draw, and render-to-present
transition. The example documents typed RGBA8 UNORM/sRGB selection and keeps
platform allocation, shader upload, submission, bounded waits, and VideoOut in
the application layer. It contains no raw or sample-private PM4 construction.

The command-recording layer is host-buildable through
`OPENAGC_BUILD_EXAMPLES=ON`. The standalone Prospero integration and FW 5.50
hardware gate are complete in `examples/cube`. A clean generic build with
examples enabled passed the complete host suite at 4,127 passed and 0 failed.

The same public example cross-builds cleanly with the Prospero Clang 18
toolchain as `build-prospero/libopenagc_triangle_example.a`. The focused FW
5.50 qualification payload then passed through foreground curl/websrv as
`samples/hw_test/agc_graphics.elf`: 508,248 bytes, SHA-256
`673bba3e560315e0f1b96ed13a5f9d6db75a022869feb3a7945b41cefdf83d95`.
The reusable Wave32 draw, fence, marker, 255,744/255,744 complete FP16 stores,
packed hash, and 1,800/1,800 flips passed. See
`analysis/fw550_public_triangle_qualification_20260727.md`.

## FW 5.50 RGBA32 FLOAT render-target qualification (2026-07-27)

The typed gfx1013 `RGBA32_FLOAT` linear render-target path is
hardware-qualified on standard PS5 FW `0x05500008`. It uses CB format `0x0e`,
FLOAT number type `7`, standard component swap, the format-derived 32_ABGR
shader export, and a 16-byte-per-pixel offscreen allocation.

Two identical curl/websrv runs each changed and completely stored all four
float components for 255,744 of 2,359,296 pixels, found eight distinct packed
values, rejected zero components outside finite `[0,1]`, and produced FNV64
`0x1e8771ed63381dce`. Both runs passed Wave32, marker, and 1 ms fence checks,
completed 1,800/1,800 flips, and left live kernel logs free of GPU-fault,
reset, process-stop, panic, privilege-register, watchdog, and fatal signatures.

## FW 5.50 RG32 FLOAT render-target qualification (2026-07-27)

The typed gfx1013 `RG32_FLOAT` linear render-target path is
hardware-qualified on standard PS5 FW `0x05500008`. It uses CB format `0x0b`,
FLOAT number type `7`, standard component swap, and the format-derived 32_GR
shader export.

Two identical curl/websrv runs each changed and completely stored both float
components for 255,744 of 2,359,296 pixels, found eight distinct packed
values, rejected zero components outside finite `[0,1]`, and produced FNV64
`0x806171be9908c276`. Both runs passed Wave32, marker, and 1 ms fence checks,
completed 1,800/1,800 flips, and left live kernel logs free of GPU-fault,
reset, process-stop, panic, privilege-register, watchdog, and fatal signatures.

## FW 5.50 R32 FLOAT render-target qualification (2026-07-27)

The typed gfx1013 `R32_FLOAT` linear render-target path is hardware-qualified
on standard PS5 FW `0x05500008`. It uses CB format `0x04`, FLOAT number type
`7`, standard component swap, and the format-derived 32_R shader export.

Two identical curl/websrv runs each changed and completely stored 255,744 of
2,359,296 native float pixels, found eight distinct values, rejected zero
components outside finite `[0,1]`, and produced deterministic FNV64
`0x43e0f1986c4ec883`. Completion fences arrived in 6 ms and 5 ms; both runs
passed Wave32 and marker checks, completed 1,800/1,800 flips, and left live
kernel logs free of GPU-fault, reset, process-stop, panic, privilege-register,
watchdog, and fatal signatures.

## FW 5.50 RG8 UNORM render-target qualification (2026-07-27)

The typed gfx1013 `RG8_UNORM` linear render-target path is hardware-qualified
on standard PS5 FW `0x05500008`. It uses CB format `0x03`, UNORM number type
`0`, standard component swap, and FP16_ABGR shader export through the reusable
format-derived frame post-bind state.

Two identical curl/websrv runs each changed 255,744 of 2,359,296 native RG8
pixels, found eight distinct packed values, and produced deterministic FNV64
`0x6babce1afaa81b2c`. Both runs passed Wave32, marker, and 1 ms fence checks,
completed 1,800/1,800 flips, and left live kernel logs free of GPU-fault,
reset, process-stop, panic, privilege-register, watchdog, and fatal signatures.

## FW 5.50 R8 UNORM render-target qualification (2026-07-27)

The typed gfx1013 `R8_UNORM` linear render-target path is hardware-qualified
on standard PS5 FW `0x05500008`. The reusable frame post-bind builder now
derives `SPI_SHADER_COL_FORMAT` from the active color-target tuple; R8 uses CB
format `0x01`, UNORM number type `0`, standard component swap, and FP16_ABGR
shader export. Exact host fixtures cover this state and the updated 24-dword
post-bind stream.

Two identical curl/websrv runs each changed 255,043 of 2,359,296 native R8
pixels, found eight distinct stored values, and produced deterministic FNV64
`0x6fe253259c7b0455`. Both draws passed the Wave32 audit, post-draw marker, and
1 ms completion fence; each preview completed 1,800/1,800 flips. Both live
ps5debug-NG kernel logs were free of panic, bad-packet, page-fault, GPU-reset,
process-stop, privilege-register, watchdog, and fatal signatures.

## FW 5.50 D16/HTILE expclear qualification (2026-07-27)

The typed gfx1013 `D16_UNORM` HTILE expclear path is hardware-qualified on
standard PS5 FW `0x05500008`. The reusable depth-surface builder emits
`DB_Z_INFO.ALLOW_EXPCLEAR=1`, `DECOMPRESS_ON_N_ZPLANES=15`, and D16
`ZRANGE_PRECISION=1`; the isolated sample initializes all metadata to the
canonical depth-one word `0xfffffff0`, writes `DB_DEPTH_CLEAR=1.0`, and omits
the ordinary full-surface depth initialization draw.

Two identical curl/websrv runs each changed all 49,152 HTILE words after typed
decompression and resummarization, recovered exactly 918,432 clear-one,
128,304 near, and 128,304 far native D16 values, and produced exactly 128,304
green plus 128,304 red pixels. All stage markers passed, both completion
fences arrived in 1 ms, each preview completed 1,800/1,800 flips, and both
live kernel logs were free of GPU-fault, reset, process-stop, and panic
signatures.

## FW 5.50 compressed D16/HTILE qualification (2026-07-27)

The typed gfx1013 `D16_UNORM` plus pipe-aligned HTILE path is
hardware-qualified on standard PS5 FW `0x05500008` with expclear, stencil,
and MSAA disabled. The 1920x1080 fixture uses a 2048x1152 D16 allocation
(`0x480000` bytes, `0x10000` alignment), a 2048x1536 HTILE allocation
(`0x30000` bytes, `0x4000` alignment), the depth-only uncompressed metadata
word `0xfffc000f`, and D16 `DB_Z_INFO.ZRANGE_PRECISION`.

Two identical curl/websrv runs each changed 4,226 HTILE words after typed
full-surface decompression and resummarization, recovered exactly 909,792
clear-one, 128,304 near, and 128,304 far native D16 values, and produced
128,304 green plus 128,304 red pixels. All four stage markers and the 1 ms
completion fence passed, each run completed 1,800/1,800 flips, and both live
kernel logs were free of GPU-fault, reset, process-stop, and panic signatures.
D16 HTILE expclear was subsequently qualified as the separate follow-up gate.
The independent-run evidence and its exact scope are consolidated in
`analysis/fw550_d16_htile_qualification_20260727.md`. The paired kernel logs
have no panic or GPU fault/reset signature, but each contains a generic
post-run fatal-signal line and a `0x4000` VM resource-leak warning. Those are
tracked as teardown issues and are not described as a completely clean log.

## FW 5.50 D16+S8 qualification (2026-07-27)

The typed uncompressed `D16_UNORM_S8_UINT` path is hardware-qualified on
standard PS5 FW `0x05500008` with separate `64KB_Z_X` depth and stencil
planes. HTILE, MSAA, and expclear remain disabled. Both websrv runs produced
exactly 128,304 green and 128,304 red pixels; native D16 contained 909,792
clear-one, 128,304 near, and 128,304 far values; native S8 contained 2,364,832
zero and 256,608 `0x5a` bytes with no other values. All markers and 1 ms
fences passed, each run completed 1,800/1,800 flips, and live kernel logs were
free of GPU fault, reset, process-stop, and panic signatures.

## FW 5.50 S8-only stencil qualification (2026-07-27)

The typed gfx1013 uncompressed `S8_UINT` path is hardware-qualified on
standard PS5 FW `0x05500008` without a depth plane. `agc_stencil_s8.elf`
allocates only the public `64KB_Z_X` stencil layout, binds zero depth
addresses and a zero depth swizzle, and keeps HTILE, MSAA, depth testing, and
depth writes disabled.

The isolated four-draw oracle clears stencil to zero with color masked, writes
`0x5a` under a green left triangle, rejects the overlapping red triangle with
`EQUAL 0`, and independently passes and replaces under the red right triangle.
Both identical websrv runs produced exactly 128,304 green and 128,304 red
pixels. Native S8 readback contained 2,364,832 zero bytes, exactly 256,608
`0x5a` bytes, and no other values. Every marker and 1 ms fence passed, each run
completed 1,800/1,800 flips, and live ps5debug-NG logs contained no panic,
bad-packet, page-fault, GPU-reset, or process-stop signature.

## FW 5.50 D16 depth qualification (2026-07-27)

The typed gfx1013 uncompressed `D16_UNORM` depth path is hardware-qualified
on standard PS5 FW `0x05500008`. `agc_depth_d16.elf` uses the public
`64KB_Z_X` layout query and depth-surface binder with HTILE, stencil, MSAA,
and expclear disabled. It reuses the proven Wave32 four-draw oracle: a
full-surface clear-depth draw, a near green pass, an overlapping farther draw
that must fail, and an independent far red pass.

Both identical websrv runs reached the GPU fence in 1 ms, passed all four
stage markers and the post-draw marker, produced exactly 128,304 green and
128,304 red pixels, and recovered 909,792 clear-one, 128,304 near, and 128,304
far values from the native 16-bit depth plane. Each run completed 1,800/1,800
flips. Live ps5debug-NG logs contained no panic, bad-packet, page-fault,
GPU-reset, or process-stop signature.

## FW 5.50 narrow FP16 render-target qualification (2026-07-27)

The typed gfx1013 `R16_FLOAT` and `RG16_FLOAT` presets are now
hardware-qualified on standard PS5 FW `0x05500008`. The isolated fixtures use
CB formats `0x02` and `0x05`, FLOAT number type `7`, standard component swap,
and FP16_ABGR shader export. Both reuse the proven Wave32 NGG+PS draw path and
validate native packed memory before converting the result to RGBA8 for
VideoOut inspection.

Two R16 runs each changed 255,680 pixels, stored one complete component per
covered pixel with no value outside `[0,1]`, and produced native FNV64
`0xedd26b35cf6fe81a`. Two RG16 runs each changed 255,744 pixels, stored two
complete components per covered pixel with no value outside `[0,1]`, and
produced native FNV64 `0xcf48c2eb4f12bc26`. Every qualifying run reached its
GPU fence, passed the post-draw marker and Wave32 audit, completed 1,800/1,800
flips, and had no panic, bad-packet, page-fault, GPU-reset, or process-stop
signature in the live ps5debug-NG kernel log.

An unchanged second R16 launch before live logging coincided with the console
shutting down. The reboot discarded its kernel evidence, so no packet-level
root cause is claimed. The sample now checks equeue creation, flip-event
registration, and flip-rate setup and explicitly closes VideoOut. The four
subsequent qualifying runs were stable; `sceKernelDeleteEqueue` consistently
returned `0x80020009` after a successful 1,800-flip session and is retained as
a non-fatal teardown diagnostic.

## FW 5.50 sRGB render-target qualification (2026-07-27)

Append-only public presets `RGBA8_SRGB` and `BGRA8_SRGB` are host-tested and
hardware-qualified on standard PS5 FW `0x05500008`. They retain CB format
`0x0a` and FP16_ABGR shader export, select CB number type `6`, and use standard
and alternate component swaps respectively. Exact 28-dword fixtures lock
`CB_COLOR0_INFO = 0x00010628` and `0x00010e28`; existing enum values remain
unchanged and the new values are 12 and 13.

Each isolated sample renders identical Wave32 content first to a native UNORM
control and then to native sRGB memory. All four runs changed 126,360 pixels in
both targets with zero coverage mismatches, zero alpha mismatches, zero RGB
values outside the quantization-aware IEC 61966-2-1 transfer envelope, and
157,421 non-identity converted channels. Both draws in every run passed the
Wave32 audit and EOP fence, followed by 1,800/1,800 flips. Repeated identical
ELFs reproduced their exact native packed hashes. User captures confirmed the
centered textured triangle on dark gray and the expected standard/alternate
color ordering. No timeout, GPU reset, kernel panic, or UI crash occurred.

Full evidence and artifact hashes are in
`analysis/fw550_srgb_qualification_20260727.md`; raw logs remain local under
`samples/hw_test/conformance-logs/srgb-20260727/`.

## FW 5.50 render-target format expansion (2026-07-27)

The typed gfx1013 color-target table and dedicated hardware fixtures now cover
the standard-swap `RGBA8_UNORM`, `RGB10A2_UNORM`, and
`R11G11B10_FLOAT` tuples in addition to the previously proven alternate-swap
BGRA8 and `R16G16B16A16_FLOAT` paths. Host golden fixtures lock each raw CB
format, number type, component swap, byte size, shader export, and resulting
`CB_COLOR0_INFO` value. `R11G11B10_FLOAT` uses gfx1013 CB format `0x06`, FLOAT
number type `7`, standard swap, and FP16_ABGR export.

All three dedicated samples passed on standard PS5 FW `0x05500008` through
foreground curl/websrv. RGBA8 standard swap changed 126,360 pixels in the
1920x1080 display target. RGB10A2 and R11G11B10 each changed 255,744 pixels in
the 1536x1536 offscreen target, passed the expected coverage window, produced
at least eight sampled colors, reached the EOP fence, and completed
1,800/1,800 flips. RGB10A2 matched the exact packed top-two-bit histogram
`{35857,27914,36523,155450}`. Two R11G11B10 runs matched packed-color FNV64
`0x4b75c00e8a6bb04d`; the second run enforced that value as a hard oracle.
No timeout, GPU reset, kernel panic, metadata fault, or UI crash occurred.

Full evidence and artifact hashes are in
`analysis/fw550_render_target_formats_20260727.md`. Raw logs remain under
`samples/hw_test/conformance-logs/formats-20260727/`. Further 16-bit tuples
remain pending and are not advertised as hardware-qualified.

## Application-facing indexed/indirect draw composition (2026-07-27)

The gfx1013 baseline graphics API now composes the existing hardware-oriented
packet builders into atomic direct-indexed, indirect, and indexed-indirect
draw calls. `agcGfx1013DrawBaselineIndexed` validates u16/u32 index alignment,
first-index range and address adjustment, maximum accessible index count,
instance count, and the complete shader/frame/resource prefix before emitting
`DRAW_INDEX_2`.

`agcGfx1013DrawBaselineIndirect` validates the indirect argument base and
offset, single/multi stride, base-vertex and start-instance register locations,
and optional index-buffer state before emitting `SET_BASE` plus the appropriate
single or multi draw packet. Exact host fixtures lock the 47-dword direct
indexed, 45-dword non-indexed indirect, and 55-dword indexed multi-indirect
streams. Short buffers, invalid ranges, and invalid strides leave the command
cursor unchanged.

All three isolated FW `0x05500008` hardware gates pass through curl/websrv.
Direct u16 indexed, non-indexed indirect, and u16 indexed-indirect each changed
255,744 FP16 pixels with the exact 768x665 coverage bounds, produced eight
sampled colors, reached the EOP fence, passed the Wave32 PM4 audit, and
completed 1,800/1,800 flips. The first indirect attempt exposed a wrapper bug:
passing control value `1` to `sceAgcDcbSetBaseIndirectArgs` produced a
noncanonical `SET_BASE` header and timed out the fence. The console recovered;
using canonical control value zero passed, and an exact host assertion now
locks that header. Full evidence is in
`analysis/fw550_indexed_indirect_qualification_20260727.md`.

## FW 5.50 combined stencil/HTILE expclear qualification (2026-07-27)

Combined D32+S8 HTILE expclear is enabled after independent qualification on a
standard PS5 running raw FW `0x05500008`. Depth-only, stencil-only, and combined
aspect masks each passed twice through foreground curl/websrv while the public
gate was off. Every run updated exactly 49,152 words in the selected `0x30000`
HTILE range, changed zero words outside it, preserved reserved and unselected
aspect bits, reached both completion fences, passed all four draw markers, and
completed 1,800/1,800 VideoOut flips. No GPU reset, kernel panic, metadata
spill, timeout, or loader failure occurred.

The exact RMW results were `0xfffc0300` for depth, `0xfffff0ff` for stencil,
and `0xfffc00f0` for both. Every downstream draw produced 128,304 green and
128,304 red pixels; raw D32 contained 1,955,232 clear-one, 128,304 near, and
128,304 far words; raw S8 contained 2,364,832 zero and 256,608 `0x5a` bytes
with no other values. `AGC_GFX1013_COMBINED_HTILE_EXPCLEAR_ENABLED` is now one.
Full evidence and hashes are in
`analysis/fw550_combined_expclear_qualification_20260727.md`.

## Typed HTILE compute RMW implementation

`agcGfx1013RmwHtile` consumes a validated non-tail HTILE subresource and aspect
plan, derives the exact address and word count, and atomically emits the
83-dword DB release/acquire, Wave32 compute dispatch, and compute release/DB
acquire sequence. It rejects shared mip tails, out-of-allocation ranges, short
command buffers, and shaders that do not expose seven user SGPRs plus TGID_X.

The `htile_rmw.comp` shader performs masked word updates while preserving
reserved and unselected-aspect bits. Its psbc record encodes RSRC2 `0x0000008e`
and five compute registers. Exact host fixtures cover depth-only, stencil-only,
and combined masks, address derivation, dispatch counts, user data, both
synchronization boundaries, reserved-bit preservation, and atomic failures.

## Hardware-sample PM4 promotion checkpoint (2026-07-27)

The current hardware-proven compute, graphics, tessellation, depth, stencil,
MSAA, and HTILE paths contain no hand-packed PM4 headers, no direct command
buffer allocation, and no raw register emission. Normal command construction
uses public typed OpenAGC builders. Remaining low-level calls are intentional
public diagnostic markers, repeated diagnostic draws, the read-only PM4 audit,
or the SharpEmu export-conformance program. Adding a wrapper around those
sample policies would not improve the application API. The reconciled evidence
and remaining non-blocking feature work are documented in
`analysis/sample_pm4_public_api_audit.md`.

## FW 5.50 HTILE subresource qualification (2026-07-27)

Nonzero mip and array-layer HTILE binding are now independently
hardware-validated on standard PS5 FW `0x05500008`. The isolated mip fixture
binds mip 1 of a two-level 1920x1080 D32 image, restricts viewport/scissor to
the 960x540 attachment extent, and changes 4,385 words in the exact 64 KiB
mip-1 HTILE range with zero changes outside it. The isolated array fixture
binds layer 1 of a two-layer D32 image and changes 18,013 words in its exact
192 KiB HTILE slice with layer 0 unchanged. Both runs reached the EOP fence in
1 ms, passed all four GPU markers and color outcomes, and completed 1,800/1,800
VideoOut flips.

`agcGfx1013GetHtileSubresourceLayout` exposes gfx10 reverse mip ordering,
per-layer slice offsets, exact ordinary-mip metadata sizes, and shared mip-tail
storage. The multi-mip D32 allocation query now uses the gfx10 2D mip-chain
footprint rather than summing independently aligned levels, which could
under-allocate real hardware images. Evidence and artifact hashes are recorded
in `analysis/fw550_htile_subresources_20260727.md`; raw logs remain under
`samples/hw_test/conformance-logs/htile-subresources-20260727/`.

## FW 5.50 17-sample qualification checkpoint (2026-07-27)

The authoritative sequential curl/websrv matrix now passes 17/17 on standard
PS5 FW `0x05500008` at revision `03b43f2`. The prior 14 base, compute,
graphics, and tessellation cases were followed by typed HTILE operations,
depth-only expclear, and combined stencil/HTILE. All per-sample machine
oracles passed in 434 seconds with no timeout, instant close, firmware
mismatch, loader overlap, GPU hang, reset, kernel panic, or UI crash.

Raw logs, the numeric run environment, ELF SHA-256 manifest, and raw-log
SHA-256 manifest are retained under
`samples/hw_test/conformance-logs/fw550-03b43f2-20260727/`. The committed
qualification summary is `analysis/fw550_qualification_03b43f2.md`.

The compute and graphics hardware samples now terminate GPU work with a gfx1013
`RELEASE_MEM` end-of-pipe fence using event `0x14`, GCR control `0x603`, cache
policy 3, and 32-bit fence data. This replaces the weaker
`ACQUIRE_MEM`-plus-`WRITE_DATA` diagnostic, which could become visible before
all shader color writes reached CPU readback. Two consecutive compute runs
each matched all 2,073,600 pixels. Corrected-fence NGG baseline, amplification,
line-input, invocation, and tessellation-geometry-line runs reached their
completion fences, passed target validation, and completed 1,800/1,800 flips.

The line fixtures use a dedicated constant-white pixel shader so interpolation
cannot hide valid edges. Direct Chiaki capture confirmed that the tessellated
outer sides and internal line endpoints are connected; the remaining faint
dotted appearance is one-pixel rasterization plus remote-stream scaling. The
run changed 6,749 FP16 pixels, emitted one exact opaque-white FP16 color, and
completed without a GPU hang or kernel panic.

`samples/hw_test/run_fw550_conformance.sh` now defines the authoritative
ordered websrv matrix. It uses isolated remote paths, bounded foreground
launches, persistent logs, numeric FW `0x0550` verification, exact per-sample
gates, and fail-fast handling for timeouts, disconnects, instant closes, or
failure markers. The complete clean generic suite passes with zero failures.
The earlier instant-close lifecycle condition did not recur. Every foreground
loader returned before the next isolated remote path launched, so this run
closes the post-fence matrix checkpoint.

## FW 5.50 reusable Wave32 VS+PS baseline

### Reusable gfx1013 fixed-function state

The FW 5.50 sample-only fixed-function PM4 setup has been promoted into
atomic public builders for color-target binding, aspect-preserving viewport,
screen/window/generic/viewport scissors, target mask, depth-disabled state,
and V8 graphics register defaults. The builders preflight their complete
packet allocation and leave the command-buffer cursor unchanged on invalid
arguments or insufficient space.

Host fixtures verify the exact Wave32 graphics streams and dword budgets:
28 dwords for a color target, 15 for the viewport, 22 for scissors, 3 for the
target mask, 15 for depth-disabled state, and 2184 for the V8 defaults
(174 SH, 493 CX, and 61 UC register writes). The supported, hardware-proven
color-target tuples include R16, RG16, and RGBA16 FLOAT/STD plus RGBA8
UNORM/ALT.

FW 5.50 gfx1013 hardware validation passed through websrv for both paths. The
RGBA16F baseline rendered the centered blended-color triangle for all 1800
flips, changed 255,744 pixels, and produced the expected bounds and eight
colors. The direct RGBA8 path rendered the centered green/dark-red blended
triangle for all 1800 flips, changed 126,360 pixels, produced eight colors,
and passed vertex-fetch, indexed-draw, and texture-sampling checks. Neither
run hung or panicked the console.

The baseline draw tail is also reusable. `AgcGfx1013BaselineDrawState` and
`agcGfx1013DrawBaselineIndexAuto` atomically compose the existing VS/PS
binder, primitive state, optional post-bind SH/CX/UC application overrides,
index size/swap, instance count, and `DRAW_INDEX_AUTO`. The base stream is
44 dwords for the exact host fixture, with three dwords per non-contiguous
post-bind override; short buffers and invalid state leave the cursor
unchanged.

Baseline draws may also carry optional typed depth-surface and depth/stencil
state. Both are validated as part of the atomic draw contract and emitted
after the frame post-bind sequence, which otherwise disables depth and clears
the DB surface registers. Vulkan and other clients therefore do not need to
duplicate ordering-sensitive DB register emission.

`agcGfx1013SetColorTargetSlot` generalizes the typed color-target builder to
all eight gfx1013 CB slots while preserving the CB0 wrapper. Frame state accepts
up to eight same-extent targets, binds each slot after graphics defaults, and
packs every target's nibble into `SPI_SHADER_COL_FORMAT`. Host tests verify a
two-target prologue, CB1 register stride, exact stream size, and `0x44` RGBA8
dual-export state.

FW 5.50 hardware validation uses the wrapper as the real draw path. Both the
RGBA16F and direct RGBA8 samples submitted a 2473-dword DCB, completed the
post-draw marker and all 1800 flips, and produced the same validated coverage
as before the refactor. The RGBA16F path changed 255,744 pixels; RGBA8 changed
126,360 pixels and passed vertex-fetch, u16 index-state, and texture-sampling
checks. Physical display validation showed the gray background with the
colorful triangle in both modes. The specialized tessellation sample still
builds with its existing binder and direct post-bind override path.

FW 5.50 qualification samples now have deterministic websrv lifecycles.
`videoout_linear.elf` applies the known FW 5.50 VideoOut linear-buffer patch
internally, completes 600 flips, releases its resources, and returns instead
of relying on an external debugger or persistent thread. `agc_compute.elf`
submits and completes the display flip after its full-buffer readback. The
qualification runner must observe foreground `/hbldr` completion before
starting another ELF; a curl timeout halts the sequence to prevent overlapping
homebrew-loader processes.

The complete post-PM4-promotion FW 5.50 qualification suite passes on revision
`c0633c7` and raw firmware `0x05500008`. Base VideoOut/AGC/compute, baseline
and NGG variants, isolated tessellation, and all combined tessellation/geometry
variants passed through foreground curl/websrv. Baseline and combined geometry
each repeated identically three times; all applicable cases completed
1,800/1,800 flips, all physical results were confirmed, and the corrected
sequential run had no hang, panic, or UI crash. Full evidence is in
`analysis/fw550_qualification_c0633c7.md`.

Complete and hardware-validated on standard PS5 gfx1013, raw firmware
`0x05500008`. `agcGfx1013ValidateWave32VsPs` and
`agcGfx1013BindWave32VsPs` now provide a reusable path for fused NGG Gs(2)
plus Wave32 PS records. The binder validates metadata and 256-byte program
alignment, derives primitive and interpolant state, patches PGM_LO/HI, preflights
the full command-buffer allocation, and emits SH/CX/UC state atomically.

Validation evidence: compiler NGG-record regressions and 2,129 generic checks
pass; the Prospero library and
`agc_graphics.elf` build cleanly; websrv hardware execution returned
`AGC_OK`, passed the NGG/PS Wave32 PM4 audit, produced 255,744 valid FP16
pixels with no out-of-range components, executed the post-draw
`0xDEADCAFE` marker, and completed 1,800/1,800 display flips.

## FW 5.50 NGG geometry bring-up

Status: **pass-through geometry passes deterministic hardware readback and
physical-display validation.**

The compiler and sample now support separate Wave32 ES-front and GS-back
records for a real geometry shader. The current implementation preserves the
pre-lowering `triangle_strip` output topology in `AgcShaderSpecials`, enables
`SPI_SHADER_PGM_RSRC1_GS.WGP_MODE`, leaves the unsafe `NGG_WAVE_ID_EN` bit
clear, and emits the same GFX10 allocation-register formulas used by Mesa.
The reusable binder consumes the record Specials through
`sceAgcCreatePrimState`; the hardware audit confirms `out_prim=2`,
`GE_CNTL=0x0000f655`, Wave32 stage state, successful DCB submission, and a live
post-draw marker.

FW 5.500.008 checkpoint probes confirmed the complete allocation/export
control path: `GS_ALLOC_REQ` is reached and returns, the following workgroup
barrier releases, and primitive plus position export points execute. The
remaining corruption was traced to double-scaled GS input offsets. Hardware
already applied `VGT_ESGS_RING_ITEMSIZE=21`, producing packed offsets
`0,21,42`, while the separately compiled GS multiplied them by its shader-side
stride of 21 again. The corrected split ABI programs the hardware register to
unit stride and retains 21 only in `AC_UD_VGT_ESGS_RING_ITEMSIZE`; captured
back-stage offsets are now exactly `0,1,2`.

The compiler also applies Mesa's required GFX10 non-tessellation workaround:
`GE_CNTL.VERT_GRP_SIZE = VGT_GS_ONCHIP_CNTL.ES_VERTS_PER_SUBGRP - 5` when the
group size is not 256. gfx1013 is modeled as GFX10, not GFX10.3.

Three consecutive probe-free runs of the identical ELF now produce exactly
255,744 changed FP16 pixels, bounds `x=384..1151, y=436..1100`, eight sampled
interpolated colors, no out-of-range components, a live post-draw marker, and
1,800/1,800 completed display flips per run. The temporary entry,
allocation, remap, and export probes have been removed. The physical display
confirmed the expected centered colorful triangle on a gray background.

Extended geometry coverage is hardware-validated on FW 5.500.008. A line-list
input programs `VGT_PRIMITIVE_TYPE=2`, submits two vertices, and reconstructs the
full centered triangle with 255,744 changed FP16 pixels. A separate
`invocations=2` fixture programs `VGT_GS_INSTANCE_CNT=0x9` and emits two
half-scale copies with deterministic 127,488-pixel FP16 coverage. An isolated
RGBA8 single-draw variant produces 126,360 changed pixels against a
dimension-derived expectation of 126,293 +/- 1,024, with eight sampled colors
and an aspect-correct centered square viewport. Each case retains the live
post-draw marker, completes 1,800/1,800 flips, and has matching physical-display
confirmation. Render-target variants remain isolated to one draw per process so
same-process second-submit sequencing is not conflated with format coverage.

Two-triangle geometry amplification is hardware-validated on FW 5.500.008. The
six-vertex GS emits two color-preserving half-scale copies at horizontal offsets
`-0.3` and `+0.3`. Repeated captured runs produce exactly 127,488 changed FP16
pixels, bounds `x=346..1189, y=602..933`, eight sampled colors, zero out-of-range
components, a live post-draw marker, and 1,800/1,800 completed flips. Three
physical-display observations confirmed two colorful triangles on a gray
background. Compiler regression coverage locks WGP mode, triangle-strip
serialization, the GFX10 `GE_CNTL` adjustment, unit hardware ESGS offsets, the
21-dword shader stride, and absence of unsupported shader-query intrinsics. The
retained compiler suite passes, as does the clean OpenAGC generic suite with
2,139 checks.

## FW 5.50 tessellation bring-up

Status: **isolated Wave32 tessellation and TES-to-NGG geometry composition are
hardware-validated on FW 5.500.008.**
The reusable gfx1013 binder consumes fused HsFront/HsBack and TES
GsFront/GsBack records, patches the runtime ring descriptors and distinct HS/TES
`VGT_TCS_OFFCHIP_LAYOUT` words, and programs the factor/offchip ring state
required by gfx1013. It also accepts the pipeline's hull LDS requirement in
bytes and patches `SPI_SHADER_PGM_RSRC2_HS.LDS_SIZE`. Gfx10.3 first rounds the
allocation to 1024 bytes and then encodes that allocation in 512-byte field
units, so the reusable binder now emits only legal even field values. This is
required when the TCS reads VS outputs through the
separate-stage LS/HS memory ABI. That nonzero-LDS rounding is host-verified and
still awaits application-level hardware qualification. The public layout
builder packs pipeline-specific patch
counts, input/output control-point counts, linked VS/TCS output counts,
primitive mode, and tess-factor reads without exposing register fields to API
clients. Its typed draw state now accepts the same optional depth-surface
and depth/stencil state as baseline graphics draws, restoring DB state after
shader binding without exposing register packets to API consumers. FW 5.50
`sceAgcDriverSetTFRing` takes a 256-byte-aligned ring
address and a dword-aligned size, clamps the size to `0x4000`, and submits the
16-byte `{uint64_t ring_addr, uint32_t size, uint32_t reserved}` payload with
ioctl `0x80108128`. The similarly named Direct export is a permission stub and
must not be used for normal ring setup.

The final hardware run returned `AGC_OK` from TF-ring setup and DCB submission,
emitted four `4.0` tessellation factors, changed 24 offchip dwords, and shaded
255,744 FP16 pixels versus 255,456 expected. Coverage bounds were
`x=384..1151, y=436..1100` (768x665), with eight sampled colors, no out-of-range
components, a live post-draw marker, and 1,800/1,800 completed display flips.
The PS5 display showed a centered colorful triangle with equal sides on the
gray background. No GPU hang or kernel panic occurred.

The combined `agc_tess_geometry.elf` fixture retains the same HS/TES control
path and adds a real NGG geometry stage. The GS shrinks every tessellated
microtriangle to 78% around its primitive centroid, providing an unambiguous
stage signature instead of pass-through output. FW 5.500.008 returned `AGC_OK`
for TF-ring setup, reusable binding, and DCB submission; retained four `4.0`
factors and 24 changed offchip dwords; and produced 155,321 changed FP16 pixels
versus 155,419 expected, bounds `x=406..1129, y=465..1091`, eight sampled
colors, 68,208 opaque samples, zero out-of-range components, and a live
`0xDEADCAFE` post-draw marker. The run completed 1,800/1,800 display flips
without a GPU hang or kernel panic.

Physical-display confirmation showed a centered equal-sided subdivided
triangle on a dark-gray background. Dark seams surround every colorful
microtriangle, exactly matching the GS centroid shrink and directly confirming
that tessellation output feeds the NGG geometry stage rather than bypassing it.

Combined-stage GS invocation count is also hardware-validated. The
`agc_tess_geometry_invocations.elf` fixture compiles an `invocations=2` GS
after TES and selects two half-scale copies with `gl_InvocationID`. The shader
record programs `VGT_GS_INSTANCE_CNT=0x00000009`; FW 5.500.008 produced
127,488 changed FP16 pixels versus 127,728 expected, bounds
`x=346..1189, y=602..933`, eight sampled colors, 56,003 opaque samples, zero
out-of-range components, four retained `4.0` factors, 24 changed offchip
dwords, a live post-draw marker, and 1,800/1,800 display flips. The PS5 display
showed two colorful tessellated triangles on the gray background without a GPU
hang or kernel panic.

Combined-stage GS output topology is hardware-validated with line strips. The
`agc_tess_geometry_lines.elf` fixture changes only the GS output declaration,
emitting a closed four-vertex `line_strip` for each TES-generated
microtriangle. Its record programs `out_prim=1`, `max_vert_out=4`, and
`GE_CNTL=0x40` while retaining the tessellation ring ABI. FW 5.500.008 produced
6,749 changed FP16 pixels versus the dimension-derived 5,760 estimate within
the 1,536-pixel rasterization tolerance, bounds `x=384..1151, y=435..1100`,
one exact opaque-white FP16 color, 6,749 opaque samples, zero out-of-range components, four
`4.0` factors, 24 changed offchip dwords, a live post-draw marker, and
1,800/1,800 display flips. The PS5 display showed the expected connected white
equal-sided triangular wire grid on gray without a GPU hang or kernel panic.

The isolated combined-stage RGBA8 target is hardware-validated. The
`agc_tess_geometry_rgba8.elf` fixture reuses the centroid-shrink TES+GS records
unchanged and switches only the render target to the registered 1920x1080
`R8G8B8A8_UNORM` display surface with `CB_COLOR0_INFO=0x00010828`. Two runs
each produced exactly 76,803 changed pixels versus 76,836 expected, eight
sampled colors, four `4.0` factors, 24 changed offchip dwords, a live post-draw
marker, and 1,800/1,800 display flips. Physical confirmation showed the
equal-sided red/green subdivided triangle with the centroid-shrink dark seams
on a gray background. No GPU hang or kernel panic occurred.

Host coverage validates record types, required HS continuation state, the
front-only TES form produced by ACO, runtime placeholder patching, Wave32
stage enables, primitive type `DI_PT_PATCH=9`, and command-buffer cursor
advance. The hardware sample is `agc_tessellation.elf`; its GLSL TCS forwards
stage-local control-point data and writes tessellation level 4, while its TES
uses `gl_TessCoord` for barycentric position and color interpolation. Keeping
the position tables local isolates HS/TES launch from inter-stage varying ABI.
The isolated sample remains the control for future combined-stage expansion.
The planned combined-stage matrix is complete: `invocations=2`, line-strip
output, FP16 offscreen rendering, and direct RGBA8 display rendering are all
independently hardware-proven on FW 5.500.008.


## Firmware compatibility

The public AGC API is firmware-agnostic. Runtime native-driver selection now
uses explicit ABI-family aliases rather than a FW 5.50-only backend or unsafe
numeric ranges.

- Submit16 runtime profiles retain exact inspected standard-PS5 builds from
  FW 1.00 through FW 12.70 as RE data; registration is not a support claim.
- FW 3.20 is the lowest active compatibility target. FW 5.50 is
  hardware-validated; other active aliases await validation on matching
  firmware.
- Runtime profile diagnostics are hardware-validated on FW 5.500.008
  (`0x05500008`): family `standard`, model `standard-ps5`, submit16
  `0xC0108102`, authenticated queues enabled, TF ring enabled, EOP offset
  `0x39000`, GPU-info span `0x100000`, CWSR working offset `0xA00000`, and
  CWSR allocation `0x1000000`.
- FW 1.00 and 2.x are archival profiles only. Their known evidence remains in
  the registry, but missing legacy ABIs will not be recovered and unsupported
  operations remain fail-closed. FW 3.20 remains an active exact-profile RE
  target around request `0xc0108102`.
- FW 9+ resolves `sceKernelHasTrinityMode`; PS5 Pro receives its firmware-
  proven 22 MiB CWSR allocation, 16 MiB working offset, and 1.5 MiB GPU-info
  span. Standard PS5 retains 16 MiB, 10 MiB, and 1 MiB respectively.
- Unknown firmware builds fail closed.

See `analysis/agc_driver_abi_families.tsv`,
`analysis/agc_driver_abi_1160.md`, and `tools/verify_agc_driver_abi.sh`.

## Current Milestone

**Graphics pipeline: Wave32 indexed drawing, FP16 render targets, NGG geometry,
and isolated tessellation are hardware-validated.** The gfx1013
ES+GS/NGG path fetches `float2` position plus `float3` color from a
20-byte-stride GPU vertex buffer, consumes a bound 16-bit index buffer, and
rasterizes an RGB triangle on real PS5 hardware. The same path also renders
to a 1536x1536 linear `R16G16B16A16_FLOAT` offscreen color buffer and converts
the readback to the registered RGBA8 display surface for inspection. Phase 7
is complete; see PLAN.md for the remaining production-hardening work.

The NGG and pixel stages are now explicitly validated as Wave32. Compiler
records contain `VGT_SHADER_STAGES_EN.GS_W32_EN` and
`SPI_PS_IN_CONTROL.PS_W32_EN`, and `agc_graphics.elf` audits those same bits in
the final PM4 stream before submission. Three runs on FW 5.500.008 produced
identical GPU results: the Wave32 draw
returned `AGC_OK`, advanced the `0xDEADCAFE` post-draw marker, changed 255,744
FP16 pixels, sampled eight distinct colors, and reported zero out-of-range
components. The display path now uses a hardware-proven 1920x1080 linear
scanout, mirrors a centered 768x768 preview across both buffers, and waits for
each flip event. The websrv run completed 1,800/1,800 vsync flips over 30
seconds and was visually confirmed as a dark-gray background with a centered
blended-color triangle. The validation also exposed and fixed the sample's
32-bit truncation of the ABI-defined `off_t` direct-memory physical offset and
decoupled FP16 pool sizing from scanout dimensions.

See [PLAN.md](PLAN.md) for the broader GNM-to-AGC architecture roadmap,
including geometry, ray tracing, cache synchronization, and VRS targets.

The host-generic implementation now has a tested model for:

- Type-3 AGC/PM4 packet headers using `length_dwords - 2` in bits `29:16`
- AGC `IT_NOP` subcommands recovered from HLE reference
- Known Gen5 AGC NID constants for mapped exports
- `SceAgcCb` cursor offsets and cursor allocation
- `sceAgcCb*` and `sceAgcDcb*` cursor-based packet builders
- DCB/ACB submit descriptor layout
- Generic submit validation and debug capture
- AGC shader record parser (magic, pointer fields, semantics counts, shader type)
- Bounds-checked serialized shader relocation (`agcShaderRecordRelocateBinary`),
  converting compiler file offsets into a caller-owned runtime record without
  mutating the compiler binary
- Unified CPU/GPU-visible flexible allocation with host parity, explicit
  cache flush/invalidate operations, 16 KiB mapping granularity, deterministic
  unmapping, requested alignment validation, and bounded 32-bit EOP-label
  waits (`agcGpuMemory*`)
- Write-combined direct GPU memory allocation with 2 MiB granularity, unified
  CPU/GPU mapping, physical-allocation tracking, and paired unmap/release
- FW 5.50 AGC shader Specials block struct (`AgcShaderSpecials`: 0x30-byte sparse register/value layout with fields at 0x00, 0x08, 0x20, and 0x28)
- AGC shader User Data Table struct (`AgcShaderUserData`: 5× 64-bit entries)
- Typed accessors for shader sub-blocks (`agcShaderRecordGetSpecialsTyped`, `agcShaderRecordGetUserDataTyped`, `agcShaderRecordGetShRegisterValues`, `agcShaderRecordGetCxRegisterValues`)
- Extended texture/surface enums: 18 tile modes, 8 image types, 28 data formats (BC1-7, depth/stencil, Fmask, subsampled), 7 number types, 5 clamp modes, 4 filter modes, 3 mip filter modes, 4 border color types
- Full 64-byte `AgcRenderTarget` struct with CMASK/FMASK/DCC compression fields, CB_COLOR register mapping, and 14 init/setter helpers
- Texture descriptor convenience helpers: `SetImageType`, `SetTileMode`, `SetMipLevels`, `SetArraySize`, `SetDepth`, `SetPitch`, `SetDstSel`, `GetBaseAddress`, `GetWidth`, `GetHeight`
- Typed sampler helpers: `SetClampMode`, `SetFilterMode`, `SetBorderColor`, `SetMaxAnisotropy` (hardware-correct SQ_IMG_SAMP_WORD0-3 bit layout)
- Texture format encode/decode helpers: `agcTextureFormatEncode`, `agcTextureFormatGetDataFormat`, `agcTextureFormatGetNumberType`
- Shader linking: `agcShaderLinkHsGs` — combines HS/LS + CS shader records into GS (matches SPRX ordinal 131)
- Fused shader support: `sceAgcGetFusedShaderSize` plus FW 5.50-accurate legacy `sceAgcFuseShaderHalves` (`nApJjpKNBl4`) and `sceAgcFuseShaderHalves_0200` (`fd5Bp5tGTgo`), including checksum copies, RSRC1/2 merging, scratch relocation, and program-address patching
- EOP flip submit: `sceAgcDriverSubmitEopFlip` (prospero) + `sceAgcDcbSetEopFlip` DCB builder (IT_RELEASE_MEM 0x49)
- NID table (FW 5.50): 354 identified exports (216 libSceAgc + 138 libSceAgcDriver) out of 366 total FW 5.50 SPRX exports (96.7% coverage). 322 NIDs algorithm-verified via SHA1(name+salt) prospero-nid computation. Sources: reference emulator LIB_FUNC, ps5-openagc agc_nid.h, FW 3.20 genstub files, aerolib.csv (154k entries), flatz ps5_symbols.txt, NID computation (67 placeholder names resolved). Remaining 12 unknown NIDs are not in any known database. 32 TSV entries are unverified placeholders (`sceAgcUnknown_*`). 9 functions have two NIDs in 5.50 SPRX (old+new version exports), disambiguated with _<NID> suffix. Version-specific NIDs (3.20-only, 11.60-only) are in `analysis/agc_nids_version_variants.tsv`. All 354 TSV NIDs confirmed present in 5.50 SPRX. All function names in source code match their NID-verified correct names (Vsh-prefixed duplicates removed, wrong-function renames fixed).
- Async-compute queue submission: generic backend queue tracking (32 slots), ACB submit validates queue in-use, full create→submit→destroy flow tested
- 13 new DCB builders from SPRX disassembly: ReleaseMem, IndirectBuffer, DrawIndirect, DrawIndex2, DrawIndexIndirect, DrawIndirectMulti, DrawIndexIndirectMulti, SetPredication, EventWrite, SetConfigReg, SetShReg, SetUconfigReg
- 4 AGC-custom flip builders: WaitFlipDone (0x4C), WaitFlip (0x51), InsertWaitFlipDone (0x54), WaitFlipEos (0x4F+0x4E)
- Workload tracking: sceAgcDriverSetWorkloadsActive / SetWorkloadComplete with SET_WORKLOAD (0x1E) submit on prospero
- FW 5.50 register-defaults blob builder/parser with embedded primary/internal tables
- Runtime firmware/backend registry: PS5 system-version ABI validation,
  four-digit major/minor ABI keys with complete raw-version diagnostics,
  capability filtering, and fail-closed selection before backend mutation
- Compatibility GS occupancy: `sceAgcGetGsOversubscription` returns the
  firmware-proven `GE_PC_ALLOC` and `SPI_SHADER_PGM_RSRC4_GS` pairs and
  reproduces the full shader-state occupancy/interpolation calculation used by
  Dragon Quest VII Reimagined's bundled AGC compatibility library.
- Compatibility GPU memset: `sceAgcCbMemsetExclusive` emits an atomic
  32-dword compute bind/dispatch sequence using an aligned, clean-room gfx1013
  kernel and nine-user-SGPR psbc ABI. Host packet fixtures are complete;
  FW `0x0550` execution/readback remains the hardware promotion gate.

## Verified

Host generic backend:

```sh
cmake -B build -DOPENAGC_PLATFORM=generic -DOPENAGC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
make -B test
```

Current expected result:

```text
4080 passed, 0 failed
```

PS5 prospero backend (cross-compiled, no tests):

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
cmake -B build-prospero -DOPENAGC_PLATFORM=prospero -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-prospero
```

Expected result: `build-prospero/libopenagc.a` — PS5 x86_64 static library,
zero warnings, zero errors. All `sceAgcDriver*` and `sce_agc_*` symbols
present in the symbol table. Hardware validation in progress — see
"Hardware Validation Results" section below.

### PS5 packaging (LibProsperoPkg)

Build the C++ packaging tools (one-time):

```sh
cmake -S /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar \
      -B /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build \
      -DCMAKE_BUILD_TYPE=Release -DLIBPROSPEROPKG_BUILD_TOOLS=ON
cmake --build /Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build --parallel
```

Deploy as installable `.pkg` (debug-mode PS5):

```sh
PKG_TOOLS=/Users/bizkut/Downloads/PS5/homebrew/LibProsperoPKG-seregonwar/build
$PKG_TOOLS/prosperopkg-fself payload.elf payload.self
$PKG_TOOLS/prosperopkg-gp5 app_dir out.gp5 --flat --type app
```

Or deploy as ELF payload (exploited PS5):

```sh
prospero-deploy -h $PS5_HOST -p $PS5_PORT payload.elf
```

## Implemented Packet Builders

Cursor-based builders:

- `sceAgcCbNop`
- `sceAgcCbDispatch`
- `sceAgcCbSetShRegistersDirect`
- `sceAgcCbSetCxRegistersDirect`
- `sceAgcDcbWriteData`
- `sceAgcDcbWaitRegMem` — reference-confirmed: 32-bit variant now 7 dwords
  (was 6) with proper control word (0x10 base, split op bits, cache_policy),
  address alignment masking, poll cycles field, and corrected field order
  (addr, mask, reference, control, poll)
- `sceAgcDcbDmaData`
- `sceAgcDcbSetBaseIndirectArgs`
- `sceAgcDcbDispatchIndirect`
- `sceAgcDcbSetIndexBuffer`
- `sceAgcDcbDrawIndexOffset`
- `sceAgcDcbDrawIndexAuto` — now emits `IT_DRAW_INDEX_AUTO` (opcode 0x2D)
  with 3-dword packet and proper draw initiator decoding (reference-confirmed;
  was incorrectly NOP-wrapped 7-dword stub)
- `sceAgcDcbWaitUntilSafeForRendering`
- `sceAgcDcbPushMarker`
- `sceAgcDcbPopMarker`
- `sceAgcDcbSetFlip`
- `sceAgcDcbSetEopFlip` — emits `IT_RELEASE_MEM` (opcode 0x49) with 8-dword
  EOP flip packet (header 0xc0064900, matching SPRX RE)
- `sceAgcCbReleaseMem`
- `sceAgcDcbSetShRegistersIndirect` — opcode 0x63, 5 dwords (SPRX-confirmed;
  was incorrectly NOP-wrapped 4 dwords)
- `sceAgcDcbSetCxRegistersIndirect` — opcode 0x9F, 5 dwords (SPRX-confirmed)
- `sceAgcDcbSetUcRegistersIndirect` — opcode 0x64, 5 dwords (SPRX-confirmed)

Typed gfx1013 state builders:

- `agcGfx1013BuildFramePrologue` / `agcGfx1013ApplyFramePostBind` — own the
  exact hardware-proven 2,275-dword context/default/target/viewport/scissor/
  launch prologue and 21-dword post-shader depth/raster phase; baseline and
  tessellation composers consume the same `AgcGfx1013FrameState`
- `agcGfx1013SignalEopFence` — atomically emits the hardware-proven 10-dword
  gfx1013 `RELEASE_MEM` cache-flush/writeback fence and trailer used by both
  compute and graphics samples
- `agcGfx1013DrawTessIndexAuto` — atomically composes Wave32 HS/TES/GS/PS
  binding, stage resource tables, post-bind tessellation context, application
  overrides, instance count, and auto-index draw in hardware-proven order
- `agcGfx1013BuildTessellationRingTable` — validates and builds the exact
  128-byte factor/offchip descriptor table atomically; the reusable gfx1013
  profile provisions 160 8K-dword offchip buffers across all four shader
  engines. The gfx10.3 buffering field uses the global encoded count (`159`),
  while an explicit 40-buffer per-engine constant documents the distinction
  from gfx11. Validation rejects ring storage smaller than the encoded
  buffering/granularity requirement
- `agcGfx1013SetTessellationRings` — emits the four hardware-proven UC ring
  registers with complete buffer preflight
- `agcGfx1013SetTessellationContext` — emits the five post-shader CX
  tessellation registers with complete buffer preflight

Old-style ACB stubs (from `src/acb.c`) and DCB raw-buffer variants (from `src/dcb.c`):

- `sceAgcAcbInitializeDefaultHardwareState_pre0090`
- `sceAgcAcbDispatchIndirect`
- `sceAgcAcbAcquireMem` — now emits `IT_ACQUIRE_MEM` (opcode 0x58) with 8-dword packet
- `sceAgcAcbEventWrite` — now emits `IT_EVENT_WRITE_EOP` (opcode 0x47) with 5-dword packet
- `sceAgcAcbAtomicMem` — now emits `IT_ATOMIC_MEM` (opcode 0x1B) with 5-dword packet
- `sceAgcAcbCondExec` — now emits `IT_COND_EXEC` (opcode 0x22) with 4-dword packet
- `sceAgcAcbWaitRegMem` — now emits `IT_WAIT_REG_MEM` (opcode 0x3C) with 6-dword packet
- `sceAgcAcbWriteData` — now emits `IT_WRITE_DATA` (opcode 0x37) with 5-dword packet
- `sceAgcAcbCopyData` — now emits `IT_COPY_DATA` (opcode 0x40) with 6-dword packet
- `sceAgcAcbMemSemaphore` — now emits `IT_MEM_SEMAPHORE` (opcode 0x39) with 4-dword packet
- `sceAgcAcbDmaData` — now emits `IT_DMA_DATA` (opcode 0x50) with 8-dword packet
- `sceAgcAcbResetQueue` — now emits `IT_AGC_0x79` (opcode 0x79) with 3-dword packet
- `sceAgcAcbRewind` — now emits `IT_NOP` (opcode 0x10) with 2-dword packet
- `sceAgcAcbSetFlip` — now emits `IT_RELEASE_MEM` (opcode 0x49) with 7-dword packet
- `sceAgcAcbSetWorkloadComplete` — now emits `IT_SET_WORKLOAD` (opcode 0x1E) with 8-dword packet
- `sceAgcAcbSetWorkloadStreamInactive` — now emits `IT_AGC_0x79` (opcode 0x79) with 3-dword packet
- `sceAgcAcbSetWorkloadsActive` — now emits `IT_SET_WORKLOAD` (opcode 0x1E) with 8-dword packet
- `sceAgcAcbAtomicGds` — now emits `IT_ATOMIC_GDS` (opcode 0x1D) with 10-dword packet
- `sceAgcAcbAtomicGds_0900` — compatibility cursor ABI, 11-dword `IT_ATOMIC_GDS`
- `sceAgcAcbPrimeUtcl2` — now emits `IT_PRIME_UTCL2` (opcode 0x5D) with 4-dword packet
- `sceAgcAcbJump`
- `sceAgcAcbPushMarker` / `sceAgcAcbPopMarker` / `sceAgcAcbSetMarker`
- `sceAgcDcbClearState` — now emits `IT_CLEAR_STATE` (opcode 0x14) with 2-dword packet
- `sceAgcDcbAtomicGds` / `ContextStateOp` / `ResetQueue` /
  `SetWorkloadComplete` / `SetWorkloadStreamInactive` / `SetWorkloadsActive` /
  `WaitUntilSafeForRendering`
- `sceAgcDcbSetPreemption` — SPRX RE shows it is an intentional VSH-only
  stub that crashes; openagc returns `AGC_ERROR_INVALID_STATE`

Default state submission:

- `sceAgcDriverNotifyDefaultStates` (prospero) now builds the primary/internal
  register-defaults blobs in GPU-visible memory and submits an
  `IT_CLEAR_STATE` (0x14) DCB to load them.

Suspend points:

- `sceAgcDriverIsSuspendPointInFlightDirect` (prospero) now queries the gfx
  queue status via ioctl `nr=0x27` and returns whether the status is non-zero.
- `sceAgcSuspendPointAndCheckStatus` combines a direct suspend-point submit
  with the in-flight query.

In-place patchers:

- `sceAgcDmaDataPatchSetDstAddressOrOffset`
- `sceAgcWaitRegMemPatchAddress`
- `sceAgcQueueEndOfPipeActionPatchAddress`
- `sceAgcSetShRegIndirectPatchSetAddress` / `AddRegisters` — SPRX-confirmed:
  validate opcode 0x63, SetAddress patches cmd[1..2] preserving low 2 bits,
  AddRegisters patches cmd[4] bits 13:0
- `sceAgcSetCxRegIndirectPatchSetAddress` / `AddRegisters` — SPRX-confirmed:
  validate opcode 0x9F, same patch logic
- `sceAgcSetUcRegIndirectPatchSetAddress` / `AddRegisters` — SPRX-confirmed:
  validate opcode 0x64, same patch logic

Game-compat packet builders (from Joe & Mac game analysis):

- `sceAgcDcbAcquireMem` — `IT_ACQUIRE_MEM` (0x58), 8 dwords
- `sceAgcDcbCopyData` — `IT_COPY_DATA` (0x40), 6 dwords
- `sceAgcDcbJump` — `IT_INDIRECT_BUFFER` (0x3F), 4 dwords (SPRX-confirmed;
  was incorrectly 0x33/IB_CNST)
- `sceAgcDcbResetQueue` — queue reset, 3 dwords
- `sceAgcDcbSetIndexCount` — `IT_INDEX_BUFFER_SIZE` (0x13), 2 dwords
  (SPRX-confirmed; was incorrectly 3 dwords; clamps count to max(count,1))
- `sceAgcDcbSetIndexSize` — opcode 0x7A, 3 dwords (SPRX-confirmed;
  was incorrectly 0x2A/INDEX_TYPE; cmd[1]=0x20000243 constant,
  cmd[2]=(type&3)|(swap<<6)|0x400)
- `sceAgcDcbSetNumInstances` — `IT_NUM_INSTANCES` (0x2F), 2 dwords
- `sceAgcDcbStallCommandBufferParser` — opcode 0x42, 2 dwords
  (SPRX-confirmed; was incorrectly NOP+subcommand)
- `sceAgcDcbDrawIndex` — `IT_DRAW_INDEX_2` (0x27), 6 dwords
  (SPRX-confirmed field order: cmd[1]=max(count,1), cmd[4]=index_count,
  cmd[5]=draw_initiator)
- `sceAgcCbSetShRegisterRangeDirect` — `IT_SET_SH_REG` (0x76), variable
- `sceAgcCbSetUcRegistersDirect` — `IT_SET_UCONFIG_REG` (0x79), variable
- `sceAgcSetNop` — patches byte at cmd+1 to 0x10 (NOP), returns NULL
  (SPRX-confirmed; 1 param, not 2)
- `sceAgcGetDataPacketPayload` — 3-param payload getter
  (out_addr, cmd, skip_header); returns NULL (SPRX-confirmed)
- `sceAgcDebugRaiseException` — debug stub (no-op on non-dev)
- `sceAgcCreateShader` — shader record validation
- `sceAgcCreatePrimState` — FW 5.50-accurate 5-param primitive state builder;
  emits two CX and three UCONFIG pairs with shader Specials, hull merging,
  GS-enable handling, and the recovered 18-entry primitive lookup
- `sceAgcCreateInterpolantMapping` — FW 5.50-accurate 3-param interpolant
  builder; emits all 32 raw CX descriptors with semantic matching, F16,
  flat/custom, missing-semantic, and default-value transformations

SPRX disassembly batch 2 (FW 5.50 deep disassembly):

DCB packet builders:
- `sceAgcDcbClearState` — AGC-custom clear state (opcode 0x12), 2 dwords
- `sceAgcDcbRewind` — `IT_REWIND` (0x59), 2 dwords
- `sceAgcDcbCondExec` — `IT_COND_EXEC` (0x22), 5 dwords
- `sceAgcDcbSetIndexIndirectArgs` — AGC-custom (opcode 0x91), 4 dwords
- `sceAgcDcbAtomicMem` — opcode 0x1E (AGC ATOMIC_MEM), 9 dwords
  (NID "1-gUn1PI4Sw" was mislabeled as SET_WORKLOAD; it's actually AtomicMem)
- `sceAgcDcbAtomicGds` — `IT_ATOMIC_GDS` (0x1D), 11 dwords
- `sceAgcDcbMemSemaphore` — `IT_MEM_SEMAPHORE` (0x39), 4 dwords
- `sceAgcDcbPrimeUtcl2` — `IT_PRIME_UTCL2` (0x5D), 5 dwords
- `sceAgcDcbDrawIndexMultiInstanced` — AGC-custom (opcode 0x3A), 9+count dwords
- `sceAgcDcbSetMarker` — NOP-wrapped marker string
- `sceAgcDcbContextStateOp` — 4-op context state switch (CLEAR_STATE /
  SET_CONTEXT_REG / SET_CX_REG_INDIRECT / combined)
- `sceAgcDcbSetWorkloadsActive` / `SetWorkloadComplete` / `SetWorkloadStreamInactive`
  — DCB cursor workload helpers (SET_WORKLOAD 0x1E, 8 dwords)

DCB register direct setters (3 dwords each):
- `sceAgcDcbSetCfRegisterDirect` — `IT_SET_CONFIG_REG` (0x68)
- `sceAgcDcbSetCxRegisterDirect` — `IT_SET_CONTEXT_REG` (0x69)
- `sceAgcDcbSetShRegisterDirect` — `IT_SET_SH_REG` (0x76)
- `sceAgcDcbSetUcRegisterDirect` — `IT_SET_UCONFIG_REG` (0x79)
- `sceAgcDcbSetCfRegisterRangeDirect` — variable-length config reg range
- `sceAgcCbSetUcRegisterRangeDirect` — variable-length uconfig reg range

CB builders:
- `sceAgcCbBranch` — `IT_INDIRECT_BUFFER` (0x3F), 14 dwords, 12-arg signature
  (SPRX-confirmed: cmd[1]=((ctrl&7)<<8)|(flags&3), cmd[2]=addr_lo&~7,
  cmd[10]=((engine&3)<<28)|(size&0xfffff), etc.)
- `sceAgcCbCondWrite` — AGC-custom `IT_COND_WRITE` (0x45), 9 dwords
  (SPRX-confirmed: cmd[2..3]=ref, cmd[4]=mask, cmd[6..7]=address, cmd[8]=write_data)
- `sceAgcCbMemSemaphore` — `IT_MEM_SEMAPHORE` (0x39), 4 dwords

WaitRegMem patchers (SPRX-confirmed: require 0x79 wrapper, use adjusted pointer):
- `sceAgcWaitRegMemPatchCompareFunction` — patches adjusted[1] bits 2:0
- `sceAgcWaitRegMemPatchReference` — patches adjusted[4]
- `sceAgcWaitRegMemPatchMask` — patches adjusted[5] (32-bit 0x3C) or adjusted[6] (64-bit 0x93)

reference-confirmed patchers and helpers:
- `sceAgcGetPacketSize` — returns packet size in dwords from PM4 header
- `sceAgcSetPacketPredication` — sets/clears bit 0 (predication) of packet header
- `sceAgcSetRangePredication` — walks packet range setting predication bit
- `sceAgcCondExecPatchSetEnd` — patches cmd[4] bits 13:0 with dword count
- `sceAgcCondExecPatchSetCommandAddress` — patches cmd[1..2] with command address
- `sceAgcWriteDataPatchSetAddressOrOffset` — patches cmd[2..3] for IT_WRITE_DATA
- `sceAgcJumpPatchSetTarget` — patches cmd[1..3] for IT_INDIRECT_BUFFER
- `sceAgcSetCxRegIndirectPatchSetNumRegisters` — patches cmd[4] bits 13:0
- `sceAgcSetShRegIndirectPatchSetNumRegisters` — patches cmd[4] bits 13:0
- `sceAgcSetUcRegIndirectPatchSetNumRegisters` — patches cmd[4] bits 13:0

reference-confirmed GetSize helpers:
- `sceAgcDcbWriteDataGetSize` — returns 4*num_dwords + 16 bytes
- `sceAgcDcbJumpGetSize` — returns 16 bytes
- `sceAgcDcbRewindGetSize` — returns 8 bytes
- `sceAgcDcbCondExecGetSize` — returns 20 bytes
- `sceAgcAcbCondExecGetSize` — returns 20 bytes
- `sceAgcDcbWaitOnAddressGetSize` — returns 56 (32-bit) or 64 (64-bit) bytes

Game-compat driver functions:

- `sceAgcDriverRegisterOwner` — stub (returns 0x8a6c9018, matches SPRX)
- `sceAgcDriverRegisterResource` — stub (returns 0x8a6c9018, matches SPRX)
- `sceAgcDriverGetEqContextId` — EQ context ID query
- `sceAgcDriverSetTFRing` — non-Direct TF ring set using ioctl `0x80108128`;
  256-byte-aligned address plus dword-aligned size, clamped to `0x4000`
- `sceAgcDriverSetHsOffchipParam` — non-Direct HS offchip param
- `sceAgcDriverAgrSubmitDcb` — AGR submit (returns 0x8a6d0003 if not initialized)
- `sceAgcDriverAddEqEvent` — EQ event registration
- `sceAgcDriverDeleteEqEvent` — EQ event deletion (stub, NOT_SUPPORTED)
- `sceAgcDriverGetEqEventType` — EQ event type query (stub, NOT_SUPPORTED)
- `sceAgcDriverIsCaptureInProgress` — capture status (returns 0)
- `sceAgcDriverGetDefaultOwner` — default owner handle (returns 0)
- `sceAgcDriverInitResourceRegistration` — resource reg init (stub, NOT_SUPPORTED)
- `sceAgcDriverQueryResourceRegistrationUserMemoryRequirements` — (stub, NOT_SUPPORTED)
- `sceAgcDriverGetResourceRegistrationMaxNameLength` — returns 32
- `sceAgcDriverUnregisterResource` — resource unregister (stub, NOT_SUPPORTED)
- `sceAgcDriverRegisterWorkloadStream` — workload stream reg (stub, NOT_SUPPORTED)

Game-compat wrapper functions:

- `sceAgcInit` — user-facing init (delegates to `sce_agc_initialize`)
- `sceAgcSuspendPoint` — wrapper for `sceAgcDriverSuspendPointSubmitDirect`
- `sceAgcGetRegisterDefaults2` — register defaults query
- `sceAgcGetRegisterDefaults2Internal` — internal register defaults query

Register defaults (reference v8, FW 5.50):
- `agcRegisterDefaultsV8GetPrimaryGroups` — 127 groups, 703 registers (489 CX, 159 SH, 55 UC)
- `agcRegisterDefaultsV8GetInternalGroups` — 22 groups, 25 registers (4 CX, 15 SH, 6 UC)
- Extracted from the reference `agcRegisterDefaults.inc` `g_agc_public_reg_defaults_v8`
  and `g_agc_internal_reg_defaults_v8`. Replaces incomplete HLE-reference-derived
  data (which had only 38/703 public and 22/25 internal registers with many
  wrong zero-placeholder values).

LOD stats helpers:

- `sceAgcDcbGetLodStatsGetSize`
- `sceAgcDcbGetLodStats`

Ioctl / submit / queue layer (FW 5.50 kernel RE, `include/agc_ioctl.h`):

- 76 ioctl command constants (IOC-encoded, nr enum)
- `AgcGcSubmitArgs` submit ioctl arg struct (24 bytes)
- `AgcGcCommandBuffer` CB descriptor (16 bytes)
- `AgcGcFrameOpenArg` (nr=0x00, NOT HANDLED in FW 5.50), `AgcGcContextQueryResult` (nr=0x2e), `AgcGcMakesysmapArg8/12/48`
- `AgcGcSuspendArg` — 16-byte layout with four dwords (RE'd from kernel handler at 0x6e6ff0)
- `AgcGcSetHsOffchipArg` — 16-byte patch-list pointer/count (RE'd from kernel handler at 0x6ee6d2)
- CB header opcodes, VMID layout, num_cbs/VMID ranges
- Kernel-side error codes (module 0x4C)
- Kernel function offsets (ioctl_internal, submit_with_pid, frame_submit)

Native prospero backend (`src/driver_prospero.c`, `#ifdef OPENAGC_PROSPERO`):

- `sce_agc_initialize` — opens `/dev/gc`, calls `CONTEXT_QUERY` ioctl (0xc004812e, nr=0x2e), mmaps GPU register space at 0xfe0200000 if context not yet initialized. **NOTE:** The old `FRAME_OPEN` (nr=0x00) does NOT exist in FW 5.50 — the kernel returns EINVAL. See `analysis/sprx_sce_agc_initialize_disasm.md`.
- `sce_agc_initialize_internal_memory` — allocates 9 named regions via `sceKernelMapNamedSystemFlexibleMemory` (matches SPRX). Region sizes confirmed from SPRX disassembly: SceGnmGpuInfo (1MB), SceGnmTrapCode (16KB), SceGnmTrapData (16KB), SceGnmDdid (1008KB), SceGnmEopFifo (240KB), SceGnmShadowReg (16KB), SceGnmCwsr (16MB), SceGnmMisc (16KB), SceGnmACQRB (1920KB). See `analysis/sprx_agc_driver_internal_mem_disasm.md`.
- `sceAgcDriverSubmitMultiCommandBuffersDirect` — builds CB descriptors, calls `SUBMIT_PID`
- `sceAgcDriverSubmitDcb` — single DCB submit via `SUBMIT_PID`
- `sceAgcDriverSubmitAcb` — single ACB submit (const IB type) via `SUBMIT_PID`
- `sceAgcDriverSubmitEopFlip` — EOP flip submit; validates display buffer
  index (< 16), delegates to `sceVideoOutSubmitEopFlip` (SPRX ordinals 49/50)
- `sceAgcDriverSetupAsyncGraphics` — `QUEUE_STATUS` ioctl (nr=0x26, arg=1; SPRX-confirmed)
- `sceAgcDriverSetTFRingDirect` — FW 5.50 permission stub; returns unsupported
  without programming the public TF ring
- `sceAgcDriverSetHsOffchipParamDirect` — `SET_HS_OFFCHIP` ioctl with RE'd 16-byte patch-list argument
- `sceAgcDriverGetPaDebugInterfaceVersion` — FW 5.50 permission stub returning
  `0x8A6D0001` without an ioctl (SPRX-confirmed)
- `_sceAgcDriverCreateUserSpecialQueue` — `QUEUE_CREATE` ioctl (nr=0x21, 64-byte RW arg). Ring buffer address computed from EOP FIFO base + 0x39000 (SPRX-confirmed). Arg layout: magic tokens, pipe_id, mmio_base, queue_id, ring_addr, ring_size.
- `_sceAgcDriverDestroyUserSpecialQueue` — `QUEUE_DESTROY` ioctl (nr=0x0e, 12-byte RW arg with magic auth tokens; SPRX-confirmed)
- `sceAgcDriverNotifyDefaultStates` — takes `uint32_t flags`; builds FW 5.50 primary/internal register-defaults blobs in GPU-visible memory (kernel consumption path still pending RE)
- `sceAgcDriverSuspendPointSubmitDirect` — `SUSPEND_16` ioctl with RE'd 4-dword argument
- `sceAgcDriverIsSuspendPointInFlightDirect` — stub query (returns false)
- `sceAgcSuspendPointAndCheckStatus` — stub query (returns OK)
- `sce_agc_internal_suspend_point_submit_final` — `SUSPEND_39` ioctl with same 4-dword argument
- CB descriptor builder using `AgcGcCommandBuffer` with VMID masking
- Queue tracking (32 slots, gfx/compute/dma types)

Submit model:

- `AgcCommandBufferSubmit`
- `sceAgcDriverSubmitDcb`
- `sceAgcDriverSubmitAcb`

## Hardware Validation Results (FW 5.50, exploited PS5 @ 10.0.1.41)

### videoout_linear.elf — PASS
- `sceVideoOutOpen(userId=0xFF, BUS_MAIN, 0)` — OK (PS5 requires userId=0xFF, not 0)
- `sceVideoOutGetResolutionStatus` — 3840x2160 (4K)
- `sceKernelAllocateDirectMemory` (garlic, 12GB range, 2MB align) — OK
- `sceVideoOutRegisterBuffers` (A8R8G8B8_SRGB, tiled) — OK
- Flip loop running at 60fps

### agc_init.elf — PASS
- **[1] sce_agc_initialize()** — PASS
  - /dev/gc opened (fd=7), CONTEXT_QUERY OK, mmap at 0xfe0200000
  - FRAME_OPEN correctly returns EINVAL (confirms ps5-openagc audit)
- **[2] sce_agc_initialize_internal_memory()** — PASS
  - All 9 firmware regions plus the OpenAGC multi-submit NOP trailer allocated
    with sceKernelMapNamedSystemFlexibleMemory (type=0x33)
  - GPU VAs: 0x200024000–0x20145C000
  - Region sizes match SPRX disassembly exactly
- **[3] sceAgcDriverNotifyDefaultStates()** — PASS
  - Sub-region carving from SceGnmDdid (1008KB) works correctly
- **[4] sceAgcDriverGetPaDebugInterfaceVersion()** — PASS
  - Returns the official FW 5.50 permission-stub value `0x8A6D0001`
  - SPRX disassembly confirms the export logs and returns without an ioctl
- **[5] sceAgcDriverSubmitMultiDcbs(two marker DCBs)** — PASS
  - Unique per-iteration markers proved that the final descriptor was deferred
    until the next submit, not lost or blocked by PM4 contents or IB alignment.
  - The Prospero backend appends a dedicated GPU-visible 16-dword NOP IB as a
    harmless trailer, so every caller-provided descriptor executes immediately.
  - Two immediate FW 5.50 deployments each passed three repeated two-DCB
    submissions with unique ordered markers and zero polling delay.
- **[6] sceAgcDriverSetupAsyncGraphics(1)** — PASS
  - Ioctl 0x80048126 with arg=1 succeeds
- **[7] _sceAgcDriverCreateUserSpecialQueue()** — PASS (with credential bypass)
  - Ioctl arg layout confirmed correct via SPRX disassembly
  - Ring buffer carved from EOP FIFO base + 0x39000 (correct)
  - Read ptr from ACQRB base + 0x1C8000, metadata from ACQRB base + 0x1CC000
  - **Credential bypass**: The kernel handler at 0xffffffffd8f66bb0
    calls 0xffffffffd8e70400 which checks the process's GPU credentials
    at `[ucred + 0x58]` (cr_sceAuthId). The check masks with 0xff0f000000000000
    and adds 0xb7ff000000000000; if the result is zero (after >>49), the
    check passes. Setting cr_sceAuthId = 0x4801000000000000 satisfies this:
    (0x4801000000000000 & 0xff0f000000000000) + 0xb7ff000000000000 = 0 (64-bit overflow).
    When the credential check passes, the handler falls through to the
    magic-value checks. The magic triple (0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c)
    selects config table at 0xd9d5b360, mapping to slot (field0=2, field1=3,
    field2=5) at ctx offset 0x158.
- **[8] sceAgcDriverSuspendPointSubmitDirect()** — PASS (with credential bypass)
  - Ioctl 0xC010811C with 4-dword arg layout confirmed correct
  - **Key finding**: The suspend point handler at 0xffffffffd8f66ff0 uses
    the SAME credential check (0xd8e70400) and the SAME magic triple
    (0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c) as the queue create handler.
    When credentials pass, the magic triple selects the SAME config table
    (0xd9d5b360), mapping to the SAME slot (2,3,5) at ctx offset 0x158.
    Non-magic values like (1,0,0) would compute a different slot (0x64)
    and fail with 0x804C0001 (no queue at that slot).
  - **field3 constraint**: The tail-called function at 0xd8e57700 checks
    `(field3 >> queue->shift_amount) == 0` where shift_amount is read from
    queue+0x48. If field3 is non-zero and shift_amount is 0, returns EINVAL.
    Passing field3=0 works.
- **[9] _sceAgcDriverDestroyUserSpecialQueue()** — PASS
  - Queue destroyed successfully after suspend point
- **[10] sceAgcDriverSetWorkloadsActive/EndWorkload** — PASS
  - Sub-region carving from SceGnmDdid for workload tracking works

### Hardware-discovered bugs fixed
- PS5 memory type constants differ from PS4:
  - PS4: WB_ONION=0, WC_GARLIC=1, WB_GARLIC=3
  - PS5: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2 (type=1 fails on exploited PS5)
- PS5 VideoOut requires userId=0xFF, not 0
- PS5 VideoOut requires tiled mode (linear needs debug setting)
  - **Workaround:** Runtime patch of `libSceVideoOut.sprx` at offset 0x7e61
    (NOP the `je` instruction that rejects linear tiling without debug setting)
- PS5 direct memory: garlic searchEnd=0x300000000, alignment=0x200000
- `__ORBIS__` → `__PROSPERO__` (prospero toolchain defines __PROSPERO__)
- **psbc compute SH register offsets were wrong** — RSRC1/2/3 for compute
  shaders are at 0x212/0x213/0x228, NOT `pgm_lo + 2/3/4` (which are
  COMPUTE_DISPATCH_PKT_ADDR_LO/HI). Fixed with per-stage offset functions.
- **AgcShaderType enum encoding was wrong** — CS was 6, should be 0.
  Firmware expects: CS=0, PS=1, ES=2, VS=3, GS=4, HS=5, ES-alt=6, LS=7.
  Confirmed by sharpemu's `PatchShaderProgramRegisters`.

### agc_videoout.elf — PASS
- GPU credential bypass (cr_sceAuthId = 0x4801000000000000) — OK
- `sce_agc_initialize()` + `sce_agc_initialize_internal_memory()` — OK
- `sceAgcDriverNotifyDefaultStates(0)` — FAIL (0x80890001, non-blocking)
- `sceAgcDriverSetupAsyncGraphics(1)` — OK
- `sceVideoOutOpen(userId=0xFF)` — OK
- `sceVideoOutRegisterBuffers` (linear, with libSceVideoOut patch) — OK
- NOP DCB submission during flip loop — OK
- CPU-rendered SMPTE color bars displayed for 600 frames — OK
- Deployed via websrv (FTP upload + HTTP /hbldr launch) — OK

### agc_compute.elf — FULL PASS (100% GPU compute execution verified on hardware)
- GPU credential bypass + AGC init + VideoOut — OK
- libSceVideoOut.sprx runtime patch (NOP at 0x7e61) — OK
- Compute shader binary loaded (AgcShaderRecord magic=OK, type=CS(0)) — OK
- Shader code uploaded to GPU flexible memory pool — OK
- SH registers set: PGM_LO/HI (0x20C), RSRC1/2/3 (0x212/0x213/0x228), START_X/Y/Z (0x204), NUM_THREAD_X/Y/Z (0x207) — OK
- User data set: `s2` (buf_lo), `s3` (buf_hi), `s4` (total_pixels), `s5` (fill_color) matching RDNA2 disassembler — OK
- Compute Unit enabling: `COMPUTE_STATIC_THREAD_MGMT_SE0..SE3` (0x216, 0x217, 0x219, 0x21A) set to 0xFFFFFFFF — OK
- FW 5.50 primary + internal SH defaults applied via `apply_sh_defaults` — OK
- `sceAgcDriverSubmitDcb` with SET_SH_REG + DISPATCH_DIRECT — OK (0x00000000)
- **Output verification**: 2073600 / 2073600 pixels match `0xFF00FF00` (100% GPU rendered on real hardware) — OK
- Display flip — OK (GPU-rendered solid green frame visible on TV/display)

## Firmware Forward Compatibility

The internal `AgcDriverOps` dispatch layer is implemented. The stable public
`sceAgc*` / `sceAgcDriver*` symbols now dispatch through a private operations
table, with both the generic backend and the validated FW 5.50 direct Prospero
backend registered behind it. Runtime selection now queries
`sceKernelGetProsperoSystemSwVersion` before initialization and requires a
registered four-digit ABI key plus the requested capability flags. FW 5.50 maps to
`prospero-fw550-direct`; detection failure and all unknown versions fail closed
with `AGC_ERROR_NOT_SUPPORTED` before `/dev/gc` or a Sony module is touched.
Host tests cover callback routing, aliases, capability rejection, unknown
versions, and detector failure.

A module-specific Sony export resolver is also implemented with mandatory
symbol validation, recursion rejection, and ABI adapters. FW 5.50 hardware
confirms collision-free calls into the installed module, but its payload-context
submission path did not execute GPU markers, so it is excluded from automatic
selection. Installed-module probing also changed submission behavior across
later payload processes, so it must never be followed by a direct fallback in
the same boot session; see `analysis/sony_export_forwarding_550.md`.

The 2026-07-26 hardware selector run called the FW 5.50 direct backend, proving
the installed version query and exact registry match on the console. The direct
backend then returned `0x8089000B` during initialization in the console session
already contaminated by the earlier Sony-module probe. Full init/submit remains
inconclusive for that run and must be repeated only after a reboot; the older
clean-session FW 5.50 direct results below remain the validation baseline.

## Next RE Tasks

### Priority 1: Indexed vertex-buffer draw (hardware validated)

Compute dispatch, the first graphics draw, and RGB interpolants are fully
hardware-validated on real PS5 hardware. `agc_graphics.elf` executes the
gfx1013 NGG front program, rasterizes a smooth RGB triangle, and changes
1,036,796 of 8,294,400 pixels. Readback sampled eight distinct colors and
reported `Spatial color variation: PASS`; the RGB gradient was also confirmed
visually on the PS5 display.

The vertex stage now consumes a real interleaved binding instead of procedural
`gl_VertexIndex` arrays. The compiler lowers location 0 (`R32G32_FLOAT`, offset
0) and location 1 (`R32G32B32_FLOAT`, offset 8) against binding 0 with stride
20, then records the RADV vertex-descriptor-table user SGPR for runtime
patching. On hardware the table was bound through SH register `0x08c`, the
descriptor referenced three records at `0x205608000`, and the sample reported
`Interleaved buffer fetch: PASS` with the same exact triangle coverage and RGB
variation. The green background and RGB-gradient triangle were also confirmed
visually on the PS5 display.

Indexed drawing now uses a four-record vertex buffer with vertex 0 deliberately
placed outside the intended geometry and a bound `uint16_t` index buffer
containing `{1,2,3}`. The DCB programs `VGT_INDEX_TYPE`, `INDEX_BASE`, and
`INDEX_BUFFER_SIZE`, then issues `DRAW_INDEX_OFFSET_2`. Real gfx1013 readback
still reports exactly 1,036,796 changed pixels and eight sampled RGB colors,
proving that the GPU skipped the decoy vertex and consumed the index buffer.
The PS5 display visually confirms the indexed run as a green background with
an RGB-gradient triangle.
Texture and sampler binding are now hardware-validated on real gfx1013. The
pixel shader samples a 2x2 linear RGBA8 image through a GFX10.3 image resource
descriptor and a nonzero bilinear clamp sampler in RADV's 64-byte combined
descriptor layout. Descriptor set 0 is supplied through PS user SGPR 2
(`SPI_SHADER_USER_DATA_PS_2`, SH offset `0x00e`). Readback reports 1,036,800
rasterized pixels, eight distinct sampled colors, bilinear spatial variation,
and a live post-draw marker. The PS5 display visually confirms a dark-gray
background with a smoothly color-textured triangle.

Additional render-target format coverage is hardware-validated for linear
`R16G16B16A16_FLOAT`. The sample programs CB format `0x0c`, FLOAT number type
`7`, and standard component swap for a 1536x1536 offscreen target. Real PS5
readback reports 255,744 changed pixels against 255,456 predicted for the
equilateral triangle, eight distinct sampled FP16 colors, 112,198 opaque
samples, zero components outside `[0,1]`, and a live post-draw marker. A 1:1
CPU conversion to the registered RGBA8 display buffer was visually confirmed
as a centered, smoothly color-textured triangle with equal sides on a
dark-gray background.

The public gfx1013 color-target layer now resolves 14 typed linear presets:
R8, RG8, RGBA8, BGRA8, and RGB10A2 UNORM plus R16, RG16, RGBA16, R32, RG32,
RGBA32, and R11G11B10 FLOAT, plus RGBA8 and BGRA8 SRGB.
`agcGfx1013GetColorTargetFormatInfo` exposes each preset's
exact gfx103 CB format, number type, component swap, byte size, and compatible
SPI shader-export format; `agcGfx1013InitColorTarget` converts it into the
existing ABI-stable target state. The packet builder accepts only table-backed
encodings and rejects linear rows that violate the gfx103 256-byte pitch
alignment. All 14 encodings have exact 28-dword host fixtures. RGBA8/BGRA8
UNORM and SRGB, RGB10A2, R16, RG16, RGBA16 FLOAT, and R11G11B10 FLOAT have
PS5 hardware evidence; R8, RG8, R32, RG32, and RGBA32 await hardware
qualification.

Typed gfx1013 resource transitions now model render-target, compute-write,
copy-source/destination, shader-read, presentation, and host-read usage. Writer
transitions use the hardware-proven 10-dword `RELEASE_MEM + NOP` EOP sequence;
GPU consumers then receive the full eight-dword gfx103 `ACQUIRE_MEM` layout with
all GLI/GLM/GLK/GLV/GL1/GL2 visibility operations (`GCR_CNTL=0xC3B1`). Exact
fixtures cover release/acquire ordering, render-to-present, compute-to-copy,
copy-to-shader, present-to-render, explicit read-only no-ops, and atomic error
paths. The compute and graphics samples use typed host-read transitions without
changing their hardware-proven completion packet bytes. Acquire-bearing paths
remain host-encoded and Prospero-compiled but await real PS5 qualification.

Typed gfx1013 blend and depth/stencil state is host-complete. The blend builder
programs all eight MRT controls deterministically, packs per-target RGBA write
masks, and emits four blend constants in one atomic 19-dword group. The
depth/stencil builder maps the eight compare operations and eight standard
stencil operations to gfx103 values, supports independent front/back state,
reference/compare/write masks, depth writes, and depth bounds in one atomic
14-dword group. Full-stream fixtures lock register order and bit encodings;
invalid enums, masks, state dependencies, and short buffers emit nothing.
Depth-surface binding and real FW 5.50 blend/depth/stencil execution remain
separate hardware-validation milestones.

Multiple DCB submission in one process is hardware-validated on FW 5.50.
Separate `sceAgcDriverSubmitDcb` ioctls accepted both buffers but advanced
only the first GPU marker. The correct contract for related DCBs is one
descriptor-array frame through `sceAgcDriverSubmitMultiCommandBuffersDirect`.
Both `sceAgcDriverSubmitMultiDcbs` and
`sceAgcDriverSubmitMultiCommandBuffers` now preserve the array through that
path instead of looping over standalone submits. The FW 5.50 exploited-
payload-context graphics ring defers the final descriptor in each nr=0x02
submit until a later submit advances the ring. The Prospero backend therefore
appends a dedicated GPU-visible NOP IB trailer after all caller DCB/ACB
descriptors; the deferred descriptor is harmless and all caller work executes
in the current frame. The standalone `sceAgcDriverSubmitDcb` path now uses the
same frame-state operation and trailer so its caller DCB also executes in the
current submit. Vulkan-PS5 then qualified this standalone path twice with a
1,024-value compute dispatch and twice with an 18,432-pixel triangle on FW
`0x05500008`; every run reached its EOP label and deterministic readback oracle.
Two immediate public-wrapper deployments each completed
three unique-marker iterations with zero polling delay, then completed async
setup, queue operations, suspend submission, and workload begin/end without a
freeze.
Automatic per-submit `SUBMITDONE` synchronization is intentionally not used;
it froze the console during workload completion. See
`analysis/multi_dcb_submission_550.md`.

The compiler now assigns standalone vertex-stage user varyings to RADV
parameter-export slots before NGG lowering. The validated shader exports
`v_color` through parameter 0 with the `xyz` channel mask; without that link
metadata the pixel shader received constant ones and produced a white
triangle. Vertex/index buffers, textures, and advanced graphics stages follow.

Subtasks:
1. ~~Submit a compute dispatch and verify the GPU executes it.~~ ✅ Done
   (agc_compute.elf — GPU accepts DISPATCH_DIRECT DCB).
2. ~~Fix `sceAgcDriverNotifyDefaultStates`~~ ✅ Done
   (Fixed by correcting DDID allocation sizes: `AGC_DDID_PRIMARY_SIZE=0x41000`, `AGC_DDID_INTERNAL_SIZE=0xc000`. Returns `AGC_OK`).
3. ~~Verify compute shader pixel output~~ ✅ Done
   (100% VERIFIED ON HARDWARE: 2073600 / 2073600 pixels match `0xFF00FF00`!).
4. ~~Compile VS+PS via psbc~~ ✅ Done
   (Minimal GLSL VS+PS compiled via glslc → SPIR-V → psbc → AgcShaderRecord).
5. ~~Set up render target + graphics state~~ ✅ Done
   (CB_COLOR0, viewport, scissor, blend, primitive type, SPI_SHADER_POS/COL_FORMAT).
6. ~~Submit IT_DRAW_INDEX_AUTO~~ ✅ Done
   (DCB accepted by `sceAgcDriverSubmitDcb`, returns AGC_OK).
7. ~~Verify visual output~~ ✅ Done — the front-entry probe wrote
   `0x4E474721`, the post-draw WRITE_DATA marker wrote `0xDEADCAFE`, and the
   real ACO VS+PS path changed 1,036,800 render-target pixels to the
   fragment shader's magenta output.

#### Key findings from graphics draw call debugging (Phase 7)

These issues were discovered and fixed during hardware validation of the
graphics draw call. The final gfx1013 path now renders successfully.

1. **Non-contiguous register default groups corrupt GPU state.** Five
   register-default groups in `register_defaults_v8.c` have non-contiguous
   offsets but were being written as batch `SET_SH_REG`/`SET_CX_REG`
   packets (which assume contiguous offsets). The worst offender is group
   72 (128 CB_COLOR0 registers) with offsets like 0x318, 0x31b, 0x31c,
   0x31d, 0x31e, 0x31f, 0x321, 0x323... — writing these contiguously
   overwrites unrelated registers and causes a GPU hang. **Fix:** write
   each register individually with `register_count=1` (see
   `apply_sh_defaults_graphics` / `apply_cx_defaults` in `agc_graphics.c`).
   The 5 non-contiguous groups are: `_64` (16 regs), `_72` (128 regs),
   `_76` (160 regs), `_90` (3 regs), `internal_regs_21` (3 regs).

2. **Tile mode 0 is Depth_2DThin_64, NOT linear.** The `AgcTileMode` enum
   starts with depth tile modes (0-7). Tile mode 0 = `kAgcTileDepth_2DThin_64`.
   For a linear color render target, use `kAgcTileDisplay_LinearGeneral` (31).
   Setting `CB_COLOR0_ATTRIB.tile_mode_index = 0` for a color RT causes the
   CB hardware to interpret the surface as a depth buffer and not write
   color data. **Fix:** `CB_COLOR0_ATTRIB = 0x0000001F` (tile_mode_index=31).

3. **SPI_SHADER_COL_FORMAT (0x1C5) and SPI_SHADER_POS_FORMAT (0x1C3) are
   NOT in shader records or register defaults.** The AgcShaderRecord
   produced by psbc does not include these registers, and the FW 5.50
   register defaults do not set them. They default to 0 (no export),
   which means the PS does not export color and the PA cannot process
   vertex positions. **Fix:** set them manually in the DCB:
   - `SPI_SHADER_POS_FORMAT (0x1C3) = 1` (4_32_32_32_32 — vec4 position)
   - `SPI_SHADER_Z_FORMAT (0x1C4) = 0` (no Z export)
   - `SPI_SHADER_COL_FORMAT (0x1C5) = 1` (8_8_8_8 — RGBA8 color)
   - `CB_SHADER_MASK (0x08F) = 0x0F` (all RGBA channels to RT0)

4. **VGT_SHADER_STAGES_EN should be 0 (default) for VS+PS.** Setting
   `ES_EN` routes the vertex shader through the ES (export shader) stage,
   which is wrong for a type-3 (VS) shader. The default (0) means VS runs
   as VS. Do NOT set this register for a simple VS+PS pipeline.

5. **CONTEXT_CONTROL packet is required.** Same as the compute sample:
   opcode 0x28, 3 dwords, `LOAD_ENABLE_CONTEXT=0x80000000`. Without this,
   the CP may not load the context state from the default-state blobs.

6. **DB_Z_INFO must be explicitly disabled.** The default `DB_Z_INFO`
   (0x010) is `0x80000000` (FORMAT=x8_24 with some bits set), which
   enables the depth buffer. If no depth buffer memory is bound, the DB
   may discard all pixels. **Fix:** set `DB_Z_INFO = 0` and
   `DB_STENCIL_INFO = 0` to disable depth/stencil.

7. **CB_COLOR0_PITCH uses 8-element tiles for linear mode.** The
   `TILE_MAX` field (11 bits) is `(pitch_elements / 8) - 1`, not
   `(pitch_elements - 1)`. For 1920px: `(1920/8)-1 = 239 = 0x000EF`.
   The `SLICE` field (22 bits) is `(tiles_per_row * height) - 1`.

#### Resolved launch and raster issues

The CP marker originally executed after every draw while no shader marker
or color output appeared. The decisive gfx1013 finding was that NGG code is
launched from `SPI_SHADER_PGM_LO_ES` while its resources remain in the GS
register block. The compiler had placed real ACO code in the GS-back record
and a dummy `s_endpgm` in the GS-front record, so fusion installed the dummy
address in the executable ES program register. Swapping those code payloads
made the front-entry probe execute on hardware. The real fixture then still
faulted on an unbound debug push-constant pointer; removing that diagnostic
global store allowed normal NGG exports and rasterization.

Additional fixes applied since the last update (still black output):

8. **PS CX block re-enables depth after explicit disable.** The PS
   shader's CX register block writes `DB_DEPTH_INFO` (0x00F) = 0x0F and
   `DB_SHADER_CONTROL` (0x203) = 0x10 *after* the code disabled depth.
   With no depth buffer bound, depth testing discards all fragments.
   **Fix:** override `DB_DEPTH_INFO`, `DB_Z_INFO`, `DB_STENCIL_INFO`,
   `DB_SHADER_CONTROL`, and `DB_DEPTH_CONTROL` to 0 *after* the PS CX
   block is written.

9. **`SPI_PS_INPUT_CNTL_0` register offset was wrong.** The code wrote
   to `0x1B8` (`AGC_REG_SPI_BARYC_CNTL`) instead of the correct
   `0x191` (`AGC_REG_SPI_PS_INPUT_CNTL_0`). This prevented the PS from
   fetching its input. **Fix:** use `AGC_REG_SPI_PS_INPUT_CNTL_0` with
   `OFFSET=0` so PS input 0 reads from VS param export 0 (v_color).

10. **`VGT_SHADER_STAGES_EN` bit layout corrected.** The previous code
    set bit 8 (0x100) thinking it was `ES_EN`, but bit 8 is actually
    `dynamicHs` on RDNA2. The real `esEn` field is at bits [4:3]
    (EsReal=2 → 0x10), and `vsEn` is at bits [7:6] (VsReal=0). For a
    VS+PS pipeline with the VS shader at ES PGM (0x0C8, as psbc outputs),
    set `VGT_SHADER_STAGES_EN = 0x10` (EsReal). This does not kernel panic.

11. **`SPI_SHADER_COL_FORMAT` must match `CB_COLOR0_INFO` format.**
    Setting COL_FORMAT=4 (16_16_16_16) while CB_COLOR0_INFO is
    COLOR_8_8_8_8 (format 1) is a mismatch that can prevent CB writes.
    **Fix:** set `SPI_SHADER_COL_FORMAT = 1` (8_8_8_8) to match.

12. **`CB_COLOR0_ATTRIB2` (0x3B0) was missing.** This register packs
    `MIP0_HEIGHT` (bits [13:0]) and `MIP0_WIDTH` (bits [27:14]). Without
    it, the CB clips all writes to 0x0. **Fix:** set
    `CB_COLOR0_ATTRIB2 = ((height-1) & 0x3FFF) | (((width-1) & 0x3FFF) << 14)`
    and `CB_COLOR0_ATTRIB3 = 0`.

13. **Invalid partial NGG state removed.** `VGT_GS_OUT_PRIM_TYPE` uses a
    GS-output enum where triangles are `2`, not the input `TRILIST` value
    `4`. A plain VS record also has no GS/NGG `specials` block, so speculative
    `GE_CNTL` and GS-output programming was removed.

14. **`PA_CL_VS_OUT_CNTL` and `PA_CL_CLIP_CNTL` set to 0.** The previous
    `VS_OUT_CNTL=0x00010000` (USE_VTX_POINT_SIZE) and
    `CLIP_CNTL=0x00010000` (CLIP_DISABLE) may have been interfering with
    NGG rasterization. Set both to 0 (defaults).

#### Corrected hardware-validation candidate

Cross-checking the latest commits against KytyPS5 and sharpemu found four
additional state bugs: the `8_8_8_8` color format is `10` rather than `2`,
`CB_COLOR_CONTROL` had MODE=Disable instead of Normal, shader color-export
format `4` was incorrectly replaced with render-target format `1`, and the
UCONFIG-only `VGT_PRIMITIVE_TYPE` offset was also emitted as a context write.

The plain-VS workaround has been replaced by a compiler-generated gfx1013 NGG
path. `openagc-psbc --ngg-front` now performs Mesa RADV no-GS NGG lowering,
runs ACO, and emits fusion-compatible GS-front/GS-back records containing
compiler-derived resource registers, subgroup state, complete `specials`,
and semantic maps. The graphics sample relocates and fuses those records,
derives primitive/interpolant state through OpenAGC, binds executable ACO code
through `SPI_SHADER_PGM_LO_ES`, and keeps the GS-back record as the fused
state/resource container. Real-PS5 validation passes: the entry probe runs,
the CP survives the draw, and an interpolated RGB triangle covers 1,036,796
pixels with eight distinct colors sampled during readback.

#### Experimental approaches that caused a kernel panic (DO NOT RETRY)

1. **Mixing compute dispatch into a graphics DCB** — inserting
   `DISPATCH_DIRECT` (compute) into the same DCB as
   `IT_DRAW_INDEX_AUTO` caused a kernel panic. Keep compute and graphics
   in separate DCB submissions.
2. **Enabling RDNA2 NGG mode** — setting `GE_NGG_SUBGRP_CNTL=1` and
   `VGT_SHADER_STAGES_EN=0x8110` without a bound GS/NGG passthrough
   shader crashed the GPU. Do not enable NGG without proper NGG shader
   setup.
3. **Dual-binding ES and VS SH registers** — copying VS registers to
   both VS (0x048-0x04B) and ES (0x0C8-0x0CB) stages simultaneously
   caused instability. Use a single active vertex-processing stage.


### Closed background RE: PA debug and FRAME_OPEN

Both questions are resolved for FW 5.50. `sceAgcDriverGetPaDebugInterfaceVersion`
is an unconditional userspace permission stub returning `0x8A6D0001`, and
`FRAME_OPEN` (`0xC0088100`) is absent from the kernel ioctl dispatcher. Neither
is a missing initialization feature or a blocker for rendering.

### Priority 3: Game compatibility expansion

Dragon Quest VII Reimagined (`PPSA17942`) is a hardware-proven FW `0x0550`
backport and the fifth target in progress. Its executable imports 253 AGC
functions; 250 are covered and 3 remain after completing its FW 5.50 GetSize
imports, seven exact packet patchers, payload-range ABI, and primitive-state
update ABI, three constant driver-status queries, the corrected workload-stream
ABI, AGR multi-DCB status path, compatibility-SPRX WriteData patchers, and the
cursor-based ACB AtomicGds `_0900` packet builder.
The owner-management register/unregister exports match the intentional FW 5.50
`0x8A6C9018` userspace stubs.
The bundled compatibility-SPRX `sceAgcGetIsTrinityMode` export is covered with
its recovered one-byte output-pointer ABI and standard-PS5 false result; the
game caller ignores RAX and later reads the output byte.
The two executed shader-instrumentation exports and the AMM semaphore-memory
setup/get-label path are also implemented from the bundled SPRX. Semaphore
memory uses 16 KiB alignment and exposes 32-byte labels with exact state and
range errors.
The exact compatibility `ACQUIRE_MEM` engine-bit patcher and async
`WRITE_DATA` address, cache-policy, and destination patchers are implemented;
their wrong-packet path returns the recovered `0x8A6C000C` compatibility error.
The ACB/DCB marker APIs use their hardware ABI cursor signatures, preserve the
color word, distinguish set from push subcommands, and expose all four
explicit-length marker-span variants.
The compatibility GS primitive-payload query uses the recovered shader-record
CX count at offset `0x5B` and register `0x1C2` mode test.
All five compatibility submit-validation controls match the bundled driver's
six-byte `0x8A6C1000` stubs and leave caller state untouched.
Eight resource/GDS exports match the bundled FW 5.50 driver's six-byte
`0x8A6C9018` stubs with recovered SysV signatures, and the three capture
controls return the exact `0x8A6C1000` status. All preserve caller outputs.
The first compute API promotion is implemented: public gfx1013 state now
validates and atomically emits the hardware-proven context-control, resource
limits, thread dimensions, program/RSRC registers, user SGPRs, and direct
dispatch packets. FW 5.50 SH defaults also have a compute-safe emitter that
sets packet shader type internally, so applications no longer need to mutate
emitted headers. Exact host fixtures cover the packet stream and atomic failure.
The compute sample now constructs no raw PM4 or register packets: it uses the
typed compute path and public diagnostic/barrier builders. FW 5.50 websrv
validation passed with 2,073,600/2,073,600 pixels matching `0xFF00FF00`, both
post-submit markers visible, and a completed VideoOut flip. The fixed 200 ms
delay has been replaced by an ordered public `ACQUIRE_MEM` plus `WRITE_DATA`
completion fence with a bounded 200 ms timeout; hardware reached it after 1 ms.

The hardware-sample PM4 audit is complete. Existing gfx1013 VS/PS,
tessellation, render-target, viewport, scissor, and baseline draw helpers are
reusable. Blocking homebrew API gaps are a typed compute binder, gfx1013
buffer/image descriptor serialization and table binding, context-control
construction, and bounded GPU completion. See
`analysis/sample_pm4_public_api_audit.md` for the operation matrix and ordered
vertical-slice exit criteria.

Hardware-ready resource encoding is now public. The gfx1013 buffer encoder
produces the proven 16-byte structured VBO descriptor, while the image encoder
produces the proven eight-dword linear 2D layout. The combined descriptor fixes
the sampler at dword 8 in a 64-byte table entry. Static layout assertions,
exact-word fixtures, validation, and atomic output failure cover all three.
Public resource-table binding also replaces application scans of shader SH
register arrays. Callers provide placeholder/address pairs; OpenAGC resolves
all VBO and descriptor-set placeholders, rejects missing/duplicate/unaligned
tables before cursor mutation, and emits the correct graphics or compute packet
type from the shader record.
The reusable baseline draw now includes primitive and pixel resource-table
bindings in its atomic preflight and emission order. Shader placeholders can no
longer overwrite a table address written earlier by an application.
The Wave32 VS/PS binder now patches the fused primitive front program's
`OPENAGC_NEXT_STAGE_PC_PLACEHOLDER` from a typed back-program address, with
alignment validation and an exact host fixture.

`agc_graphics.c` no longer constructs raw PM4, packs resource descriptors, or
scans shader registers. The baseline FW 5.50 websrv run reached its completion
fence after 4 ms and passed FP16 coverage, bounds, color, opacity, and VideoOut
checks. Tessellation reached its fence after 9 ms, changed 24 offchip words and
four factor-ring words, and passed the same FP16 checks. Tessellation ring and
context setup is now fully typed: OpenAGC builds the 128-byte ring table and
emits all four UC plus five CX registers. The sample-local
`gfx1013_tess_ring.h` was deleted. Revalidation retained the 9 ms fence, 24
offchip changes, four factor changes, and passing FP16 output.

Subnautica (`PPSA02453`) content `01.022.394` passes the strict analyzer with
63/63 AGC imports covered. The exact executable hash, SDK `0x0400` metadata,
five versioned wrappers, and the distinction between static API coverage and
FW `0x0550` runtime compatibility are recorded in
`analysis/subnautica_ppsa02453_audit.md`.
Dragon Quest VII Reimagined coverage is now 252/253. Its FW 11.60
`dbOlWdppb4o` and `vieBRwlh1Lw` imports are implemented as the recovered
create-style and update-style enhanced interpolant mappers. Exact host fixtures
cover all four descriptor modes, matched and unmatched semantics, identity-tail
fill, and update-tail preservation. These helpers are CPU-only; game-runtime
validation is still pending.
`sceAgcDriverFindResourcesPublic` remains intentionally unresolved because its
constant stub does not prove the public prototype.
The cross-version audit covers every available driver from FW `1.00` through
`12.70`; all return `0x8A6C9018` with the same six-byte body. Dragon Quest and
FW `0x0550` system applications `NPXS40099` and `NPXS40074` expose only dead
import thunks with no code or relocated-data callers. The full negative
evidence and unblock criteria are recorded in
`analysis/find_resources_public_audit.md`.
Three NID-specific `IT_INDIRECT_BUFFER` field patchers are implemented directly
from matching FW 5.50 and 11.60 SPRX bodies. Their exact four-argument ABIs,
three packet-relative offsets, field masks, reserved-bit preservation, and
`0x8A6C000C` rejection paths have host fixtures; their official names remain
unknown and are not guessed.
The NID-specific `-KRzWekV120` builder preserves the 11.60 four-argument
`SET_INDEX_SIZE` form and its extra control bit. The NID-specific
`zARR5aCmkoY` builder preserves the full 12-argument atomic-GDS ABI and exact
11-dword packet layout. These coexist with the older named source-compatible
entry points rather than silently changing their public signatures.
The NID-specific `qj7QZpgr9Uw` export now implements its firmware-proven
two-argument ABI and exact `5/27/27/32`-dword operation layouts. The common
22-dword synchronization block is the recovered `COND_EXEC` + `RELEASE_MEM` +
`ATOMIC_MEM` sequence. Prospero provides its `{0,1}` synchronization label and
flattened v8 CX restore list from GPU-visible `SceGnmMisc`; all four layouts and
short-buffer atomicity are host-tested, while FW `0x0550` execution remains a
separate hardware promotion gate.
AcquireMem size
follows the firmware's
title-workaround mode rather than the emulator's fixed 32-byte path. Dragon
Quest is not yet counted in the four-title, 100%-covered corpus. Astro's
Playroom remains explicitly excluded.

Continue analyzing game binaries to identify and implement remaining missing
AGC functions. The completed corpus covers 4 games and 73 unique AGC functions
at 100%; see `analysis/game_agc_usage.md`. Dragon Quest is tracked separately
until its remaining imports are implemented.

### Blocked: Remaining 12+32 unknown NIDs

12 SPRX NIDs are not in any known database (aerolib.csv 154k entries,
flatz ps5_symbols.txt, reference emulator, ps5-openagc, FW 3.20 genstubs
all exhausted). 32 TSV entries are unverified placeholders. These are
blocked on new external data sources — no actionable work without new
NID databases or firmware dumps.

## Game Compatibility

### Coverage across 4 game binaries

| Game | Title ID | AGC imports | Implemented | Missing |
|------|----------|-------------|-------------|---------|
| Joe & Mac Caveman Ninja | PPSA02801 | 70 | 70 | 0 |
| PPSA09076 (backport) | PPSA09076 | 69 | 69 | 0 |
| PPSA03157 | PPSA03157 | 58 | 58 | 0 |
| Subnautica 01.022.394 | PPSA02453 | 63 | 63 | 0 |

**Total unique AGC functions across all 4 games: 73**
**All 73 implemented.** 100% observed-import coverage.

### Joe & Mac Caveman Ninja (PPSA02801, v01.003)
- **Engine:** Unity IL2CPP
- **SDK:** PS5 5.00
- **AGC imports:** 70 total (61 from libSceAgc, 9 from libSceAgcDriver)
- **Analysis:** `analysis/game_agc_usage.md`

### PPSA09076 (01.000.000 backport)
- **AGC imports:** 69 total (60 from libSceAgc, 9 from libSceAgcDriver)
- Same import set as Joe & Mac minus `sceAgcInit`, `sceAgcGetDataPacketPayload`

### PPSA03157
- **AGC imports:** 58 total (52 from libSceAgc, 6 from libSceAgcDriver)
- Smallest import set; no `sceAgcAcbJump`, `sceAgcAcbCopyData`,
  `sceAgcAcbPopMarker`, `sceAgcAcbPushMarker`, `sceAgcCbSetUcRegistersDirect`,
  `sceAgcDebugRaiseException`, `sceAgcSetNop`, `sceAgcDriverGetEqContextId`,
  `sceAgcDriverRegisterOwner`, `sceAgcDriverRegisterResource`
- Uses `sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate` (unique to this game)

### Implemented missing functions

All missing functions across the 3 games are now implemented:

1. **libSceAgcDriver stubs** — `RegisterOwner`/`RegisterResource` return
   `0x8a6c9018` (not supported on non-dev hardware per SPRX).
2. **Non-Direct driver variants** — `SetTFRing`/`SetHsOffchipParam` are
   wrapper versions that delegate to the Direct variants.
3. **DCB packet builders** — `AcquireMem`, `CopyData`, `Jump`,
   `ResetQueue`, `SetIndexCount`, `SetIndexSize`, `SetNumInstances`,
   `StallCommandBufferParser`, `DrawIndex`.
4. **CB register setters** — `SetShRegisterRangeDirect`,
   `SetUcRegistersDirect`.
5. **Indirect register patchers** — 6 functions for Sh/Cx/Uc register
   indirect write patching.
6. **Utility functions** — `SetNop`, `DebugRaiseException`,
   `GetDataPacketPayload`, `CreateShader`, `CreatePrimState`.
7. **Wrapper functions** — `sceAgcInit`, `sceAgcSuspendPoint`,
   `sceAgcGetRegisterDefaults2` / `2Internal`.
8. **DmaData Src patcher** — `sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate`
   (SPRX-confirmed: checks raw DMA_DATA opcode 0x50, patches cmd[2..3]).
   Also fixed `sceAgcDmaDataPatchSetDstAddressOrOffset` to match SPRX
   (now accepts both raw DMA_DATA and NOP-wrapped formats).

## ps5-openagc Audit

The sibling `ps5-openagc` project is **NOT proven working on hardware** and
contains known errors. It was used for initial NID mapping cross-reference
only. All ioctl layouts and struct sizes in openagc have been independently
verified from SPRX/kernel disassembly.

**Confirmed wrong in ps5-openagc:**
- `FRAME_OPEN` (nr=0x00) — claimed valid, but kernel returns `EINVAL`
  (hardware-confirmed)
- Queue create — used nr=0x2a (4-byte), real SPRX uses nr=0x21 (64-byte RW
  with magic tokens)
- Queue destroy — used nr=0x2b (4-byte), real SPRX uses nr=0x0e (12-byte RW)
- VMID mask — `0x000FFFFF00000000` (transcription error), correct is
  `0x000FFFFFFFFFFFFF`
- Memory type constants — ps5-openagc had WB_ONION=0, WC_GARLIC=1, WB_GARLIC=2
  (wrong for PS5). Correct PS5 values: WB_ONION=1, WC_GARLIC=3, WB_GARLIC=2
  (hardware-confirmed)
- Internal memory region sizes — 6 of 9 sizes were wrong. SPRX uses
  `sceKernelMapNamedSystemFlexibleMemory` (not `sceKernelAllocateDirectMemory`)
  with type=0x33. Correct sizes from SPRX disassembly:
  SceGnmGpuInfo=1MB, SceGnmDdid=1008KB, SceGnmEopFifo=240KB,
  SceGnmCwsr=16MB, SceGnmACQRB=1920KB (all were 4KB-64KB in ps5-openagc).
  See `analysis/sprx_agc_driver_internal_mem_disasm.md`.
- Queue create ioctl arg layout — field order at offsets 0x10-0x28 was
  completely wrong. Correct layout from SPRX: pipe_id at 0x10, caller_arg
  at 0x18, mmio_base at 0x20, queue_id at 0x28. Ring buffer is carved
  from EOP FIFO base + 0x39000, not separately allocated.

## Typed gfx1013 depth-surface binding

The companion gfx1013 `64KB_Z_X` layout query is hardware-validated for the
single-level D32 FW `0x0550` gate and host-complete for its wider matrix. It reports
separate depth/stencil plane pitch, padded height, block geometry, mip-tail
entry, 64 KiB alignment, slice size, and allocation size for D16, D32, S8,
array layers, mip chains, and 1x-8x samples. Checked 64-bit sizing covers the
largest bindable layout without truncation. The depth hardware sample now uses
the query instead of a fixed 16 MiB reservation.

The companion typed HTILE layout is hardware-validated for non-RB+ gfx1013
`64KB_Z_X`. It reports metadata pitch, padded height, block geometry,
alignment, per-layer slice size, full allocation size, and packed mip-tail
accounting. Address-pipe count remains explicit; FW `0x0550` hardware validates
the sample's eight-pipe layout. `agcGfx1013SetDepthSurface` now emits
`DB_HTILE_SURFACE.PIPE_ALIGNED`, and the sample initializes depth-only metadata
to gfx10.3 uncompressed `0xfffc000f` rather than zero.

The first follow-up gate is hardware-validated on FW `0x05500008`.
`agc_depth_stencil.elf` uses separate typed D32/S8 `64KB_Z_X` allocations,
1x sampling, and no HTILE. It exercises front-face compare masks, write masks,
and `REPLACE 0x5a`, then requires deterministic color/depth output and raw S8
containing only zero and the replacement value. Exact layout and packet
fixtures are host-covered. The real-console curl/websrv run produced 256,608
`0x5a` bytes, 2,364,832 zero bytes, no other stencil values, all depth/color
checks, and 1,800/1,800 completed flips. The screen showed the expected green
and red triangles without a hang or kernel panic.

The second follow-up gate is hardware-validated on FW `0x05500008`.
`agc_depth_msaa.elf` uses typed 4x RGBA8 `64KB_R_X` and D32 `64KB_Z_X`
surfaces with stencil and HTILE disabled, then shader-resolves all four
fragments into the 1x VideoOut buffer. The image descriptor undoes the source
`ALT` red/blue storage, and the fixture shader composites resolved coverage
over dark gray. Repeated websrv runs accepted the 5,131-dword DCB, reached all
stage and completion markers, found 127,818 exact green and 127,818 exact red
pixels, retained all three raw D32 classes, and completed 1,800/1,800 flips.
The captured framebuffer showed the expected resolved green/red triangles on
dark gray without a hang or kernel panic.

The third follow-up gate is hardware-validated on FW `0x05500008`.
`agc_depth_htile.elf` enables pipe-aligned HTILE for 1x D32 while stencil,
MSAA, expclear, CMASK, FMASK, and DCC remain disabled. Its 2,604-dword DCB
reached the fence in 1 ms, all four stage markers and exact green/red logical
depth outcomes passed, 18,013 of 49,152 metadata words changed from
`0xfffc000f`, and VideoOut completed 1,800/1,800 flips. Compressed raw D32 is
informational until a decompression path exists. The physical display showed
green and red triangles on dark gray without a hang or panic.

The fourth follow-up gate is hardware-validated on FW `0x05500008`.
`agc_depth_htile_ops.elf` uses the same isolated depth-only HTILE surface and
adds full-surface decompression and resummarization raster passes. The public
`agcGfx1013SetHtileOperation` builder emits a three-dword
`DB_RENDER_CONTROL` write for neutral (`0x0000`), depth decompression
(`DEPTH_COMPRESS_DISABLE | DECOMPRESS_ENABLE`, `0x1040`), or depth
resummarization (`RESUMMARIZE_ENABLE`, `0x0010`). Exact fixtures cover all
three encodings, invalid modes, short-buffer rejection, and atomic cursor
behavior. The sample disables ordinary color/depth writes, places typed DB
release/acquire transitions between modes, and restores neutral state.

The real-console curl/websrv run accepted the 2,695-dword DCB, reached all
four stage markers and the completion fence in 1 ms, recovered exact raw D32
counts of 909,792 clear, 128,304 near, and 128,304 far words, retained 128,304
green and 128,304 red pixels, and found 4,226 non-initial HTILE words after
resummarization. VideoOut completed 1,800/1,800 flips without a hang, reset,
or kernel panic.

The fifth follow-up gate is hardware-validated on FW `0x05500008`.
`agc_depth_expclear.elf` uses depth-only HTILE with the canonical depth-one
clear word `0xfffffff0`. `agcGfx1013SetDepthExpclear` emits an atomic
three-dword `DB_DEPTH_CLEAR` write and rejects values other than 0.0 or 1.0;
exact host fixtures cover both encodings and failure atomicity. The sample
sets `DB_Z_INFO.ALLOW_EXPCLEAR`, initializes the raw D32 plane to NaNs, and
omits the full-surface initialization draw, so all untouched logical depth
comes only from HTILE metadata.

The real-console curl/websrv run accepted the 2,695-dword DCB and reached its
fence in 1 ms. After typed decompression, raw D32 contained 918,432 exact 1.0,
128,304 near, and 128,304 far words. Color readback retained 128,304 green and
128,304 red pixels, all 49,152 HTILE words differed from the original clear
encoding after resummarization, all markers passed, and VideoOut completed
1,800/1,800 flips without a hang, reset, or kernel panic. The physical display
showed green and red triangles on a dark-gray background.

The sixth follow-up gate is hardware-validated on FW `0x05500008`.
`agc_depth_stencil_htile.elf` binds separate D32 and S8 `64KB_Z_X` planes to
one pipe-aligned HTILE allocation initialized with the gfx10.3 combined
uncompressed word `0xfffff30f`; expclear and MSAA remain disabled. The typed
combined decompression operation emits `DB_RENDER_CONTROL=0x1060`
(`STENCIL_COMPRESS_DISABLE | DEPTH_COMPRESS_DISABLE | DECOMPRESS_ENABLE`).
Host fixtures lock this encoding alongside depth-only decompression,
resummarization, and neutral restoration.

The real-console curl/websrv run accepted the 2,695-dword DCB and reached its
fence in 1 ms. Raw D32 contained 909,792 clear, 128,304 near, and 128,304 far
words. Raw S8 contained 2,364,832 zero bytes, 256,608 replacement `0x5a`
bytes, and no other values. Color readback retained 128,304 green and 128,304
red pixels, all 49,152 HTILE words changed after resummarization, every marker
passed, and VideoOut completed 1,800/1,800 flips without a hang, reset, or
kernel panic.

An earlier diagnostic `COPY_DATA` read of global register `0x13de`
(`GB_ADDR_CONFIG`) is permanently excluded. FW logged `GPU Bad packet error:
Privilege reg` for the game VMID, stopped the process, and automatically reset
the graphics rings and microcode. Baseline D32 passed after recovery. User
graphics queues must not read privileged global registers.

Host implementation is complete for typed gfx1013 depth-surface memory state.
`agcGfx1013SetDepthSurface` emits a deterministic 27-dword packet stream for
`DB_DEPTH_VIEW`, `DB_HTILE_SURFACE`, `DB_HTILE_DATA_BASE`, `DB_DEPTH_SIZE_XY`, `DB_Z_INFO`,
`DB_STENCIL_INFO`, four depth/stencil read/write bases, and their five high
address fields. Supported typed combinations are D16, D32 float, S8, D16+S8,
and D32 float+S8, with gfx103 swizzle modes, mip/layer selection, 1x through
8x samples, read-only aspects, and optional HTILE/expclear controls.

Validation rejects zero, unaligned, or wider-than-48-bit required addresses;
unused aspect addresses; invalid extents, views, samples, and flags; ambiguous
HTILE state; unsupported formats; and short command buffers without advancing
the cursor. The stale gfx103 `DB_Z_INFO[8:4]` tile-mode-index name was corrected
to the hardware-defined `SW_MODE` field.

The baseline D32, isolated stencil, isolated 4x MSAA, compressed HTILE, typed
HTILE decompression/resummarization, depth-only expclear, combined
stencil/HTILE, and combined stencil/HTILE expclear PS5 hardware gates are
complete on FW `0x05500008`. Combined expclear is enabled after depth-only,
stencil-only, and both-aspect cases each passed twice with exact metadata,
D32/S8, fence, and 1,800-flip oracles; see
`analysis/fw550_combined_expclear_qualification_20260727.md`.

`samples/hw_test/agc_depth.elf` hardware-validates a dedicated baseline-NGG
mode with an uncompressed D32-only
64KB-Z-X surface, HTILE disabled, deterministic depth initialization and
pass/fail geometry, four post-draw markers, an EOP completion marker, and both
color and raw-depth readback checks. Its expected screen and curl/websrv launch
procedure are documented in `samples/hw_test/DEPTH_VALIDATION.md`. The FW
`0x05500008` run produced all four stage markers, 128,304 green and 128,304 red
pixels, the expected raw D32 values, and 1,800/1,800 completed flips without a
hang or kernel panic. The intentionally separated triangles are tall and
narrow; their shorter bases are expected fixture geometry.

Explicit depth/stencil resource usages are host-complete. A transition away
from `DEPTH_STENCIL_WRITE` emits gfx1013 `FLUSH_AND_INV_DB_META` (`0x2c`), then
`FLUSH_AND_INV_DB_DATA_TS` (`0x2b`), followed by the existing GCR acquire when
the destination is another GPU usage. `DEPTH_STENCIL_READ` is modeled as a
read-only usage. Exact fixtures lock the 20-dword DB write-to-read stream, the
12-dword DB write-to-host stream, read-to-read no-op behavior, and atomic short
buffer rejection. `agc_depth.elf` now uses the typed DB transition in addition
to a separate color-target transition before CPU readback. Hardware execution
is covered by the passing FW 5.50 depth, HTILE, and expclear gates.

## Non-Goals For Current Milestone

- No firmware blobs or proprietary microcode are embedded.
- No claim of official SDK drop-in completeness.
- Graphics draw calls are hardware-validated for the current gfx1013 no-GS
  NGG VS+PS sample with linear RGBA8, R16_FLOAT, RG16_FLOAT, and
  `R16G16B16A16_FLOAT` color targets.
  This does not yet claim all color/depth formats, compression, complete
  tessellation, geometry-shader, mesh-shader, or game-wide compatibility.
# Installable SDK package

The OpenAGC 0.1.0 SDK packaging goal is complete. Generic and Prospero builds
install the public headers, platform-specific `libopenagc.a`, host-native
`openagc-psbc`, license, README, and relocatable `OpenAGCConfig.cmake` metadata.
Downstream projects consume `OpenAGC::openagc` and `OpenAGC::psbc` through
`find_package(OpenAGC CONFIG REQUIRED)` and may compile SPIR-V with
`openagc_compile_shader()`.

Validation completed on 2026-07-27:

- Clean generic configure/build and the complete CTest suite pass.
- Generic install is consumed by a separate CMake project and links/runs.
- The installed generic compiler converts the hardware compute SPIR-V fixture.
- Prospero configure/build/install passes with the ps5-payload-sdk toolchain.
- A separate Prospero consumer finds the installed package through
  `OpenAGC_DIR`, compiles and links against the installed archive, and invokes
  the installed host compiler during the cross build.
- CPack generates `openagc-0.1.0-generic.tar.gz` and
  `openagc-0.1.0-prospero.tar.gz`.

## Gfx1013 4x MSAA hardware validation

Host implementation, Prospero compilation, and FW `0x05500008` hardware
validation are complete.

The host gate now enforces the 64 KiB base alignment returned by the R_X/Z_X
layout queries, programs tiled color pitch from the padded layout rather than
the logical width, and keeps the default non-depth hardware sample build
independent of the optional MSAA allocation.

The typed 29-dword sample-state builder programs gfx1013 4x AA with
`PA_SC_AA_CONFIG=0x2020c002`, `DB_EQAA=0x00002202`, standard DX sample
locations `0xe62a62ae`, centroid priority `0x3210321032103210`, and full
coverage masks. The color binder programs log2 sample/fragment fields and
`64KB_R_X`; typed layout fixtures lock a 1920x1080 RGBA8 4x surface to
1920x1088 and 33,423,360 bytes. D32 4x allocation continues through the
existing `64KB_Z_X` layout API.

Gfx10.3 does not support the legacy fixed-function `CB_RESOLVE` mode. The new
resolve wrapper therefore transitions the 4x image to shader-read, binds a 1x
destination frame, restores 1x raster state after register defaults, and runs
a caller-supplied fullscreen draw. `agc_depth_msaa.elf` supplies a psbc-built
`sampler2DMS` fragment shader that averages samples 0-3 into the VideoOut
buffer. Its descriptor compensates `ALT` red/blue storage and the fixture
shader composites resolved coverage over dark gray. Stencil, HTILE, expclear,
CMASK, FMASK, and DCC stay disabled.

The preparation host result was 3878 passed, 0 failed. Both the Prospero
library and `samples/hw_test/agc_depth_msaa.elf` cross-built without warnings.
On 2026-07-27, repeated FW `0x05500008` websrv runs passed all marker, exact
color, raw-depth, visual, and responsiveness checks with 1,800/1,800 flips.
