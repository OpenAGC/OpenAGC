# openagc

openagc is a clean-rewrite PS5 AGC (Advanced Graphics Controller) library
for homebrew and emulator-port work. It follows the recovered `sceAgc*` /
`sceAgcDriver*` naming and command-buffer split used by the PS5 firmware, while
keeping the implementation buildable without proprietary SDK headers.

The current tree is a foundation layer, not a complete official-SDK drop-in.
Host builds provide testable AGC packet, command-buffer, submit-descriptor,
descriptor, and shader helpers. The PS5 `/dev/gc` backend is implemented and
builds with ps5-payload-sdk; hardware validation is the remaining step.

## Features

- **AGC driver API surface** — `sceAgcDriver*` submission, SDMA, setup
- **ACB command building** — `sceAgcAcb*` async compute buffer packets
  (event write, atomic mem/GDS, cond exec, wait-reg-mem, write/copy/dma data,
  mem semaphore, acquire mem, queue reset, rewind, set flip, workload
  markers, prime UTC L2)
- **DCB command building** — `sceAgcVshDcb*` draw command buffer packets
  (clear state, atomic GDS, context state ops, reset queue, set flip, workload
  markers, wait-safe, preemption stub)
- **Context state management** — default state queries, primary/internal
  register-defaults blob builder, and `CLEAR_STATE` submission path
- **Gen5 AGC/PM4 packet encoding** — PS5 packet header helpers and decoded
  opcode/subcommand constants (including kernel-side suspend-point marker)
- **RE packet model** — SharpEmu/RPCSX-compatible type-3 packet length and
  AGC `IT_NOP` subcommand helpers
- **Command-buffer cursor model** — recovered `SceAgcCb` cursor offsets and
  host allocation helpers
- **Sony-style CB builders** — recovered `sceAgcCb*` / `sceAgcDcb*` packet
  builders for NOP, dispatch, SH registers, write-data, wait-reg-mem, DMA,
  indirect dispatch/base args, index buffers, indexed draws, markers, wait-safe,
  and flip
- **Submit packet model** — recovered DCB/ACB submit descriptor layout with
  generic validation/debug capture
- **Suspend points** — RE'd `SUSPEND_16`/`SUSPEND_39` ioctl argument layout and
  in-flight query placeholder
- **Known NID index** — local Gen5 AGC NID constants for mapped exports
- **Texture/buffer descriptors** — RDNA2 SQ_IMG_RSRC / SQ_BUF_RSRC
- **Shader binary format** — RDNA2 ISA shader header parsing
- **Two backend targets:**
  - `generic` — pure software implementation for host testing
  - `orbis` — native PS5 `/dev/gc` backend with ioctl submission and internal
    memory allocation
- **Binary-compatible struct layouts** — `_Static_assert` verified sizes
- **Hardware validation samples** — `samples/hw_test/` builds ELF + fake-SELF
  packages for VideoOut and AGC init smoke tests

## Architecture Differences from opengnm (PS4)

| Aspect | PS4 GNM (opengnm) | PS5 AGC (openagc) |
|--------|-------------------|-------------------|
| GPU | AMD GCN (Liverpool/Gladius) | AMD RDNA2 (Oberon) |
| API prefix | `sceGnm*` | `sceAgc*` |
| Command buffers | PM4 packets (DCB/CCB) | RDNA2 PM4 (DCB/ACB) |
| Shader ISA | GCN (Southern Islands) | RDNA2 (GFX10.3) |
| Wave size | 64 only | 32 or 64 |
| Shader stages | VS/PS/GS/HS/DS/CS | VS/PS/GS/HS/DS/CS + Mesh/Task |
| Context state | PM4 SET_*_REG inline | Flat context state block |
| Preemption | Limited | Full (suspend points) |
| Tiling | GCN macro/micro tile | RDNA2 swizzle modes |
| Memory | Garlic/Onion (GDDR5) | Unified (GDDR6) |
| GPU microcode | Tonga CE/ME/PFP/MEC | Oberon CE/ME/PFP/MEC/SDMA |

## Status

**Phase 1: Reverse-engineering foundation** — In progress.

Completed and tested:
- Core type definitions and error codes
- Driver API header for the currently mapped firmware-exported functions
- Reverse-engineering headers for packet layout, NIDs, shader offsets, and
  command-buffer offsets
- Cursor-based command-buffer allocation using recovered `SceAgcCb` offsets
- SharpEmu-confirmed `sceAgcCb*` and `sceAgcDcb*` packet builders for NOP,
  dispatch, SH registers, release memory, indirect register sets, write-data,
  wait-reg-mem, DMA, indirect dispatch/base args, index buffer setup, indexed
  draw packets, markers, wait-safe, flip, and LOD stats helpers
- ACB packet builders for event write, atomic mem/GDS, cond exec, wait-reg-mem,
  write/copy/dma data, mem semaphore, acquire mem, queue reset, rewind, set
  flip, workload markers, and prime UTC L2
- DCB/VSH packet builders for clear state, atomic GDS, context state ops, reset
  queue, set flip, workload markers, wait-safe, and preemption (SPRX-confirmed
  unimplemented VSH-only stub)
- In-place patchers for DMA-data destination, wait-reg-mem address, and
  end-of-pipe action addresses
- Generic `sceAgcDriverSubmitDcb` and `sceAgcDriverSubmitAcb` validation for
  recovered submit descriptors
