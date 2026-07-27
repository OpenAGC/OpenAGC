# FW 5.50 Combined Stencil/HTILE Expclear Qualification

Date: 2026-07-27  
Hardware: standard PS5 gfx1013  
Firmware: raw `0x05500008`, ABI profile `0x0550`  
Deployment: foreground curl/websrv only

## Result

Combined depth/stencil HTILE expclear passed its independent activation gate.
The public enable constant remained zero while three aspect masks were each
executed twice from identical ELFs. All six runs completed without a GPU reset,
kernel panic, metadata spill, timeout, or loader failure.

| Case | Runs | Exact HTILE result | Selected words | Outside changes | D32/S8 | Fence | Flips |
| --- | ---: | --- | ---: | ---: | --- | --- | --- |
| Depth only | 2 | `0xfffc0300` | 49,152 | 0 | PASS | PASS | 1,800/1,800 |
| Stencil only | 2 | `0xfffff0ff` | 49,152 | 0 | PASS | PASS | 1,800/1,800 |
| Depth + stencil | 2 | `0xfffc00f0` | 49,152 | 0 | PASS | PASS | 1,800/1,800 |

Every RMW covered offset `0x0`, size `0x30000`, preserved all bits outside its
aspect mask, and reached fence value `0x48544c45`. Every graphics submission
then reached marker `0xd32fffff` and all four stage markers.

The six downstream readbacks were identical:

- Color: 128,304 green and 128,304 red pixels.
- D32: 1,955,232 clear-one, 128,304 near, and 128,304 far words.
- S8: 2,364,832 zero, 256,608 `0x5a`, and zero unexpected bytes.
- VideoOut: 1,800 accepted and 1,800 completed flips.

## Artifacts

ELF SHA-256:

```text
8cabbf85fa0540bb3d79b0456390fa953aed1bc9c491847bbb891708cc829b02  agc_depth_stencil_expclear_depth.elf
baf103692ae0229116fde9a8a24094c1b25622b03e7fb3f7fb4268ab5afda344  agc_depth_stencil_expclear_stencil.elf
9d4174e1e969a05cb59905d81bdbdcbacaf7cb9ee8106308196b8e57a0109885  agc_depth_stencil_expclear_both.elf
```

Raw log SHA-256:

```text
119dbc17eb297b424ea2c2c1937dfc46da86c45975fc1cf0024a8d77f8c8d81c  depth-run1.log
50074afc388dc624536170d7e60a8479993682a08c4ba9d6d4aae6b48f4233ef  depth-run2.log
af78ae99a75de85c33760a33437f3c68d6e8aa63ac7d21a24db994737e5b390c  stencil-run1.log
607ea3191c25680aab843be8ac882239953ea9c493515e726590f8a9f151b911  stencil-run2.log
2eaef92332c410920869a8f61bf922af9e8cb0bce27f221cd648dbca9cb0892a  both-run1.log
17f19f635dccb7a4cbedd81efc4940e79bbd510f78c80873b497f18655519ff5  both-run2.log
```

Raw logs remain in
`samples/hw_test/conformance-logs/combined-expclear-20260727/`.

## Enabled API

Qualification enables `AGC_GFX1013_COMBINED_HTILE_EXPCLEAR_ENABLED`.
Applications can build exact aspect plans, set depth and stencil clear
registers selectively, bind matching `ALLOW_EXPCLEAR` bits, and use
`agcGfx1013RmwHtile` for a non-tail mip/layer metadata range. Shared mip tails
remain rejected because an exact independently owned range cannot be proven.
