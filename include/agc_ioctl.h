/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _AGC_IOCTL_H_
#define _AGC_IOCTL_H_

#include <stdint.h>
#include <stddef.h>

/*
 * /dev/gc ioctl command table and submit/queue structures for the standard
 * PS5 direct-submit ABI family independently observed on exact FW builds from
 * 4.00 through 12.70. OpenAGC does not infer compatibility from a range.
 * FW 5.50 remains the primary hardware-validated target. Runtime profiles
 * account for early submit16 families and the PS5 Pro CWSR allocation branch.
 *
 * RE source: kernel dump gc_ioctl_internal at 0x6ed39c (BST + 4 jump tables),
 * gc_submit_with_pid at 0x6e65c0, gc_frame_submit_internal at 0xb7da90.
 * Cross-referenced with the sibling ps5-openagc project's
 * analysis/ioctl_dispatch.md — but ps5-openagc is NOT proven working and
 * its ioctl_dispatch.md contains known errors (e.g. it claimed FRAME_OPEN
 * nr=0x00 was valid; it used wrong queue create/destroy ioctl numbers).
 * All ioctl numbers and struct layouts used by openagc have been independently
 * verified from SPRX disassembly. See analysis/agc_driver_abi_1160.md and
 * analysis/ps5_openagc_audit.md.
 *
 * openagc is a clean rewrite — these constants are recovered ABI facts, not
 * copied code. The operation-specific facts used by the direct backend are
 * reproduced across every active firmware in analysis/*.tsv; unrelated
 * ioctl-table entries remain FW 5.50 reference data and are not promoted.
 *
 * Nothing here is used by the generic host backend. It exists so the prospero
 * backend (driver_prospero.c) has a typed surface to call into once native
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
    /* NOTE: nr=0x00 (FRAME_OPEN) does NOT exist in FW 5.50. The kernel's
     * gc_ioctl_internal has no handler for it and returns EINVAL via the
     * default path. The real init is done by libSceAgcDriver's module_start
     * which opens /dev/gc, queries context state via CONTEXT_QUERY (nr=0x2e),
     * and mmaps GPU register space. See analysis/sprx_sce_agc_initialize_disasm.md */
    AGC_GC_NR_FRAME_OPEN       = 0x00, /* NOT HANDLED in FW 5.50 (EINVAL) */
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
    AGC_GC_NR_QUEUE_STATUS     = 0x26, /* setup async graphics (SPRX-confirmed) */
    AGC_GC_NR_QUEUE_STAT_16    = 0x27, /* gc_gfx_queue_status query */
    AGC_GC_NR_QUEUE_DESTROY    = 0x0e, /* destroy queue (12-byte RW, SPRX-confirmed) */
    /* nr=0x2a and nr=0x2b are kernel-internal queue ops, not used by SPRX */

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

    /* Context query — used by libSceAgcDriver module_start to check if
     * the GPU context is already initialized. Returns a 32-bit bitmask:
     *   bit  0: ctx->field_30 != 0 (context initialized)
     *   bit 16: ctx->field_40 != 0 (secondary capability)
     * Kernel handler at 0x6ee691. */
    AGC_GC_NR_CONTEXT_QUERY    = 0x2e, /* context capability query (4-byte RW) */

    /* WFDebug (wavefront debug) */
    AGC_GC_NR_WFDEBUG_15       = 0x15,
    AGC_GC_NR_WFDEBUG_17       = 0x17,
    AGC_GC_NR_WFDEBUG_18       = 0x18,
    AGC_GC_NR_WFDEBUG_1D       = 0x1d,

    /* Large arg structs */
    AGC_GC_NR_QUEUE_CREATE     = 0x21, /* queue create (64-byte RW, SPRX-confirmed) */
    AGC_GC_NR_LARGE_132        = 0x19, /* 132-byte RW (used 3x in AGC driver) */
    AGC_GC_NR_LARGE_48         = 0x1e, /* 48-byte RW */
    AGC_GC_NR_LARGE_72         = 0x31, /* 72-byte RW */
    AGC_GC_NR_LARGE_24         = 0x32, /* 24-byte RW */
    AGC_GC_NR_LARGE_260        = 0x0f, /* 260-byte RW */

    /* Misc */
    AGC_GC_NR_SET_TF_RING_DIRECT = 0x20, /* privileged direct variant */
    AGC_GC_NR_SET_TF_RING      = 0x28, /* public TF ring address/size */
    AGC_GC_NR_SET_HS_OFFCHIP   = 0x2c, /* set hull shader offchip params */
    AGC_GC_NR_SETUP_ASYNC      = 0x1f, /* (not used by SPRX for setup_async) */
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
#define AGC_GC_IOCTL_FRAME_OPEN     AGC_GC_IOC(3u, 0x00u, 8u)  /* NOT HANDLED in FW 5.50 */
#define AGC_GC_IOCTL_CONTEXT_QUERY  AGC_GC_IOC(3u, 0x2eu, 4u)  /* context capability query */
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
#define AGC_GC_IOCTL_SETUP_ASYNC    AGC_GC_IOC(3u, 0x1fu, 4u)   /* nr=0x1f (not used by SPRX) */
#define AGC_GC_IOCTL_SET_TF_RING_DIRECT AGC_GC_IOC(3u, 0x20u, 16u)
#define AGC_GC_IOCTL_SET_TF_RING    AGC_GC_IOC(2u, 0x28u, 16u)
#define AGC_GC_IOCTL_QUEUE_CREATE   AGC_GC_IOC(3u, 0x21u, 64u)  /* nr=0x21, 64-byte RW (SPRX-confirmed) */
#define AGC_GC_IOCTL_QUERY_120      AGC_GC_IOC(2u, 0x23u, 120u)
#define AGC_GC_IOCTL_QUERY_68       AGC_GC_IOC(2u, 0x24u, 68u)
#define AGC_GC_IOCTL_SUBMITDONE     AGC_GC_IOC(3u, 0x25u, 4u)
#define AGC_GC_IOCTL_QUEUE_STATUS   AGC_GC_IOC(2u, 0x26u, 4u)   /* nr=0x26, setup async (SPRX-confirmed) */
#define AGC_GC_IOCTL_QUEUE_STAT_16  AGC_GC_IOC(2u, 0x27u, 4u)   /* nr=0x27, queue status query */
#define AGC_GC_IOCTL_QUEUE_DESTROY  AGC_GC_IOC(3u, 0x0eu, 12u)  /* nr=0x0e, 12-byte RW (SPRX-confirmed) */
#define AGC_GC_IOCTL_SET_HS_OFFCHIP AGC_GC_IOC(3u, 0x2cu, 16u)
#define AGC_GC_IOCTL_IH_TASKLET_2D  AGC_GC_IOC(3u, 0x2du, 8u)
/* 0xc004812e (nr=0x2e) is CONTEXT_QUERY, not WFDEBUG — see AGC_GC_IOCTL_CONTEXT_QUERY above */
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
 * Submit ioctl argument struct.
 *
 * RE'd from libSceAgcDriver.sprx at vaddr 0x86e0 (FW 5.50).
 *
 * The SPRX uses SUBMIT_16 (nr=0x02, cmd=0xC0108102) for DCB/ACB submission.
 * The 16-byte struct is fully copyin'd by the kernel:
 *   offset 0x00: uint32_t queue_type  (SPRX always passes 3 = graphics)
 *   offset 0x04: uint32_t num_cbs     (number of CB descriptors, max 0xFFF)
 *   offset 0x08: uint64_t cb_array    (user pointer to CB descriptor array)
 *
 * Kernel submit path:
 *   gc_submit_with_pid (0x6e65c0):
 *     - gets VMID from process context
 *     - validates VMID in [2, 15]
 *     - tail-calls gc_frame_submit_internal
 *   gc_frame_submit_internal (0xb7da90):
 *     - validates num_cbs in [1, 0xFFF]
 *     - allocates ring space: num_cbs * 16 bytes
 *     - copyin(cb_array, ring_buf, num_cbs * 16)
 *     - per CB: checks header opcode, masks ib_base, ORs in VMID<<52,
 *       calls gc_insert_indirect_buffer
 */
