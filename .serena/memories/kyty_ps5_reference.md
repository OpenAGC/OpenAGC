# KytyPS5 — PS5 AGC emulator reference

**Path:** `/Users/bizkut/Downloads/PS5/homebrew/KytyPS5`
**License:** GPL-2.0 (derivative of Kyty by InoriRus, heavily modified)
**Status:** Early dev, boots 2D + some 3D games (UE4/5, Unity). Windows-only build.

## Why it matters for openagc

KytyPS5 is the most complete open-source PS5 AGC userspace implementation found so far. It implements the FULL `sceAgc*` / `sceAgcDriver*` ABI with real packet builders (not stubs), a complete PM4 command processor, register-defaults tables for versions 0..12, and a host-GPU renderer. Unlike `ps5-openagc` (which has known errors) or `Kyty` (which only had a small subset), KytyPS5 is a working emulator that runs real games — so its packet encodings are empirically validated.

## Key files

- `src/libs/agc.h` — full AGC public ABI declarations (Gen5 + Gen5Driver namespaces)
- `src/libs/agc.cpp` (4099 lines) — all packet builders, patchers, register-defaults accessors, submit paths
- `src/libs/agcRegisterDefaults.inc` (2845 lines) — register defaults for all 13 versions (0..12), public + internal
- `src/libs/libGraphicsDriver.cpp` — NID → function mapping table (the authoritative NID list)
- `src/graphics/guest_gpu/pm4.h` — PM4 opcodes (IT_*) and custom subcommands (R_*)
- `src/graphics/guest_gpu/pm4.cpp` — PM4 packet dumper
- `src/graphics/guest_gpu/command_processor/pm4Dispatch.cpp` — opcode → handler dispatch tables
- `src/graphics/guest_gpu/command_processor/pm4Handlers.cpp` (4784 lines) — actual packet execution (state tracking)
- `src/graphics/guest_gpu/command_processor/commandProcessor.h` — command processor interface
- `src/graphics/guest_gpu/graphicsRun.cpp/.h` — submit entry points
- `src/graphics/guest_gpu/hardwareContext.h` — HW context state (ShaderRegisters struct)

## PM4 header encoding (confirmed)

```c
#define KYTY_PM4(len, op, r) \
  (0xC0000000u | (((len - 2u) & 0x3fffu) << 16u) | ((op & 0xffu) << 8u) | ((r & 0x3F) << 2u))
```

- Bits 31:30 = 0b11 (type-3)
- Bits 29:16 = `length_dwords - 2` (so total dwords = field + 2)
- Bits 15:8 = opcode
- Bits 7:2 = subcommand R_* (only meaningful for IT_NOP custom packets)
- Bit 1 = SH:GX (0) vs CX (1) — distinguishes shader vs context register file

## PM4 opcodes (IT_*) — confirmed by KytyPS5

Key differences from openagc marked with ←

```
IT_NOP                       0x10  (custom subcommands via R_*)
IT_SET_BASE                  0x11
IT_CLEAR_STATE               0x12
IT_INDEX_BUFFER_SIZE         0x13
IT_DISPATCH_DIRECT           0x15
IT_DISPATCH_INDIRECT         0x16
IT_SET_PREDICATION           0x20
IT_COND_EXEC                 0x22
IT_DRAW_INDIRECT             0x24
IT_DRAW_INDEX_INDIRECT       0x25
IT_INDEX_BASE                0x26
IT_DRAW_INDEX_2              0x27
IT_CONTEXT_CONTROL           0x28
IT_INDEX_TYPE                0x2A
IT_DRAW_INDIRECT_MULTI       0x2C
IT_DRAW_INDEX_AUTO           0x2D
IT_NUM_INSTANCES             0x2F
IT_INDIRECT_BUFFER_CNST      0x33
IT_DRAW_INDEX_OFFSET_2       0x35
IT_WRITE_DATA                0x37
IT_MEM_SEMAPHORE             0x39
IT_DRAW_INDEX_INDIRECT_MULTI 0x38
IT_DISPATCH_DRAW_PREAMBLE    0x3A   ← NEW vs openagc (openagc had 0x8C/0x8D)
IT_INDIRECT_BUFFER           0x3F
IT_COPY_DATA                 0x40
IT_CP_DMA                    0x41
IT_PFP_SYNC_ME               0x42
IT_SURFACE_SYNC              0x43
IT_EVENT_WRITE               0x46
IT_EVENT_WRITE_EOP           0x47
IT_EVENT_WRITE_EOS           0x48
IT_RELEASE_MEM               0x49
IT_DMA_DATA                  0x50
IT_ACQUIRE_MEM               0x58
IT_REWIND                    0x59
IT_SET_SH_REG_INDIRECT       0x63   ← confirms openagc SPRX finding
IT_SET_UCONFIG_REG_INDIRECT  0x64   ← confirms openagc SPRX finding
IT_SET_CONFIG_REG            0x68
IT_SET_CONTEXT_REG           0x69
IT_SET_SH_REG                0x76
IT_SET_QUEUE_REG             0x78
IT_SET_UCONFIG_REG           0x79
IT_SET_UCONFIG_REG_INDEX     0x7A   ← NEW vs openagc (this is the real SetIndexSize opcode)
IT_WRITE_CONST_RAM           0x81
IT_DUMP_CONST_RAM            0x83
IT_INCREMENT_CE_COUNTER      0x84
IT_INCREMENT_DE_COUNTER      0x85
IT_WAIT_ON_CE_COUNTER        0x86
IT_WAIT_ON_DE_COUNTER_DIFF   0x88
IT_DISPATCH_DRAW             0x8D
IT_GET_LOD_STATS             0x8E   ← NEW vs openagc
IT_SET_CONTEXT_REG_INDIRECT  0x9F   ← confirms openagc SPRX finding
```

