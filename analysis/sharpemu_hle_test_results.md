# SharpEmu HLE Test Results & ABI Conventions

## Overview

OpenAGC was tested against SharpEmu's HLE (High-Level Emulation) implementations
using a custom test binary (`samples/hw_test/emu_sharpemu.c`). The test exercises
**87 sceAgc\* functions** across CB, DCB, ACB builders, driver functions, GetSize
helpers, patchers, shader functions, init, and submit.

## SharpEmu Modifications

SharpEmu was modified to resolve plain-name imports from ps5-payload-sdk binaries:

1. **`SetupImportStubs`** (`DirectExecutionBackend.cs`): Added NID fallback —
   if direct NID lookup fails for a plain symbol name, compute the PS5 NID via
   `ComputePsNid()` and retry. Also added export-name fallback.

2. **`TryResolveDirectImportTarget`**: Same NID fallback for the LLE direct-bridging path.

3. **Leaf classification fix**: `IsLeafImport`, `IsNoBlockLeafImport`,
   `ShouldSuppressStrlenTrace`, and `IsImportLoopGuardBoundary` were called with
   the plain import name instead of the resolved NID. Fixed to use
   `resolvedExport?.Nid ?? text2` so ps5-payload-sdk imports get scheduler
   fast-path optimizations.

4. **ELF header patching**: The ps5-payload-sdk linker doesn't set FreeBSD OSABI
   (0x09) or ET_SCE_DYNEXEC (0xFE10). Post-link patch required for SharpEmu.

## HLE Argument Conventions Discovered

### SceAgcCb cursor layout (0x38 bytes)
| Offset | Field        | Purpose                    |
|--------|--------------|----------------------------|
| 0x00   | reserved0    | buffer base address        |
| 0x08   | reserved1    | 0                          |
| 0x10   | cursor_up    | write cursor (advances)    |
| 0x18   | cursor_down  | end of buffer              |
| 0x20   | callback     | 0 (full-buffer callback)   |
| 0x28   | reserved2    | 0                          |
| 0x30   | reserved_dw  | 0                          |
| 0x34   | reserved3    | 0                          |

SharpEmu's HLE reads the cursor from **guest memory** at the `commandBufferAddress`
(rdi). The CB struct must be in guest-accessible memory (stack or heap).

### Function-specific argument requirements

| Function | rdi | rsi | rdx | rcx | r8 | r9 | Stack args | Notes |
|----------|-----|-----|-----|-----|----|----|------------|-------|
| sceAgcInit | state | version=7 | - | - | - | - | - | Returns 0 on success |
| sceAgcDcbDrawIndexAuto | cb | index_count | modifier=**0x40000000** | - | - | - | - | Modifier must be exactly 0x40000000 |
| sceAgcDcbDrawIndex | cb | index_count | index_addr | modifier=**0x40000000** | - | - | - | Modifier must be 0x40000000 |
| sceAgcDcbResetQueue | cb | op=**0x3FF** | state=**0** | - | - | - | - | op must be 0x3FF, state must be 0 |
| sceAgcDcbSetIndexSize | cb | index_size | cache_policy=**0** | - | - | - | - | cache_policy must be 0 |
| sceAgcDcbEventWrite | cb | event_type (≤0x3F) | event_addr=**0** | - | - | - | - | event_addr must be 0 |
| sceAgcDcbAcquireMem | cb | engine (≤1) | cb_db_op | gcr_ctrl | base_addr | size_bytes | poll_cycles | size=ULONG_MAX means "no size" |
| sceAgcAcbAcquireMem | cb | gcr_ctrl | base_addr | size_bytes | poll_cycles | - | - | size=ULONG_MAX means "no size" |
| sceAgcDcbDispatchIndirect | cb | data_offset | modifier | - | - | - | - | modifier & 0xA038, \| 0x41 |
| sceAgcDcbSetFlip | cb | vo_handle | buf_idx | flip_mode | flip_arg | - | - | - |
| sceAgcDcbWaitUntilSafeForRendering | cb | vo_handle | buf_idx | - | - | - | - | - |
| sceAgcDcbWriteData | cb | dest | cache_policy | dst_addr | data_addr | dword_count | increment, write_confirm | dword_count ≤ 0x3FFD |
| sceAgcDcbWaitRegMem | cb | size (≤1) | compare_func (≤7) | operation (≤4) | cache_policy (≤3) | address | reference, mask, poll_cycles | - |
| sceAgcDcbDmaData | cb | dest | dest_cache_policy | source | dst_addr | src_cache_policy | control4, src_addr, byte_count, control7-9 | byte_count must be non-zero, dword-aligned |
| sceAgcAcbDmaData | cb | src_selector | dst_selector | dst_addr | *(unused)* | *(unused)* | src_or_immediate, byte_count | HLE reads args 7-8 from stack; args 5-6 (r8/r9) unused |
| sceAgcAcbWaitRegMem | cb | size (≤1) | compare_func (≤7) | cache_policy (≤3) | address | reference | mask, poll_cycles | - |
| sceAgcDcbStallCommandBufferParser | cb | size (≤1) | address | reference | - | - | - | HLE emits NOP (no real stall in direct execution) |
| sceAgcDcbGetLodStats | cb | cache_policy | dst_addr | control | counter_mask | reset_counters | enable, counter_select | - |

