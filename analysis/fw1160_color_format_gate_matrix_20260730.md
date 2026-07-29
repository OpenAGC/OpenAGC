# FW 11.60 color-format gate matrix

## Built headless gates

The FW 11.60 baseline already qualifies `RGBA16_FLOAT`. The reusable graphics
fixture now builds exact-profile, headless, self-terminating gates for every
other FW 5.50-qualified offscreen color format whose oracle is independent of
VideoOut memory:

| Order | Target | Artifact SHA-256 |
|---:|---|---|
| 1 | `R16_FLOAT` | `c1745a15151a80fe65e0d1f7f5cc29fc47cb6cc3651d0e7aa5d5dde3261847de` |
| 2 | `RG16_FLOAT` | `09aaade1661b52cb8498122d1a9ccf0f5ec17a6ed2a32325ec3fd75a812f3c20` |
| 3 | `R8_UNORM` | `61a9eafb7ef48d5f86dc44be997a172edcb4ac01bb6ad801ac4a4b733754f287` |
| 4 | `RG8_UNORM` | `3c8e753a75373fea5811e19cb218ed093ab5cadf0edf840b68409c881eb5d861` |
| 5 | `RGB10A2_UNORM` | `8e563dc9c1c776eaf3a410e53b4ce12a91222ae4fadd92ae01f434901f89b67b` |
| 6 | `R11G11B10_FLOAT` | `57cd498eff669dde6bfd0798d416fd701418c9ba2e41bec36ed7dda28699a7de` |
| 7 | `R32_FLOAT` | `47d4b25dcd174ff5859073c0221137bf6ecd00b3a76aff91a4ad7e05d178349d` |
| 8 | `RG32_FLOAT` | `02124e8e89fecb91f5be5c47e3182f009829ce1a5b22ea6055d1b607171a202a` |
| 9 | `RGBA32_FLOAT` | `1d7b26afc2bee20119c6f178aa630d24cc028b13f377a71f66e6708088dd2952` |

Every artifact forces firmware ABI key `0x1160`, rejects Trinity hardware,
uses version-12 defaults through the normal runtime, waits on the bounded EOP
fence, validates native memory, shuts down the driver, and kills itself after
flushing the verdict. The shared runner requires the exact target-specific
PASS line and rejects failure, fatal, mismatch, and timeout text.

The first R8 launch completed in 1 ms, passed its native UNORM8 oracle, shut
down cleanly, and left no residual process, but the wrapper rejected it because
it looked for the FP16-only generic target line. The runner now first requires
the exact render-target name and then selects the validator actually emitted by
that format: UNORM8, FLOAT32, RGB10A2 histogram, R11G11B10 hash, or FP16. This
does not weaken any GPU oracle. The rejected wrapper run does not count toward
the required two passes.

The headless audit also fixed preview-only references for R8/RG8, R16/RG16,
R32/RG32/RGBA32, RGB10A2, and R11G11B10. CPU preview conversion remains part of
the FW 5.50 display fixtures but is not compiled or called by headless gates.
No GPU validation rule was removed.

## Deferred display-backed formats

`RGBA8_UNORM`, `BGRA8_UNORM`, `RGBA8_SRGB`, and `BGRA8_SRGB` currently use
VideoOut-owned buffers as one or both native oracle targets. Headless mode
intentionally leaves those pointers null. They are not included in this matrix
until the fixture allocates equivalent flexible-memory control and result
targets. Merely compiling around the null buffers would weaken the oracle.

## Hardware policy

The stage-17 workload stall contaminated the current GPU boot even though its
process was cleaned up. Reboot and reinject ps5debug-NG before order 1. Run each
gate twice, inspect residual processes and the kernel fault log after every
run, and stop the sequence on the first mismatch or stall. Regress the matching
FW 5.50 artifact after each promoted tuple.

The first two gates and their exact commands are detailed in
`fw1160_narrow_fp16_gate_audit_20260730.md`. Later gates use the corresponding
`deploy_agc_graphics_<format>_fw1160` Make target.

## FW 11.60 hardware result

The standard PS5 reporting raw firmware `0x11600005` completed this matrix:

| Target | Qualifying fences | Reproduced native oracle | State |
| --- | --- | --- | --- |
| R8_UNORM | 1 ms, 1 ms | changed `255043`, distinct `8`, FNV64 `0x6fe253259c7b0455` | passed twice |
| RG8_UNORM | 1 ms, 1 ms | changed `255744`, distinct `8`, FNV64 `0x6babce1afaa81b2c` | passed twice |
| RGB10A2_UNORM | 1 ms, 1 ms | histogram `{35857,27914,36523,155450}` | passed twice |
| R11G11B10_FLOAT | 1 ms, 1 ms | FNV64 `0x4b75c00e8a6bb04d` | passed twice |
| R32_FLOAT | 1 ms, 3 ms | changed/complete `255744`, invalid `0`, FNV64 `0x43e0f1986c4ec883` | passed twice |
| RG32_FLOAT | 1 ms | changed/complete `255744`, invalid `0`, FNV64 `0x806171be9908c276` | one pass |
| RGBA32_FLOAT | not run | — | pending |

Every qualifying run reported the exact `0x1160` profile, passed the Wave32
and completion-marker audits, shut the driver down, returned final graphics
PASS, and left no `eboot` according to ps5debug-NG.

After the first RG32 pass, the next two cleanup foreground requests timed out
without launching RG32 and left no process. A daemon-mode cleanup then
completed and ps5debug-NG again proved an empty process list, but the following
RG32 websrv request produced no stdout before its 30-second transport bound and
also left no process. There is no captured GPU verdict for that request, so it
does not count. Stop this boot here; reboot and reinject ps5debug-NG before the
second RG32 pass and both RGBA32 passes.

The five twice-passed formats are hardware-qualified on this FW 11.60 console,
but project-wide parity promotion still requires matching modern headless FW
5.50 regressions. The FW 5.50 console was unreachable on ports 8080 and 744
during this session.
