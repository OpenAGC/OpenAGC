# Game Compatibility

## Coverage (100%)
| Game | Title ID | AGC imports | Implemented | Missing |
|------|----------|-------------|-------------|---------|
| Joe & Mac Caveman Ninja | PPSA02801 | 70 | 70 | 0 |
| PPSA09076 (backport) | PPSA09076 | 69 | 69 | 0 |
| PPSA03157 | PPSA03157 | 58 | 58 | 0 |

Total unique AGC functions across all 3 games: **72. All implemented.**

## Joe & Mac (PPSA02801, v01.003)
- Engine: Unity IL2CPP, ELF 29.7MB
- SDK: PS5 5.00 (build path PS5_5_00_nondev_i_m)
- 70 imports: 61 libSceAgc + 9 libSceAgcDriver
- Analysis: `analysis/game_agc_usage.md`

## PPSA09076 (01.000.000 backport)
- 69 imports: 60 libSceAgc + 9 libSceAgcDriver
- Same as Joe & Mac minus `sceAgcInit`, `sceAgcGetDataPacketPayload`

## PPSA03157
- 58 imports: 52 libSceAgc + 6 libSceAgcDriver
- Smallest set; lacks sceAgcAcbJump/CopyData/PopMarker/PushMarker, sceAgcCbSetUcRegistersDirect, sceAgcDebugRaiseException, sceAgcSetNop, sceAgcDriverGetEqContextId/RegisterOwner/RegisterResource
- Uses `sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate` (unique to this game)

## Architecture insight
PS5 AGC stack has two layers games link against:
1. **libSceAgc.sprx** — user-facing API: packet builders (sceAgcDcb*/Cb*/Acb*), patchers, shader/state creators, init wrappers
2. **libSceAgcDriver.sprx** — driver-facing: submit functions, queue mgmt, hardware control. Games link some directly.

openagc must provide exports from BOTH modules.

## Implemented missing functions (across 3 games)
1. libSceAgcDriver stubs — RegisterOwner/RegisterResource return 0x8a6c9018 (not supported on non-dev per SPRX)
2. Non-Direct driver variants — SetTFRing/SetHsOffchipParam delegate to Direct variants
3. DCB builders — AcquireMem, CopyData, Jump, ResetQueue, SetIndexCount, SetIndexSize, SetNumInstances, StallCommandBufferParser, DrawIndex
4. CB register setters — SetShRegisterRangeDirect, SetUcRegistersDirect
5. Indirect register patchers — 6 functions for Sh/Cx/Uc register indirect write patching
6. Utility — SetNop, DebugRaiseException, GetDataPacketPayload, CreateShader, CreatePrimState
7. Wrappers — sceAgcInit, sceAgcSuspendPoint, sceAgcGetRegisterDefaults2/2Internal
8. DmaData Src patcher — sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate (SPRX-confirmed: checks raw DMA_DATA 0x50, patches cmd[2..3]). Also fixed Dst patcher to accept both raw and NOP-wrapped formats.