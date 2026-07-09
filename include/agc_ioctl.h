#ifndef _AGC_IOCTL_H_
#define _AGC_IOCTL_H_

#include <stdint.h>
#include <stddef.h>

/*
 * /dev/gc ioctl command table and submit/queue structures for PS5 FW 5.50.
 *
 * RE source: kernel dump gc_ioctl_internal at 0x6ed39c (BST + 4 jump tables),
 * gc_submit_with_pid at 0x6e65c0, gc_frame_submit_internal at 0xb7da90.
 * Cross-referenced with the sibling ps5-openagc project's
 * analysis/ioctl_dispatch.md and include/ps5/internal/agc_{ioctl,submit,fw}.h.
 *
 * openagc is a clean rewrite — these constants are recovered ABI facts, not
 * copied code. The ioctl table is FW-version-specific; 5.50 is the first
 * target. Other FW versions would populate a different table.
 *
 * Nothing here is used by the generic host backend. It exists so the orbis
 * backend (driver_orbis.c) has a typed surface to call into once native
 * submission is implemented.
 */

/* FreeBSD IOC encoding: (dir << 30) | (size << 16) | (type << 8) | nr */
#define AGC_GC_IOCTL_TYPE 0x81u

#define AGC_GC_IOC(dir, nr, size) \
    (((uint32_t)(dir) << 30) | ((uint32_t)(size) << 16) | \
     (AGC_GC_IOCTL_TYPE << 8) | (uint32_t)(nr))

/* Direction codes */
#define AGC_GC_IOCTL_DIR_NONE  0u
#define AGC_GC_IOCTL_DIR_WRITE 1u  /* user -> kernel */
#define AGC_GC_IOCTL_DIR_READ  2u  /* kernel -> user */
#define AGC_GC_IOCTL_DIR_RW    3u  /* bidirectional */

/*
 * Ioctl command numbers (nr field) for FW 5.50.
 * Mapped by tracing the kernel BST dispatch at gc_ioctl_internal (0x6ed39c)
 * and cross-referencing with AGC driver SPRX ioctl calls.
 */
enum AgcGcIoctlNr {
    /* Frame / submit */
    AGC_GC_NR_FRAME_OPEN       = 0x00, /* gc_open_internal — open submit frame */
    AGC_GC_NR_CLOSE            = 0x01, /* close / cleanup */
    AGC_GC_NR_SUBMIT_16        = 0x02, /* gc_submit_with_pid (16-byte arg) */
    AGC_GC_NR_SUBMIT_4         = 0x13, /* submit variant (4-byte arg) */
    AGC_GC_NR_SUBMIT_MFENCE    = 0x14, /* submit + mfence (4-byte arg) */
    AGC_GC_NR_SUBMIT_40        = 0x30, /* submit variant (40-byte arg) */
    AGC_GC_NR_SUBMIT_PID       = 0x3b, /* gc_submit_with_pid (16-byte, pid check) */

    /* Memory mapping */
    AGC_GC_NR_MAKESYSMAP_8     = 0x09, /* gc_makesysmap (8-byte arg) */
    AGC_GC_NR_MAKESYSMAP_32    = 0x0c, /* gc_makesysmap (32-byte arg) */
    AGC_GC_NR_MAKESYSMAP_12    = 0x0d, /* gc_makesysmap (12-byte arg) */
    AGC_GC_NR_MAKESYSMAP_48    = 0x0c, /* gc_makesysmap (48-byte arg variant) */

    /* Queue management */
    AGC_GC_NR_QUEUE_DISCONNECT = 0x16, /* gc_gfx_queue_disconnect */
    AGC_GC_NR_QUEUE_STATUS     = 0x27, /* gc_gfx_queue_status */
    AGC_GC_NR_QUEUE_CREATE     = 0x2a, /* create/map queue */
    AGC_GC_NR_QUEUE_DESTROY    = 0x2b, /* destroy queue */

    /* Suspend / resume */
    AGC_GC_NR_SUSPEND_16       = 0x1c, /* gc_accept_suspend_locked (16-byte) */
    AGC_GC_NR_SUSPEND_39       = 0x39, /* gc_accept_suspend_locked variant */

