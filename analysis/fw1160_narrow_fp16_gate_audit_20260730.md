# FW 11.60 narrow-FP16 graphics gate audit

## Scope and order

FW 11.60 baseline Wave32 NGG+PS execution is already qualified twice with an
RGBA16F offscreen target. The next lowest-risk FW 5.50-qualified graphics
capabilities are the same draw and shader path with one- and two-component
FP16 render targets:

| Gate | CB format | number type | swap | bytes/pixel |
|---|---:|---:|---:|---:|
| `R16_FLOAT` | `0x02` | FLOAT (`7`) | standard | 2 |
| `RG16_FLOAT` | `0x05` | FLOAT (`7`) | standard | 4 |

They change only the typed color-target tuple and native readback width. They
do not add depth/stencil, HTILE, MSAA, tessellation, workload packets, or
VideoOut presentation. This makes R16 followed by RG16 the appropriate first
advanced-format gates before wider formats and metadata-bearing paths.

## Harness correction

The existing R16/RG16 fixtures performed their CPU preview call
unconditionally even though the preview helper is intentionally absent from a
headless build. The GPU draw and native validation were already independent of
that preview. The call is now guarded by `!AGC_GRAPHICS_HEADLESS`, matching the
qualified RGBA16F headless path.

`run_fw1160_graphics.sh` now accepts an exact `EXPECTED_TARGET` while retaining
all existing gates:

- exact runtime profile `0x1160`;
- bounded GPU completion fence;
- target-specific native validation PASS;
- driver shutdown and final PASS;
- rejection of any `FAIL`, `FATAL`, `MISMATCH`, or timeout text.

Both artifacts force `AGC_GRAPHICS_HEADLESS=1`,
`AGC_SELF_TERMINATE=1`, and the exact firmware ABI key `0x1160`. Their dynamic
dependencies are limited to VideoOut, libkernel, libc, and libnet; neither has
a `libSceAgcDriver.sprx` dependency.

Artifact hashes:

| Artifact | SHA-256 |
|---|---|
| `agc_graphics_r16_float_fw1160.elf` | `0a6826bd8b4cf2fb4ee552509918a68e80180535cf72f96a3466b59f300b7f8b` |
| `agc_graphics_rg16_float_fw1160.elf` | `15089e7a48b13d03f1e30fc3cde8101a4d830b30fd6f03d96e45afd5797583b3` |

## Hardware boundary

The preceding stage-17 workload gate stalled even though cleanup removed its
process. Reboot before any graphics gate; do not treat process cleanup as a GPU
state reset. After reinjecting ps5debug-NG, run R16 twice, then RG16 twice:

```sh
make -C samples/hw_test deploy_agc_graphics_r16_float_fw1160 \
    PS5_HOST=10.0.1.39
make -C samples/hw_test deploy_agc_graphics_rg16_float_fw1160 \
    PS5_HOST=10.0.1.39
```

Record the changed-pixel count, complete-sample count, out-of-range count,
native FNV64, fence latency, shutdown, residual-process state, and ps5debug-NG
fault scan for every run. Require two identical successful runs per format,
then rerun the existing FW 5.50 R16 and RG16 artifacts before promoting parity.
