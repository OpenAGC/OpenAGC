# Runtime typed whole-image copy — FW 5.50

Date: 2026-07-31

Endpoint: standard PS5, exact system software `0x05500008` (ABI key `0x0550`)

Artifact SHA-256:

```text
29110963a218ac7e5de2fc5073c23d5373e7eaa1365ccb3e2b6cf26fe1f85046
```

The local artifact and the bytes read back from websrv FTP had the same digest.
Those exact bytes passed twice after complete teardown and relaunch.

The public runtime probe creates two identical 16x16 RGBA8 images, uploads a
deterministic 256-word source, transitions the complete allocations to
`CopySource` and `CopyDestination` on Compute, calls `agcCmdCopyImage`, and
transitions the destination to host read. Both runs completed the bounded fence
and reproduced:

```text
Copy verification: words=256 source-fnv64=0x588c119c73f54d83 destination-fnv64=0x588c119c73f54d83 PASS
Native runtime typed image copy result: PASS
```

The full first run returned `AGC_OK` for command reset, fence/command
destruction, both image destructions, queue destruction, and device teardown.

This qualifies complete identical-layout RGBA8 image copying on exact FW 5.50.
Partial subresources, format/layout conversion, and other firmware profiles
remain unqualified.
