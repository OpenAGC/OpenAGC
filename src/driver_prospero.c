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

/*
 * openagc — driver_prospero.c
 *
 * Native PS5 (prospero) backend for AGC driver functions.
 * Talks directly to the kernel /dev/gc driver via ioctls using the
 * constants and struct layouts from include/agc_ioctl.h.
 *
 * This file is only compiled when OPENAGC_PROSPERO is defined. It cannot be
 * built or tested on the host — it requires the ps5-payload-sdk toolchain
 * and PS5 hardware for validation.
 *
 * RE sources:
 * - Kernel dump gc_ioctl_internal at 0x6ed39c (BST + 4 jump tables)
 * - gc_submit_with_pid at 0x6e65c0, gc_frame_submit_internal at 0xb7da90
 * - Sibling ps5-openagc project's agc_driver.c / agc_submit.c / agc_queue.c
 *   (NOT proven working — used for initial NID mapping only; ioctl layouts
 *   and queue structs were independently verified from SPRX disassembly)
 * - HLE reference for userspace ioctl call patterns
 *
 * openagc is a clean rewrite — it calls ioctls directly rather than
 * delegating to libSceAgcDriver.sprx firmware symbols.
 */

#include "agcdriver.h"
#include "agc_types.h"
#include "agc_error.h"
#include "agc_ioctl.h"
#include "agc_context.h"
#include "agc_pm4.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef OPENAGC_PROSPERO

/* /dev/gc device path */
#define AGC_GC_DEVICE_PATH "/dev/gc"

/* Maximum number of GPU command queues */
#define AGC_PROSPERO_MAX_QUEUES 32

/*
 * PS5 kernel memory type constants. These are normally provided by the
 * ps5-payload-sdk libkernel headers. We declare them here so the host
 * build (which never enters this #ifdef block) stays SDK-free. If the
 * SDK headers are included before this file, the header definitions take
 * precedence.
 */
/* PS5 memory type constants — verified on hardware.
 * These differ from PS4! PS4 had WB_ONION=0, WC_GARLIC=1, WB_GARLIC=3.
 * PS5 (proven by PS5_DEV_HOMEBREW/examples/ps5_sdk/gnm_onion_probe.c):
 *   1 = WB_ONION  (CPU-coherent, for command buffers)
 *   3 = WC_GARLIC (GPU-direct, for framebuffers/data)
 * ps5-openagc had these wrong (0,1,2) — another confirmed error. */
#ifndef SCE_KERNEL_WB_ONION
#define SCE_KERNEL_WB_ONION  1
#endif
#ifndef SCE_KERNEL_WC_GARLIC
#define SCE_KERNEL_WC_GARLIC 3
#endif
#ifndef SCE_KERNEL_WB_GARLIC
#define SCE_KERNEL_WB_GARLIC 2
#endif

/* PS5 direct memory search range and alignment.
 * Onion: searchEnd=0x100000000 (4GB), alignment=0x1000 (4KB)
 * Garlic: searchEnd=0x300000000 (12GB), alignment=0x200000 (2MB)
 * Verified from PS5_DEV_HOMEBREW/examples/ps5_sdk/. */
#define PS5_DM_SEARCH_END_ONION   0x100000000LL
#define PS5_DM_SEARCH_END_GARLIC  0x300000000LL
#define PS5_DM_ALIGN_ONION        0x1000u
#define PS5_DM_ALIGN_GARLIC       0x200000u

#ifndef SCE_KERNEL_PROT_CPU_RW
#define SCE_KERNEL_PROT_CPU_RW 0x03   /* CPU_READ(0x01) | CPU_WRITE(0x02) */
#endif
#ifndef SCE_KERNEL_PROT_GPU_RW
#define SCE_KERNEL_PROT_GPU_RW 0x30   /* GPU_READ(0x10) | GPU_WRITE(0x20) */
#endif

/* libkernel direct-memory API (prospero only). */
extern int32_t sceKernelAllocateDirectMemory(
    int64_t searchStart, int64_t searchEnd, size_t length,
    uint64_t alignment, int memoryType, off_t *physicalAddrOut);
extern int32_t sceKernelMapDirectMemory(
    void **virtualAddr, size_t length, int prot, int flags,
    off_t physicalAddr, uint64_t alignment);
extern int32_t sceKernelMunmap(void *addr, size_t len);
extern int32_t sceKernelReleaseDirectMemory(off_t physicalAddr, size_t length);

/*
 * sceKernelMapNamedSystemFlexibleMemory — the allocation API used by the
 * real SPRX for internal memory regions. Signature RE'd from SPRX PLT:
 *   int sceKernelMapNamedSystemFlexibleMemory(
 *       void **addr, size_t size, int type, int flags, const char *name);
 * The SPRX passes type=0x33 for GPU regions and type=3 for SceGnmGpuInfo.
 */
extern int32_t sceKernelMapNamedSystemFlexibleMemory(
    void **addr, size_t size, int type, int flags, const char *name);

/* Named internal memory region allocated by sce_agc_initialize_internal_memory */
typedef struct {
    void    *cpu_addr;
    off_t    physical_addr;
    uint64_t gpu_addr;
    size_t   size;
} AgcProsperoRegion;

/* Per-queue state */
typedef struct {
    bool            in_use;
    uint64_t        ring_base;    /* GPU VA of ring buffer */
    uint64_t        read_ptr;     /* GPU VA of read pointer */
    AgcProsperoRegion  ring_region;  /* backing allocation for ring + read ptr */
} AgcProsperoQueue;

/*
 * Prospero backend context.
 *
 * The generic backend uses global statics; the prospero backend needs a
 * /dev/gc file descriptor and queue tracking. We keep it in a single
 * static struct for simplicity — the PS5 AGC driver is singleton.
 */
