# FW 5.50 native dual-source panic investigation (2026-08-01)

## Incident

The first native-runtime replay of Vulkan-PS5's dual-source blend probe on
FW 5.50 timed out without a verdict and the console kernel-panicked. The
runner produced no target klog because the reset occurred before target-scoped
collection completed. That ELF must not be rerun.

## Root cause

The native migration had originally failed closed for dual-source pipelines.
The first enablement candidate accepted the two pixel exports and forced
`SPI_SHADER_COL_FORMAT=0x99` after binding the shader. The compiler-generated
primitive half subsequently restored `DB_SHADER_CONTROL=0x10`, leaving paired
exports active while DB dual export was disabled immediately before draw.
This inconsistent state is the identified panic trigger.

An offline DCB comparison used the exact historical qualification revisions:

- Vulkan-PS5 `e7e4ff5`
- OpenAGC `2723208`
- openagc-psbc `63e4365`

The hardware-proven stream temporarily emits paired `0x99` and
`DB_SHADER_CONTROL=0x210` while binding the pixel program, then settles to
`SPI_SHADER_COL_FORMAT=0x04` and `DB_SHADER_CONTROL=0x10` before its draw.
Its blend state remains `CB_COLOR_CONTROL=0x00cc0011` and disables SX blend
optimization. The corrected native stream now preserves those same final
values before `DRAW_INDEX_2`.

## Fix and safety gates

- openagc-psbc marks only fragment reflection as dual-source; the shared
  pipeline context no longer contaminates NGG pre-raster reflection.
- OpenAGC accepts exactly one color attachment with two compatible floating
  pixel exports when the dual-source reflection contract is present.
- SRC1 factors remain rejected without that contract.
- Color-target rebinding restores the pipeline's logic/dual-quad control
  instead of leaving the target helper's COPY default active.
- Dual-source pipeline binding restores the hardware-proven final
  `SPI_SHADER_COL_FORMAT=0x04` after the paired pixel-program setup.

Host reflection, pipeline, and final-register checks plus clean generic and
Prospero builds are prerequisites. Hardware replay requires a fresh console
reboot and the guarded runner. Until that replay passes, the corrected native
path is host-qualified only.