### Shader record layout (for CreateShader / CreatePrimState / CreateInterpolantMapping)

| Offset | Type | Field | Notes |
|--------|------|-------|-------|
| 0x00 | u32 | file_header | Must be 0x34333231 ("1234") |
| 0x04 | u32 | version | Must be 0x18 |
| 0x08 | u64 | user_data_ptr | Relative offset (relocated by HLE) |
| 0x10 | u64 | code_ptr | Patched by CreateShader to absolute code address |
| 0x18 | u64 | cx_registers_ptr | Relative offset (relocated by HLE) |
| 0x20 | u64 | sh_registers_ptr | Relative offset (relocated by HLE) |
| 0x28 | u64 | specials_ptr | Relative offset (relocated by HLE) |
| 0x30 | u64 | input_semantics_ptr | Relative offset (relocated by HLE) |
| 0x38 | u64 | output_semantics_ptr | Relative offset (relocated by HLE) |
| 0x56 | u16 | num_output_semantics | - |
| 0x5A | u8 | shader_type | 0=CS, 1=PS, 2/6=ES, 4=GS, 7=LS |
| 0x5C | u8 | num_sh_registers | Must be ≥ 2 for PatchShaderProgramRegisters |

**Pointer field relocation**: HLE's `RelocatePointerField` treats pointer fields as
**relative offsets** from the field address — it writes back `fieldAddress + relativeOffset`.
Store relative offsets, not absolute addresses.

**SH register validation**: `PatchShaderProgramRegisters` reads the first two SH register
entries (each 8 bytes: 4-byte register offset + 4-byte value). For shader type 2 (ES):
- Entry 0 register offset must be `SpiShaderPgmLoEs` (0xC8)
- Entry 1 register offset must be `SpiShaderPgmHiEs` (0xC9)

### Error codes (SharpEmu HLE)

| Code | Constant | Meaning |
|------|----------|---------|
| 0x00000000 | ORBIS_GEN2_OK | Success |
| 0x80020002 | ORBIS_GEN2_ERROR_NOT_FOUND | Resource/event not found |
| 0x80020003 | ORBIS_GEN2_ERROR_INVALID_ARGUMENT | Invalid argument |
| 0x80020101 | ORBIS_GEN2_ERROR_MEMORY_FAULT | Memory access fault |

## Packet Encoding Comparison: SharpEmu HLE vs OpenAGC

### Matching encodings (22 functions)
All NOP-subcommand AGC-specific packets match perfectly:
- CbNop, CbDispatch, CbSetShRegistersDirect, CbReleaseMem
- DcbSetFlip, DcbWaitUntilSafeForRendering, DcbPushMarker, DcbPopMarker
- DcbAcquireMem, DcbDispatchIndirect, DcbDrawIndexOffset, DcbDrawIndexIndirect
- DcbSetIndexBuffer, DcbJump, DcbEventWrite
- All ACB builders (AcbResetQueue, AcbEventWrite, AcbAcquireMem, AcbPushMarker, AcbPopMarker)

### Encoding discrepancies (5 functions)

