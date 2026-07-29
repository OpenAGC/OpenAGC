# FW 11.60 register-shadow constructor state

Source: external standard-console FW 11.60 `libSceAgcDriver.sprx`, SHA-256
`8314b5d388445a3b9a23f787c1b752f84fd35887b9bb2af4def4d49c77bfab3c`.
The firmware image is reference-only and is not part of this repository.

## Recovered state

The module constructor at vaddr `0x74f0` calls the allocation helper at
`0x4f0`. Its standard-console path requests a separate 2 MiB direct-memory
allocation with 2 MiB alignment, memory type `0x0c`, protection `0x33`, and
virtual-address hint `0xfe0000000`. This mapping ends immediately below the
`/dev/gc` register MMIO hint at `0xfe0200000`; it is not the MMIO mapping.
The returned address remains authoritative if the hint is not honored.

The standard path constructs two 40-byte descriptors:

| Field | Descriptor 0 | Descriptor 1 |
| --- | ---: | ---: |
| address | driver base + `0x8000` | driver base + `0x21000` |
| size | `0x19000` | `0x19000` |
| six range words | `0`, `0x3bf`, `0x2000`, `0x2281`, `0x2400`, `0x2843` | identical |
| reserved | `0` | `0` |

The helper at `0x7f70`, called from `0x786a` with its standard-console branch
enabled, then:

1. Publishes `Sce.Debug:Gn2` with DDID and the CWSR shadow-copy range.
2. Names the combined `0x32000` aperture range `SceAgcRegShadow`.
3. Copies the two descriptors into the 16 KiB `SceGnmShadowReg` allocation.
4. Publishes and names `Sce.Debug:Gn3` / `SceAgcGprDumpArea`.
5. Publishes `Sce.Debug:Gn4` from the aperture to `SceGnmShadowReg`.
6. Names the copy, descriptor, and DDID ranges `SceAgcRegShadowCopy`,
   `SceAgcRegShadowInfo`, and `SceAgcDdid`.

The Trinity branch does not execute the same Gn4 path, so OpenAGC gates this
diagnostic state to exact firmware key `0x1160` on a standard console. No
neighboring firmware inherits it without separate evidence.

## Corrected ELF mapping

An initial draft incorrectly read the six leading words from raw file offset
`0x10220`, yielding `{0x12f8, 0xffff7410, 0x1328, 0xffff7450, ...}`. That is
not the data referenced by the instruction at vaddr `0x75d6`.

The sectionless ELF program headers map the read-only `LOAD` segment from file
offset `0x10000` to vaddr `0xc000`. Therefore referenced vaddr `0x10220` maps
to file offset `0x14220`, whose bytes decode to
`{0, 0x3bf, 0x2000, 0x2281}`. The instruction at `0x75e3` supplies the final
two words as packed immediate `0x284300002400`. The incorrect draft bytes
were caught before any hardware launch.

`tools/verify_agc_driver_fw1160.sh` now checks the `LOAD`-mapped bytes,
allocation parameters, helper call, descriptor copy, and all property/range
names. `test_fw1160_register_shadow_descriptors` locks the resulting 80 bytes
and rejects null or overflowing construction.

## Qualification stages

- Stage 14 is the low-risk cache-coherency counterfactual. It is stage 13 with
  the entire 40-dword inline workload DCB flushed instead of only its first
  64-byte cache line. It also resets the workload verdict timer after the
  preflight wait. It does not install Gn2/Gn3/Gn4.
- Stage 15 adds the exact standard-console aperture, descriptors, and
  Gn2/Gn3/Gn4 state to stage 14. The mapping is idempotent within the process
  and is unmapped/released during driver shutdown.

Both are diagnostic gates. FW 11.60 workload capability remains disabled
until one candidate passes twice, followed by an FW 5.50 regression run. Each
hardware launch must use the guarded Make target so the process-cleanup ELF is
the immediately preceding payload. A stalled stage must be killed before any
next payload; reboot before changing backend families.

Build artifacts:

- stage 14 SHA-256: `97b778f0c7fba7daaca99ebd0aa9f89494a19a5bdca644d87adf020d1643c10a`
- stage 15 SHA-256: `7c9251642698cfc1f4c87ee68d8362b6b410c075ea41fd9d23d82845c7d4ac7e`

Stage 15 has no `libSceAgcDriver.sprx` dependency. Its `DT_NEEDED` set is
limited to VideoOut, libkernel, libc, and libnet.

## Stage 14 hardware result

Stage 14 was launched once on standard PS5 FW `0x11600005` through the guarded
runner. The immediately preceding cleanup payload found no stale `eboot.elf`.
Initialization, all nine internal mappings, version-12 defaults, async setup,
the corrected `Sce.Debug:Gnm` property, and stream registration returned
`AGC_OK`. The ordinary preflight submit completed its `0x1160f014` marker in
50 ms. The fully flushed 40-dword inline workload DCB then returned `AGC_OK`,
but neither workload marker nor shutdown text arrived before the 20-second
websrv timeout.

The cleanup payload afterward reported zero stale `eboot.elf` matches, a live
ps5debug-NG process-list query also returned no `eboot.elf`, and websrv plus
TCP 744 remained reachable. This reproduces stage 13 after eliminating partial
cache flushing and the stale timer as causes. Do not repeat stage 14. Reboot
before the higher-risk stage 15 shadow-property gate.
