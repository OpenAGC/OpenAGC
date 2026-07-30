# FW 11.60 combined tessellation/geometry gate plan (2026-07-30)

## Scope

Isolated HS+TES+PS and all three non-tessellation NGG geometry variants are
twice-qualified on FW 11.60. This tier carries the remaining FW 5.50 combined
stage paths:

1. TES-to-NGG geometry;
2. combined geometry invocations;
3. combined geometry line strip;
4. combined geometry rendered directly to BGRA8 UNORM.

The gates do not use HTILE, MSAA, or workload packets.

## Exact artifacts

| Profile | Variant | SHA-256 |
| --- | --- | --- |
| `0x1160` logged | `agc_tess_geometry_fw1160_logged.elf` | `2ed3544476782e5ce00918b0f6917ff354cb0d582adbb014699477bcffaae493` |
| `0x1160` logged | `agc_tess_geometry_invocations_fw1160_logged.elf` | `75a4b45a6b49cf1b03fb5ca5f2d2f5b3a0787995eb58e143194a225ec29073f4` |
| `0x1160` logged | `agc_tess_geometry_lines_fw1160_logged.elf` | `a91cb19f862c8f07c36f2b3bf783570704b7ef08051f89726a923cc87a5e77c7` |
| `0x1160` logged | `agc_tess_geometry_rgba8_fw1160_logged.elf` | `e9a71857205bb48412609f469e4508376aa9123d1c30fa1506f46dcc4c3e809b` |
| `0x0550` mirror | `agc_tess_geometry_fw550_headless.elf` | `e2e3bc766697ad4acab811b1fa2fe8f38a8a0fbf5c5f158b242e52985d49ff8e` |
| `0x0550` mirror | `agc_tess_geometry_invocations_fw550_headless.elf` | `b7687aaa92e3690f4e8c7c529624dd3833ef570a216b7ded28cbb00158b2624e` |
| `0x0550` mirror | `agc_tess_geometry_lines_fw550_headless.elf` | `96b6c9046d732f15dc942e60e3c2ef13e04c72e11c507e952ee59631482ca82d` |
| `0x0550` mirror | `agc_tess_geometry_rgba8_fw550_headless.elf` | `e9757a14397a9e718019f660db023d0e5b84fad3f4715f316b132f173beeb6bb` |

All eight artifacts cross-build without warnings from current source and the
current `libopenagc.a`.

## Gate contract and order

Every gate requires its exact variant identity, exact four-digit ABI, reusable
HS+TES+PS binder success, positive offchip mutation, exactly four whole-ring
`4.0f` factors with zero invalid values, bounded EOP fence, marker,
target-specific readback, driver shutdown, and final PASS. The RGBA8 variant
also requires the headless BGRA8 vertex/index/texture and packed-memory oracle.

## FW 11.60 hardware result

All four variants passed twice on the standard PS5 reporting raw firmware
`0x11600005`:

| Variant | Complete/changed pixels | Bounds/colors | Native FNV64 |
| --- | ---: | --- | --- |
| TES-to-NGG geometry | 155,321 FP16 | `724x627`, 8 colors | `0xce4e39671f7448bc` |
| geometry invocations | 127,488 FP16 | `844x332`, 8 colors | `0x1527e4785be7854a` |
| geometry line strip | 6,749 FP16 | `768x666`, 1 white color | `0x929cf8dfd6a5b809` |
| geometry BGRA8 | 135,831 packed pixels | 8 colors; vertex/index/texture PASS | `0xe63963f065bd9a51` |

Every run reached its fence immediately, changed 24 offchip words, found
exactly four whole-ring `4.0f` factors with zero invalid values, passed the
Wave32/tessellation PM4 audit and marker, shut down the driver cleanly, and
reported final graphics PASS. ps5debug-NG found no residual `eboot` between or
after runs; ports 744 and 8080 remained reachable.

This hardware-qualifies the four combined-stage paths on the tested FW 11.60
console. The result does not extend to HTILE, MSAA, or workload packets.

The current-source FW 5.50 mirrors remain a separate regression requirement
when that console becomes reachable.