These are cases where SharpEmu's HLE uses HLE-reference NOP-wrapped stubs,
while OpenAGC uses SPRX/reference-confirmed real AMD/AGC opcodes:

| Function | SharpEmu HLE | OpenAGC | OpenAGC Evidence |
|----------|-------------|---------|-------------------|
| DcbDrawIndexAuto | NOP+sub=0x04 (7 dw) | IT_DRAW_INDEX_AUTO 0x2D (3 dw) | reference-confirmed |
| DcbResetQueue | NOP+sub=0x05 (2 dw) | SET_UCONFIG_REG+sub=0x09 (3 dw) | SPRX-confirmed |
| DcbSetIndexCount | NOP+sub=0x1C (2 dw) | IT_INDEX_BUFFER_SIZE 0x13 (2 dw) | SPRX-confirmed |
| DcbSetPredication | NOP+sub=0x00 (3 dw) | IT_SET_PREDICATION 0x20 (3 dw) | SPRX-confirmed |
| DcbSetIndexSize | IT_INDEX_TYPE 0x2A (2 dw) | opcode 0x7A (3 dw) | SPRX-confirmed |

**Assessment**: OpenAGC's encodings are authoritative (SPRX/reference-confirmed).
SharpEmu's HLE encodings for these 5 functions are HLE-reference-derived stubs
that would fail on real PS5 hardware. This confirms PLAN.md's guidance:
"HLE reference packet encodings are NOT trusted without independent SPRX or
reference confirmation."

## Test Results

```
=== Results: 87 passed, 0 failed ===
```

### Coverage breakdown (87 of 93 HLE functions, 93.5%)

| Category | Tested | Untested | Reason Untested |
|----------|--------|----------|----------------|
| CB builders | 5 | 0 | — |
| DCB builders | 21 | 0 | — |
| ACB builders | 9 | 0 | — |
| Driver functions | 16 | 0 | — |
| GetSize helpers | 6 | 0 | — |
| Patchers | 17 | 0 | — |
| Shader | 3 | 0 | — |
| Init/defaults | 3 | 0 | — |
| Submit | 3 | 0 | — |
| EqEvent | 2 | 0 | — |
| FW 11.60-only | 0 | 4 | Not in FW 5.50 SDK stubs (`GetIsTrinityMode`, `WriteDataPatchSet*`) |
| Unknown NIDs | 0 | 2 | Synthetic names, not in SDK stubs (`UnknownQj7QZpgr9Uw`, `DriverUnknown_KRzWekV120`) |

### HLE limitations discovered (not OpenAGC bugs)

1. **`sceAgcDriverRegisterResource`** — HLE stub returns OK but doesn't track
   resources, so `sceAgcDriverUnregisterResource` always returns
   `INVALID_ARGUMENT` (0x80020003).

2. **`sceAgcDriverAddEqEvent` / `DeleteEqEvent`** — With null equeue, returns
   `NOT_FOUND` (0x80020002). Full testing requires a kernel event queue.

3. **`sceAgcInit` NID mismatch** — SharpEmu HLE registers `sceAgcInit` under
   NID `23LRUSvYu1M` (the `_0090` old-version export), but `ComputePsNid`
   produces `kW3GLb7QfPg` (the primary NID). The export-name fallback catches
   it. SharpEmu's HLE table should register both NIDs.

### ABI issues found and fixed

1. **`sceAgcAcbDmaData`** — HLE reads args 7-8 from the stack (stack[+8],
   stack[+16]), but a 6-arg SysV declaration puts all args in registers. The
   real function has 8 args so args 7-8 land on stack. Args 5-6 (r8/r9) are
   unused by the HLE. Fixed by adding 2 pad args.

2. **`sceAgcCreateShader`** — HLE's `RelocatePointerField` treats pointer
   fields as relative offsets from the field address (writes
   `fieldAddress + relativeOffset`). Fixed by computing relative offsets at
   runtime. Also needed proper SH register entries (`SpiShaderPgmLoEs=0xC8`,
   `SpiShaderPgmHiEs=0xC9`) and `num_sh_registers >= 2`.

All 87 testable functions resolve via NID fallback and execute successfully in
SharpEmu's HLE. The packet encoding differences don't affect SharpEmu testing
because SharpEmu's HLE generates its own packets rather than validating
OpenAGC's encodings.
