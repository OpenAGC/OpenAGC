# FW 5.50 Indexed and Indirect Draw Qualification

Date: 2026-07-27  
Hardware: standard PS5 gfx1013  
Firmware: raw `0x05500008`, ABI profile `0x0550`  
Deployment: foreground curl/websrv only

## Passing matrix

| Case | DCB dwords | Changed FP16 pixels | Bounds | Fence | Flips |
| --- | ---: | ---: | --- | --- | --- |
| Direct u16 indexed | 2,470 | 255,744 | 768x665 | PASS | 1,800/1,800 |
| Non-indexed indirect | 2,468 | 255,744 | 768x665 | PASS | 1,800/1,800 |
| U16 indexed-indirect | 2,476 | 255,744 | 768x665 | PASS | 1,800/1,800 |

Every passing case produced eight sampled FP16 colors, 112,198 exact opaque
samples, zero out-of-range components, the expected x=384..1151 and
y=436..1100 bounds, a passing Wave32 NGG/PS register audit, marker
`0xdeadcafe`, and the centered 768x768 CPU preview.

The direct indexed case validated `DRAW_INDEX_2` against a three-element u16
buffer. The non-indexed indirect case used a 16-byte argument record
`{3,1,0,0}`. The indexed-indirect case used a 20-byte record
`{3,1,0,0,0}` plus the same u16 index buffer.

## Corrected SET_BASE incident

The first non-indexed indirect attempt submitted but timed out its 200 ms EOP
fence. The wrapper passed `1` as the low header-control parameter to
`sceAgcDcbSetBaseIndirectArgs`; that helper already writes PM4 base index one
in its payload, so the parameter made the packet header noncanonical. The
homebrew process exited, websrv returned HTTP 200 afterward, and no reboot was
required.

The application wrapper now passes control value zero. An exact host fixture
requires `agcPm4Header3(AGC_PM4_OP_SET_BASE, 4)` with no low control bits. The
rebuilt indirect case then passed the complete oracle, followed by the passing
indexed-indirect case.

## Artifacts

ELF SHA-256:

```text
42b51b13bf4b15c3b3e3b840411da678e35cc2f5aebcdfeebf2fe3a0dce8a1bb  agc_graphics_indexed.elf
0afc71d547eec8d2b24ce26e14f020511b33673e364b4eb07ee51778fb42a964  agc_graphics_indirect.elf
8a63a1205fb0275b6707e78f126fc69fc6617817eacb974ce2552fb7f7e56bc3  agc_graphics_indexed_indirect.elf
```

Log SHA-256:

```text
c2ca7bc37148570c1950891d4fe6f248a983c587fa7e51bcec113de8d1040ee9  indexed-run1.log
2ea2e39331f38b241bf1965eb0deda6e2242edd90d0fcbbbc2f3dedaa54e468c  indirect-run1.log
5e3a57ba592033fc47894ee14d6382822301927a241e24a1a8dae557dd259c13  indirect-run2-canonical-header.log
b4d8fe2e35bcfb8b5c25fbc2ea4b6c5e8f6b9da7ded52459ba7e5bb01512745a  indexed-indirect-run1.log
```

Raw logs remain under
`samples/hw_test/conformance-logs/indexed-indirect-20260727/`.