- Generic backend with host-side validation/debug capture
- ACB and DCB raw pointer helpers with basic packet emission
- Texture/buffer descriptor helpers
- AGC shader record parser (magic, pointer fields, semantics counts, shader type)
- FW 5.50 register-defaults blob builder and parser with embedded primary/internal tables
- Native orbis `/dev/gc` backend with ioctl submission, internal memory allocation,
  default-state `CLEAR_STATE` submission, and suspend-point submit/query
- Hardware validation samples (`samples/hw_test/`) built as ELF and fake-SELF
- Build system (CMake + Makefile)
- Test suite with 519 passing assertions on the host generic backend

Still open before hardware submission:
- Register-block and Wave32/Wave64 shader parsing (pending observed evidence)
- Actual `/dev/gc` ioctl submission validation on PS5 hardware
- Queue setup and native GPU command submission validation
- Validate default-state blob and suspend-point ioctl behavior on PS5 hardware

### Firmware Reference

Reference inputs used for the open implementation:
- Local firmware dump: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50`
- AGC modules: `libSceAgc.sprx`, `libSceAgcDriver.sprx`, `libSceAgcVsh.sprx`
- GPU microcode names: `oberon_c0_{ce,me,mec,pfp,rlc,sdma0,sdma1}.bin`
- Incomplete reference project: `/Users/bizkut/Downloads/PS5/homebrew/ps5-openagc`
- Emulator reference: `/Users/bizkut/Downloads/PS5/homebrew/sharpemu`
- GPU/PM4 reference: `/Users/bizkut/Downloads/PS5/homebrew/rpcsx`

Firmware modules and microcode are used only as reverse-engineering references;
they are not embedded in this repository.

## Building

### Host (generic, for testing)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOPENAGC_PLATFORM=generic
cmake --build build
ctest --test-dir build
```

Or with Make:

```sh
make
make test
```

Current expected host result:

```text
519 passed, 0 failed
```

### PS5

```sh
export PS5_PAYLOAD_SDK=~/ps5-payload-sdk
cmake -B build-orbis -DOPENAGC_PLATFORM=orbis -DOPENAGC_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/toolchain/prospero.cmake
cmake --build build-orbis
# Output: build-orbis/libopenagc.a
```

The `orbis` backend builds as a native PS5 `/dev/gc` driver backend. It
implements ioctl submission, internal memory allocation, default-state
submission, and suspend-point submission. It requires hardware validation
before it can be considered production-ready.

## Project Structure

```
openagc/
├── include/
│   ├── agc_types.h          # Core type definitions
│   ├── agc_error.h          # Error codes
│   ├── agc_cb.h             # SceAgcCb cursor helpers
│   ├── agc_driver_debug.h   # Generic backend submit debug accessors
│   ├── agc_pm4.h            # RDNA2 PM4 packet definitions
│   ├── agc_nids.h           # Known Gen5 AGC NIDs
│   ├── agc_re.h             # RE constants and offsets
│   ├── agc_texture.h        # Texture/buffer descriptor types
│   ├── agc_shader.h         # Shader binary format
│   └── agcdriver.h          # Main AGC driver API (sceAgc* functions)
├── analysis/
│   ├── agc_packet_model.md  # Decoded packet header/subcommand model
│   └── agc_known_nids.tsv   # Known AGC NID table
├── src/
│   ├── driver_generic.c     # Generic (host) backend
│   ├── driver_orbis.c       # PS5 hardware backend
│   ├── cb.c                 # SceAgcCb cursor allocation
│   ├── cb_builders.c        # sceAgcCb*/sceAgcDcb* packet builders
│   ├── context_state.c      # Context state management
│   ├── acb.c                # ACB command building
│   ├── dcb.c                # DCB command building
│   ├── texture.c            # Texture descriptor helpers
│   └── shader.c             # Shader binary utilities
├── tests/
│   ├── test.h               # Test framework
│   ├── test_main.c          # Test runner
│   ├── test_types.c         # Type/constant tests
│   ├── test_acb.c           # ACB command tests
│   ├── test_cb.c            # Cursor command-buffer tests
│   ├── test_dcb.c           # DCB command tests
│   ├── test_driver.c        # Submit descriptor tests
│   ├── test_texture.c       # Texture descriptor tests
│   └── test_shader.c        # Shader binary tests
├── samples/
│   └── hw_test/             # PS5 hardware validation payloads
│       ├── videoout_linear.c # VideoOut display pipeline smoke test
│       ├── agc_init.c       # AGC init + NOP submit test
│       └── Makefile         # ELF + fake-SELF build targets
├── CMakeLists.txt           # CMake build
├── Makefile                 # Make build
├── PLAN.md                  # Architecture and reverse-engineering roadmap
├── STATUS.md                # Current milestone and roadmap
├── REFERENCE_FINDINGS.md    # Detailed RE notes and conclusions
├── AGENTS.md                # Agent guidance and conventions
├── COPYING                  # MIT license
└── README.md                # This file
```

## Sources

- Clean rewrite, based on local PS5 firmware ABI analysis
- PS5 firmware 5.50 SPRX module references
- AMD RDNA2 public documentation and Mesa packet/register references
- Existing `ps5-openagc` notes for NID maps, ioctl tables, and PM4 cataloging
- `sharpemu` as a PS5 HLE/runtime behavior reference
- `rpcsx` as a GPU/PM4/GNM queue, packet, tiler, and shader reference

## License

MIT, see [COPYING](COPYING).
