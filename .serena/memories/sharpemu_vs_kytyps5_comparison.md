# SharpEmu vs KytyPS5 — AGC implementation comparison

## Overview

Both are PS5 emulators implementing the AGC userspace ABI. They differ significantly in approach and completeness:

| Aspect | KytyPS5 | SharpEmu |
|--------|---------|----------|
| Language | C++ | C# (.NET) |
| Platform | Windows only | Windows/Linux/macOS |
| AGC file | `src/libs/agc.cpp` (4099 lines) | `src/SharpEmu.Libs/Agc/AgcExports.cs` (11451 lines, but mostly tracing/submit logic) |
| Register defaults | `agcRegisterDefaults.inc` (2845 lines, versions 0..12) | `AgcPrimaryRegisterDefaults.cs` (143 lines, single version only) |
| PM4 command processor | Full (`pm4Dispatch.cpp` + `pm4Handlers.cpp` 4784 lines) | None (interprets packets inline during submit) |
| Host GPU backend | Vulkan | Vulkan + Metal |
| Game compatibility | Boots 2D + some 3D (UE4/5, Unity) | Experimental, boots some games |
| NID count | ~150+ (full LIB_DEFINE table) | 92 exports implemented |
| License | GPL-2.0 | GPL-2.0-or-later |

## Key architecture differences

### KytyPS5
- Builds packets in C++ with direct memory writes via `CommandBuffer::AllocateDW()`
- Has a full PM4 command processor that executes packets and tracks GPU state
- Register defaults are versioned (0..12) with both public and internal tables
- Submit path: `submit_dcb()` → `GraphicsRunSubmitGraphics()` (full GPU execution)
- ACB submit checks for descriptor indirection (magic `0x5533ccaa`)

### SharpEmu
- Builds packets in C# by reading/writing guest memory through `CpuContext`
- No separate PM4 command processor — packets are interpreted during submit
- Register defaults are a single flat table (no versioning)
- Submit path: `DriverSubmitDcb()` → `EnqueueSubmittedDcb()` → `DrainResumableDcbs()` (inline interpretation)
- ACB submit does NOT check for descriptor indirection (just reads addr+size from packet struct)

## PM4 opcode constants — side by side

### IT_* (opcodes)

