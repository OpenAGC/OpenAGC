# Stage-local runtime push constants (2026-08-01)

## Problem

The native runtime tracked readiness per shader stage but stored every stage's
push-constant bytes in one shared arena range. Two graphics stages could
therefore be marked ready with different values for the same byte offset while
both shader bindings still addressed the value written last.

This is observable through Vulkan, where `vkCmdPushConstants` updates the
selected shader stages independently.

## Resolution

The pipeline resource arena now reserves one 16-byte-aligned push-constant
slot per `AgcShaderStage`. `agcCmdPushConstants` copies the supplied bytes only
to the requested stage slots. Pointer and inline user-SGPR resolution select
the slot for the shader being emitted. The public API and reflection records
remain unchanged, and applications still receive no GPU addresses.

The complete stage arena is preallocated with the command buffer's other
reflected resources, so the change adds no draw-time allocation and preserves
command recycling and retention rules.

## Verification

The generic runtime regression creates a vertex/pixel pipeline whose two
stages consume different inline values at offset zero. It records and submits
one draw, decodes the resulting shader-register writes, and proves that the
vertex value is `0x11223344` while the pixel value is `0xaabbccdd`. The generic
runtime binary reports 17,817 assertions passed and zero failed.

This internal arena-layout change is host-qualified. Any Prospero library or
ELF linked after it is a new hardware candidate and must receive a new digest.
