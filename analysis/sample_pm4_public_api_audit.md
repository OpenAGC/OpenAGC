# Sample PM4 to Public API Audit

## Purpose

OpenAGC is a GPU API for homebrew applications and games running on a
jailbroken PS5. Hardware samples prove command sequences, but applications
must not need to copy raw gfx1013 packets or register numbers. This audit
classifies every remaining low-level use after the FW 5.50 hardware
qualification.

## Result

All normal command construction in the hardware-proven compute, graphics,
tessellation, depth, stencil, MSAA, and HTILE paths now uses public OpenAGC
builders. A source gate over `samples/hw_test/*.[ch]` finds no hand-packed PM4
headers and no direct `agcCbAllocDwords` calls.

The only direct-register API use is in `emu_sharpemu.c`, an emulator-facing ABI
conformance program that intentionally tests the low-level Sony-compatible
escape hatch. The only `AGC_PM4_OP_*` uses are in the graphics sample's PM4
decoder, which audits emitted commands but does not construct them.

## Coverage matrix

| Area | Public reusable path | Hardware status |
| --- | --- | --- |
| Context/default state | `agcGfx1013BuildFramePrologue`, `agcGfx1013ApplyFramePostBind`, `agcGfx1013ApplyComputeDefaultsV8` | FW 5.50 PASS |
| Compute | `AgcGfx1013ComputeState`, `agcGfx1013DispatchCompute` | FW 5.50 PASS |
| VS/PS and NGG | Wave32 record validators and binders | FW 5.50 PASS |
| Tessellation | Typed ring table, ring/context setters, HS/TES/GS/PS composer | FW 5.50 PASS |
| Resources | Buffer, image, sampler, combined descriptor encoders and typed resource-table binding | FW 5.50 PASS |
| Frame state | Typed color target, viewport, scissor, blend, depth/stencil, sample, and target-mask builders | FW 5.50 PASS |
| Depth metadata | Typed depth surface, HTILE operation, expclear, layout, and subresource-layout helpers | FW 5.50 PASS for enabled gates |
| Draw | Baseline and tessellation composers plus public low-level indexed/indirect builders | FW 5.50 PASS for exercised paths |
| Synchronization | Typed resource transitions and EOP fence | FW 5.50 PASS |
| Diagnostics | Public `sceAgcDcbWriteData` and PM4 decoder | Intentional sample code |
| Submission | Public driver submit, queue, suspend-point, and bounded fence polling paths | FW 5.50 PASS |

## Intentional low-level uses

### GPU diagnostic markers

`agc_init.c`, `agc_compute.c`, and the depth branch of `agc_graphics.c` use the
public `sceAgcDcbWriteData` builder for ordered markers. Marker placement and
expected values are test policy. A draw-plus-marker wrapper would make the
public graphics API less reusable, so these calls remain explicit.

### Repeated depth diagnostic draws

The depth fixture changes resource tables and depth/stencil state between four
draws to prove pass, fail, overlap, and stencil outcomes. It composes existing
public typed state with `sceAgcDcbDrawIndexAuto`; no packet or register is
constructed locally. A dedicated "depth validation pass" API would encode a
sample scenario rather than an application primitive.

### Emulator conformance

`emu_sharpemu.c` directly invokes Sony-compatible low-level builders, including
direct register setters, because its purpose is export and packet conformance.
It is not a real-PS5 application path and is excluded from typed API policy.

### PM4 decoding

`agc_graphics.c` decodes SET_SH_REG, SET_CONTEXT_REG, and SET_UCONFIG_REG
opcodes after construction to print a hardware audit. This is read-only
diagnostic code and does not bypass public builders.

## Remaining API work outside this promotion goal

- Add application-level indexed and indirect draw composers when a real
  hardware fixture exercises their complete state contract.
- Extend descriptor/image formats and multi-target state incrementally.
- Add bounds-aware shader upload/lifetime helpers.
- Simplify application lifecycle without hiding memory-domain ownership.
- Keep low-level packet/register builders public for research and unsupported
  state, but do not use them as the ordinary hardware sample path.

## Completion criterion

This promotion checkpoint is complete when normal hardware samples contain no
hand-packed PM4 headers, direct command-buffer allocation, or raw register
emission; each normal sequence instead uses an exact host-fixtured public
builder and retains FW 5.50 hardware evidence. The current tree satisfies that
criterion. Future features reopen the audit only when they introduce a newly
hardware-proven command sequence.
