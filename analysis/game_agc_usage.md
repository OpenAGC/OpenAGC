# Game Binary AGC Usage Analysis

## Source
- Game: "New Joe & Mac Caveman Ninja" (PPSA02801, v01.003)
- ELF size: 29.7 MB (Unity IL2CPP engine)
- SDK: PS5 5.00 (based on build path `PS5_5_00_nondev_i_m`)

## AGC Import Summary

The game imports **71 AGC functions** across two SPRX modules:

### libSceAgc.sprx (user-facing API): 62 functions
- **38 implemented** in openagc
- **22 missing** (some are wrappers around libSceAgcDriver)

### libSceAgcDriver.sprx (driver-facing API): 9 functions
- **2 implemented** (sceAgcDriverSubmitDcb, sceAgcDriverSubmitAcb)
- **7 missing** (but 2 are trivial stubs)

## Missing libSceAgcDriver Functions

### sceAgcDriverRegisterOwner (NID: X-Nm5KLREeg)
- **STUB**: Returns `0x8a6c9018` (AGC_ERROR_NOT_SUPPORTED)
- Size: 6 bytes (`mov eax, 0x8a6c9018; ret`)

### sceAgcDriverRegisterResource (NID: W5z4eZrjEas)
- **STUB**: Returns `0x8a6c9018` (AGC_ERROR_NOT_SUPPORTED)
- Size: 6 bytes (`mov eax, 0x8a6c9018; ret`)

### sceAgcDriverGetEqContextId (NID: Zw7uUVPulbw)
- Calls internal function at 0xad00, right-shifts result by 16
- Returns the EQ (event queue) context ID

### sceAgcDriverSetTFRing (NID: XlNp7jzGiPo)
- Non-Direct variant. Checks SDK version, clamps TF ring size to 0x4000
- Jumps through function pointer table based on SDK version

### sceAgcDriverSetHsOffchipParam (NID: MM4IZSEYytQ)
- Non-Direct variant. Trampoline through function pointer table.
- Different from sceAgcDriverSetHsOffchipParamDirect (NID: DPcAnsOlTQs)

### sceAgcDriverAgrSubmitDcb (NID: AhGvpITrf4M)
- Checks a flag at [global + 0x148]. If set, submits via internal path.
- If not set, returns `0x8a6d0003` (AGR not initialized)

### sceAgcDriverAddEqEvent (NID: w2rJhmD+dsE)
- Sets up an event queue with type 0x1fff2
- Calls internal function at 0xace0

## Missing libSceAgc Functions

### Packet Builders (DCB)
- **sceAgcDcbAcquireMem** (0x02f30, 0x02af bytes) — IT_ACQUIRE_MEM for DCB
- **sceAgcDcbCopyData** (0x03c10, 0x0131 bytes) — IT_COPY_DATA for DCB
- **sceAgcDcbJump** (0x03b30, 0x00d4 bytes) — IT_JUMP for DCB
- **sceAgcDcbResetQueue** (0x064d0, 0x0051 bytes) — queue reset for DCB
- **sceAgcDcbSetIndexCount** (0x05ec0, 0x008e bytes) — set index count
- **sceAgcDcbSetIndexSize** (0x05d70, 0x00ab bytes) — set index size
- **sceAgcDcbSetNumInstances** (0x05f50, 0x0085 bytes) — set instance count
- **sceAgcDcbStallCommandBufferParser** (0x05fe0, 0x0081 bytes) — stall parser
- **sceAgcDcbDrawIndex** (0x04760, 0x00d5 bytes) — indexed draw

### Packet Builders (CB)
- **sceAgcCbSetShRegisterRangeDirect** (0x02570, 0x00d8 bytes) — set SH reg range
- **sceAgcCbSetUcRegistersDirect** (0x029f0, 0x02c3 bytes) — set UC regs

### Patcher Functions
- **sceAgcSetShRegIndirectPatchSetAddress** (0x0b0d0, 0x0038 bytes)
- **sceAgcSetShRegIndirectPatchAddRegisters** (0x0b150, 0x0048 bytes)
- **sceAgcSetCxRegIndirectPatchSetAddress** (0x0b1a0, 0x0038 bytes)
- **sceAgcSetCxRegIndirectPatchAddRegisters** (0x0b220, 0x0048 bytes)
- **sceAgcSetUcRegIndirectPatchSetAddress** (0x0b270, 0x0038 bytes)
- **sceAgcSetUcRegIndirectPatchAddRegisters** (0x0b2f0, 0x0048 bytes)

### Utility Functions
- **sceAgcSetNop** (0x0b530, 0x0007 bytes) — 7-byte NOP setter
- **sceAgcCreateShader** (0x0c380, 0x036a bytes) — shader record parser
- **sceAgcCreatePrimState** (0x0e2d0, 0x00ff bytes) — primitive state builder
- **sceAgcDebugRaiseException** (0x08970, 0x0005 bytes) — 5-byte debug stub
- **sceAgcGetDataPacketPayload** — data packet payload getter

### Init/Config Functions
- **sceAgcInit** (0x084a0, 0x0033 bytes) — user-facing init wrapper
  - Calls internal init at 0x75e0 which:
    1. Locks mutex
    2. Checks SDK version via sceKernelGetProsperoCompiledSdkVersion
    3. Gets app info via sceKernelGetAppInfo
    4. Checks title workarounds (0x52, 0x53)
    5. Calls internal register defaults init (0xe8f0)
    6. Calls internal register defaults internal init (0x119a0)
- **sceAgcGetRegisterDefaults2** (NID: 2JtWUUiYBXs) — get register defaults
- **sceAgcGetRegisterDefaults2Internal** (NID: wRbq6ZjNop4) — get internal defaults

### Wrapper Functions
- **sceAgcSuspendPoint** (0x07560, 0x0074 bytes) — wrapper that calls
  sceAgcDriverSuspendPointSubmit (NID: QcmHLO2n7mk) via PLT

## Architecture Insight

The PS5 AGC stack has two layers:

1. **libSceAgc.sprx** — User-facing API that games link against.
   Contains packet builders (sceAgcDcb*, sceAgcCb*, sceAgcAcb*),
   patchers, shader/state creators, and init wrappers.

2. **libSceAgcDriver.sprx** — Driver-facing API for ioctl submission.
   Contains submit functions, queue management, and hardware control.
   Games also link some of these directly.

For openagc to work with real games, we need to provide exports from
**both** modules. The libSceAgc functions are mostly packet builders
(which we already have for many), while the libSceAgcDriver functions
are the ioctl wrappers (which we have for the Direct variants but not
the non-Direct variants).
