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
| 10 | `RGBA8_UNORM` | `09c509f3dac6f6864ed53caf969a0046a33e4f7ad9f5cafe872b8a36b2bef406` |
| 11 | `BGRA8_UNORM` | `37ed666df195750e32308819a372f5256b4f58caace8a01cb6f7daa0a5e0a840` |
| 12 | `RGBA8_SRGB` | `92dcd0cb29926a2c1d9aaf24efe3eda6c1c2548225b739a98092d05fa80a1a94` |
| 13 | `BGRA8_SRGB` | `f73f67b5ae326a4af2bb1ad9dfec4b056f7ee8535e2cf69c7493b3354b40f2bb` |

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

## Headless RGBA8 and sRGB formats

`RGBA8_UNORM`, `BGRA8_UNORM`, `RGBA8_SRGB`, and `BGRA8_SRGB` now use real
flexible-memory targets rather than VideoOut-owned buffers. The sRGB variants
reserve separate aligned UNORM-control and sRGB-result spans and retain the
complete transfer, coverage, alpha, conversion-count, and native-hash oracle.
Exact FW 5.50 mirrors are also built. See
`fw1160_rgba8_srgb_headless_gate_audit_20260730.md` for layouts, hashes, and
the guarded hardware order.

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

The cleanup helper deliberately ends with `SIGKILL`, so websrv can retain a
foreground pipe until the client timeout even after the helper has finished.
The guarded graphics runner now launches cleanup with `pipe=0&daemon=1`, waits
two seconds, and requires websrv to respond before it uploads the next graphics
ELF. This preserves the mandatory immediately-preceding cleanup while avoiding
a false transport failure; residual-process checks still use ps5debug-NG.

After reboot, the same websrv instance again launched RG32 and the process
self-terminated without residue, but its foreground stdout pipe returned no
bytes. The run cannot count without its oracle log. A bounded file-backed mode
now redirects only the pending headless artifacts to
`/data/homebrew/openagc_fw1160_graphics/result.log`. The runner deletes that
file before every launch, starts the foreground payload with `pipe=0`, polls
FTP for a fresh final `Graphics result:` line, and then applies the same exact
log validators. A launch-request timeout is tolerated only inside this mode
because the fresh final file is the independent completion proof; any other
HTTP error remains fatal. A stale, missing, or partial file cannot qualify a
run. The file is opened only after the normal GPU credential transition so the
homebrew process has the same `/data` access used by the loader.

| File-backed target | Artifact SHA-256 |
| --- | --- |
| `RG32_FLOAT` | `2b27fcc4820b951b289a45c081ed002bc6c2808e462e08170e1bef234130b13f` |
| `RGBA32_FLOAT` | `53f47e52cdf42b4eca654fd09945c920f8d9a72f351f1cc9e5e1162eeb3c9df8` |

Both variants retain the exact profile, PM4 stream, readback, shutdown, and
self-termination code of their streaming counterparts; the only conditional
addition opens and line-buffers the result file before initialization. Run the
file-backed RG32 artifact twice rather than mixing evidence from two artifacts.

The five twice-passed formats are hardware-qualified on this FW 11.60 console,
but project-wide parity promotion still requires matching modern headless FW
5.50 regressions. The FW 5.50 console was unreachable on ports 8080 and 744
during this session.