    /* CWSR (Compute Wavefront Save/Restore) */
    AGC_GC_NR_CWSR_INIT_8      = 0x36, /* gc_acq_cwsr_initialize (8-byte) */
    AGC_GC_NR_CWSR_INIT_4A     = 0x3a, /* gc_acq_cwsr_initialize (4-byte W) */
    AGC_GC_NR_CWSR_INIT_4D     = 0x3d, /* gc_acq_cwsr_initialize (4-byte W) */

    /* PA debug */
    AGC_GC_NR_PADEBUG_8        = 0x37, /* gc_padebug_operation (8-byte W) */
    AGC_GC_NR_PADEBUG_4        = 0x38, /* gc_padebug_operation (4-byte R) */

    /* Wait / query */
    AGC_GC_NR_WAIT_QUEUE       = 0x38, /* gc_wait_system_call_queue */
    AGC_GC_NR_QUERY_120        = 0x23, /* gc_push_only_bad_packet_to_user */
    AGC_GC_NR_QUERY_68         = 0x24, /* related query (68-byte R) */
    AGC_GC_NR_QUERY_4C         = 0x3c, /* query (4-byte R) */

    /* WFDebug (wavefront debug) */
    AGC_GC_NR_WFDEBUG_15       = 0x15,
    AGC_GC_NR_WFDEBUG_17       = 0x17,
    AGC_GC_NR_WFDEBUG_18       = 0x18,
    AGC_GC_NR_WFDEBUG_1D       = 0x1d,
    AGC_GC_NR_WFDEBUG_2E       = 0x2e,

    /* Large arg structs */
    AGC_GC_NR_LARGE_64         = 0x21, /* 64-byte RW (used 3x in AGC driver) */
    AGC_GC_NR_LARGE_132        = 0x19, /* 132-byte RW (used 3x in AGC driver) */
    AGC_GC_NR_LARGE_48         = 0x1e, /* 48-byte RW */
    AGC_GC_NR_LARGE_72         = 0x31, /* 72-byte RW */
    AGC_GC_NR_LARGE_24         = 0x32, /* 24-byte RW */
    AGC_GC_NR_LARGE_260        = 0x0f, /* 260-byte RW */

    /* Misc */
    AGC_GC_NR_SET_TF_RING      = 0x20, /* set tessellation factor ring */
    AGC_GC_NR_SET_HS_OFFCHIP   = 0x2c, /* set hull shader offchip params */
    AGC_GC_NR_SETUP_ASYNC      = 0x1f, /* setup async graphics queue */
    AGC_GC_NR_SUBMITDONE       = 0x25, /* gc_setup_submitdone */
    AGC_GC_NR_CPASSETID        = 0x11, /* gc_cpassetid */
    AGC_GC_NR_IH_TASKLET       = 0x1b, /* gc_ih_tasklet */
    AGC_GC_NR_IH_TASKLET_2D    = 0x2d, /* gc_ih_tasklet variant */
    AGC_GC_NR_PROTECTION       = 0x35, /* protection fault info */
    AGC_GC_NR_INFO_33          = 0x33, /* info query (8-byte) */
    AGC_GC_NR_INFO_34          = 0x34, /* info query (4-byte R) */
};

/* Total ioctl commands in FW 5.50 (across all dir/size variants) */
#define AGC_GC_IOCTL_COUNT 76u