| Opcode | KytyPS5 | SharpEmu | Notes |
|--------|---------|----------|------|
| IT_NOP | 0x10 | 0x10 | match |
| IT_SET_BASE | 0x11 | 0x11 | match |
| IT_CLEAR_STATE | 0x12 | — | SharpEmu missing |
| IT_INDEX_BUFFER_SIZE | 0x13 | 0x13 | match |
| IT_DISPATCH_DIRECT | 0x15 | 0x15 | match |
| IT_DISPATCH_INDIRECT | 0x16 | 0x16 | match |
| IT_SET_PREDICATION | 0x20 | — | SharpEmu uses NOP stub instead |
| IT_COND_EXEC | 0x22 | — | SharpEmu missing |
| IT_DRAW_INDIRECT | 0x24 | 0x24 | match |
| IT_DRAW_INDEX_INDIRECT | 0x25 | 0x25 | match |
| IT_INDEX_BASE | 0x26 | 0x26 | match |
| IT_DRAW_INDEX_2 | 0x27 | 0x27 | match |
| IT_CONTEXT_CONTROL | 0x28 | — | SharpEmu missing |
| IT_INDEX_TYPE | 0x2A | 0x2A | match (but SharpEmu uses it for SetIndexSize — WRONG) |
| IT_DRAW_INDIRECT_MULTI | 0x2C | — | SharpEmu missing |
| IT_DRAW_INDEX_AUTO | 0x2D | 0x2D | defined but NOT used (uses NOP instead) |
| IT_NUM_INSTANCES | 0x2F | 0x2F | match |
| IT_INDIRECT_BUFFER_CNST | 0x33 | — | SharpEmu missing |
| IT_DRAW_INDEX_OFFSET_2 | 0x35 | 0x35 | match |
| IT_WRITE_DATA | 0x37 | 0x37 | defined but SharpEmu uses NOP+R_WRITE_DATA instead |
| IT_MEM_SEMAPHORE | 0x39 | — | SharpEmu missing |
| IT_DRAW_INDEX_INDIRECT_MULTI | 0x38 | — | SharpEmu missing |
| IT_DISPATCH_DRAW_PREAMBLE | 0x3A | — | SharpEmu missing |
| IT_INDIRECT_BUFFER | 0x3F | 0x3F | match |
| IT_COPY_DATA | 0x40 | — | SharpEmu missing |
| IT_CP_DMA | 0x41 | — | SharpEmu missing |
| IT_PFP_SYNC_ME | 0x42 | — | SharpEmu missing (uses NOP stub for StallCommandBufferParser) |
| IT_SURFACE_SYNC | 0x43 | — | SharpEmu missing |
| IT_EVENT_WRITE | 0x46 | 0x46 | match |
| IT_EVENT_WRITE_EOP | 0x47 | — | SharpEmu missing |
| IT_EVENT_WRITE_EOS | 0x48 | — | SharpEmu missing |
| IT_RELEASE_MEM | 0x49 | 0x49 | defined but SharpEmu uses NOP+R_RELEASE_MEM instead |
| IT_DMA_DATA | 0x50 | 0x50 | match |
| IT_ACQUIRE_MEM | 0x58 | — | SharpEmu missing (uses NOP+R_ACQUIRE_MEM) |
| IT_REWIND | 0x59 | — | SharpEmu missing |
| IT_SET_SH_REG_INDIRECT | 0x63 | — | SharpEmu uses NOP+R_SH_REGS_INDIRECT instead |
| IT_SET_UCONFIG_REG_INDIRECT | 0x64 | — | SharpEmu uses NOP+R_UC_REGS_INDIRECT instead |
| IT_SET_CONFIG_REG | 0x68 | — | SharpEmu missing |
| IT_SET_CONTEXT_REG | 0x69 | 0x69 | match |
| IT_SET_SH_REG | 0x76 | 0x76 | match |
| IT_SET_QUEUE_REG | 0x78 | — | SharpEmu missing |
| IT_SET_UCONFIG_REG | 0x79 | 0x79 | match |
| IT_SET_UCONFIG_REG_INDEX | 0x7A | — | SharpEmu missing (uses IT_INDEX_TYPE for SetIndexSize — WRONG) |
| IT_WRITE_CONST_RAM | 0x81 | — | SharpEmu missing |
| IT_DUMP_CONST_RAM | 0x83 | — | SharpEmu missing |
| IT_INCREMENT_CE_COUNTER | 0x84 | — | SharpEmu missing |
| IT_INCREMENT_DE_COUNTER | 0x85 | — | SharpEmu missing |
| IT_WAIT_ON_CE_COUNTER | 0x86 | — | SharpEmu missing |
| IT_WAIT_ON_DE_COUNTER_DIFF | 0x88 | — | SharpEmu missing |
| IT_DISPATCH_DRAW | 0x8D | — | SharpEmu missing |
| IT_GET_LOD_STATS | 0x8E | 0x8E | match |
| IT_SET_CONTEXT_REG_INDIRECT | 0x9F | — | SharpEmu uses NOP+R_CX_REGS_INDIRECT instead |
| IT_WAIT_REG_MEM | — | 0x3C | SharpEmu ONLY (KytyPS5 uses NOP+R_WAIT_MEM_32/64) |
| IT_DRAW_INDEX_MULTI_AUTO | — | 0x30 | SharpEmu ONLY (not in KytyPS5) |

### R_* (NOP subcommands)