typedef struct {
    int              gc_fd;          /* /dev/gc file descriptor */
    bool             initialized;    /* sce_agc_initialize succeeded */
    bool             mem_initialized;/* sce_agc_initialize_internal_memory succeeded */
    bool             defaults_notified;/* sceAgcDriverNotifyDefaultStates succeeded */
    bool             async_setup_done;/* sceAgcDriverSetupAsyncGraphics succeeded */
    void            *mmio_base;      /* GPU register MMIO mapping (0xfe0200000) */
    uint32_t         ctx_capability; /* context query result from ioctl 0x2e */
    AgcProsperoQueue    queues[AGC_PROSPERO_MAX_QUEUES];
    /* Internal memory regions — sizes from SPRX disassembly (FW 5.50) */
    AgcProsperoRegion   gpu_info;    /* SceGnmGpuInfo:   0x100000 (1 MB) */
    AgcProsperoRegion   trap_code;   /* SceGnmTrapCode:  0x4000   (16 KB) */
    AgcProsperoRegion   trap_data;   /* SceGnmTrapData:  0x4000   (16 KB) */
    AgcProsperoRegion   ddid;        /* SceGnmDdid:      0xFC000  (1008 KB) */
    AgcProsperoRegion   eop_fifo;    /* SceGnmEopFifo:   0x3C000  (240 KB) */
    AgcProsperoRegion   shadow_reg;  /* SceGnmShadowReg: 0x4000   (16 KB) */
    AgcProsperoRegion   cwsr;        /* SceGnmCwsr:      0x1000000 (16 MB) */
    AgcProsperoRegion   misc;        /* SceGnmMisc:      0x4000   (16 KB) */
    AgcProsperoRegion   acqrb;       /* SceGnmACQRB:     0x1E0000 (1920 KB) */
    AgcProsperoRegion   primary_defaults;
    AgcProsperoRegion   internal_defaults;
} AgcProsperoContext;

static AgcProsperoContext g_prospero = {
    .gc_fd = -1,
};

/*
 * Low-level ioctl wrapper.
 * Returns 0 on success, negative errno on ioctl failure.
 */
static int agcProsperoIoctl(uint32_t cmd, void *arg)
{
    if (g_prospero.gc_fd < 0)
        return -1;
    int ret = ioctl(g_prospero.gc_fd, cmd, arg);
    if (ret < 0) {
        printf("[agc_ioctl] cmd=0x%08X ret=%d errno=%d (0x%08X)\n",
               cmd, ret, errno, (unsigned)errno);
    }
    return ret;
}

/*
 * Build a CB descriptor for the submit ioctl.
 *
 * The CB descriptor is 16 bytes:
 *   header (8 bytes): PM4 type-3 header | (ib_size_dwords << 32)
 *   ib_base (8 bytes): GPU VA in lower 52 bits, VMID=0 (kernel inserts)
 *
 * Two valid header opcodes:
 *   0xC0023300 = IT_INDIRECT_BUFFER_CONST (opcode 0x33)
 *   0xC0023F00 = IT_INDIRECT_BUFFER       (opcode 0x3F)
 *
 * The upper 32 bits of the 64-bit header field contain the IB size
 * (in dwords). The lower 32 bits contain the PM4 packet header.
 */
static void agcProsperoBuildCbDescriptor(AgcGcCommandBuffer *cb,
                                       uint64_t gpu_addr,
                                       uint32_t size_dwords,
                                       bool is_const)
{
    if (is_const)
        cb->header = AGC_GC_CB_HEADER_IB_CNST | ((uint64_t)size_dwords << 32);
    else
        cb->header = AGC_GC_CB_HEADER_IB | ((uint64_t)size_dwords << 32);

    /* ib_base: GPU VA in lower 52 bits, VMID = 0 (kernel will insert) */
    cb->ib_base = gpu_addr & AGC_GC_IB_VMASK;
}

/* Find a free queue slot. Returns index >= 0, or -1 if all slots in use. */
static int agcProsperoFindFreeQueue(void)
{
    for (int i = 0; i < AGC_PROSPERO_MAX_QUEUES; i++) {
        if (!g_prospero.queues[i].in_use)
            return i;
    }
    return -1;
}

/*
 * Allocate a single named internal region using the flexible memory API.
 *
 * The SPRX uses sceKernelMapNamedSystemFlexibleMemory for all internal
 * memory regions. This is a single-call API that both allocates and maps.
 * The GPU VA is the same as the CPU VA (flexible memory is unified).
 *
 * For regions that need a GPU VA mapping via MAKESYSMAP, we still call
 * the ioctl after the flexible memory allocation.
 *
 * On failure, any partial allocation is unwound and the region is zeroed.
 */