typedef struct AgcGcSubmitArgs {
    uint32_t queue_type;    /* offset 0x00: 3 = graphics queue (SPRX-confirmed) */
    uint32_t num_cbs;       /* offset 0x04: number of CBs (max 0xFFF) */
    uint64_t cb_array;      /* offset 0x08: user pointer to CB descriptor array */
} AgcGcSubmitArgs;
_Static_assert(offsetof(AgcGcSubmitArgs, queue_type) == 0x00,
    "AgcGcSubmitArgs queue_type offset mismatch");
_Static_assert(offsetof(AgcGcSubmitArgs, num_cbs) == 0x04,
    "AgcGcSubmitArgs num_cbs offset mismatch");
_Static_assert(offsetof(AgcGcSubmitArgs, cb_array) == 0x08,
    "AgcGcSubmitArgs cb_array offset mismatch");
_Static_assert(sizeof(AgcGcSubmitArgs) == 0x10,
    "AgcGcSubmitArgs size mismatch");

/*
 * Command buffer descriptor (16 bytes each, copyin'd by kernel).
 *
 * RE'd from libSceAgcDriver.sprx at vaddr 0x1077 (FW 5.50).
 *
 * The 16-byte descriptor IS an IT_INDIRECT_BUFFER PM4 packet:
 *   Word 0 (header[31:0]):  PM4 type-3 header (opcode 0x3F or 0x33)
 *   Word 1 (header[63:32]): ib_base_lo — lower 32 bits of GPU VA
 *   Word 2 (ib_base[31:0]): ib_base_hi — upper bits of GPU VA (only [15:0])
 *   Word 3 (ib_base[63:32]): control — ib_size in [19:0], VMID in [63:52]
 *
 * The kernel inserts VMID into ib_base[63:52] after copyin.
 * Mask 0x000FFFFF0000FFFF limits ib_size to 20 bits and ib_base_hi to 16 bits.
 */
