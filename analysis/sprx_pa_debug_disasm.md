# FW 5.50 PA-debug export disassembly

Source: decrypted FW 5.50 `libSceAgcDriver.sprx` from the local firmware dump.
This records ABI facts only; no firmware code is included in OpenAGC.

## sceAgcDriverGetPaDebugInterfaceVersion

- NID: `Pqxglq1oKec`
- Export virtual address: `0x2b0`
- Export size: 39 bytes
- Loads the driver logger and the strings `permission insufficient` and
  `sceAgcDriverGetPaDebugInterfaceVersion`.
- Calls the logger.
- Returns constant `0x8a6d0001`.
- Performs no `/dev/gc` ioctl and does not inspect driver initialization.

OpenAGC therefore models this export as an unconditional FW 5.50 permission
stub. The old `PADEBUG_4` ioctl call was inherited from an unverified mapping
and did not match the official userspace module.

Hardware validation on FW 5.50 confirms the OpenAGC export returns exactly
`0x8a6d0001`. The same `agc_init.elf` run retained successful initialization,
default-state notification, NOP submission, async setup, queue operations,
suspend-point submission, and workload tracking.

## sceAgcDriverIsPaDebug

- NID: `HMnVBVUyajk`
- Export virtual address: `0x2e0`
- Export size: 3 bytes
- Semantics: constant zero (`xor eax,eax; ret`).

## FRAME_OPEN clarification

This investigation does not revive `FRAME_OPEN`. Command `0xc0088100` is
absent from the FW 5.50 kernel ioctl dispatcher and deterministically returns
`EINVAL`. It is a documented invalid command, not an unresolved initialization
or credential path.

## FW 3.20 comparison

The generated 3.20 stub metadata lists
`sceAgcDriverGetPaDebugInterfaceVersion` with the same NID `Pqxglq1oKec`, so
the export identity is stable between 3.20 and 5.50. It does not list
`sceAgcDriverIsPaDebug`; that export is not present in the 3.20 surface.

The local 3.20 decrypt log reports repeated `load_self_segment ... err=2` for
`libSceAgcDriver.sprx`, and its resulting executable segment is zero-filled.
Consequently, 3.20 provides export names/NIDs but no trustworthy function-body
semantics. The decoded FW 5.50 body remains authoritative for the constant
permission-stub behavior.