/* Full ioctl command words for FW 5.50 */
#define AGC_GC_IOCTL_FRAME_OPEN     AGC_GC_IOC(3u, 0x00u, 8u)
#define AGC_GC_IOCTL_CLOSE          AGC_GC_IOC(3u, 0x01u, 8u)
#define AGC_GC_IOCTL_SUBMIT_16      AGC_GC_IOC(3u, 0x02u, 16u)
#define AGC_GC_IOCTL_MAKESYSMAP_8   AGC_GC_IOC(3u, 0x09u, 8u)
#define AGC_GC_IOCTL_MAKESYSMAP_32  AGC_GC_IOC(3u, 0x0cu, 32u)
#define AGC_GC_IOCTL_MAKESYSMAP_12  AGC_GC_IOC(3u, 0x0du, 12u)
#define AGC_GC_IOCTL_MAKESYSMAP_48  AGC_GC_IOC(3u, 0x0cu, 48u)
#define AGC_GC_IOCTL_LARGE_260      AGC_GC_IOC(3u, 0x0fu, 260u)
#define AGC_GC_IOCTL_CPASSETID      AGC_GC_IOC(3u, 0x11u, 8u)
#define AGC_GC_IOCTL_SUBMIT_4       AGC_GC_IOC(3u, 0x13u, 4u)
#define AGC_GC_IOCTL_SUBMIT_MFENCE  AGC_GC_IOC(3u, 0x14u, 4u)
#define AGC_GC_IOCTL_WFDEBUG_15     AGC_GC_IOC(3u, 0x15u, 4u)
#define AGC_GC_IOCTL_QUEUE_DISC     AGC_GC_IOC(3u, 0x16u, 4u)
#define AGC_GC_IOCTL_WFDEBUG_17     AGC_GC_IOC(3u, 0x17u, 4u)
#define AGC_GC_IOCTL_WFDEBUG_18     AGC_GC_IOC(3u, 0x18u, 4u)
#define AGC_GC_IOCTL_LARGE_132      AGC_GC_IOC(3u, 0x19u, 132u)
#define AGC_GC_IOCTL_IH_TASKLET     AGC_GC_IOC(3u, 0x1bu, 8u)
#define AGC_GC_IOCTL_SUSPEND_16     AGC_GC_IOC(3u, 0x1cu, 16u)
#define AGC_GC_IOCTL_WFDEBUG_1D     AGC_GC_IOC(3u, 0x1du, 4u)
#define AGC_GC_IOCTL_LARGE_48       AGC_GC_IOC(3u, 0x1eu, 48u)
#define AGC_GC_IOCTL_SETUP_ASYNC    AGC_GC_IOC(3u, 0x1fu, 4u)
#define AGC_GC_IOCTL_SET_TF_RING    AGC_GC_IOC(3u, 0x20u, 16u)
#define AGC_GC_IOCTL_LARGE_64       AGC_GC_IOC(3u, 0x21u, 64u)
#define AGC_GC_IOCTL_QUERY_120      AGC_GC_IOC(2u, 0x23u, 120u)
#define AGC_GC_IOCTL_QUERY_68       AGC_GC_IOC(2u, 0x24u, 68u)
#define AGC_GC_IOCTL_SUBMITDONE     AGC_GC_IOC(3u, 0x25u, 4u)
#define AGC_GC_IOCTL_QUEUE_STATUS   AGC_GC_IOC(2u, 0x26u, 4u)
#define AGC_GC_IOCTL_QUEUE_STAT_16  AGC_GC_IOC(2u, 0x27u, 4u)
#define AGC_GC_IOCTL_QUEUE_CREATE   AGC_GC_IOC(2u, 0x2au, 4u)
#define AGC_GC_IOCTL_QUEUE_DESTROY  AGC_GC_IOC(1u, 0x2bu, 4u)
#define AGC_GC_IOCTL_SET_HS_OFFCHIP AGC_GC_IOC(3u, 0x2cu, 16u)
#define AGC_GC_IOCTL_IH_TASKLET_2D  AGC_GC_IOC(3u, 0x2du, 8u)
#define AGC_GC_IOCTL_WFDEBUG_2E     AGC_GC_IOC(3u, 0x2eu, 4u)
#define AGC_GC_IOCTL_SUBMIT_40      AGC_GC_IOC(3u, 0x30u, 40u)
#define AGC_GC_IOCTL_LARGE_72       AGC_GC_IOC(3u, 0x31u, 72u)
#define AGC_GC_IOCTL_LARGE_24       AGC_GC_IOC(3u, 0x32u, 24u)
#define AGC_GC_IOCTL_INFO_33        AGC_GC_IOC(3u, 0x33u, 8u)
#define AGC_GC_IOCTL_INFO_34        AGC_GC_IOC(2u, 0x34u, 4u)
#define AGC_GC_IOCTL_CWSR_INIT_8    AGC_GC_IOC(2u, 0x36u, 8u)
#define AGC_GC_IOCTL_PADEBUG_8      AGC_GC_IOC(1u, 0x37u, 8u)
#define AGC_GC_IOCTL_PADEBUG_4      AGC_GC_IOC(2u, 0x38u, 4u)
#define AGC_GC_IOCTL_SUSPEND_39     AGC_GC_IOC(3u, 0x39u, 16u)
#define AGC_GC_IOCTL_CWSR_INIT_4A   AGC_GC_IOC(1u, 0x3au, 4u)
#define AGC_GC_IOCTL_SUBMIT_PID     AGC_GC_IOC(3u, 0x3bu, 16u)
#define AGC_GC_IOCTL_QUERY_4C       AGC_GC_IOC(2u, 0x3cu, 4u)
#define AGC_GC_IOCTL_CWSR_INIT_4D   AGC_GC_IOC(1u, 0x3du, 4u)