typedef struct AgcGcCommandBuffer {
    uint64_t header;        /* offset 0x00: [63:32]=ib_base_lo, [31:0]=PM4 header */
    uint64_t ib_base;       /* offset 0x08: [63:32]=control(ib_size), [31:0]=ib_base_hi */
} AgcGcCommandBuffer;
_Static_assert(offsetof(AgcGcCommandBuffer, header) == 0x00,
    "AgcGcCommandBuffer header offset mismatch");
_Static_assert(offsetof(AgcGcCommandBuffer, ib_base) == 0x08,
    "AgcGcCommandBuffer ib_base offset mismatch");
_Static_assert(sizeof(AgcGcCommandBuffer) == 0x10,
    "AgcGcCommandBuffer size mismatch");

/* Valid CB header opcodes for the kernel submit descriptor.
 *   0x33 = IT_INDIRECT_BUFFER_CNST (used for ACB / const command buffers)
 *   0x3F = IT_INDIRECT_BUFFER      (used for DCB / draw command buffers)
 * NOTE: names follow the IB type, not the descriptor usage. */
#define AGC_GC_CB_HEADER_IB_CNST    0xC0023300u  /* opcode 0x33 = IT_INDIRECT_BUFFER_CNST */
#define AGC_GC_CB_HEADER_IB         0xC0023F00u  /* opcode 0x3F = IT_INDIRECT_BUFFER */

/* VMID is stored in bits [63:52] of the ib_base qword (word 3 of the
 * IT_INDIRECT_BUFFER packet). The kernel inserts VMID after copyin.
 * The SPRX masks ib_base with 0x000FFFFF0000FFFF before submit:
 *   bits [51:32] = ib_size (20 bits, max 1M dwords)
 *   bits [31:16] = 0 (zeroed)
 *   bits [15:0]  = ib_base_hi (upper 16 bits of GPU VA) */
