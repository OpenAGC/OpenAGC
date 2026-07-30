# Native runtime multi-command graphics qualification — FW 5.50

## Scope

This record covers the public runtime's first multi-command-buffer submission
on a standard PS5 reporting raw system software `0x05500008` (FW ABI key
`0x0550`). The exact artifact was
`agc_runtime_graphics.elf` SHA-256:

```
30564bfdd87de4c89e575a03b7456aad57a2ca72af174aa41d1598a20322142b
```

The graphics queue submits two nonempty DCBs in one recovered direct kernel
frame. The first records the typed MRT draw and color-target-to-host-read
flush. The second binds the same validated graphics pipeline without issuing a
draw; it provides the final runtime-owned EOP completion point. No application
constructs PM4, a kernel submit descriptor, or a fence packet.

## Result

The artifact was uploaded once through websrv to
`/data/homebrew/agc_runtime_graphics_oracle/eboot.elf` and launched in the
foreground. Both command buffers reached `AGC_OK` submit and the 200 ms bounded
wait completed. Both MRT images read back 1,152 changed pixels with 1,152
distinct pairs. Both command buffers reset successfully; every object and the
device then destroyed with `AGC_OK`. HTTP and FTP remained reachable after the
run.

## Qualification boundary

This hardware-qualifies a two-DCB graphics batch on exact FW 5.50, including
one batch fence and all-command-buffer release after completion. The runtime
accepts 2–63 nonempty graphics command buffers in a batch; compute batching,
empty members, wait/signal lists, GPU labels, timelines, cross-queue ownership,
and FW 11.60 remain fail-closed or unqualified.