| Subcommand | KytyPS5 | SharpEmu | Notes |
|-----------|---------|----------|------|
| R_ZERO | 0x00 | 0x00 | match |
| R_DRAW_INDEX_AUTO | — | 0x04 | SharpEmu ONLY (KytyPS5 uses IT_DRAW_INDEX_AUTO directly) |
| R_DRAW_RESET | 0x05 | 0x05 | match |
| R_WAIT_FLIP_DONE | 0x06 | 0x06 | match |
| R_DISPATCH_RESET | 0x09 | — | SharpEmu missing (uses R_ACB_RESET=0x09 instead) |
| R_ACB_RESET | — | 0x09 | SharpEmu ONLY (same value as R_DISPATCH_RESET, different name) |
| R_WAIT_MEM_32 | 0x0A | 0x0A | match |
| R_PUSH_MARKER | 0x0B | 0x0B | match |
| R_POP_MARKER | 0x0C | 0x0C | match |
| R_SH_REGS_INDIRECT | — | 0x11 | SharpEmu ONLY (KytyPS5 uses IT_SET_SH_REG_INDIRECT directly) |
| R_CX_REGS_INDIRECT | — | 0x12 | SharpEmu ONLY (KytyPS5 uses IT_SET_CONTEXT_REG_INDIRECT directly) |
| R_UC_REGS_INDIRECT | — | 0x13 | SharpEmu ONLY (KytyPS5 uses IT_SET_UCONFIG_REG_INDIRECT directly) |
| R_ACQUIRE_MEM | 0x14 | 0x14 | match |
| R_WRITE_DATA | 0x15 | 0x15 | match |
| R_WAIT_MEM_64 | 0x16 | 0x16 | match |
| R_FLIP | 0x17 | 0x17 | match |
| R_RELEASE_MEM | 0x18 | 0x18 | match |
| R_DMA_DATA | 0x19 | 0x19 | match |
| R_INDEX_BASE | — | 0x1B | SharpEmu ONLY |
| R_INDEX_COUNT | — | 0x1C | SharpEmu ONLY |

## Key packet builder differences

### DrawIndexAuto — MAJOR DIVERGENCE

**KytyPS5:** `IT_DRAW_INDEX_AUTO (0x2D)`, 3 dwords
```c
cmd[0] = PM4(3, IT_DRAW_INDEX_AUTO, 0);
cmd[1] = index_count;
cmd[2] = decode_draw_index_initiator(modifier) | 0x2;
```

**SharpEmu:** `IT_NOP + R_DRAW_INDEX_AUTO (0x04)`, 7 dwords (mostly zeros)
```c
cmd[0] = PM4(7, IT_NOP, RDrawIndexAuto);  // 0x04
cmd[1] = index_count;
cmd[2..6] = 0;  // padding
```

**openagc:** Also uses `IT_NOP + R_DRAW_INDEX_AUTO` (like SharpEmu)

**Verdict:** KytyPS5 is most likely correct — it uses the real AMD opcode with proper initiator encoding. SharpEmu's 7-dword NOP-wrapped version with zeros looks like a stub that happens to work because SharpEmu's command processor doesn't validate the packet format.

### SetIndexSize — MAJOR DIVERGENCE

**KytyPS5:** `IT_SET_UCONFIG_REG_INDEX (0x7A)`, 3 dwords
```c
cmd[0] = PM4(3, IT_SET_UCONFIG_REG_INDEX, 0);
cmd[1] = 0x20000243;  // constant
cmd[2] = (type & 3) | (swap << 6) | 0x400;
```

**SharpEmu:** `IT_INDEX_TYPE (0x2A)`, 2 dwords
```c
cmd[0] = PM4(2, IT_INDEX_TYPE, 0);
cmd[1] = indexSize;
```

**openagc:** Uses `0x7A` with `cmd[1]=0x20000243` (matches KytyPS5, from SPRX RE)

**Verdict:** KytyPS5 and openagc are correct. SharpEmu uses the wrong opcode (0x2A is the PS4/GNM INDEX_TYPE, not the PS5 AGC SET_UCONFIG_REG_INDEX). SharpEmu's version is a simplified stub.

### SetShRegistersIndirect / SetCxRegistersIndirect / SetUcRegistersIndirect — DIVERGENCE