## Custom NOP subcommands (R_*) — confirmed by KytyPS5

```
R_ZERO           0x00
R_DRAW_RESET     0x05
R_WAIT_FLIP_DONE 0x06
R_DISPATCH_RESET 0x09
R_WAIT_MEM_32    0x0A
R_PUSH_MARKER    0x0B
R_POP_MARKER     0x0C
R_ACQUIRE_MEM    0x14
R_WRITE_DATA     0x15
R_WAIT_MEM_64    0x16
R_FLIP           0x17
R_RELEASE_MEM    0x18
R_DMA_DATA       0x19   ← NEW vs openagc
R_NUM            0x40
```

## Key differences from openagc's current implementation

**openagc has these R_* that DO NOT appear in KytyPS5:**
- R_VS, R_PS, R_DRAW_INDEX, R_DRAW_INDEX_AUTO, R_CS, R_DISPATCH_DIRECT, R_VS_EMBEDDED, R_PS_EMBEDDED, R_VS_UPDATE, R_PS_UPDATE, R_SH_REGS_INDIRECT, R_CX_REGS_INDIRECT, R_UC_REGS_INDIRECT

**KytyPS5 uses real opcodes instead of NOP-wrapped subcommands for:**
- DrawIndexAuto → IT_DRAW_INDEX_AUTO (0x2D) directly, NOT IT_NOP + R_DRAW_INDEX_AUTO
- SetCxRegistersIndirect → IT_SET_CONTEXT_REG_INDIRECT (0x9F) directly, NOT IT_NOP + R_CX_REGS_INDIRECT
- SetShRegistersIndirect → IT_SET_SH_REG_INDIRECT (0x63) directly
- SetUcRegistersIndirect → IT_SET_UCONFIG_REG_INDIRECT (0x64) directly

**These match between openagc and KytyPS5 (NOP-wrapped):**
- AcquireMem → IT_NOP + R_ACQUIRE_MEM (8 dwords)
- SetFlip → IT_NOP + R_FLIP (6 dwords)
- CbReleaseMem → IT_NOP + R_RELEASE_MEM (7 dwords)
- ResetQueue → IT_NOP + R_DRAW_RESET (2 dwords)
- WaitRegMem (64-bit) → IT_NOP + R_WAIT_MEM_64 (9 dwords)

**openagc is MISSING the 32-bit WaitRegMem variant:**
- WaitRegMem (32-bit) → IT_NOP + R_WAIT_MEM_32 (7 dwords) — openagc only has 64-bit

## Register defaults — versioned (0..12)

KytyPS5 has register defaults for versions 0, 4, 5, 7, 8, 9, 10, 11 (with 1→0, 2→0, 3→0, 6→5, 12→10 aliases). Both public (`GraphicsGetRegisterDefaults2`) and internal (`GraphicsGetRegisterDefaults2Internal`) tables.

Layout (`CompactRegisterDefaults`):
- 4 tables (tbl0=cx, tbl1=sh, tbl2=uc, tbl3=??) — each with regs array + pointer offsets
- `types[]` array — 3 dwords per entry: `{type_id, pointer_index_packed, flags}`
- Type IDs match the 0xXXXXXXXX hashes openagc already has in `agc_re.h`

openagc's `register_defaults.c` should be cross-checked against `agcRegisterDefaults.inc` for version 8 (the FW 5.50 version).

## NID table — authoritative

From `libGraphicsDriver.cpp`. Key NEW NIDs openagc should add:

