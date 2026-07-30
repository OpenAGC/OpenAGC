# FW 5.50/FW 11.60 offline portability audit (2026-07-30)

## Result boundary

The offline evidence supports one baseline `/dev/gc` binary across the exact
FW 5.50 and standard-PS5 FW 11.60 profiles. It does not replace the pending
same-byte FW 5.50 hardware run: GPU execution, VideoOut, teardown, relaunch,
and absence of a kernel panic remain hardware-only claims.

`tools/verify_fw550_fw1160_compatibility.py` mechanically locks every
non-provenance difference listed below. The underlying extractors reproduced
all 39 active rows directly from `/Volumes/Untitled/unp`; the clean generic
suite passed 5,650 assertions and the clean Prospero cross-build completed
without warnings.

## Shared baseline ABI

| Area | Mechanically identical endpoint contract |
|---|---|
| Submission | `0xc0108102`; request `u32@0,u32@4,u64@8`; common carrier, DCB wrapper, and multi-DCB wrapper |
| Queue | Create `0xc0408121`, destroy `0xc00c810e`, four authentication tokens, EOP ring `0x39000`, read pointer `0x1c8000`, metadata `0x1cc000`, pipe `0xc`, ring size `0x1000` |
| Standard memory | GPU info `0x100000`, trap code/data `0x4000`, DDID `0xfc000`, EOP FIFO `0x3c000`, shadow `0x4000`, CWSR `0x1000000`, misc `0x4000`, work offset `0xa00000` |
| Suspend | Primary `0xc010811c` with four dwords; final `0xc0108139`; public Direct exports remain permission stubs |
| TF ring | `0x80108128`; `u64@0,u32@8,u32@0xc`; address `0x100` and size `4` validation |
| HS offchip | `0xc010812c`; `u64@0,u32@8,u32@0xc` |
| Sony workload packet | Active/complete maxima `18/12` dwords; common nine-dword packet `0xc0071e00`; identical ranges and controls |
| Defaults selection | Same `sceAgcInit` argument flow, runtime-record offset `0x44`, table stride `0x50`, and pointer-returning runtime wrappers |
| Register shadow | Address `0xfe0000000`, 2 MiB aperture/alignment, protection `0x33`, identical two-slice layout and descriptor words |

The implementation fixture feeds 39 nonzero-suffix full raw versions through
normalization and exact profile construction. It verifies BCD major/minor
decoding, fail-closed selection, every common baseline capability, and explicit
V7 acceptance for all 39 profiles.

## Classified endpoint differences

| Difference | FW 5.50 | Standard FW 11.60 | Portability effect |
|---|---|---|---|
| Defaults maximum | V9 | V12 | None for the pinned common-V7 caller |
| Trinity memory/predicate | Absent | Present for supported Pro branches | Standard-console values remain identical; runtime model selection is exact |
| Workload policy | Hardware-qualified OpenAGC one-ID extension | Disabled after the Sony nine-dword adapter stalled | Optional and excluded from the portability baseline |
| EOP flip | Hardware-qualified | Disabled | Optional and excluded from the portability baseline |
| ACB wrapper | 67-instruction normalized group | 51-instruction normalized group | Kernel submit request and DCB/multi-DCB carrier are unchanged |
| Final/query suspend wrappers | Different normalized compiler/carrier groups | Different normalized compiler/carrier groups | Commands and public permission-stub boundary remain explicit |
| TF wrapper | Older 31-instruction carrier | Later 33-instruction carrier | Payload ABI is identical; later wrapper adds reserved-field handling |
| Register-shadow constructor | No Gn4 or Trinity branch | Gn4 plus Trinity predicate | Exact runtime profile selects standard or Trinity state |
| Qualification | FW 5.50 hardware-qualified | Standard FW 11.60 hardware-qualified | Other 37 rows remain SPRX-qualified/hardware-unverified |

Different SPRX virtual addresses, instruction fingerprints, and provenance
paths are recorded but are not `/dev/gc` ABI differences. The verifier fails
if any new layout or policy difference appears without being classified.

## Pinned no-rebuild inputs

| Role | Preserved path | SHA-256 |
|---|---|---|
| Portability payload | `samples/hw_test/pinned/agc_portability-e04004fee2254e6169805f153ce4812197726ed5f53a9295a4493f0d8ac9a9ce.elf` | `e04004fee2254e6169805f153ce4812197726ed5f53a9295a4493f0d8ac9a9ce` |
| Read-only firmware probe | `samples/hw_test/pinned/agc_firmware_probe-b88790c948a98810cf2fb4799a3ea529cc07e1e18532047e8780e72bde0ce7a7.elf` | `b88790c948a98810cf2fb4799a3ea529cc07e1e18532047e8780e72bde0ce7a7` |
| Renderer cleanup | `../Vulkan-PS5/build-prospero-m2/vulkan_ps5_process_cleanup.elf` | `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76` |

The probe depends only on kernel, LibcInternal, and Net. It reads the full
system-software value, writes a file-backed raw/key verdict, performs no GPU
authorization or `/dev/gc` operation, and self-terminates.

`tests/test_portability_runner.sh` simulates websrv/FTP and proves that a bad
local payload hash, bad local cleanup hash, corrupted uploaded cleanup, or
wrong console key stops before the unsafe next stage. Its passing path also
locks three cleanup launches—before the probe and immediately before both
payload iterations—and two payload launches.

## FW 5.50 preflight checklist

1. Boot the standard FW 5.50 console and confirm websrv HTTP `8080` and FTP
   `2121` are responsive at `10.0.1.41`.
2. Do not rebuild or substitute any pinned input. Run:

   ```sh
   cd samples/hw_test
   PS5_HOST=10.0.1.41 \
   PS5_PAYLOAD_SDK=/Users/bizkut/ps5-payload-sdk \
   make portability_fw550
   ```

3. The runner must authenticate all three local hashes before networking.
4. It uploads and downloads the cleanup and probe ELFs and authenticates the
   remote bytes, runs cleanup, then requires a file-backed full raw value whose
   high four digits are exactly `0x0550`.
5. Only after that match does it upload/download/authenticate the pinned
   portability ELF. Cleanup runs again immediately before each of two payload
   launches.
6. Each launch must report exact profile selection, V7 defaults and async
   success, GPU marker `0x504f5254`, two flips, zero-valued teardown, final
   `PASS`, self-termination, and a successful second launch.
7. Stop on any hash mismatch, wrong console key, missing file-backed verdict,
   timeout, failure marker, unclean teardown, unresponsive UI, or kernel panic.

This checklist requires no source or binary rebuild. Its eventual two-pass FW
5.50 log is the only remaining endpoint portability gate.