/*
 * Submit ioctl argument struct (nr=0x3b, dir=RW, size=16).
 *
 * The ioctl arg size field (16) only covers the first 16 bytes. The
 * cb_array pointer at offset 0x10 is accessed directly from user memory
 * by the kernel driver.
 *
 * Kernel submit path:
 *   gc_submit_with_pid (0x6e65c0):
 *     - looks up proc by pid
 *     - gets VMID from [proc+0x200+0x1e4]
 *     - validates VMID in [2, 15]
 *     - tail-calls gc_frame_submit_internal
 *   gc_frame_submit_internal (0xb7da90):
 *     - validates num_cbs in [1, 0xFFF]
 *     - allocates ring space: num_cbs * 16 bytes
 *     - copyin(cb_array, ring_buf, num_cbs * 16)
 *     - per CB: checks header opcode, masks ib_base with 0x000FFFFF00000000,
 *       ORs in VMID<<52, calls gc_insert_indirect_buffer
 */
typedef struct AgcGcSubmitArgs {
    uint32_t pid;           /* offset 0x00: process ID (0 = current) */
    uint32_t pad0;          /* offset 0x04: padding */
    uint32_t num_cbs;       /* offset 0x08: number of CBs (max 0xFFF) */
    uint32_t pad1;          /* offset 0x0C: padding */
    uint64_t cb_array;      /* offset 0x10: user pointer to CB descriptor array */
} AgcGcSubmitArgs;
_Static_assert(offsetof(AgcGcSubmitArgs, pid) == 0x00,
    "AgcGcSubmitArgs pid offset mismatch");
_Static_assert(offsetof(AgcGcSubmitArgs, num_cbs) == 0x08,
    "AgcGcSubmitArgs num_cbs offset mismatch");
_Static_assert(offsetof(AgcGcSubmitArgs, cb_array) == 0x10,
    "AgcGcSubmitArgs cb_array offset mismatch");
_Static_assert(sizeof(AgcGcSubmitArgs) == 0x18,
    "AgcGcSubmitArgs size mismatch");

/*
 * Command buffer descriptor (16 bytes each, copyin'd by kernel).
 *
 * The header is a PM4 type-3 packet header:
 *   (3 << 30) | (opcode << 8) | (count - 1)
 *
 * Valid opcodes:
 *   0xC0023300 = PM4_TYPE3(0x23, 1) — IT_DRAW_INDEX_INDIRECT (used as IB)
 *   0xC0023F00 = PM4_TYPE3(0x3F, 1) — IT_COND_INDIRECT_BUFFER_CNST
 *
 * The ib_base field contains:
 *   bits [51:0]  = GPU virtual address of the indirect buffer
 *   bits [63:52] = VMID (inserted by kernel, originally 0 from user)
 */
typedef struct AgcGcCommandBuffer {
    uint64_t header;        /* offset 0x00: PM4 packet header (opcode + count) */
    uint64_t ib_base;       /* offset 0x08: IB base address + VMID (bits 63:52) */
} AgcGcCommandBuffer;
_Static_assert(offsetof(AgcGcCommandBuffer, header) == 0x00,
    "AgcGcCommandBuffer header offset mismatch");
_Static_assert(offsetof(AgcGcCommandBuffer, ib_base) == 0x08,
    "AgcGcCommandBuffer ib_base offset mismatch");
_Static_assert(sizeof(AgcGcCommandBuffer) == 0x10,
    "AgcGcCommandBuffer size mismatch");

/* Valid CB header opcodes */
#define AGC_GC_CB_HEADER_IB         0xC0023300u  /* IT_DRAW_INDEX_INDIRECT as IB */
#define AGC_GC_CB_HEADER_IB_CNST    0xC0023F00u  /* IT_COND_INDIRECT_BUFFER_CNST */

/* VMID is stored in bits [63:52] of ib_base; bits [51:0] are the GPU VA.
 * The kernel masks ib_base with 0x000FFFFFFFFFFFFF (52-bit address space)
 * before ORing in VMID<<52. Note: the sibling ps5-openagc project documented
 * this mask as 0x000FFFFF00000000, which only preserves bits [51:32] and
 * zeroes the low 32 bits — that is a transcription error; a 52-bit GPU VA
 * mask must preserve all of bits [51:0]. */
