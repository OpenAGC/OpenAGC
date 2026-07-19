# Reference Projects (host-only, do NOT copy code)

## SharpEmu — `/Users/bizkut/Downloads/PS5/homebrew/sharpemu`
Most directly useful PS5 AGC HLE. GPL-2.0-or-later (reference only, don't copy into MIT tree).
- `src/SharpEmu.Libs/Agc/AgcExports.cs` — NID→function map
- `src/SharpEmu.Libs/Agc/Gen5ShaderTranslator.cs`
- `src/SharpEmu.Libs/VideoOut/VideoOutExports.cs`, `VulkanVideoPresenter.cs`
- `src/SharpEmu.Core/Cpu/Native/DirectExecutionBackend.Imports.cs`

Key findings: type-3 PM4 header layout (`lengthDwords-2` in bits 29:16), AGC IT_NOP subcommands, command encodings for DCB flip/write-data/wait-reg-mem/ACB acquire-mem/DMA/draw/index/marker/submit parsing, shader header validation + pointer relocation + register patching + primitive-state creation + interpolant mapping + small Gen5 shader translation path.

## RPCSX — `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`
GPU/PM4/GNM reference (NOT direct AGC HLE). GPLv2.
- `rpcsx/gpu/lib/gnm/include/gnm/pm4.hpp`, `src/pm4.cpp` — confirms type-3 length layout
- `rpcsx/gpu/Pipe.cpp` — IT_SET_SH_REG, DISPATCH_DIRECT/INDIRECT, RELEASE_MEM, WAIT_REG_MEM, WRITE_DATA, INDIRECT_BUFFER, ACQUIRE_MEM, DMA_DATA, draw/register packets
- `rpcsx/gpu/DeviceCtl.cpp` — graphics submit, IB validation, VMID patching, flip-on-EOP, compute queue mapping
- `rpcsx/gpu/lib/gnm/include/gnm/descriptors.hpp` — 16-byte VBuffer, 32-byte TBuffer, 16-byte sampler
- `rpcsx/gpu/lib/gnm/include/gnm/constants.hpp` — data formats, number types, primitive types, texture types, swizzles, blend factors, compare funcs, sampler state
- `rpcsx/gpu/lib/amdgpu-tiler/src/tiler.cpp` — AMD surface layout (validates tiling/swizzle)
- `rpcsx/gpu/lib/gcn-shader/src/` — GCN→SPIR-V (GCN-oriented, not RDNA2-native)

## opengnm — `../opengnm` (sibling)
PS4 GNM clean rewrite, considered complete and workable. Reference for structures/APIs used by apps/games. PS4 `sceGnm*` belongs there, NOT in openagc.

## Firmware — `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50`
- SPRX: `sprx/common_lib/libSceAgc.sprx`, `libSceAgcDriver.sprx`, `libSceAgcVsh.sprx`
- Microcode: `oberon_c0_{ce,me,mec,pfp,rlc,sdma0,sdma1}.bin`
- Kernel dump: `5.50-kv-dump/merged/kernel_550_merged_by_offset.bin`
RE-only. Never copy/embed/commit. Constants → `include/agc_re.h`, `agc_nids.h`, `agc_ioctl.h`.

## PS5 SDK — `/Users/bizkut/Downloads/PS5/sdk` (install `~/ps5-payload-sdk`)
Clang 18 + LLD 18, x86_64 PS5 FreeBSD-ish. Defines `__PROSPERO__`. Provides crt, libc, libufs, kernel headers, sce_stubs. See `mem:tech_stack`.