**KytyPS5:** Uses real opcodes directly:
- `IT_SET_SH_REG_INDIRECT (0x63)`, 5 dwords
- `IT_SET_CONTEXT_REG_INDIRECT (0x9F)`, 5 dwords
- `IT_SET_UCONFIG_REG_INDIRECT (0x64)`, 5 dwords

Packet format: `{header, offset, addr_lo, addr_hi, count}`

**SharpEmu:** Uses NOP-wrapped subcommands:
- `IT_NOP + R_SH_REGS_INDIRECT (0x11)`, 4 dwords
- `IT_NOP + R_CX_REGS_INDIRECT (0x12)`, 4 dwords
- `IT_NOP + R_UC_REGS_INDIRECT (0x13)`, 4 dwords

Packet format: `{header, count, addr_lo, addr_hi}`

**openagc:** Uses real opcodes (0x63, 0x9F, 0x64) — matches KytyPS5 (SPRX-confirmed)

**Verdict:** KytyPS5 and openagc are correct. SharpEmu's NOP-wrapped versions are stubs.

### WriteData — DIVERGENCE

**KytyPS5:** `IT_WRITE_DATA (0x37)` directly, 4+num_dwords
```c
cmd[0] = PM4(4+num_dwords, IT_WRITE_DATA, 0);
cmd[1] = ((dst&1)<<30) | ((dst&0x1e)<<7) | ((increment&1)<<16) | (write_confirm<<20) | ((cache_policy&3)<<25);
cmd[2] = addr_lo & ~3;
cmd[3] = addr_hi;
// cmd[4..] = data
```

**SharpEmu:** `IT_NOP + R_WRITE_DATA (0x15)`, 4+num_dwords
```c
cmd[0] = PM4(4+num_dwords, IT_NOP, RWriteData);
cmd[1] = dst | (cachePolicy<<8) | (increment<<16) | (writeConfirm<<24);  // different bit layout!
cmd[2] = addr_lo;
cmd[3] = addr_hi;
// cmd[4..] = data
```

**openagc:** Uses `IT_WRITE_DATA (0x37)` directly — matches KytyPS5

**Verdict:** KytyPS5 and openagc are correct. SharpEmu uses NOP-wrapped with a different control word bit layout.

### WaitRegMem — PARTIAL DIVERGENCE

**KytyPS5:**
- 32-bit: `IT_NOP + R_WAIT_MEM_32 (0x0A)`, 7 dwords
- 64-bit: `IT_NOP + R_WAIT_MEM_64 (0x16)`, 9 dwords
- Also has a `standardWait` path using `IT_WAIT_REG_MEM` — but this is not in KytyPS5's pm4.h

**SharpEmu:**
- 32-bit: `IT_NOP + R_WAIT_MEM_32 (0x0A)`, 6 dwords (one fewer than KytyPS5!)
- 64-bit: `IT_NOP + R_WAIT_MEM_64 (0x16)`, 9 dwords
- `standardWait` (op 2 or 3): `IT_WAIT_REG_MEM (0x3C)`, 7 dwords — SharpEmu ONLY opcode

**openagc:** Only 64-bit `IT_NOP + R_WAIT_MEM_64`, 9 dwords

**Verdict:** KytyPS5's 32-bit variant has 7 dwords; SharpEmu's has 6. The control word encoding also differs. KytyPS5 is more likely correct as it includes a poll_cycles field. SharpEmu's `IT_WAIT_REG_MEM (0x3C)` is a SharpEmu-only extension not seen in KytyPS5 or SPRX.

### AcquireMem — MATCH

Both use `IT_NOP + R_ACQUIRE_MEM (0x14)`, 8 dwords, identical encoding.

### SetFlip — MATCH

Both use `IT_NOP + R_FLIP (0x17)`, 6 dwords, identical encoding.

### CbReleaseMem — MATCH

Both use `IT_NOP + R_RELEASE_MEM (0x18)`, 8 dwords (SharpEmu) / 7 dwords (KytyPS5).

Wait — SharpEmu allocates 8 dwords but KytyPS5 allocates 7. Let me recheck...