static int32_t agcProsperoAllocRegion(AgcProsperoRegion *region,
                                      size_t size, int mem_type,
                                      const char *name)
{
    if (!region || size == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    memset(region, 0, sizeof(*region));

    /* Use sceKernelMapNamedSystemFlexibleMemory (matches SPRX behavior).
     * The SPRX passes type=0x33 for GPU regions, type=3 for SceGnmGpuInfo. */
    void *addr = NULL;
    int32_t ret = sceKernelMapNamedSystemFlexibleMemory(
        &addr, size, mem_type, 0, name);
    if (ret != 0 || !addr) {
        printf("    [alloc] flexible mem ret=%d (size=0x%zx type=%d name=%s)\n",
               ret, size, mem_type, name ? name : "?");
        return AGC_ERROR_OUT_OF_MEMORY;
    }

    /* The flexible memory API returns a unified CPU/GPU address.
     * No separate MAKESYSMAP ioctl is needed — the kernel handles the
     * GPU VA mapping internally for flexible memory. */
    region->cpu_addr = addr;
    region->physical_addr = 0;  /* flexible memory has no separate physical addr */
    region->gpu_addr = (uint64_t)(uintptr_t)addr;
    region->size = size;
    return AGC_OK;
}

/*
 * Free a single named internal region.
 */
static void agcProsperoFreeRegion(AgcProsperoRegion *region)
{
    if (!region || region->size == 0)
        return;

    if (region->cpu_addr)
        sceKernelMunmap(region->cpu_addr, region->size);

    memset(region, 0, sizeof(*region));
}

/*
 * Carve out a sub-region from an already-allocated parent region.
 * This avoids calling sceKernelMapNamedSystemFlexibleMemory again (which
 * fails with ENOMEM after the 9 internal regions exhaust the kernel's
 * flexible memory quota).
 *
 * The SPRX uses the SceGnmDdid region (1008 KB) for default-state blobs
 * and small DCB buffers, not separate allocations.
 */
static int32_t agcProsperoCarveSubRegion(
    AgcProsperoRegion *parent, size_t offset, size_t size,
    AgcProsperoRegion *out)
{
    if (!parent || !out || offset + size > parent->size)
        return AGC_ERROR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->cpu_addr = (char *)parent->cpu_addr + offset;
    out->gpu_addr = parent->gpu_addr + offset;
    out->size = size;
    out->physical_addr = 0;  /* sub-region, no separate physical addr */
    return AGC_OK;
}

/* DDID sub-region offsets for default-state blobs and DCB scratch.
 * SceGnmDdid is 0xFC000 (1008 KB); we carve from the end to avoid
 * conflicting with SPRX-expected DDID layout. */
#define AGC_DDID_PRIMARY_OFFSET   0xF0000   /* 960 KB offset → 48 KB for primary */
#define AGC_DDID_INTERNAL_OFFSET  0xF8000   /* 992 KB offset → 16 KB for internal */
#define AGC_DDID_DCB_OFFSET       0xFC000 - 16  /* last 16 bytes for DCB scratch */

/* ===================================================================== */
/* Public API — initialization                                           */
/* ===================================================================== */

/*
 * Open /dev/gc and initialize the AGC context.
 *
 * RE'd from libSceAgcDriver.sprx module_start (vaddr 0x77f0) and its
 * /dev/gc open helper at vaddr 0x7cc0. See
 * analysis/sprx_sce_agc_initialize_disasm.md for the full disassembly.
 *
 * The real initialization sequence (matching the SPRX):
 * 1. Open /dev/gc with O_RDWR
 * 2. Query context state via ioctl 0xc004812e (CONTEXT_QUERY, nr=0x2e)
 *    - Returns a 32-bit capability mask
 *    - If lower 16 bits are 0, the context is not yet initialized
 * 3. If context is not initialized (capability == 0):
 *    - mmap GPU register space at fixed address 0xfe0200000 (16KB, MAP_SHARED)
 *    - mlock the mapping to prevent page-out
 * 4. Store the fd and capability for later use
 *
 * NOTE: The old implementation used FRAME_OPEN (nr=0x00) which does NOT
 * exist in FW 5.50 — the kernel returns EINVAL for it. This was the
 * cause of the hardware validation failure.
 */
int32_t PS5_SYSV_ABI sce_agc_initialize(void)
{
    if (g_prospero.initialized)
        return AGC_OK;

    /* Step 1: Open /dev/gc */
    g_prospero.gc_fd = open(AGC_GC_DEVICE_PATH, O_RDWR);
    if (g_prospero.gc_fd < 0)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Step 2: Query context capability via ioctl 0xc004812e (nr=0x2e).
     * This is a 4-byte RW ioctl that returns a capability bitmask. */
    AgcGcContextQueryResult query_result = {0};
    int ret = agcProsperoIoctl(AGC_GC_IOCTL_CONTEXT_QUERY, &query_result);
    if (ret < 0) {
        close(g_prospero.gc_fd);
        g_prospero.gc_fd = -1;
        return AGC_ERROR_NOT_INITIALIZED;
    }

    g_prospero.ctx_capability = query_result.capability_mask;

    /* Step 3: If context is not initialized (capability lower 16 bits == 0),
     * mmap the GPU register space at the fixed address used by the SPRX. */
    if ((query_result.capability_mask & 0xFFFF) == 0) {
        void *mmio = mmap((void *)AGC_GC_MMIO_BASE, AGC_GC_MMIO_SIZE,
                          PROT_READ | PROT_WRITE, MAP_SHARED,
                          g_prospero.gc_fd, 0);
        if (mmio == MAP_FAILED || mmio == NULL) {
            close(g_prospero.gc_fd);
            g_prospero.gc_fd = -1;
            return AGC_ERROR_NOT_INITIALIZED;
        }
        g_prospero.mmio_base = mmio;

        /* Lock the mapping to prevent page-out (matches SPRX behavior) */
        mlock(mmio, AGC_GC_MMIO_SIZE);
    }

    g_prospero.initialized = true;
    return AGC_OK;
}

/*
 * Allocate internal memory regions.
 *
 * RE'd from libSceAgcDriver.sprx function at vaddr 0x7e70 (FW 5.50).
 * The SPRX uses sceKernelMapNamedSystemFlexibleMemory for all regions.
 *
 * Region sizes and names are confirmed from SPRX disassembly — see
 * analysis/sprx_agc_driver_internal_mem_disasm.md for the full analysis.
 *
 * The SPRX allocates 9 regions totaling ~19.3 MB:
 *   SceGnmGpuInfo:   0x100000  (1 MB)    type=3  (WC_GARLIC)
 *   SceGnmTrapCode:  0x4000    (16 KB)   type=0x33 (flexible)
 *   SceGnmTrapData:  0x4000    (16 KB)   type=0x33
 *   SceGnmDdid:      0xFC000   (1008 KB) type=0x33
 *   SceGnmEopFifo:   0x3C000   (240 KB)  type=0x33
 *   SceGnmShadowReg: 0x4000    (16 KB)   type=0x33
 *   SceGnmCwsr:      0x1000000 (16 MB)   type=0x33
 *   SceGnmMisc:      0x4000    (16 KB)   type=0x33
 *   SceGnmACQRB:     0x1E0000  (1920 KB) type=0x33
 *
 * The EOP FIFO region (SceGnmEopFifo) is used as the ring buffer base
 * for queue creation — the ring buffer is carved at offset 0x39000 from
 * the EOP FIFO base.
 */
int32_t PS5_SYSV_ABI sce_agc_initialize_internal_memory(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (g_prospero.mem_initialized)
        return AGC_OK;

    /* Memory type for flexible memory (SPRX uses 0x33 for GPU regions).
     * This is a flexible-memory-specific type flag, not the same as
     * sceKernelAllocateDirectMemory types (1/2/3). */
    #define AGC_FLEX_TYPE_GPU   0x33
    #define AGC_FLEX_TYPE_INFO  3

    struct {
        AgcProsperoRegion *region;
        size_t          size;
        int             mem_type;
        const char     *name;
    } regions[] = {
        { &g_prospero.gpu_info,   0x100000,  AGC_FLEX_TYPE_INFO, "SceGnmGpuInfo"   },
        { &g_prospero.trap_code,  0x4000,    AGC_FLEX_TYPE_GPU,  "SceGnmTrapCode"  },
        { &g_prospero.trap_data,  0x4000,    AGC_FLEX_TYPE_GPU,  "SceGnmTrapData"  },
        { &g_prospero.ddid,       0xFC000,   AGC_FLEX_TYPE_GPU,  "SceGnmDdid"      },
        { &g_prospero.eop_fifo,   0x3C000,   AGC_FLEX_TYPE_GPU,  "SceGnmEopFifo"   },
        { &g_prospero.shadow_reg, 0x4000,    AGC_FLEX_TYPE_GPU,  "SceGnmShadowReg" },
        { &g_prospero.cwsr,       0x1000000, AGC_FLEX_TYPE_GPU,  "SceGnmCwsr"      },
        { &g_prospero.misc,       0x4000,    AGC_FLEX_TYPE_GPU,  "SceGnmMisc"      },
        { &g_prospero.acqrb,      0x1E0000,  AGC_FLEX_TYPE_GPU,  "SceGnmACQRB"     },
    };

    for (int i = 0; i < (int)(sizeof(regions) / sizeof(regions[0])); i++) {
        printf("    [mem] %s: size=0x%zx type=0x%x... ", regions[i].name,
               regions[i].size, regions[i].mem_type);
        int32_t ret = agcProsperoAllocRegion(regions[i].region,
                                          regions[i].size,
                                          regions[i].mem_type,
                                          regions[i].name);
        if (ret != AGC_OK) {
            printf("FAILED (0x%x)\n", (unsigned)ret);
            /* Clean up any regions we already allocated. */
            for (int j = 0; j < i; j++)
                agcProsperoFreeRegion(regions[j].region);
            return ret;
        }
        printf("OK (addr=%p)\n", regions[i].region->cpu_addr);
    }

    g_prospero.mem_initialized = true;
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — submission                                               */
/* ===================================================================== */

/*
 * Submit multiple command buffers (DCB + ACB) to the GPU.
 *
 * Builds CB descriptors for each DCB and ACB, then calls the submit
 * ioctl (nr=0x3b) to send them to the GPU via gc_submit_with_pid.
 *
 * The kernel path:
 *   gc_submit_with_pid → gc_frame_submit_internal
 *     - validates num_cbs in [1, 0xFFF]
 *     - copyin CB array into ring buffer
 *     - per CB: checks header opcode, masks ib_base, ORs in VMID<<52
 *     - calls gc_insert_indirect_buffer per CB
 */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitMultiCommandBuffersDirect(
    uint32_t count,
    void *const dcb_gpu_addrs[],
    uint32_t *dcb_sizes_in_bytes,
    void *const acb_gpu_addrs[],
    uint32_t *acb_sizes_in_bytes)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (count == 0 || count > AGC_GC_NUM_CBS_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!dcb_gpu_addrs || !dcb_sizes_in_bytes)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* Count total CBs: DCBs + ACBs (if present) */
    uint32_t num_acbs = 0;
    if (acb_gpu_addrs && acb_sizes_in_bytes) {
        for (uint32_t i = 0; i < count; i++) {
            if (acb_gpu_addrs[i])
                num_acbs++;
        }
    }

    uint32_t num_dcbs = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (dcb_gpu_addrs[i])
            num_dcbs++;
    }

    uint32_t total_cbs = num_dcbs + num_acbs;
    if (total_cbs == 0 || total_cbs > AGC_GC_NUM_CBS_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* Build CB descriptor array on stack (max 0xFFF = 4095 CBs, but
     * practically we use a small stack array) */
    AgcGcCommandBuffer cb_descs[64];
    if (total_cbs > 64)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint32_t cb_idx = 0;
    for (uint32_t i = 0; i < count && cb_idx < total_cbs; i++) {
        if (!dcb_gpu_addrs[i])
            continue;
        if (dcb_sizes_in_bytes[i] & 0x3)
            return AGC_ERROR_CB_INVALID_SIZE;
        uint32_t size_dwords = dcb_sizes_in_bytes[i] / 4;
        agcProsperoBuildCbDescriptor(&cb_descs[cb_idx],
                                   (uint64_t)(uintptr_t)dcb_gpu_addrs[i],
                                   size_dwords, false);
        cb_idx++;
    }

    for (uint32_t i = 0; i < count && cb_idx < total_cbs; i++) {
        if (!acb_gpu_addrs || !acb_gpu_addrs[i])
            continue;
        if (acb_sizes_in_bytes[i] & 0x3)
            return AGC_ERROR_CB_INVALID_SIZE;
        uint32_t size_dwords = acb_sizes_in_bytes[i] / 4;
        uint32_t *acb = (uint32_t *)(uintptr_t)acb_gpu_addrs[i];

        /* reference-confirmed: ACB descriptor indirection.
         * If the ACB starts with a descriptor header (magic 0x5533ccaa),
         * the actual ACB data is at the address pointed to by acb[0..1]. */
        if (size_dwords >= 5 && acb[3] == 0 && acb[4] == 0x5533ccaau) {
            uint64_t desc_addr = (uint64_t)acb[0] | ((uint64_t)acb[1] << 32);
            uint32_t desc_size = acb[2];
            if (desc_addr != 0 && desc_size != 0) {
                acb = (uint32_t *)(uintptr_t)desc_addr;
                size_dwords = desc_size;
            }
        }

        /* ACBs use the const IB type */
        agcProsperoBuildCbDescriptor(&cb_descs[cb_idx],
                                   (uint64_t)(uintptr_t)acb,
                                   size_dwords, true);
        cb_idx++;
    }

    /* Build submit ioctl arg */
    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.pid = 0;  /* 0 = current process */
    submit_arg.num_cbs = total_cbs;
    submit_arg.cb_array = (uint64_t)(uintptr_t)cb_descs;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_SUBMIT_PID, &submit_arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

/*
 * Submit a single DCB (draw command buffer) to the GPU.
 *
 * Wraps the single CB in a submit ioctl call.
 */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitDcb(const AgcCommandBufferSubmit *packet)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    AgcGcCommandBuffer cb_desc;
    agcProsperoBuildCbDescriptor(&cb_desc,
                               (uint64_t)packet->command_address,
                               packet->dword_count, false);

    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.pid = 0;
    submit_arg.num_cbs = 1;
    submit_arg.cb_array = (uint64_t)(uintptr_t)&cb_desc;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_SUBMIT_PID, &submit_arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

/*
 * Submit a single ACB (async compute command buffer) to the GPU.
 *
 * The owner_handle selects which compute queue to submit to.
 * ACBs use the IT_INDIRECT_BUFFER_CONST header type.
 */
int32_t PS5_SYSV_ABI sceAgcDriverSubmitAcb(
    uint32_t owner_handle, const AgcCommandBufferSubmit *packet)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (owner_handle >= AGC_PROSPERO_MAX_QUEUES)
        return AGC_ERROR_CB_INVALID_QUEUE;
    if (!g_prospero.queues[owner_handle].in_use)
        return AGC_ERROR_CB_INVALID_QUEUE;

    uint64_t acb_addr = packet->command_address;
    uint32_t size_dwords = packet->dword_count;
    uint32_t *acb = (uint32_t *)(uintptr_t)acb_addr;

    /* reference-confirmed: ACB descriptor indirection.
     * If the ACB starts with a descriptor header (magic 0x5533ccaa),
     * the actual ACB data is at the address pointed to by acb[0..1]. */
    if (size_dwords >= 5 && acb[3] == 0 && acb[4] == 0x5533ccaau) {
        uint64_t desc_addr = (uint64_t)acb[0] | ((uint64_t)acb[1] << 32);
        uint32_t desc_size = acb[2];
        if (desc_addr != 0 && desc_size != 0) {
            acb_addr = desc_addr;
            size_dwords = desc_size;
        }
    }

    AgcGcCommandBuffer cb_desc;
    agcProsperoBuildCbDescriptor(&cb_desc, acb_addr, size_dwords, true);

    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.pid = 0;
    submit_arg.num_cbs = 1;
    submit_arg.cb_array = (uint64_t)(uintptr_t)&cb_desc;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_SUBMIT_PID, &submit_arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

/* ===================================================================== */
/* Public API — suspend points                                           */
/* ===================================================================== */

int32_t PS5_SYSV_ABI sceAgcDriverSuspendPointSubmitDirect(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /*
     * Submit a suspend point via the 16-byte gc_accept_suspend_locked ioctl.
     * The FW 5.50 kernel handler at 0x6e6ff0 expects:
     *   field0 in {1, 2} (or one of two magic triples)
     *   field1 <= 3
     *   field2 <= 7
     *   field3 is the value written to the selected suspend ring slot.
     */
    AgcGcSuspendArg arg = {0};
    arg.field0 = field0;
    arg.field1 = field1;
    arg.field2 = field2;
    arg.field3 = field3;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_SUSPEND_16, &arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

bool PS5_SYSV_ABI sceAgcDriverIsSuspendPointInFlightDirect(uint32_t value)
{
    (void)value;
    if (!g_prospero.initialized)
        return false;

    /*
     * Query the gfx queue status via the 4-byte read ioctl (nr=0x27).
     * The exact suspend-point bit layout is still pending RE; until then
     * treat any non-zero status as "in flight".
     */
    uint32_t status = 0;
    int ret = agcProsperoIoctl(AGC_GC_IOCTL_QUEUE_STAT_16, &status);
    if (ret < 0)
        return false;

    return status != 0;
}

int32_t PS5_SYSV_ABI sce_agc_internal_suspend_point_submit_final(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /*
     * Internal final suspend point submission variant. On FW 5.50 this is
     * the nr=0x39 suspend ioctl variant. The argument layout is the same 16
     * bytes as the primary suspend ioctl.
     */
    AgcGcSuspendArg arg = {0};
    arg.field0 = field0;
    arg.field1 = field1;
    arg.field2 = field2;
    arg.field3 = field3;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_SUSPEND_39, &arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — async graphics / TF ring / HS offchip                    */
/* ===================================================================== */

/*
 * Setup async graphics queue.
 *
 * RE'd from libSceAgcDriver.sprx at vaddr 0x3ac0. The SPRX calls
 * ioctl nr=0x26 (QUEUE_STATUS, 4-byte READ) with a fixed value of 1
 * to initialize the async graphics queue. The pipe_id parameter only
 * controls a flag in the SPRX's global state (pipe_id != 0 → async
 * compute enabled). The ioctl is only called once; subsequent calls
 * are a no-op.
 *
 * Ioctl command: 0x80048126 = IOC(READ, 0x81, 0x26, 4)
 */
int32_t PS5_SYSV_ABI sceAgcDriverSetupAsyncGraphics(uint32_t pipe_id)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    if (g_prospero.async_setup_done) {
        /* SPRX only calls the ioctl once, then sets a flag. */
        return AGC_OK;
    }

    uint32_t arg = 1;
    int ret = agcProsperoIoctl(AGC_GC_IOCTL_QUEUE_STATUS, &arg);
    if (ret < 0)
        return AGC_ERROR_INTERNAL;

    g_prospero.async_setup_done = true;
    /* Store pipe_id != 0 flag (used by SPRX for async compute routing). */
    (void)pipe_id;

    return AGC_OK;
}

/*
 * Set tessellation factor ring.
 * Uses ioctl nr=0x20 (SET_TF_RING, 16-byte RW).
 */
int32_t PS5_SYSV_ABI sceAgcDriverSetTFRingDirect(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    uint32_t arg[4] = {0};
    int ret = agcProsperoIoctl(AGC_GC_IOCTL_SET_TF_RING, arg);
    return (ret < 0) ? AGC_ERROR_INTERNAL : AGC_OK;
}

/*
 * Set hull shader offchip parameters.
 * Uses ioctl nr=0x2c (SET_HS_OFFCHIP, 16-byte RW).
 *
 * The FW 5.50 kernel handler at 0x6ee6d2 passes the argument directly to
 * gc_pm4_clearstate_patch (0xb7dd20) as a patch list pointer and count.
 */
int32_t PS5_SYSV_ABI sceAgcDriverSetHsOffchipParamDirect(
    uint64_t list_addr, uint32_t num_entries)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    AgcGcSetHsOffchipArg arg = {0};
    arg.list_addr = list_addr;
    arg.num_entries = num_entries;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_SET_HS_OFFCHIP, &arg);
    return (ret < 0) ? AGC_ERROR_INTERNAL : AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSetTargetRingForDiag(void)
{
    /* No direct ioctl mapping — diagnostic target ring setup.
     * The firmware uses a large-arg ioctl or direct register write. */
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — default states                                           */
/* ===================================================================== */

int32_t PS5_SYSV_ABI sceAgcDriverNotifyDefaultStates(uint32_t flags)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (g_prospero.defaults_notified)
        return AGC_OK;
    if (!g_prospero.mem_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    (void)flags; /* flags meaning is still pending RE */

    /*
     * Build the FW 5.50 primary and internal register-defaults blobs in
     * GPU-visible memory. The blobs are then available to the driver for
     * context-state loading. The actual submission path that consumes these
     * blobs is still pending hardware validation.
     */
    uint32_t primary_count = 0;
    uint32_t internal_count = 0;
    const AgcRegisterDefaultsGroup *primary_groups =
        agcRegisterDefaultsGetPrimaryGroups(&primary_count);
    const AgcRegisterDefaultsGroup *internal_groups =
        agcRegisterDefaultsGetInternalGroups(&internal_count);

    size_t primary_size = agcRegisterDefaultsComputeSize(
        primary_count, AGC_PRIMARY_CX_LENGTH, AGC_PRIMARY_SH_LENGTH, AGC_PRIMARY_UC_LENGTH);
    size_t internal_size = agcRegisterDefaultsComputeSize(
        internal_count, AGC_INTERNAL_CX_LENGTH, AGC_INTERNAL_SH_LENGTH, AGC_INTERNAL_UC_LENGTH);
    (void)primary_size;   /* sub-region carved from DDID, size checked at build */
    (void)internal_size;  /* sub-region carved from DDID, size checked at build */

    int32_t ret = agcProsperoCarveSubRegion(
        &g_prospero.ddid, AGC_DDID_PRIMARY_OFFSET, 0x8000,
        &g_prospero.primary_defaults);
    if (ret != AGC_OK)
        return ret;

    ret = agcProsperoCarveSubRegion(
        &g_prospero.ddid, AGC_DDID_INTERNAL_OFFSET, 0x4000,
        &g_prospero.internal_defaults);
    if (ret != AGC_OK)
        return ret;

    ret = agcRegisterDefaultsBuild(
        g_prospero.primary_defaults.cpu_addr,
        g_prospero.primary_defaults.size,
        g_prospero.primary_defaults.gpu_addr,
        primary_groups,
        primary_count,
        AGC_PRIMARY_CX_LENGTH,
        AGC_PRIMARY_SH_LENGTH,
        AGC_PRIMARY_UC_LENGTH);
    if (ret != AGC_OK) {
        agcProsperoFreeRegion(&g_prospero.internal_defaults);
        agcProsperoFreeRegion(&g_prospero.primary_defaults);
        return ret;
    }

    ret = agcRegisterDefaultsBuild(
        g_prospero.internal_defaults.cpu_addr,
        g_prospero.internal_defaults.size,
        g_prospero.internal_defaults.gpu_addr,
        internal_groups,
        internal_count,
        AGC_INTERNAL_CX_LENGTH,
        AGC_INTERNAL_SH_LENGTH,
        AGC_INTERNAL_UC_LENGTH);
    if (ret != AGC_OK) {
        agcProsperoFreeRegion(&g_prospero.internal_defaults);
        agcProsperoFreeRegion(&g_prospero.primary_defaults);
        return ret;
    }

    /*
     * Submit a CLEAR_STATE packet to initialize the GPU context to the
     * primary defaults. The kernel patches CLEAR_STATE (opcode 0x14) via
     * gc_pm4_clearstate_patch; the primary/internal blobs we just built are
     * GPU-visible and consumed during context reset.
     */
    AgcProsperoRegion dcb_region = {0};
    ret = agcProsperoCarveSubRegion(
        &g_prospero.ddid, AGC_DDID_DCB_OFFSET, 16, &dcb_region);
    if (ret != AGC_OK)
        return ret;

    uint32_t *dcb = (uint32_t *)dcb_region.cpu_addr;
    dcb[0] = agcPm4Header3(AGC_PM4_OP_CLEAR_STATE, 2);
    dcb[1] = 0;

    AgcCommandBufferSubmit submit = {0};
    submit.command_address = (uintptr_t)dcb_region.gpu_addr;
    submit.dword_count = 2;

    ret = sceAgcDriverSubmitDcb(&submit);
    /* dcb_region is a sub-region of ddid — don't munmap it, just clear */
    memset(&dcb_region, 0, sizeof(dcb_region));
    if (ret != AGC_OK)
        return ret;

    g_prospero.defaults_notified = true;
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — SDMA copy                                                */
/* ===================================================================== */

/*
 * SDMA linear copy (blocking).
 *
 * On PS5 hardware, this would use the SDMA engine via a dedicated
 * ioctl or command buffer submission. For now, fall back to memcpy
 * since the command buffer data is in CPU-visible memory.
 */
int32_t PS5_SYSV_ABI sceAgcDriverSdmaCopyLinearBlocking(
    void *dst, const void *src, size_t size)
{
    if (!dst || !src)
        return AGC_ERROR_INVALID_ARGUMENT;
    memcpy(dst, src, size);
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — EOP flip submit                                          */
/* ===================================================================== */

/*
 * Submit an end-of-pipe flip to the display.
 *
 * RE'd from libSceAgcDriver.sprx ordinals 49 (cwbxjPSJ7WQ, 585B) and 50
 * (u8BkdHb1+Po, 428B). Both functions:
 *   - Validate display buffer index: r8d+2 < 0x12 (max 16 display buffers)
 *   - Return VideoOut error 0x8029000a on invalid index
 *   - Build an IT_RELEASE_MEM (opcode 0x49) PM4 type-3 header (0xc0064900)
 *   - Call sceVideoOutSubmitEopFlip from the VideoOut library
 *   - Use 0xfffd1000 as a mask/flag value in the EOP packet
 *
 * The SPRX internally delegates to sceVideoOutSubmitEopFlip rather than
 * issuing a /dev/gc ioctl — the EOP flip is a VideoOut-side operation that
 * the GPU signals via the IT_RELEASE_MEM packet.
 */

/* VideoOut error code for invalid display buffer index (from SPRX RE). */
#define AGC_VIDEO_OUT_ERROR_INVALID_INDEX  ((int32_t)0x8029000a)

/* Maximum number of display buffers (SPRX checks r8d+2 < 0x12 → max 16). */
#define AGC_PROSPERO_MAX_DISPLAY_BUFFERS 16

/* sceVideoOutSubmitEopFlip — provided by libSceVideoOut.sprx at runtime. */
extern int32_t sceVideoOutSubmitEopFlip(
    int32_t videoOutHandle, int32_t bufferIndex,
    int32_t flipMode, int32_t presentPtr);

int32_t PS5_SYSV_ABI sceAgcDriverSubmitEopFlip(
    void *video_out_handle, uint32_t display_buf_index,
    uint32_t flip_mode, void *present_ptr)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Validate display buffer index (SPRX: r8d+2 < 0x12 → index < 16). */
    if (display_buf_index >= AGC_PROSPERO_MAX_DISPLAY_BUFFERS)
        return AGC_VIDEO_OUT_ERROR_INVALID_INDEX;

    /*
     * Delegate to sceVideoOutSubmitEopFlip. The VideoOut handle is an
     * int32_t from sceVideoOutOpen; we cast from the void* wrapper. The
     * present_ptr is a GPU address or 0; cast to int32_t for the VideoOut
     * ABI (the upper bits are unused on PS5 since GPU VAs fit in 32 bits
     * for the present-queue range).
     */
    int32_t handle = (int32_t)(intptr_t)video_out_handle;
    int32_t present = (int32_t)(intptr_t)present_ptr;

    return sceVideoOutSubmitEopFlip(
        handle, (int32_t)display_buf_index, (int32_t)flip_mode, present);
}

/* ===================================================================== */
/* Public API — workload tracking                                        */
/* ===================================================================== */

/*
 * Workload tracking — prospero backend.
 *
 * RE'd from libSceAgcDriver.sprx ordinals 87 (UM9b9NunSrE, BeginWorkload)
 * and 88 (i6bfTi13ApA, EndWorkload). Both functions:
 *   - Validate workload_id (error 0x8a6c0033 if invalid/zero)
 *   - Build a SET_WORKLOAD (0x1E) PM4 packet with a subcommand
 *   - Submit it to the GPU via the submit ioctl
 *
 * The 0xcc / 0xcd prefix bits in the SPRX correspond to the begin/end
 * subcommand selectors (AGC_PM4_SUB_WORKLOAD_BEGIN / _END).
 *
 * Packet layout (3 dwords):
 *   [0] header = agcPm4Header3Sub(SET_WORKLOAD, sub, 3)
 *   [1] workload_id
 *   [2] reserved (0)
 */
static int32_t agcProsperoSubmitWorkload(uint32_t workload_id, uint32_t sub)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (workload_id == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* Build a 3-dword SET_WORKLOAD packet in a small GPU-visible buffer.
     * Use a sub-region of SceGnmDdid to avoid exhausting flexible memory. */
    AgcProsperoRegion dcb_region = {0};
    int32_t ret = agcProsperoCarveSubRegion(
        &g_prospero.ddid, AGC_DDID_DCB_OFFSET, 16, &dcb_region);
    if (ret != AGC_OK)
        return ret;

    uint32_t *dcb = (uint32_t *)dcb_region.cpu_addr;
    dcb[0] = agcPm4Header3Sub(AGC_PM4_OP_SET_WORKLOAD, sub, 3);
    dcb[1] = workload_id;
    dcb[2] = 0;

    AgcCommandBufferSubmit submit = {0};
    submit.command_address = (uintptr_t)dcb_region.gpu_addr;
    submit.dword_count = 3;

    ret = sceAgcDriverSubmitDcb(&submit);
    /* dcb_region is a sub-region — don't munmap, just clear */
    memset(&dcb_region, 0, sizeof(dcb_region));
    return ret;
}

int32_t PS5_SYSV_ABI sceAgcDriverBeginWorkload(uint32_t workload_id)
{
    return agcProsperoSubmitWorkload(workload_id, AGC_PM4_SUB_WORKLOAD_BEGIN);
}

int32_t PS5_SYSV_ABI sceAgcDriverEndWorkload(uint32_t workload_id)
{
    return agcProsperoSubmitWorkload(workload_id, AGC_PM4_SUB_WORKLOAD_END);
}

/* ===================================================================== */
/* Public API — user special queue management                            */
/* ===================================================================== */

/*
 * Create a user special queue.
 *
 * RE'd from libSceAgcDriver.sprx at vaddr 0x8900 (FW 5.50). The SPRX
 * builds a 64-byte argument with hardcoded magic authentication tokens
 * and computes the ring buffer address from the SceGnmEopFifo base.
 *
 * Ring buffer address computation (from SPRX disassembly):
 *   ring_addr = eop_fifo_gpu_addr + 0x39000
 * (for the standard magic tokens 0xaf1e80b7, 0x8b4cdd90, 0x99f68d6c)
 *
 * The ioctl arg layout (64 bytes) — see analysis/sprx_agc_driver_internal_mem_disasm.md:
 *   0x00: magic1, magic2, magic3, token (4x uint32)
 *   0x10: pipe_id (uint64, value 0xc)
 *   0x18: caller_arg (uint64, from stack — 0 for our use)
 *   0x20: mmio_base (uint64, the mmap'd register base at 0xFE0200000)
 *   0x28: queue_id (uint32), flags (uint32, 0)
 *   0x30: ring_addr (uint64, eop_fifo_base + 0x39000)
 *   0x38: ring_size (uint64, 0x1000)
 *
 * Ioctl command: 0xc0408121 = IOC(RW, 0x81, 0x21, 64)
 */
int32_t PS5_SYSV_ABI _sceAgcDriverCreateUserSpecialQueue(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!g_prospero.mem_initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    int index = agcProsperoFindFreeQueue();
    if (index < 0)
        return AGC_ERROR_OUT_OF_MEMORY;

    /* Compute ring buffer address from EOP FIFO base + 0x39000.
     * The EOP FIFO region is 0x3C000 (240 KB), so offset 0x39000
     * (228 KB) is within bounds. */
    uint64_t ring_addr = g_prospero.eop_fifo.gpu_addr + 0x39000;

    /* Compute read_ptr and queue metadata addresses from ACQRB base.
     * SPRX: read_ptr = acqrb_base + 0x1C8000, metadata = acqrb_base + 0x1CC000
     * ACQRB is 0x1E0000 (1920 KB), so both offsets are within bounds. */
    uint64_t read_ptr_addr = g_prospero.acqrb.gpu_addr + 0x1C8000;
    uint64_t metadata_addr = g_prospero.acqrb.gpu_addr + 0x1CC000;

    AgcGcQueueCreateArg arg = {0};
    arg.magic1        = AGC_GC_QUEUE_MAGIC1;
    arg.magic2        = AGC_GC_QUEUE_MAGIC2;
    arg.magic3        = AGC_GC_QUEUE_MAGIC3;
    arg.token         = AGC_GC_QUEUE_TOKEN;
    arg.read_ptr_addr = read_ptr_addr;
    arg.caller_arg    = metadata_addr;
    arg.mmio_base     = (uint64_t)(uintptr_t)g_prospero.mmio_base;
    arg.pipe_id       = AGC_GC_QUEUE_PIPE_ID;
    arg.flags         = 0;
    arg.ring_addr     = ring_addr;
    arg.ring_size     = AGC_GC_QUEUE_RING_SIZE;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_QUEUE_CREATE, &arg);
    if (ret < 0)
        return AGC_ERROR_INTERNAL;

    g_prospero.queues[index].in_use      = true;
    g_prospero.queues[index].ring_base   = ring_addr;
    g_prospero.queues[index].read_ptr    = 0;  /* no separate read ptr allocation */
    memset(&g_prospero.queues[index].ring_region, 0, sizeof(AgcProsperoRegion));

    /* Return the queue index as the handle so callers can pass it to
     * sceAgcDriverSubmitAcb as the owner_handle. The generic backend
     * does the same. */
    return (int32_t)index;
}

/*
 * Destroy a user special queue.
 *
 * RE'd from libSceAgcDriver.sprx at vaddr 0x2e30. The SPRX sends a
 * 12-byte argument containing only the three magic authentication
 * tokens. The kernel identifies the queue to destroy from the
 * calling process's state. The ioctl is nr=0x0e (12-byte RW).
 *
 * Ioctl command: 0xc00c810e = IOC(RW, 0x81, 0x0e, 12)
 */
int32_t PS5_SYSV_ABI _sceAgcDriverDestroyUserSpecialQueue(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Find the first in-use queue and destroy it.
     * The SPRX takes no parameters — the kernel tracks the queue. */
    for (int i = 0; i < AGC_PROSPERO_MAX_QUEUES; i++) {
        if (g_prospero.queues[i].in_use) {
            AgcGcQueueDestroyArg arg = {
                .magic1 = AGC_GC_QUEUE_MAGIC1,
                .magic2 = AGC_GC_QUEUE_MAGIC2,
                .magic3 = AGC_GC_QUEUE_MAGIC3,
            };

            agcProsperoIoctl(AGC_GC_IOCTL_QUEUE_DESTROY, &arg);
            /* No ring_region to free — the ring buffer is carved from
             * the EOP FIFO allocation, not a separate region. */
            g_prospero.queues[i].in_use = false;
            return AGC_OK;
        }
    }

    return AGC_ERROR_NOT_FOUND;
}

/* ===================================================================== */
/* Public API — capture / debug                                          */
/* ===================================================================== */

int32_t PS5_SYSV_ABI sceAgcDriverRegisterCaptureInterface(void)
{
    /* TODO: Razor ACQ registration via WFDebug ioctls. */
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverDeregisterCaptureInterface(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverAcquireRazorACQ(void)
{
    /* TODO: WFDebug ioctl nr=0x15. */
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverReleaseRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitToRazorACQ(void)
{
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcDriverSubmitToHDRScopesACQ(void)
{
    return AGC_OK;
}

uint32_t PS5_SYSV_ABI sceAgcDriverGetPaDebugInterfaceVersion(void)
{
    /* Query PA debug interface version via ioctl nr=0x38 (PADEBUG_4, 4-byte R). */
    if (!g_prospero.initialized || g_prospero.gc_fd < 0)
        return 0;

    uint32_t version = 0;
    agcProsperoIoctl(AGC_GC_IOCTL_PADEBUG_4, &version);
    return version;
}

/* ===================================================================== */
/* Internal helpers (not public API)                                     */
/* ===================================================================== */

/*
 * Map CPU memory into GPU virtual address space.
 *
 * Uses the makesysmap_8 ioctl (nr=0x09, 8-byte RW):
 *   input  = CPU virtual address
 *   output = GPU virtual address assigned by kernel
 *
 * RE'd from libSceAgcDriver.sprx at vaddr 0x9510, kernel handler at 0x6ee6de.
 * Exposed as an internal helper for future use by the memory management layer.
 */
int agcProsperoMakeSysmap(void *cpu_addr, uint64_t *out_gpu_addr)
{
    if (!g_prospero.initialized || !cpu_addr || !out_gpu_addr)
        return -1;

    AgcGcMakesysmapArg8 arg;
    arg.addr = (uint64_t)(uintptr_t)cpu_addr;

    int ret = agcProsperoIoctl(AGC_GC_IOCTL_MAKESYSMAP_8, &arg);
    if (ret < 0)
        return ret;

    *out_gpu_addr = arg.addr;
    return 0;
}

#endif /* OPENAGC_PROSPERO */