**Gen5 new NIDs:**
- `dolOmWH+huQ` → GraphicsUnknownGetFusedShaderSize
- `fd5Bp5tGTgo` → GraphicsUnknownFuseShaderHalves
- `whb1RL7K4Ss` → GraphicsSetCxRegIndirectPatchSetNumRegisters
- `nCUgItdN2ms` → GraphicsSetShRegIndirectPatchSetNumRegisters
- `fRG-JOH5+sI` → GraphicsSetUcRegIndirectPatchSetNumRegisters
- `Y3ymLfZ1384` → GraphicsUpdatePrimState
- `s+VGAMDQ0AQ` → GraphicsGetDataPacketPayloadRange
- `fPSCdQxgpSw` → GraphicsWriteDataPatchSetAddressOrOffset
- `bxca0BK4FNg` → GraphicsUnknownJumpPatchSetTarget
- `2BS4EtAaF28` → GraphicsJumpPatchSetTarget
- `qj7QZpgr9Uw` → GraphicsUnknownQj7QZpgr9Uw
- `BfBDZGbti7A` → GraphicsGetIsTrinityMode
- `dbOlWdppb4o` → Graphics5UnknownDb (resource semantic DB builder)

**Driver/resource registration new NIDs:**
- `F0ZXt5q0ZTA` → GraphicsDriverGetDefaultOwner
- `F0Y42t-3e18` → GraphicsDriverInitResourceRegistration
- `AOLcoIkQDgM` → GraphicsDriverQueryResourceRegistrationUserMemoryRequirements
- `uJziRsODk1c` → GraphicsDriverGetResourceRegistrationMaxNameLength
- `3AyTaWcF-H8` → GraphicsDriverRegisterWorkloadStream

**CB/DCB/ACB new NIDs:**
- `UZbQjYAwwXM` → GraphicsCbSetShRegistersDirect (multi-reg with run-coalescing)
- `hL7C0IRpWZI` → GraphicsCbQueueEndOfPipeActionGetSize
- `LHFXRrlTPD8` → GraphicsDcbSetCxRegisterDirect
- `1DeUNpRIDDA` → GraphicsDcbSetCxRegisterDirectGetSize
- `LFSPFmGc9Hg` → GraphicsDcbSetWorkloadsActive (different NID than openagc)
- `hEK26Wdny6s` → GraphicsDcbSetWorkloadComplete (different NID)
- `QhCbS4X9Rl8` → GraphicsDcbSetMarker (different NID)
- `Ikfdt-rIqCE` → GraphicsUnknownIkfdtRIqCE
- `-KRzWekV120` → GraphicsUnknownKRzWekV120
- `Lkf86B98qPc` → GraphicsGetPacketSize
- `w6Dj1VJt5qY` → GraphicsSetPacketPredication
- `n8vgpaQg6dA` → GraphicsSetRangePredication
- `ORWsxIbk4TE` → GraphicsCondExecPatchSetEnd
- `YWTKOju587o` → GraphicsCondExecPatchSetCommandAddress
- `k-JpyR2dYAM` → GraphicsCondExecPatchSetEnd (alias)
- `3ZWa3AoyWZQ` → GraphicsCondExecPatchSetCommandAddress (alias)

**Gen5Driver new NIDs:**
- `AhGvpITrf4M` → GraphicsDriverSubmitDcb (second NID, same func)
- `+T8Xo6LtFJI` → GraphicsDriverSubmitMultiDcbs (second NID)
- `Ddwk4gLT5j0` → GraphicsDriverIsCaptureInProgress

## Key packet builder encodings (verified by emulation)

### GraphicsDcbDrawIndexAuto (3 dwords) — DIFFERS from openagc
```c
cmd[0] = KYTY_PM4(3, IT_DRAW_INDEX_AUTO, 0);  // 0xC0004000 | (0x2D << 8)
cmd[1] = index_count;
cmd[2] = decode_draw_index_initiator(modifier) | 0x2;
```
openagc currently uses `IT_NOP + R_DRAW_INDEX_AUTO` — **should switch to IT_DRAW_INDEX_AUTO (0x2D).**

### GraphicsDcbSetIndexSize (3 dwords) — confirms openagc SPRX finding
```c
cmd[0] = KYTY_PM4(3, IT_SET_UCONFIG_REG_INDEX, 0);  // 0x7A
cmd[1] = 0x20000243;  // constant
cmd[2] = (type & 3) | (swap << 6) | 0x400;
```

### GraphicsDcbStallCommandBufferParser (2 dwords) — matches openagc
```c
cmd[0] = KYTY_PM4(2, IT_PFP_SYNC_ME, 0);  // 0x42
cmd[1] = 0;
```