KytyPS5 `GraphicsCbReleaseMem` was not directly examined, but `GraphicsDcbSetFlip` uses 6 dwords. SharpEmu's CbReleaseMem uses 8 dwords with an interruptContextId field at cmd[7]. This needs cross-checking.

### ResetQueue — MATCH

Both use `IT_NOP + R_DRAW_RESET (0x05)`, 2 dwords, identical encoding.

### Jump — MATCH

Both use `IT_INDIRECT_BUFFER (0x3F)`, 4 dwords, identical encoding.

### DrawIndexOffset — MATCH

Both use `IT_DRAW_INDEX_OFFSET_2 (0x35)`, 5 dwords, identical encoding.

### DrawIndex (DrawIndex2) — MATCH

Both use `IT_DRAW_INDEX_2 (0x27)`, 6 dwords.

SharpEmu also emits a separate IT_INDEX_BASE + IT_INDEX_BUFFER_SIZE prefix before the draw (5 dwords), which KytyPS5 does not do inline (it relies on prior SetIndexBuffer/SetIndexCount calls).

### EventWrite — PARTIAL MATCH

**KytyPS5:** Handles event_type 7/15/16 with `0x400 | event_type`, addressed events (0x38-0x39) with `0x100 | event_type` + addr, others with `event_type & 0x3f`.

**SharpEmu:** Only handles addressed events (`(eventType & ~1) == 0x38`) with `0x100 | eventType` + addr, others with `eventType & 0x3f`. Missing the special case for event_type 7/15/16.

### StallCommandBufferParser — DIVERGENCE

**KytyPS5:** `IT_PFP_SYNC_ME (0x42)`, 2 dwords
**SharpEmu:** `IT_NOP + R_ZERO`, 2 dwords (bas a NOP stub)
**openagc:** `IT_PFP_SYNC_ME (0x42)`, 2 dwords — matches KytyPS5

### CbDispatch — MATCH

Both use `IT_DISPATCH_DIRECT (0x15)`, 5 dwords, with `(modifier & 0xA038) | 0x41` initiator.

### CbSetShRegistersDirect — MATCH (both do run-coalescing)

Both sort registers by offset, coalesce contiguous runs, and emit `IT_SET_SH_REG (0x76)` packets per run.

## Register defaults comparison

### KytyPS5
- Versioned: versions 0, 4, 5, 7, 8, 9, 10, 11 (with aliases 1→0, 2→0, 3→0, 6→5, 12→10)
- Both public and internal tables
- 4 tables per version (cx, sh, uc, ??)
- Stored in `agcRegisterDefaults.inc` (2845 lines)
- Type IDs are 32-bit hashes (e.g. 0xE24F806D for CB_COLOR_CONTROL)

### SharpEmu
- Single version only (unversioned)
- Only primary (public) table — no internal table
- 3 groups (cx=0, sh=1, uc=2)
- Stored in `AgcPrimaryRegisterDefaults.cs` (143 lines)
- Same type ID hashes as KytyPS5 (e.g. 0xE24F806D for CB_COLOR_CONTROL)
- Comment says "originally reverse-engineered by Kyty (MIT)"

**Notable:** SharpEmu's register defaults appear to be derived from Kyty/KytyPS5. The type IDs and offset/value pairs match. SharpEmu has a subset (78 groups vs KytyPS5's larger versioned tables).

SharpEmu includes some registers not seen in KytyPS5's primary table:
- PA_STEREO_CNTL, PA_STATE_STEREO_X, PA_SU_SMALL_PRIM_FILTER_CNTL
- FSR_* registers (FSR_ENABLE, FSR_RECURSIONS0/1, FSR_EXTEND_SUBPIXEL_ROUNDING, FSR_ALPHA_VALUE0/1, FSR_CONTROL_POINT0-3, FSR_WINDOW0/1)
- MEMORY_MAPPING_MASK, UC_NOP
- GE_USER_VGPR1

These may be from a different AGC version or from SharpEmu's own RE work.

## NID comparison