#define AGC_GC_IB_VMASK             0x000FFFFFFFFFFFFFULL
#define AGC_GC_IB_VSHIFT            52u

/* VMID valid range (validated in gc_submit_with_pid) */
#define AGC_GC_VMIN                 2u
#define AGC_GC_VMAX                 15u

/* num_cbs valid range (validated in gc_frame_submit_internal) */
#define AGC_GC_NUM_CBS_MIN          1u
#define AGC_GC_NUM_CBS_MAX          0xFFFu

/*
 * Frame open ioctl argument struct (nr=0x00, dir=RW, size=8).
 * RE'd from gc_open_internal at 0x6ed8dc.
 */
typedef struct AgcGcFrameOpenArg {
    uint32_t flags;
    uint32_t reserved;
} AgcGcFrameOpenArg;
_Static_assert(sizeof(AgcGcFrameOpenArg) == 0x08,
    "AgcGcFrameOpenArg size mismatch");

/*
 * Makesysmap argument struct (nr=0x09, dir=RW, size=8).
 * RE'd from libSceAgcDriver.sprx at vaddr 0x9510 and libSceGnmDriver.sprx
 * at vaddr 0x66d0. The 8-byte variant is the only one used by userspace.
 *
 * Input:  cpu_addr (user virtual address to map into GPU space)
 * Output: gpu_addr (GPU virtual address assigned by kernel)
 *
 * The kernel handler at 0x6ee6de reads the 8-byte arg from r12, passes
 * it to the internal VM mapping function, and writes back the GPU VA.
 */
typedef struct AgcGcMakesysmapArg8 {
    uint64_t addr;          /* in: CPU VA, out: GPU VA */
} AgcGcMakesysmapArg8;
_Static_assert(sizeof(AgcGcMakesysmapArg8) == 0x08,
    "AgcGcMakesysmapArg8 size mismatch");

/*
 * Makesysmap argument struct (nr=0x0d, dir=RW, size=12).
 */
typedef struct AgcGcMakesysmapArg12 {
    uint32_t gpu_addr_lo;
    uint32_t gpu_addr_hi;
    uint32_t size;
} AgcGcMakesysmapArg12;
_Static_assert(sizeof(AgcGcMakesysmapArg12) == 0x0C,
    "AgcGcMakesysmapArg12 size mismatch");

/*
 * Makesysmap argument struct (nr=0x0c, dir=RW, size=48) — kernel internal.
 */
typedef struct AgcGcMakesysmapArg48 {
    uint64_t cpu_addr;      /* offset 0x00: CPU virtual address */
    uint64_t gpu_addr;      /* offset 0x08: GPU virtual address (out) */
    uint64_t size;          /* offset 0x10: mapping size */
    uint32_t flags;         /* offset 0x18: mapping flags */
    uint32_t pad;
    uint64_t reserved[2];   /* offset 0x20: padding */
} AgcGcMakesysmapArg48;
_Static_assert(offsetof(AgcGcMakesysmapArg48, cpu_addr) == 0x00,
    "AgcGcMakesysmapArg48 cpu_addr offset mismatch");
_Static_assert(offsetof(AgcGcMakesysmapArg48, gpu_addr) == 0x08,
    "AgcGcMakesysmapArg48 gpu_addr offset mismatch");
_Static_assert(offsetof(AgcGcMakesysmapArg48, size) == 0x10,
    "AgcGcMakesysmapArg48 size offset mismatch");
_Static_assert(offsetof(AgcGcMakesysmapArg48, flags) == 0x18,
    "AgcGcMakesysmapArg48 flags offset mismatch");
_Static_assert(sizeof(AgcGcMakesysmapArg48) == 0x30,
    "AgcGcMakesysmapArg48 size mismatch");

/*
 * Suspend-point argument struct (nr=0x1c, dir=RW, size=16).
 *
 * RE'd from the FW 5.50 kernel handler at 0x6e6ff0. The kernel validates
 * the argument and then writes field3 into a driver-internal suspend ring.
 *   field0 - type/selector: 1 or 2 in the simple path; two known magic
 *            triples are also accepted for anti-tamper verification.
 *   field1 - first index, must be <= 3.
 *   field2 - second index, must be <= 7.
 *   field3 - value written to the selected suspend ring slot.
 */