#define AGC_GC_IB_VMASK             0x000FFFFF0000FFFFULL
#define AGC_GC_IB_VSHIFT            52u

/* VMID valid range (validated in gc_submit_with_pid) */
#define AGC_GC_VMIN                 2u
#define AGC_GC_VMAX                 15u

/* num_cbs valid range (validated in gc_frame_submit_internal) */
#define AGC_GC_NUM_CBS_MIN          1u
#define AGC_GC_NUM_CBS_MAX          0xFFFu

/*
 * Frame open ioctl argument struct (nr=0x00, dir=RW, size=8).
 *
 * WARNING: This ioctl is NOT handled by the FW 5.50 kernel. Calling it
 * returns EINVAL. It is kept here for documentation only. The real
 * initialization uses CONTEXT_QUERY (nr=0x2e) + mmap instead.
 * See analysis/sprx_sce_agc_initialize_disasm.md for details.
 */
typedef struct AgcGcFrameOpenArg {
    uint32_t flags;
    uint32_t reserved;
} AgcGcFrameOpenArg;
_Static_assert(offsetof(AgcGcFrameOpenArg, flags) == 0x00,
    "AgcGcFrameOpenArg flags offset mismatch");
_Static_assert(offsetof(AgcGcFrameOpenArg, reserved) == 0x04,
    "AgcGcFrameOpenArg reserved offset mismatch");
_Static_assert(sizeof(AgcGcFrameOpenArg) == 0x08,
    "AgcGcFrameOpenArg size mismatch");

/*
 * Context query ioctl result (nr=0x2e, dir=RW, size=4).
 *
 * RE'd from the kernel handler at 0x6ee691 and libSceAgcDriver.sprx
 * module_start at vaddr 0x7cc0.
 *
 * The kernel reads two fields from the gc context struct and returns
 * a 32-bit bitmask:
 *   bits [15:0]  = (ctx->field_30 != 0) — context initialized flag
 *   bits [31:16] = (ctx->field_40 != 0) — secondary capability flag
 *
 * The SPRX checks the lower 16 bits: if zero, the context is not yet
 * initialized and the SPRX performs mmap + default state setup.
 */
typedef struct AgcGcContextQueryResult {
    uint32_t capability_mask;   /* (field40 != 0) << 16 | (field30 != 0) */
} AgcGcContextQueryResult;
_Static_assert(offsetof(AgcGcContextQueryResult, capability_mask) == 0x00,
    "AgcGcContextQueryResult capability_mask offset mismatch");
_Static_assert(sizeof(AgcGcContextQueryResult) == 0x04,
    "AgcGcContextQueryResult size mismatch");

/* Context query capability bits */
#define AGC_GC_CTX_QUERY_INITIALIZED    0x0001u  /* ctx->field_30 != 0 */
#define AGC_GC_CTX_QUERY_CAPABILITY2    0x00010000u  /* ctx->field_40 != 0 */

/* Fixed GPU register MMIO address used by libSceAgcDriver module_start */
#define AGC_GC_MMIO_BASE    0xfe0200000ULL
#define AGC_GC_MMIO_SIZE    0x4000u

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
_Static_assert(offsetof(AgcGcMakesysmapArg8, addr) == 0x00,
    "AgcGcMakesysmapArg8 addr offset mismatch");
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
_Static_assert(offsetof(AgcGcMakesysmapArg12, gpu_addr_lo) == 0x00,
    "AgcGcMakesysmapArg12 gpu_addr_lo offset mismatch");
_Static_assert(offsetof(AgcGcMakesysmapArg12, gpu_addr_hi) == 0x04,
    "AgcGcMakesysmapArg12 gpu_addr_hi offset mismatch");