### NIDs in SharpEmu but NOT in KytyPS5 (or different mapping)
- `t1vNu082-jM` → sceAgcDcbDrawIndexIndirect (KytyPS5 has `t1vNu082-jM` → GraphicsDcbDrawIndexIndirect — SAME)
- `mStuvI0zOtc` → sceAgcDcbDrawIndexIndirectGetSize (not seen in KytyPS5's libGraphicsDriver)
- `rUuVjyR+Rd4` → sceAgcDcbGetLodStatsGetSize (KytyPS5 has `vuSXe69VILM` → GraphicsDcbGetLodStats, `rUuVjyR+Rd4` not seen)
- `eAy8eGNsCuU` → sceAgcWriteDataPatchSetCachePolicy (not in KytyPS5)
- `tmy-+rBpspY` → sceAgcWriteDataPatchSetDst (not in KytyPS5)
- `n485EBnIWmk` → sceAgcWaitRegMemPatchCompareFunction (not in KytyPS5)
- `hXAnLgDHCoI` → sceAgcWaitRegMemPatchMask (not in KytyPS5)
- `J8YCgfKAMQs` → sceAgcQueueEndOfPipeActionPatchGcrCntl (not in KytyPS5)
- `T9fjQIINoeE` → sceAgcQueueEndOfPipeActionPatchType (not in KytyPS5)
- `+u6dKSLWM2o` → sceAgcDcbStallCommandBufferParserGetSize (not in KytyPS5)
- `2ccJz9LQI+w` → sceAgcDcbDmaDataGetSize (not in KytyPS5)
- `M0ttm8h7SKA` → sceAgcAcbDmaDataGetSize (not in KytyPS5)
- `mljzuGDZRQ4` → sceAgcDcbSetIndexCountGetSize (not in KytyPS5)

These are likely legitimate NIDs that SharpEmu identified independently through SPRX disassembly.

### NIDs in KytyPS5 but NOT in SharpEmu

KytyPS5 has ~60 more exports than SharpEmu. Key missing ones in SharpEmu:
- All the SetNumRegisters patchers (whb1RL7K4Ss, nCUgItdN2ms, fRG-JOH5+sI)
- UpdatePrimState (Y3ymLfZ1384)
- GetDataPacketPayloadRange (s+VGAMDQ0AQ)
- JumpPatchSetTarget (2BS4EtAaF28)
- GetIsTrinityMode (BfBDZGbti7A)
- Fused shader support (dolOmWH+huQ, fd5Bp5tGTgo)
- SubmitCommandBuffer (b4fpgH5ZXxQ)
- SubmitMultiCommandBuffers (Fj7r9EHzF38)
- SubmitMultiAcbs (HF3YllT3mXU)
- GetEqEventType (5CdQTZIQPxM)
- GetEqContextId (Zw7uUVPulbw)
- IsCaptureInProgress (Ddwk4gLT5j0)
- RegisterWorkloadStream (3AyTaWcF-H8)
- CondExecPatchSetEnd/SetCommandAddress (ORWsxIbk4TE, YWTKOju587o)
- GetPacketSize (Lkf86B98qPc)
- SetRangePredication (n8vgpaQg6dA)
- DcbSetCxRegisterDirect (LHFXRrlTPD8)
- DcbSetCxRegisterDirectGetSize (1DeUNpRIDDA)
- DcbRewind (zfcxg-ewMK8)
- DcbCondExec (BIPexNBSGog)
- DcbCopyData (1rZSWUv1IRc)
- DcbSetWorkloadsActive/SetWorkloadComplete/SetWorkloadStreamInactive
- DcbContextStateOp
- DcbSetEopFlip
- CbBranch (w1KFAHVqpaU)
- CbCondWrite
- CbMemSemaphore
- DcbDrawIndexMultiInstanced
- DcbSetIndexIndirectArgs
- DcbAtomicMem / DcbAtomicGds / DcbMemSemaphore / DcbPrimeUtcl2
- DcbClearState
- DcbSetMarker
- All the SetCfRegister/SetCxRegister/SetShRegister/SetUcRegister direct setters and range setters
- AcbCondExec, AcbCopyData, AcbSetMarker

## ACB descriptor indirection

**KytyPS5:** Checks for magic `0x5533ccaa` at acb[4] and follows the descriptor pointer.
**SharpEmu:** Does NOT check for descriptor indirection — reads addr+size directly from packet struct.
**openagc:** Does NOT handle this either.

This is a KytyPS5-only finding that openagc should adopt.

## Overall assessment

### Trust hierarchy for packet encodings

1. **KytyPS5** — most trustworthy for packet encodings. It's a working emulator with a full PM4 command processor. Uses real AMD/AGC opcodes. Register defaults are versioned.

2. **openagc** — second most trustworthy. SPRX RE-confirmed encodings match KytyPS5. Some builders still use NOP-wrapped stubs (DrawIndexAuto) that should be switched to real opcodes.

3. **SharpEmu** — least trustworthy for packet encodings. Many builders use NOP-wrapped stubs instead of real opcodes. SetIndexSize uses the wrong opcode (0x2A instead of 0x7A). Register defaults are a single-version subset derived from Kyty. However, SharpEmu has identified some NIDs and patchers (WriteDataPatchSetCachePolicy, WriteDataPatchSetDst, WaitRegMemPatchCompareFunction, WaitRegMemPatchMask, QueueEndOfPipeActionPatchGcrCntl, QueueEndOfPipeActionPatchType) that neither KytyPS5 nor openagc have.

### What SharpEmu has that openagc should adopt
1. **New NIDs** for patchers not in KytyPS5:
   - `eAy8eGNsCuU` → WriteDataPatchSetCachePolicy
   - `tmy-+rBpspY` → WriteDataPatchSetDst
   - `n485EBnIWmk` → WaitRegMemPatchCompareFunction (openagc has this NID already)
   - `hXAnLgDHCoI` → WaitRegMemPatchMask (openagc has this NID already)
   - `J8YCgfKAMQs` → QueueEndOfPipeActionPatchGcrCntl
   - `T9fjQIINoeE` → QueueEndOfPipeActionPatchType
   - `+u6dKSLWM2o` → DcbStallCommandBufferParserGetSize
   - `2ccJz9LQI+w` → DcbDmaDataGetSize
   - `M0ttm8h7SKA` → AcbDmaDataGetSize
   - `mljzuGDZRQ4` → DcbSetIndexCountGetSize
   - `rUuVjyR+Rd4` → DcbGetLodStatsGetSize (different from KytyPS5's `vuSXe69VILM`)

2. **GetLodStats implementation** — SharpEmu has a full implementation with counter mask, reset, enable, counter select parameters. KytyPS5's was not examined in detail but uses IT_GET_LOD_STATS (0x8E).

3. **WaitRegMem standardWait path** — operation 2 or 3 uses `IT_WAIT_REG_MEM (0x3C)`. This needs verification against SPRX. KytyPS5 doesn't have this opcode constant, but SharpEmu might have found it via RE.

### What SharpEmu has WRONG (don't adopt)
1. SetIndexSize uses IT_INDEX_TYPE (0x2A) instead of IT_SET_UCONFIG_REG_INDEX (0x7A)
2. DrawIndexAuto uses NOP-wrapped 7-dword stub instead of IT_DRAW_INDEX_AUTO (0x2D) 3-dword
3. SetSh/Cx/UcRegistersIndirect use NOP-wrapped 4-dword stubs instead of real opcodes (0x63/0x9F/0x64) 5-dword
4. WriteData uses NOP-wrapped instead of IT_WRITE_DATA (0x37) directly
5. StallCommandBufferParser uses NOP stub instead of IT_PFP_SYNC_ME (0x42)
6. SetPredication uses NOP stub instead of IT_SET_PREDICATION (0x20)
7. ACB submit missing descriptor indirection (magic 0x5533ccaa)
8. EventWrite missing special case for event_type 7/15/16 (0x400 | event_type)
9. Register defaults are single-version only (no versioning)