typedef struct AgcGcSuspendArg {
    uint32_t field0;
    uint32_t field1;
    uint32_t field2;
    uint32_t field3;
} AgcGcSuspendArg;
_Static_assert(sizeof(AgcGcSuspendArg) == 0x10,
    "AgcGcSuspendArg size mismatch");
_Static_assert(offsetof(AgcGcSuspendArg, field0) == 0x00,
    "AgcGcSuspendArg field0 offset mismatch");
_Static_assert(offsetof(AgcGcSuspendArg, field1) == 0x04,
    "AgcGcSuspendArg field1 offset mismatch");
_Static_assert(offsetof(AgcGcSuspendArg, field2) == 0x08,
    "AgcGcSuspendArg field2 offset mismatch");
_Static_assert(offsetof(AgcGcSuspendArg, field3) == 0x0C,
    "AgcGcSuspendArg field3 offset mismatch");

/*
 * Hull-shader offchip parameter ioctl argument struct (nr=0x2c, dir=RW, size=16).
 *
 * RE'd from the FW 5.50 kernel handler at 0x6ee6d2. The handler passes the
 * argument straight to gc_pm4_clearstate_patch (0xb7dd20), which copies the
 * patch list from userspace and applies it to a CLEAR_STATE packet.
 *   list_addr    - user pointer to an array of 8-byte patch entries.
 *   num_entries  - number of entries in the list (kernel enforces <= 0x400).
 */
typedef struct AgcGcSetHsOffchipArg {
    uint64_t list_addr;     /* offset 0x00 */
    uint32_t num_entries;   /* offset 0x08 */
    uint32_t reserved;      /* offset 0x0C */
} AgcGcSetHsOffchipArg;
_Static_assert(sizeof(AgcGcSetHsOffchipArg) == 0x10,
    "AgcGcSetHsOffchipArg size mismatch");
_Static_assert(offsetof(AgcGcSetHsOffchipArg, list_addr) == 0x00,
    "AgcGcSetHsOffchipArg list_addr offset mismatch");
_Static_assert(offsetof(AgcGcSetHsOffchipArg, num_entries) == 0x08,
    "AgcGcSetHsOffchipArg num_entries offset mismatch");
_Static_assert(offsetof(AgcGcSetHsOffchipArg, reserved) == 0x0C,
    "AgcGcSetHsOffchipArg reserved offset mismatch");

/*
 * Kernel-side error codes from the submit path.
 * Module ID 0x4C = GC driver (distinct from AGC userspace module 0x89).
 */
#define AGC_GC_ERROR_NO_QUEUE       ((int32_t)0x804C0001)  /* no queue object */
#define AGC_GC_ERROR_NO_PROC        ((int32_t)0x804C0013)  /* process not found */
#define AGC_GC_ERROR_NO_VA_SPACE    ((int32_t)0x804C0005)  /* no GPU VA space for pid */
#define AGC_GC_ERROR_TOO_MANY_CBS   ((int32_t)0x804C000F)  /* num_cbs > 0xFFF */
#define AGC_GC_ERROR_BAD_CB_ADDR    ((int32_t)0x804C000E)  /* copyin failed */
#define AGC_GC_ERROR_BAD_OPCODE     ((int32_t)0x804C0010)  /* invalid CB header opcode */
#define AGC_GC_ERROR_BAD_IB_SIZE    ((int32_t)0x804C0011)  /* invalid IB size */
#define AGC_GC_ERROR_RING_FULL      ((int32_t)0x00000010)  /* ring buffer alloc failed */

/*
 * Kernel function offsets in FW 5.50 kernel dump
 * (kernel_550_merged_by_offset.bin). Documented for reference; not used
 * by the host backend, but needed if the orbis backend ever needs to
 * validate behavior against the dump.
 */
#define AGC_GC_KERN_IOCTL_INTERNAL      0x6ed39cu  /* gc_ioctl_internal */
#define AGC_GC_KERN_OPEN_INTERNAL       0x6ec100u  /* gc_open_internal (FRAME_OPEN) */
#define AGC_GC_KERN_SUBMIT_WITH_PID     0x6e65c0u  /* gc_submit_with_pid */
#define AGC_GC_KERN_FRAME_SUBMIT        0xb7da90u  /* gc_frame_submit_internal */

#endif /* _AGC_IOCTL_H_ */