_Static_assert(offsetof(AgcGcMakesysmapArg12, size) == 0x08,
    "AgcGcMakesysmapArg12 size offset mismatch");
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
 * Payload stores and request word are also confirmed in the FW 11.60 wrapper
 * at vaddr 0x9430. The FW 5.50 kernel handler at 0x6e6ff0 validates
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
 * The same payload stores and request are confirmed in the FW 11.60 wrapper
 * at vaddr 0x9820. The FW 5.50 kernel handler at 0x6ee6d2 passes the
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

/* Public sceAgcDriverSetTFRing ioctl payload recovered from FW 5.50 at vaddr
 * 0x9180 and independently confirmed in FW 11.60 at vaddr 0x8fc0. */
typedef struct AgcGcSetTFRingArg {
    uint64_t ring_addr;     /* offset 0x00, 256-byte aligned */
    uint32_t size;          /* offset 0x08, multiple of 4, max 0x4000 */
    uint32_t reserved;      /* offset 0x0C */
} AgcGcSetTFRingArg;
_Static_assert(sizeof(AgcGcSetTFRingArg) == 0x10,
    "AgcGcSetTFRingArg size mismatch");
_Static_assert(offsetof(AgcGcSetTFRingArg, ring_addr) == 0x00,
    "AgcGcSetTFRingArg ring_addr offset mismatch");
_Static_assert(offsetof(AgcGcSetTFRingArg, size) == 0x08,
    "AgcGcSetTFRingArg size offset mismatch");
_Static_assert(offsetof(AgcGcSetTFRingArg, reserved) == 0x0C,
    "AgcGcSetTFRingArg reserved offset mismatch");

/*
 * Queue create ioctl argument struct (nr=0x21, dir=RW, size=64).
 *
 * RE'd from libSceAgcDriver.sprx _sceAgcDriverCreateUserSpecialQueue
 * at vaddr 0x2c20 and its internal helper at vaddr 0x8d80.
 *
 * The SPRX hardcodes three magic authentication tokens that the kernel
 * validates before creating the queue. The ring_base and read_ptr are
 * GPU VAs carved out of the internal memory allocated by
 * sce_agc_initialize_internal_memory. The pipe_id is fixed at 0xc.
 *
 * Ioctl command: 0xc0408121 = IOC(RW, 0x81, 0x21, 64)
 */
typedef struct AgcGcQueueCreateArg {
    uint32_t magic1;        /* offset 0x00: 0xaf1e80b7 (auth token 1) */
    uint32_t magic2;        /* offset 0x04: 0x8b4cdd90 (auth token 2) */
    uint32_t magic3;        /* offset 0x08: 0x99f68d6c (auth token 3) */
    uint32_t token;         /* offset 0x0C: 0xe5fcc174 (secondary token) */
    uint64_t read_ptr_addr; /* offset 0x10: ACQRB base + 0x1C8000 (read ptr GPU VA) */
    uint64_t caller_arg;    /* offset 0x18: ACQRB base + 0x1CC000 (queue metadata GPU VA) */
    uint64_t mmio_base;     /* offset 0x20: mmap'd register base (0xFE0200000) */
    uint32_t pipe_id;       /* offset 0x28: pipe_id (0xc) */
    uint32_t flags;         /* offset 0x2C: 0 */
    uint64_t ring_addr;     /* offset 0x30: eop_fifo_base + 0x39000 */
    uint64_t ring_size;     /* offset 0x38: ring entry size (0x1000) */
} AgcGcQueueCreateArg;
_Static_assert(sizeof(AgcGcQueueCreateArg) == 0x40,
    "AgcGcQueueCreateArg size mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, magic1) == 0x00,
    "AgcGcQueueCreateArg magic1 offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, magic2) == 0x04,
    "AgcGcQueueCreateArg magic2 offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, magic3) == 0x08,
    "AgcGcQueueCreateArg magic3 offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, token) == 0x0C,
    "AgcGcQueueCreateArg token offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, read_ptr_addr) == 0x10,
    "AgcGcQueueCreateArg read_ptr_addr offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, caller_arg) == 0x18,
    "AgcGcQueueCreateArg caller_arg offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, mmio_base) == 0x20,
    "AgcGcQueueCreateArg mmio_base offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, pipe_id) == 0x28,
    "AgcGcQueueCreateArg pipe_id offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, flags) == 0x2C,
    "AgcGcQueueCreateArg flags offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, ring_addr) == 0x30,
    "AgcGcQueueCreateArg ring_addr offset mismatch");
