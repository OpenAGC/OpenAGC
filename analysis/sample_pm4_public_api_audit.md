# Sample PM4 to Public API Audit

## Purpose

OpenAGC is a GPU API for homebrew applications and games running on a
jailbroken PS5. The FW 5.50 hardware samples prove command sequences, but an
application should not need to copy them or know raw gfx1013 registers.

This audit separates reusable public functionality from sample-only PM4,
platform integration, and diagnostics. Retail binaries remain ABI evidence;
they do not determine implementation order.

## Coverage matrix

| Area | Current coverage | Classification | Required action |
| --- | --- | --- | --- |
| Driver lifecycle and submission | Public initialization, internal-memory, defaults, queue, and DCB APIs | Reusable | Later add a simpler application lifecycle |
| Command-buffer allocation | `SceAgcCb`, `agcCbAllocDwords`, typed packet builders | Reusable escape hatch | Retain for research and unsupported state |
| Context initialization | Samples hand-emit `CONTEXT_CONTROL` | Missing | Add typed builder and FW 5.50 fixture |
| Compute binding | Samples program PGM, RSRC, thread, and USER_DATA registers | Missing, blocking | Add gfx1013 compute state, validation, binder, and dispatch wrapper |
| Graphics VS/PS binding | Wave32 validation and bind APIs | Reusable, hardware-proven | Preserve and extend |
| Color target, viewport, scissor | Typed gfx1013 setters | Reusable, hardware-proven | Expand formats and state incrementally |
| Draw | Baseline auto-index draw and low-level indexed/indirect packets | Partial | Add typed indexed and indirect draw paths |
| Buffer descriptors | Abstract setters do not serialize the raw gfx1013 SQ descriptor used by samples | Partial, blocking | Add explicit gfx1013 encoding and fixtures |
| Texture descriptors | Abstract texture state does not serialize the sample's image descriptor | Partial, blocking | Add explicit image descriptor encoding and fixtures |
| Samplers | Four-dword sampler helper works in the sample | Reusable | Validate and pair with descriptor tables |
| Descriptor tables | Samples lay out tables and patch shader placeholders manually | Missing, blocking | Add typed table layout and resource binding |
| Tessellation rings | Driver registration and tess binder exist; table construction is sample-local | Partial | Promote ring descriptor/table construction |
| Barriers | `sceAgcDcbAcquireMem` exists; samples duplicate raw packets/constants | Partial | Use public builder; add a documented visibility helper if required |
| Diagnostic GPU writes | `sceAgcDcbWriteData` exists; samples duplicate it | Reusable but unused | Replace sample packets and fixture exact proven form |
| Completion | Samples sleep 200 ms; low-level suspend points exist | Partial, blocking | Add bounded submit/wait or the smallest missing fence primitive |
| Firmware defaults | Graphics helper is explicitly V8/FW 5.50-shaped | Partial | Select defaults through four-digit firmware profiles |
| Shader records | Accessors/binders exist; samples parse bounds and upload manually | Partial | Add bounds-aware views and an upload contract |
| VideoOut and presentation | Hardware-sample integration | Platform-only | Keep outside the command API and document boundary |
| Direct/flexible memory allocation | Application-owned | Platform-only | Document domains, alignment, lifetime, and visibility |
| PM4 dump/audit | Sample diagnostics | Intentional sample code | Keep outside the public API |

## Sample findings

### `agc_compute.c`

The sample manually emits `CONTEXT_CONTROL`; applies SH defaults and edits bit 0
of already-emitted headers; programs compute resource limits, thread dimensions,
PGM address, RSRC1/2/3, and USER_DATA; and encodes buffer/push data into fixed
SGPRs. It also hand-emits `WRITE_DATA` and `ACQUIRE_MEM`, despite public builders,
and uses a fixed sleep instead of observable completion.

The public API must own shader-type selection and the gfx1013 register ABI.
Applications should provide a shader record, resources, push data, and dispatch
dimensions, never edit PM4 headers.

### `agc_graphics.c`

The sample already uses the hardware-proven Wave32 VS/PS and tessellation
binders, color-target, viewport, scissor, target-mask, defaults, and baseline
draw APIs. Remaining reusable sample-only work is:

