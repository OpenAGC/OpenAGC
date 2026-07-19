# Kernel /dev/gc Driver RE (FW 5.50)

Kernel dump: `/Users/bizkut/Downloads/PS5/FIRMWARE_FILES/5.50/5.50-kv-dump/merged/kernel_550_merged_by_offset.bin`. RE-only — constants re-expressed in MIT openagc tree.

## Key kernel offsets
- `gc_ioctl_internal` @ 0x6ed39c (ioctl dispatch, BST + jump tables)
- `gc_submit_with_pid` @ 0x6e65c0, `gc_frame_submit_internal` @ 0xb7da90
- Suspend handler `sub_06e6ff0` @ 0x6e6ff0 for ioctl 0xC010811C (nr=0x1c)
- Final suspend handler for 0xC0108139 (nr=0x39, same arg layout)
- Async/TF/HS setup handler `sub_06ee43c` @ 0x6ee43c for 0xC004811F (SETUP_ASYNC), 0xC0108120 (SET_TF_RING)
- Queue create handler `sub_06ee502` @ 0x6ee502 for kernel-internal 0x8004812A (NOT used by SPRX; SPRX uses nr=0x21)
- CONTEXT_QUERY handler @ 0x6ee691 (reads ctx->field30 and ctx->field40, returns 16-bit capability mask)
- `gc_pm4_clearstate_patch` @ 0xb7dd20 (only direct caller: SET_HS_OFFCHIP handler @ 0x6ee6d2)
- `gc_pm4_suspend_point_marker` @ 0xb7eaf0 (emits IT_AGC_0x93, 8-dword packet)

## Ioctl table (SPRX-confirmed, IOC-encoded)
| Cmd | nr | Dir | Size | Purpose |
|-----|----|----|------|---------|
| 0xc004812e | 0x2e | RW | 4 | CONTEXT_QUERY |
| 0xc0088101 | 0x01 | RW | 8 | Close/cleanup |
| 0xc00c810e | 0x0e | RW | 12 | QUEUE_DESTROY (3 magic tokens) |
| 0xc0408121 | 0x21 | RW | 64 | QUEUE_CREATE (magic triple + ring info) |
| 0x80048126 | 0x26 | R | 4 | SETUP_ASYNC (arg=1) |
| 0xc010811c | 0x1c | RW | 16 | SUSPEND_16 (4-dword arg) |
| 0xc0108139 | 0x39 | RW | 16 | SUSPEND_39 (final variant) |
| 0xc0108120 | 0x20 | RW | 16 | SET_TF_RING (user arg ignored on 5.50) |
| 0xc010812c | 0x2c | RW | 16 | SET_HS_OFFCHIP (patch-list ptr + count, max 0x400) |
| 0xc0088100 | 0x00 | — | — | **NOT HANDLED** (EINVAL) |

76 ioctl command constants in `include/agc_ioctl.h`.

## Suspend ioctl arg (16 bytes = 4 dwords)
- field0 @ 0x00: validated as 1 or 2 in simple path; magic triples (0x0769c766,0x72e8e3c1,0xdb72af28) and (0xaf1e80b7,0x8b4cdd90,0x99f68d6c) also accepted
- field1 @ 0x04: must be <= 3
- field2 @ 0x08: must be <= 7
- field3 @ 0x0C: value written to selected internal suspend ring by sub_05d7700

## GPU process credential check (0xd8e70400)
```c
int gpu_process_check(proc_t *proc) {
    uint64_t rax = 0xff0f000000000000ULL & proc->creds;  // [proc + 0x58] = cr_sceAuthId
    uint64_t rcx = 0xb7ff000000000000ULL + rax;
    return (rcx >> 49) == 0;  // 1 = pass, 0 = fail
}
```
Pass condition: `(cr_sceAuthId & 0xff0f000000000000) == 0x4901000000000000`. **cr_sceAuthId = 0x4801000000000000** satisfies this (0x4801... & 0xff0f... = 0x4801..., + 0xb7ff... = 0x10000... → >>49 = 0). Gates queue create AND suspend point.

## NotifyDefaultStates kernel path
NO separate kernel ioctl for NotifyDefaultStates. `CONTEXT_STATE`/`CONTEXT_STATE_OP` strings @ 0xf929aa/0x100a8b1 only used by GPU-fault string-lookup @ 0x52817f. Primary/internal register-defaults blobs built in GPU-visible memory are consumed by GPU itself when userspace emits CLEAR_STATE or CONTEXT_STATE PM4 packet. Kernel role = validate/submit the DCB containing the packet.

## Error codes
- 0x804C0001 — EPERM (no queue at slot / frame context NULL)
- 0x804C000B — EAGAIN (GPU credential check failed)
- 0x8a6cNNNN — AGC library errors
- 0x8a6dNNNN — driver errors (0x8a6d0003 = INVALID_ARG)
- 0x8029000a — VideoOut errors