_Static_assert(offsetof(AgcGcQueueCreateArg, ring_size) == 0x38,
    "AgcGcQueueCreateArg ring_size offset mismatch");

/* Queue create authentication tokens (hardcoded in libSceAgcDriver.sprx) */
#define AGC_GC_QUEUE_MAGIC1     0xaf1e80b7u
#define AGC_GC_QUEUE_MAGIC2     0x8b4cdd90u
#define AGC_GC_QUEUE_MAGIC3     0x99f68d6cu
#define AGC_GC_QUEUE_TOKEN      0xe5fcc174u
#define AGC_GC_QUEUE_PIPE_ID    0xcu
#define AGC_GC_QUEUE_RING_SIZE  0x1000u

/*
 * Queue destroy ioctl argument struct (nr=0x0e, dir=RW, size=12).
 *
 * RE'd from libSceAgcDriver.sprx _sceAgcDriverDestroyUserSpecialQueue
 * at vaddr 0x2e30 and its internal helper at vaddr 0x8fc0.
 *
 * Only the three magic tokens are sent; the kernel identifies the
 * queue to destroy from the calling process's state.
 *
 * Ioctl command: 0xc00c810e = IOC(RW, 0x81, 0x0e, 12)
 */
typedef struct AgcGcQueueDestroyArg {
    uint32_t magic1;        /* offset 0x00: 0xaf1e80b7 */
    uint32_t magic2;        /* offset 0x04: 0x8b4cdd90 */
    uint32_t magic3;        /* offset 0x08: 0x99f68d6c */
} AgcGcQueueDestroyArg;
_Static_assert(offsetof(AgcGcQueueDestroyArg, magic1) == 0x00,
    "AgcGcQueueDestroyArg magic1 offset mismatch");
_Static_assert(offsetof(AgcGcQueueDestroyArg, magic2) == 0x04,
    "AgcGcQueueDestroyArg magic2 offset mismatch");
_Static_assert(offsetof(AgcGcQueueDestroyArg, magic3) == 0x08,
    "AgcGcQueueDestroyArg magic3 offset mismatch");
_Static_assert(sizeof(AgcGcQueueDestroyArg) == 0x0C,
    "AgcGcQueueDestroyArg size mismatch");

/*
 * Setup async graphics ioctl argument (nr=0x26, dir=READ, size=4).
 *
 * RE'd from libSceAgcDriver.sprx sceAgcDriverSetupAsyncGraphics
 * at vaddr 0x3ac0 and its internal helper at vaddr 0xa110.
 *
 * The SPRX sends a fixed value of 1 to initialize the async graphics
 * queue. The pipeId parameter only controls a flag stored in the
 * SPRX's global state (pipeId != 0 → async compute enabled).
 *
 * Ioctl command: 0x80048126 = IOC(READ, 0x81, 0x26, 4)
 */

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
 * by the host backend, but needed if the prospero backend ever needs to
 * validate behavior against the dump.
 */
#define AGC_GC_KERN_IOCTL_INTERNAL      0x6ed39cu  /* gc_ioctl_internal */
#define AGC_GC_KERN_OPEN_INTERNAL       0x6ec100u  /* gc_open_internal (NOT FRAME_OPEN) */
#define AGC_GC_KERN_CONTEXT_QUERY       0x6ee691u  /* context capability query handler */
#define AGC_GC_KERN_SUBMIT_WITH_PID     0x6e65c0u  /* gc_submit_with_pid */
#define AGC_GC_KERN_FRAME_SUBMIT        0xb7da90u  /* gc_frame_submit_internal */

#endif /* _AGC_IOCTL_H_ */
