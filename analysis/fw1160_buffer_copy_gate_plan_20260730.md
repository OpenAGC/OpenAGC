# FW 11.60 standalone buffer-copy gate plan (2026-07-30)

## Scope

FW 5.50 proved the application-facing raw gfx1013 `DMA_DATA` copy through an
SDL consumer, but that run did not have an exact payload oracle. The new
standalone gate removes rendering and VideoOut from the result:

1. allocate separate 8,294,400-byte flexible-memory source and destination;
2. fill the source with a deterministic word pattern and destination with a
   sentinel;
3. flush both mappings;
4. emit `agcGfx1013CopyBuffer`, which splits the copy into four seven-dword
   raw `DMA_DATA` packets;
5. transition copy-destination to host-read with an EOP completion fence;
6. invalidate the destination and compare every word plus native FNV64 hashes.

No shader, render target, VideoOut, HTILE, MSAA, or workload packet is used.

## Exact artifacts

| Profile | ELF | SHA-256 |
| --- | --- | --- |
| `0x1160` logged | `agc_copy_fw1160_logged.elf` | `ea3032b4eff182d035510acc82a768ab34dd6777e1f2f65eae2a4eaafd68c1fb` |
| `0x0550` headless mirror | `agc_copy_fw550_headless.elf` | `dd9670b4836cab96e84d033d855b0cad3da4e49f5571b2f29c56e387cc13ae11` |

Both artifacts cross-build without warnings from current source and the
current `libopenagc.a`. Partial initialization is cleanup-safe: once
`sce_agc_initialize` succeeds, every later failure still calls
`agcDriverShutdown`.

## Fail-closed runner

`run_fw1160_copy.sh` uses the established cleanup-first websrv flow and the
file-backed daemon verdict on FW 11.60. It requires:

- exact four-digit runtime ABI selection;
- submit success reporting four packets and exactly 8,294,400 bytes;
- bounded completion-fence PASS;
- zero mismatched words and exact source/destination FNV64
  `0xdd3702089b80f950`;
- driver shutdown PASS and final copy PASS;
- no failure, fatal, mismatch, or timeout text.

## FW 11.60 hardware result

The exact artifact passed twice on the standard PS5 reporting raw firmware
`0x11600005`. Both runs submitted a 38-dword DCB containing four raw DMA
packets for exactly 8,294,400 bytes, reached the completion fence, found zero
of 2,073,600 words mismatched, and reproduced source/destination FNV64
`0xdd3702089b80f950`. Driver shutdown and final copy verdict passed.

ps5debug-NG found no residual `eboot` after either run; ports 744 and 8080
remained reachable. This independently hardware-qualifies the application-
facing raw buffer-copy composition on the tested FW 11.60 console. The
current-source FW 5.50 mirror remains a separate regression requirement when
that console becomes reachable.