### GraphicsDcbAcquireMem (8 dwords, NOP-wrapped) — matches openagc
### GraphicsDcbWriteData (4+num_dwords) — matches openagc
### GraphicsDcbSetFlip (6 dwords, NOP-wrapped) — matches openagc
### GraphicsCbReleaseMem (7 dwords, NOP-wrapped) — matches openagc
### GraphicsDcbResetQueue (2 dwords, NOP + R_DRAW_RESET) — matches openagc
### GraphicsDcbJump (4 dwords, IT_INDIRECT_BUFFER 0x3F) — matches openagc
### GraphicsDcbRewind (2 dwords, IT_REWIND 0x59) — matches openagc
### GraphicsDcbCondExec (5 dwords, IT_COND_EXEC 0x22) — matches openagc

### GraphicsDcbEventWrite (2 or 4 dwords)
```c
// event_type 7/15/16: cmd[1] = 0x400 | event_type (2 dwords)
// addressed event (0x38-0x39): cmd[1] = 0x100 | event_type, cmd[2..3] = addr (4 dwords)
// other: cmd[1] = event_type & 0x3f (2 dwords)
cmd[0] = KYTY_PM4(size, IT_EVENT_WRITE, 0);
```

## Submit descriptor (ACB) — NEW finding

ACB submit has a descriptor header at the start of the buffer:
```c
if (size >= 5 && acb[3] == 0 && acb[4] == 0x5533ccaau) {
    descriptor_addr = acb[0] | (acb[1] << 32);
    descriptor_size = acb[2];
    acb = descriptor_addr;
    size = descriptor_size;
}
```
Magic `0x5533ccaa` — this is the ACB indirect descriptor. openagc's ACB submit does NOT handle this.

## Shader record struct (Shader) — confirmed layout

```c
struct Shader {
    uint32_t file_header;      // 0x34333231 ("1234")
    uint32_t version;          // 0x00000018
    ShaderUserData* user_data;
    const volatile void* code;
    ShaderRegister* cx_registers;
    ShaderRegister* sh_registers;
    ShaderSpecialRegs* specials;
    ShaderSemantic* input_semantics;
    ShaderSemantic* output_semantics;
    uint32_t header_size, shader_size, embedded_constant_buffer_size_dqw, target;
    uint32_t num_input_semantics;
    uint16_t scratch_size_dw_per_thread, num_output_semantics, special_sizes_bytes;
    uint8_t type, num_cx_registers, num_sh_registers;
};
```

## CommandBuffer struct — confirmed layout

```c
struct CommandBuffer {
    uint32_t* bottom;        // 0x00
    uint32_t* top;           // 0x08
    uint32_t* cursor_up;     // 0x10  (write cursor)
    uint32_t* cursor_down;   // 0x18  (end of available space)
    Callback  callback;      // 0x20  (grow callback)
    void*     user_data;     // 0x28
    uint32_t  reserved_dw;   // 0x30
};
```

## Action items for openagc

1. **Switch DrawIndexAuto from NOP-wrapped to IT_DRAW_INDEX_AUTO (0x2D)** — KytyPS5 confirms the real opcode is used.
2. **Add 32-bit WaitRegMem variant** (R_WAIT_MEM_32, 7 dwords) — openagc only has 64-bit.
3. **Add missing builders:** SetCxRegisterDirect, SetCxRegisterDirectGetSize, SetShRegistersDirect (multi-reg with run-coalescing), CbNop, CbDispatch, CbBranch, GetLodStats, SetPredication, CondExecPatchSetEnd, CondExecPatchSetCommandAddress, GetPacketSize, SetPacketPredication, SetRangePredication.
4. **Add missing patchers:** SetCxRegIndirectPatchSetNumRegisters, SetShRegIndirectPatchSetNumRegisters, SetUcRegIndirectPatchSetNumRegisters, WriteDataPatchSetAddressOrOffset, JumpPatchSetTarget, WaitRegMemPatchAddress (32-bit variant).
5. **Add missing driver funcs:** SubmitMultiDcbs, SubmitCommandBuffer, SubmitMultiCommandBuffers, SubmitAcb, SubmitMultiAcbs, GetEqEventType, IsCaptureInProgress, RegisterWorkloadStream, QueryResourceRegistrationUserMemoryRequirements, InitResourceRegistration, GetResourceRegistrationMaxNameLength, GetDefaultOwner.
6. **Add ACB descriptor indirection** (magic 0x5533ccaa) to prospero ACB submit.
7. **Cross-check register defaults** for version 8 against `agcRegisterDefaults.inc`.
8. **Add IT_DISPATCH_DRAW_PREAMBLE (0x3A)** — openagc has 0x8C/0x8D but KytyPS5 has 0x3A for the preamble.
9. **Add IT_GET_LOD_STATS (0x8E)** — new opcode for LOD stats.
10. **Add R_DMA_DATA (0x19)** custom subcommand.
11. **Verify SetIndexSize** uses IT_SET_UCONFIG_REG_INDEX (0x7A) — openagc already found this via SPRX, now confirmed.
12. **Add fused shader support** (GraphicsUnknownGetFusedShaderSize / FuseShaderHalves).