- `CONTEXT_CONTROL`;
- raw vertex-buffer and image descriptors;
- descriptor-table layout and shader-placeholder patching;
- tessellation ring descriptors, slots, sizes, and register state;
- typed raster, depth/stencil, blend, indexed, and indirect draw state;
- duplicate raw diagnostic/barrier packets; and
- completion based on a fixed sleep.

The PM4 parser/dumper, CPU preview, VideoOut setup, and presentation loop are
diagnostic or platform integration and remain outside the core library.

### Other hardware samples

`agc_init.c`, `agc_videoout.c`, and emulator-facing samples primarily exercise
public lifecycle, queue, defaults, and submission APIs. VideoOut and color-bar
samples reveal integration requirements, not missing PM4 builders.

## Prioritized implementation roadmap

### Goal 1: Reusable compute vertical slice

Status: command construction and FW 5.50 hardware validation are complete.
`agc_compute.c` now uses public APIs for defaults, binding, dispatch,
diagnostic writes, cache visibility, NOP, and submission. Bounded completion
still replaces the temporary 200 ms wait before the full goal is closed.

1. Add a typed `CONTEXT_CONTROL` builder with an exact FW 5.50 fixture.
2. Add `AgcGfx1013ComputeState` validation and binding for Wave32 program
   address, RSRC registers, thread dimensions, user SGPRs, and resource limits.
3. Make builders encode compute packet headers; remove caller header mutation.
4. Add typed dispatch state while preserving the low-level dispatch builder.
5. Replace raw sample `WRITE_DATA` and `ACQUIRE_MEM` with public builders and
   fixture the exact hardware-proven packets.
6. Provide bounded completion with errors/timeouts using existing driver
   primitives or the smallest missing fence primitive.
7. Convert `agc_compute.c` to public APIs and repeat the FW 5.50 websrv test.

Exit criterion: a homebrew can bind a psbc compute shader and storage buffer,
push data, dispatch, wait with a timeout, and observe output without raw register
numbers or PM4 header edits.

### Goal 2: Application-usable resources

1. Define gfx1013 buffer and image descriptor encoders with layout checks and
   host fixtures.
2. Define descriptor-table builders for buffers, textures, and samplers.
3. Resolve compiler resource placeholders without application scans of shader
   register arrays.
4. Reconcile `AgcRenderTarget` and `AgcGfx1013ColorTargetState` into one
   documented ownership/encoding model.
5. Document address alignment, memory domains, lifetime, and cache visibility;
   allocation remains application-owned.

Exit criterion: an application binds vertex, storage, texture, and sampler
resources without constructing SQ descriptor words.

### Goal 3: Reusable graphics vertical slice

1. Add typed indexed and indirect draw state above existing packet builders.
2. Add raster, depth/stencil, blend, and primitive state needed by normal render
   passes.
3. Select register defaults through four-digit firmware profiles.
4. Add bounds-aware shader-record views and a caller-owned upload contract.
5. Convert the baseline sample to public APIs and repeat the full FW 5.50 websrv
   graphics test.

Exit criterion: a homebrew uploads VS/PS records, binds resources and a target,
sets fixed-function state, draws indexed geometry, synchronizes, and presents
without raw PM4.

### Goal 4: Tessellation and NGG geometry

1. Promote the sample tessellation ring descriptor, table layout, sizing, and
   state setup into gfx1013 public helpers.
2. Stabilize pass-through NGG geometry before broader geometry expansion.
3. Add tessellation-plus-geometry fixtures and repeated-draw FW 5.50 tests.

Exit criterion: applications use tessellation and NGG geometry through typed
OpenAGC state without importing sample headers or register constants.

### Goal 5: Broaden the homebrew API

After those vertical slices are stable, add render-target formats, multiple
targets, depth surfaces, sampling variants, multi-pass synchronization, reusable
command buffers, and complete lifetime/error documentation. Use retail imports
only to find ABI omissions that also improve the homebrew API.

## Low-level escape hatch

Direct register and packet builders remain public for research and bring-up,
but are not the ordinary application path. Every hardware-proven sequence used
by normal compute or graphics needs a typed API, validation, atomic cursor
failure, a host fixture, and an FW 5.50 hardware result.

## Audit limitations

This source audit changes no packet encoding and ran no build or hardware test.
Implementation goals add host fixtures first, then use curl/websrv for FW 5.50
validation. Firmware blobs and retail binaries remain external references.
