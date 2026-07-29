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
#include "game_compat_internal.h"
#include "driver_ops.h"
#include "driver_registry.h"
#include "agc_types.h"
#include "agc_error.h"
#include "agc_ioctl.h"
#include "agc_context.h"
#include "agc_pm4.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <machine/cpufunc.h>
#include <ps5/kernel.h>

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
extern int32_t sceKernelReleaseFlexibleMemory(void *addr, size_t len);
extern int32_t sceKernelSetVirtualRangeName(
    const void *addr, size_t len, const char *name);

/* Named internal memory region allocated by agcProsperoInitializeInternalMemory */
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
    bool             initialized;    /* agcProsperoInitialize succeeded */
    bool             mem_initialized;/* agcProsperoInitializeInternalMemory succeeded */
    bool             defaults_notified;/* agcProsperoNotifyDefaultStates succeeded */
    bool             async_setup_done;/* agcProsperoSetupAsyncGraphics succeeded */
    void            *mmio_base;      /* GPU register MMIO mapping (0xfe0200000) */
    uint32_t         ctx_capability; /* context query result from ioctl 0x2e */
    uint32_t         firmware_version; /* raw system-software version */
    AgcProsperoRuntimeProfile profile; /* firmware and hardware ABI profile */
    AgcProsperoDirectProfile direct_profile; /* exact per-operation /dev/gc ABI */
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
    AgcProsperoRegion   multi_trailer; /* payload-context deferred-IB trailer */
    AgcProsperoRegion   primary_defaults;
    AgcProsperoRegion   internal_defaults;
} AgcProsperoContext;

static AgcProsperoContext g_prospero = {
    .gc_fd = -1,
};

typedef int (PS5_SYSV_ABI *AgcKernelHasTrinityModeFn)(void);

extern int PS5_SYSV_ABI sceKernelDlsym(int handle, const char *name,
    void *symbol_out);
extern int PS5_SYSV_ABI sceKernelGetModuleList(int *modules,
    size_t module_capacity, size_t *module_count);

static int32_t agcProsperoQueryTrinityMode(bool *is_trinity)
{
    static const char *const symbols[] = {
        "sceKernelHasTrinityMode",
        "yu17wG8L5FI"
    };
    static const int handles[] = {0x2001, 1};
    AgcKernelHasTrinityModeFn query = NULL;
    void *symbol = NULL;
    int modules[128];
    uint32_t main_socid = 0;
    size_t main_socid_size = sizeof(main_socid);
    size_t module_count = 0;
    size_t symbol_index;
    size_t handle_index;

    if (!is_trinity)
        return AGC_ERROR_INVALID_ARGUMENT;
    for (symbol_index = 0;
         symbol_index < sizeof(symbols) / sizeof(symbols[0]);
         ++symbol_index) {
        for (handle_index = 0;
             handle_index < sizeof(handles) / sizeof(handles[0]);
             ++handle_index) {
            symbol = NULL;
            if (sceKernelDlsym(handles[handle_index], symbols[symbol_index],
                    &symbol) == 0 && symbol)
                break;
        }
        if (symbol)
            break;
    }
    if (!symbol && sceKernelGetModuleList(modules,
            sizeof(modules) / sizeof(modules[0]), &module_count) == 0) {
        if (module_count > sizeof(modules) / sizeof(modules[0]))
            module_count = sizeof(modules) / sizeof(modules[0]);
        for (handle_index = 0; handle_index < module_count; ++handle_index) {
            for (symbol_index = 0;
                 symbol_index < sizeof(symbols) / sizeof(symbols[0]);
                 ++symbol_index) {
                symbol = NULL;
                if (sceKernelDlsym(modules[handle_index],
                        symbols[symbol_index], &symbol) == 0 && symbol)
                    break;
            }
            if (symbol)
                break;
        }
    }
    if (symbol) {
        if (sizeof(query) != sizeof(symbol))
            return AGC_ERROR_NOT_SUPPORTED;
        memcpy(&query, &symbol, sizeof(query));
        *is_trinity = query() != 0;
        return AGC_OK;
    }

    /* FW 11.60 libkernel implements sceKernelHasTrinityMode by reading this
     * four-byte sysctl and comparing the SoC family with low stepping bits
     * masked off.  The protected export is not visible through sceKernelDlsym
     * in websrv payloads, so reproduce that exact predicate directly. */
    if (sysctlbyname("hw.sce_main_socid", &main_socid, &main_socid_size,
            NULL, 0) != 0 || main_socid_size != sizeof(main_socid))
        return AGC_ERROR_NOT_SUPPORTED;
    *is_trinity = (main_socid & ~0x1fu) == 0x00840fc0u;
    return AGC_OK;
}

int32_t agcProsperoConfigureRuntimeProfile(uint32_t raw_version)
{
    bool is_trinity = false;

    if (agcProsperoFirmwareUsesTrinityPredicate(raw_version)) {
        int32_t result = agcProsperoQueryTrinityMode(&is_trinity);

        if (result != AGC_OK)
            return result;
    }
    if (!agcProsperoBuildDirectProfile(raw_version, is_trinity,
            &g_prospero.direct_profile))
        return AGC_ERROR_NOT_SUPPORTED;
    g_prospero.profile = g_prospero.direct_profile.runtime;
    g_prospero.firmware_version = raw_version;
    printf("[openagc] profile fw=0x%08X family=%s model=%s "
           "submit=0x%08X queue_auth=%u tf_ring=%u eop=0x%X "
           "gpu_info=0x%X cwsr_work=0x%X cwsr_size=0x%X\n",
           raw_version, agcProsperoAbiFamilyName(g_prospero.profile.family),
           g_prospero.profile.is_trinity ? "trinity" : "standard-ps5",
           AGC_GC_IOCTL_SUBMIT_16,
           g_prospero.profile.authenticated_special_queue ? 1u : 0u,
           g_prospero.profile.supports_tf_ring ? 1u : 0u,
           g_prospero.profile.eop_ring_offset,
           g_prospero.profile.gpu_info_span,
           g_prospero.profile.cwsr_work_offset,
           g_prospero.profile.cwsr_size);
    return AGC_OK;
}

#define AGC_GPU_AUTHID_REQUIRED 0x4801000000000000ull
#define AGC_GPU_AUTHID_MASK 0xff0f000000000000ull
#define AGC_FW550_PROC_THREADS_OFFSET 0x10u
#define AGC_FW550_PROC_UCRED_OFFSET 0x40u
#define AGC_FW550_THREAD_NEXT_OFFSET 0x10u
#define AGC_FW550_THREAD_UCRED_OFFSET 0x140u
#define AGC_FW550_UCRED_AUTHID_OFFSET 0x58u

/* Prepare the payload process for /dev/gc before any file descriptor is
 * opened. The SDK helper updates p_ucred. FW 5.50's ioctl path reads
 * curthread->td_ucred, so also repair a detached thread credential using the
 * offsets proven by the hardware qualification samples. Keeping this inside
 * the Prospero backend lets ordinary OpenAGC and Vulkan clients stay unaware
 * of kernel credential layout. */
static int32_t agcProsperoPrepareGpuCredentials(void)
{
    pid_t pid = getpid();
    uint64_t authid;

    if (kernel_set_ucred_authid(pid, AGC_GPU_AUTHID_REQUIRED) != 0)
        return AGC_ERROR_NOT_INITIALIZED;
    authid = kernel_get_ucred_authid(pid);
    if ((authid & AGC_GPU_AUTHID_MASK) != AGC_GPU_AUTHID_REQUIRED)
        return AGC_ERROR_NOT_INITIALIZED;

    if ((g_prospero.firmware_version & 0xffff0000u) == 0x05500000u) {
        intptr_t proc = kernel_get_proc(pid);
        uint64_t process_ucred = 0u;
        uint64_t thread;
        uint32_t thread_count = 0u;

        if (proc == 0 || kernel_copyout(
                proc + AGC_FW550_PROC_UCRED_OFFSET, &process_ucred,
                sizeof(process_ucred)) != 0 || process_ucred == 0u)
            return AGC_ERROR_NOT_INITIALIZED;
        thread = kernel_getlong(proc + AGC_FW550_PROC_THREADS_OFFSET);
        while (thread != 0u && thread_count < 64u) {
            uint64_t thread_ucred = 0u;

            if (kernel_copyout(
                    (intptr_t)thread + AGC_FW550_THREAD_UCRED_OFFSET,
                    &thread_ucred, sizeof(thread_ucred)) != 0 ||
                thread_ucred == 0u)
                return AGC_ERROR_NOT_INITIALIZED;
            if (thread_ucred != process_ucred) {
                if (kernel_copyin(&authid,
                        (intptr_t)thread_ucred +
                            AGC_FW550_UCRED_AUTHID_OFFSET,
                        sizeof(authid)) != 0)
                    return AGC_ERROR_NOT_INITIALIZED;
            }
            thread = kernel_getlong(
                (intptr_t)thread + AGC_FW550_THREAD_NEXT_OFFSET);
            ++thread_count;
        }
        if (thread_count == 0u || thread != 0u)
            return AGC_ERROR_NOT_INITIALIZED;
    }

    printf("[openagc] GPU process authorization prepared\n");
    return AGC_OK;
}

int32_t agcProsperoGetRuntimeProfile(AgcProsperoRuntimeProfile *profile_out)
{
    if (!profile_out)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (g_prospero.profile.family == AGC_PROSPERO_ABI_UNSUPPORTED)
        return AGC_ERROR_NOT_INITIALIZED;
    *profile_out = g_prospero.profile;
    return AGC_OK;
}

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
 * RE'd from libSceAgcDriver.sprx at vaddr 0x1077 (FW 5.50).
 *
 * The 16-byte CB descriptor IS an IT_INDIRECT_BUFFER PM4 packet:
 *   Word 0: PM4 type-3 header (0xC0023F00 for DCB, 0xC0023300 for ACB)
 *   Word 1: ib_base_lo (lower 32 bits of GPU VA)
 *   Word 2: ib_base_hi (upper bits of GPU VA, only [15:0] valid)
 *   Word 3: control — ib_size in [19:0], other control bits in [31:20]
 *
 * Stored as two little-endian qwords:
 *   qword 0 = (ib_base_lo << 32) | pm4_header
 *   qword 1 = ((ib_size & 0xFFFFF) << 32) | (ib_base_hi & 0xFFFF)
 *
 * The kernel inserts VMID into bits [63:52] of ib_base after copyin.
 * Mask 0x000FFFFF0000FFFF zeroes bits [31:16] of ib_base_hi and
 * limits ib_size to 20 bits (max 1M dwords = 4MB IB).
 */
static void agcProsperoBuildCbDescriptor(AgcGcCommandBuffer *cb,
                                       uint64_t gpu_addr,
                                       uint32_t size_dwords,
                                       bool is_const)
{
    uint32_t header = is_const ? AGC_GC_CB_HEADER_IB_CNST : AGC_GC_CB_HEADER_IB;
    uint32_t addr_lo = (uint32_t)gpu_addr;
    uint32_t addr_hi = (uint32_t)(gpu_addr >> 32);

    cb->header = ((uint64_t)addr_lo << 32) | header;
    cb->ib_base = ((uint64_t)(size_dwords & 0xFFFFF) << 32) | (addr_hi & 0xFFFF);
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
                                      uintptr_t address_hint,
                                      const char *name)
{
    if (!region || size == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    memset(region, 0, sizeof(*region));

    /* Use sceKernelMapNamedSystemFlexibleMemory (matches SPRX behavior).
     * The SPRX passes type=0x33 for GPU regions, type=3 for SceGnmGpuInfo. */
    void *addr = (void *)address_hint;
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

    if (region->cpu_addr &&
        sceKernelReleaseFlexibleMemory(region->cpu_addr, region->size) != 0)
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
 * SceGnmDdid is 0xFC000 (1008 KB). The primary defaults blob needs
 * ~262 KB (127 groups), the internal defaults blob needs ~46 KB (22 groups).
 * We carve from the start of DDID to fit both, plus DCB scratch at the end. */
#define AGC_DDID_PRIMARY_SIZE     0x41000   /* 260 KB for primary defaults */
#define AGC_DDID_INTERNAL_V8_SIZE 0xC000    /* 48 KB for version 8 */
#define AGC_DDID_INTERNAL_V10_SIZE 0xF000   /* 60 KB for version 10 */
#define AGC_DDID_PRIMARY_OFFSET   0x00000   /* start of DDID */
#define AGC_DDID_INTERNAL_OFFSET  AGC_DDID_PRIMARY_OFFSET + AGC_DDID_PRIMARY_SIZE
#define AGC_DDID_MULTI_TRAILER_SIZE 64
#define AGC_DDID_DCB_OFFSET       0xFC000 - 16  /* last 16 bytes for DCB scratch */

typedef struct AgcProsperoDefaultsLayout {
    uint32_t primary_cx_length;
    uint32_t primary_sh_length;
    uint32_t primary_uc_length;
    uint32_t internal_cx_length;
    uint32_t internal_sh_length;
    uint32_t internal_uc_length;
    size_t internal_blob_size;
} AgcProsperoDefaultsLayout;

static AgcProsperoDefaultsLayout agcProsperoGetDefaultsLayout(void)
{
    if (g_prospero.direct_profile.defaults_version ==
        AGC_REGISTER_DEFAULTS_VERSION_12) {
        const AgcProsperoDefaultsLayout layout = {
            AGC_REGISTER_DEFAULTS_V10_PRIMARY_CX_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_PRIMARY_SH_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_PRIMARY_UC_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_INTERNAL_CX_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_INTERNAL_SH_LENGTH,
            AGC_REGISTER_DEFAULTS_V10_INTERNAL_UC_LENGTH,
            AGC_DDID_INTERNAL_V10_SIZE,
        };
        return layout;
    }

    {
        const AgcProsperoDefaultsLayout layout = {
            AGC_REGISTER_DEFAULTS_V8_PRIMARY_CX_LENGTH,
            AGC_REGISTER_DEFAULTS_V8_PRIMARY_SH_LENGTH,
            AGC_REGISTER_DEFAULTS_V8_PRIMARY_UC_LENGTH,
            AGC_REGISTER_DEFAULTS_V8_INTERNAL_CX_LENGTH,
            AGC_REGISTER_DEFAULTS_V8_INTERNAL_SH_LENGTH,
            AGC_REGISTER_DEFAULTS_V8_INTERNAL_UC_LENGTH,
            AGC_DDID_INTERNAL_V8_SIZE,
        };
        return layout;
    }
}

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
int32_t PS5_SYSV_ABI agcProsperoInitialize(void)
{
    if (g_prospero.initialized)
        return AGC_OK;

    if (agcProsperoPrepareGpuCredentials() != AGC_OK)
        return AGC_ERROR_NOT_INITIALIZED;

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
                          AGC_GC_MMIO_PROT, MAP_SHARED,
                          g_prospero.gc_fd, 0);
        if (mmio == MAP_FAILED || mmio == NULL) {
            close(g_prospero.gc_fd);
            g_prospero.gc_fd = -1;
            return AGC_ERROR_NOT_INITIALIZED;
        }
        g_prospero.mmio_base = mmio;

        /* Exact SPRX behavior: name the mapping; do not POSIX-mlock it. */
        (void)sceKernelSetVirtualRangeName(
            mmio, AGC_GC_MMIO_SIZE, "SceGnmDingDong");
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
 *   SceGnmCwsr:      0x1000000 standard / 0x1600000 Trinity, type=0x33
 *   SceGnmMisc:      0x4000    (16 KB)   type=0x33
 *   SceGnmACQRB:     0x1E0000  (1920 KB) type=0x33
 *
 * The EOP FIFO region (SceGnmEopFifo) is used as the ring buffer base
 * for queue creation — the ring buffer is carved at offset 0x39000 from
 * the EOP FIFO base.
 */
int32_t PS5_SYSV_ABI agcProsperoInitializeInternalMemory(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (g_prospero.mem_initialized)
        return AGC_OK;
    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_MEMORY) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

    /* Memory type for flexible memory (SPRX uses 0x33 for GPU regions).
     * This is a flexible-memory-specific type flag, not the same as
     * sceKernelAllocateDirectMemory types (1/2/3). */
    #define AGC_FLEX_TYPE_GPU   0x33
    #define AGC_FLEX_TYPE_INFO  3

    struct {
        AgcProsperoRegion *region;
        size_t          size;
        int             mem_type;
        uintptr_t       address_hint;
        const char     *name;
    } regions[] = {
        { &g_prospero.gpu_info,   g_prospero.profile.gpu_info_span,
          AGC_FLEX_TYPE_INFO, AGC_GC_GPU_INFO_ADDRESS_HINT, "SceGnmGpuInfo" },
        { &g_prospero.trap_code, 0x4000, AGC_FLEX_TYPE_GPU,
          AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmTrapCode" },
        { &g_prospero.trap_data, 0x4000, AGC_FLEX_TYPE_GPU,
          AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmTrapData" },
        { &g_prospero.ddid, 0xFC000, AGC_FLEX_TYPE_GPU,
          AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmDdid" },
        { &g_prospero.eop_fifo, 0x3C000, AGC_FLEX_TYPE_GPU,
          AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmEopFifo" },
        { &g_prospero.shadow_reg, 0x4000, AGC_FLEX_TYPE_GPU,
          AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmShadowReg" },
        { &g_prospero.cwsr,       g_prospero.profile.cwsr_size,
          AGC_FLEX_TYPE_GPU, AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmCwsr" },
        { &g_prospero.misc, 0x4000, AGC_FLEX_TYPE_GPU,
          AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmMisc" },
        { &g_prospero.acqrb, 0x1E0000, AGC_FLEX_TYPE_GPU,
          AGC_GC_INTERNAL_ADDRESS_HINT, "SceGnmACQRB" },
    };

    for (int i = 0; i < (int)(sizeof(regions) / sizeof(regions[0])); i++) {
        printf("    [mem] %s: size=0x%zx type=0x%x... ", regions[i].name,
               regions[i].size, regions[i].mem_type);
        int32_t ret = agcProsperoAllocRegion(regions[i].region,
                                          regions[i].size,
                                          regions[i].mem_type,
                                          regions[i].address_hint,
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

    memset(g_prospero.ddid.cpu_addr, 0, g_prospero.ddid.size);

    const AgcProsperoDefaultsLayout defaults_layout =
        agcProsperoGetDefaultsLayout();
    const size_t multi_trailer_offset =
        AGC_DDID_INTERNAL_OFFSET + defaults_layout.internal_blob_size;
    int32_t trailer_ret = agcProsperoCarveSubRegion(
        &g_prospero.ddid, multi_trailer_offset,
        AGC_DDID_MULTI_TRAILER_SIZE, &g_prospero.multi_trailer);
    if (trailer_ret != AGC_OK) {
        for (int i = 0; i < (int)(sizeof(regions) / sizeof(regions[0])); i++)
            agcProsperoFreeRegion(regions[i].region);
        return trailer_ret;
    }
    printf("    [mem] OpenAgcMultiTrailer: size=0x%x subregion=%p\n",
           AGC_DDID_MULTI_TRAILER_SIZE,
           g_prospero.multi_trailer.cpu_addr);

    uint32_t *trailer = (uint32_t *)g_prospero.multi_trailer.cpu_addr;
    trailer[0] = agcPm4Header3(AGC_PM4_OP_NOP, 16);
    memset(&trailer[1], 0, 15 * sizeof(*trailer));
    clflush((u_long)(uintptr_t)trailer);
    mfence();

    g_prospero.mem_initialized = true;
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — submission                                               */
/* ===================================================================== */

/*
 * Submit multiple command buffers (DCB + ACB) to the GPU.
 *
 * Builds CB descriptors for each DCB and ACB, then submits the complete array
 * with ioctl nr=0x02. The standard compatibility group additionally uses its
 * FW 5.50-hardware-proven payload completion sequence: frame-state ioctl
 * nr=0x01 plus a trailing NOP IB.
 *
 * The kernel path:
 *   gc_submit_with_pid → common graphics-ring submit
 *     - validates num_cbs in [1, 0xFFF]
 *     - copyin CB array into ring buffer
 *     - per CB: checks header opcode, masks ib_base, ORs in VMID<<52
 *     - calls gc_insert_indirect_buffer per CB
 */
int32_t PS5_SYSV_ABI agcProsperoSubmitMultiCommandBuffersDirect(
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
    if (total_cbs == 0 || total_cbs >= AGC_GC_NUM_CBS_MAX)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (g_prospero.direct_profile.submit_uses_frame_close_trailer &&
        (!g_prospero.mem_initialized ||
         g_prospero.multi_trailer.gpu_addr == 0))
        return AGC_ERROR_NOT_INITIALIZED;

    /* Build CB descriptor array on stack (max 0xFFF = 4095 CBs, but
     * practically we use a small stack array) */
    AgcGcCommandBuffer cb_desc_storage[65];
    AgcGcCommandBuffer *cb_descs = (AgcGcCommandBuffer *)
        (((uintptr_t)cb_desc_storage + 15u) & ~(uintptr_t)15u);
    if (total_cbs > 63)
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

    if (g_prospero.direct_profile.submit_uses_frame_close_trailer) {
        /* In the standard exploited-payload context, the graphics ring can
         * defer the final descriptor until the next submit. Append a harmless
         * GPU-visible NOP IB so every caller CB executes in this frame. */
        agcProsperoBuildCbDescriptor(&cb_descs[cb_idx],
            g_prospero.multi_trailer.gpu_addr, 16, false);
        cb_idx++;
    }

    /* Build submit ioctl arg */
    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.queue_type = 3;  /* 3 = graphics queue (SPRX-confirmed) */
    submit_arg.num_cbs = cb_idx;
    submit_arg.cb_array = (uint64_t)(uintptr_t)cb_descs;

    if (g_prospero.direct_profile.submit_uses_frame_close_trailer) {
        uint32_t frame_arg[2] = {3u, 0u};
        int frame_ret = agcProsperoIoctl(AGC_GC_IOCTL_CLOSE, frame_arg);
        if (frame_ret < 0)
            return AGC_ERROR_SUBMIT_FAILED;
    }

    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_SUBMIT) == 0)
        return AGC_ERROR_NOT_SUPPORTED;
    int ret = agcProsperoIoctl(g_prospero.direct_profile.submit_ioctl,
        &submit_arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

/*
 * Submit a single DCB (draw command buffer) to the GPU.
 *
 * Standard-firmware profiles wrap the caller CB and completion trailer in one
 * frame. This preserves the shared Sony carrier ABI while applying the
 * payload-context completion behavior hardware-proven on FW 5.50.
 */
int32_t PS5_SYSV_ABI agcProsperoSubmitDcb(const AgcCommandBufferSubmit *packet)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    if (g_prospero.direct_profile.submit_uses_frame_close_trailer &&
        (!g_prospero.mem_initialized ||
         g_prospero.multi_trailer.gpu_addr == 0))
        return AGC_ERROR_NOT_INITIALIZED;

    AgcGcCommandBuffer cb_desc_storage[3];
    AgcGcCommandBuffer *cb_descs = (AgcGcCommandBuffer *)
        (((uintptr_t)cb_desc_storage + 15u) & ~(uintptr_t)15u);
    agcProsperoBuildCbDescriptor(&cb_descs[0],
                               (uint64_t)packet->command_address,
                               packet->dword_count, false);
    uint32_t descriptor_count = 1u;
    if (g_prospero.direct_profile.submit_uses_frame_close_trailer) {
        agcProsperoBuildCbDescriptor(&cb_descs[1],
                                   g_prospero.multi_trailer.gpu_addr, 16, false);
        descriptor_count++;
    }

    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.queue_type = 3;  /* 3 = graphics queue (SPRX-confirmed) */
    submit_arg.num_cbs = descriptor_count;
    submit_arg.cb_array = (uint64_t)(uintptr_t)cb_descs;

    if (g_prospero.direct_profile.submit_uses_frame_close_trailer) {
        uint32_t frame_arg[2] = {3u, 0u};
        int frame_ret = agcProsperoIoctl(AGC_GC_IOCTL_CLOSE, frame_arg);
        if (frame_ret < 0)
            return AGC_ERROR_SUBMIT_FAILED;
    }

    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_SUBMIT) == 0)
        return AGC_ERROR_NOT_SUPPORTED;
    int ret = agcProsperoIoctl(g_prospero.direct_profile.submit_ioctl,
        &submit_arg);
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
int32_t PS5_SYSV_ABI agcProsperoSubmitAcb(
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
    submit_arg.queue_type = 3;  /* 3 = graphics queue (SPRX-confirmed) */
    submit_arg.num_cbs = 1;
    submit_arg.cb_array = (uint64_t)(uintptr_t)&cb_desc;

    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_SUBMIT) == 0)
        return AGC_ERROR_NOT_SUPPORTED;
    int ret = agcProsperoIoctl(g_prospero.direct_profile.submit_ioctl,
        &submit_arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

/* ===================================================================== */
/* Public API — suspend points                                           */
/* ===================================================================== */

int32_t PS5_SYSV_ABI agcProsperoSuspendPointSubmitDirect(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities &
            AGC_DIRECT_CAP_SUSPEND_PRIMARY) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

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

    int ret = agcProsperoIoctl(
        g_prospero.direct_profile.suspend_primary_ioctl, &arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

bool PS5_SYSV_ABI agcProsperoIsSuspendPointInFlightDirect(uint32_t value)
{
    (void)value;
    if (!g_prospero.initialized)
        return false;
    if ((g_prospero.direct_profile.capabilities &
            AGC_DIRECT_CAP_SUSPEND_QUERY) == 0)
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

int32_t PS5_SYSV_ABI agcProsperoInternalSuspendPointSubmitFinal(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities &
            AGC_DIRECT_CAP_SUSPEND_FINAL) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

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

    int ret = agcProsperoIoctl(
        g_prospero.direct_profile.suspend_final_ioctl, &arg);
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
int32_t PS5_SYSV_ABI agcProsperoSetupAsyncGraphics(uint32_t pipe_id)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities &
            AGC_DIRECT_CAP_ASYNC_GRAPHICS) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

    if (g_prospero.async_setup_done) {
        /* SPRX only calls the ioctl once, then sets a flag. */
        return AGC_OK;
    }

    uint32_t arg = 1;
    int ret = agcProsperoIoctl(
        g_prospero.direct_profile.async_graphics_ioctl, &arg);
    if (ret < 0)
        return AGC_ERROR_INTERNAL;

    g_prospero.async_setup_done = true;
    /* Store pipe_id != 0 flag (used by SPRX for async compute routing). */
    (void)pipe_id;

    return AGC_OK;
}

/* Public tessellation factor-ring setup recovered from FW 5.50
 * libSceAgcDriver.sprx vaddrs 0x67e0 and 0x9180. */
int32_t PS5_SYSV_ABI agcProsperoSetTFRing(
    uintptr_t ring_addr, uint32_t size)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_TF_RING) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

    AgcGcSetTFRingArg arg = {0};
    arg.ring_addr = (uint64_t)ring_addr;
    arg.size = size;
    int ret = agcProsperoIoctl(g_prospero.direct_profile.tf_ring_ioctl, &arg);
    return (ret < 0) ? AGC_ERROR_INTERNAL : AGC_OK;
}

/* FW 5.50 exports sceAgcDriverSetTFRingDirect as a permission stub. */
int32_t PS5_SYSV_ABI agcProsperoSetTFRingDirect(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_TF_RING) == 0)
        return AGC_ERROR_NOT_SUPPORTED;
    return AGC_ERROR_NOT_SUPPORTED;
}

/*
 * Set hull shader offchip parameters.
 * Uses ioctl nr=0x2c (SET_HS_OFFCHIP, 16-byte RW).
 *
 * The FW 5.50 kernel handler at 0x6ee6d2 passes the argument directly to
 * gc_pm4_clearstate_patch (0xb7dd20) as a patch list pointer and count.
 */
int32_t PS5_SYSV_ABI agcProsperoSetHsOffchipParamDirect(
    uint64_t list_addr, uint32_t num_entries)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities &
            AGC_DIRECT_CAP_HS_OFFCHIP) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

    AgcGcSetHsOffchipArg arg = {0};
    arg.list_addr = list_addr;
    arg.num_entries = num_entries;

    int ret = agcProsperoIoctl(
        g_prospero.direct_profile.hs_offchip_ioctl, &arg);
    return (ret < 0) ? AGC_ERROR_INTERNAL : AGC_OK;
}

int32_t PS5_SYSV_ABI agcProsperoSetTargetRingForDiag(void)
{
    /* No direct ioctl mapping — diagnostic target ring setup.
     * The firmware uses a large-arg ioctl or direct register write. */
    return AGC_ERROR_NOT_SUPPORTED;
}

/* ===================================================================== */
/* Public API — default states                                           */
/* ===================================================================== */

int32_t PS5_SYSV_ABI agcProsperoNotifyDefaultStates(uint32_t flags)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (g_prospero.defaults_notified)
        return AGC_OK;
    if (!g_prospero.mem_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities &
            AGC_DIRECT_CAP_DEFAULT_STATES) == 0 ||
        g_prospero.direct_profile.defaults_version ==
            AGC_DIRECT_DEFAULTS_VERSION_UNKNOWN)
        return AGC_ERROR_NOT_SUPPORTED;

    (void)flags; /* flags meaning is still pending RE */

    /*
     * Build the primary and internal register-defaults blobs in GPU-visible
     * memory. The blobs are then available to the driver for context-state
     * loading. The actual submission path that consumes these blobs is still
     * pending hardware validation.
     *
     * Version selection is exact-profile data. FW 5.50 uses version 8;
     * firmware without a recovered mapping returns NOT_SUPPORTED above.
     */
    uint32_t primary_count = 0;
    uint32_t internal_count = 0;
    const AgcRegisterDefaultsGroup *primary_groups =
        agcRegisterDefaultsGetPrimaryGroupsForVersion(
            g_prospero.direct_profile.defaults_version, &primary_count);
    const AgcRegisterDefaultsGroup *internal_groups =
        agcRegisterDefaultsGetInternalGroupsForVersion(
            g_prospero.direct_profile.defaults_version, &internal_count);

    const AgcProsperoDefaultsLayout layout = agcProsperoGetDefaultsLayout();

    size_t primary_size = agcRegisterDefaultsComputeSize(
        primary_count, layout.primary_cx_length, layout.primary_sh_length,
        layout.primary_uc_length);
    size_t internal_size = agcRegisterDefaultsComputeSize(
        internal_count, layout.internal_cx_length, layout.internal_sh_length,
        layout.internal_uc_length);
    printf("    [defaults] primary: %u groups, size=0x%zx (blob=0x%x)\n",
           primary_count, primary_size, AGC_DDID_PRIMARY_SIZE);
    printf("    [defaults] internal: %u groups, size=0x%zx (blob=0x%x)\n",
           internal_count, internal_size, (unsigned)layout.internal_blob_size);

    int32_t ret = agcProsperoCarveSubRegion(
        &g_prospero.ddid, AGC_DDID_PRIMARY_OFFSET, AGC_DDID_PRIMARY_SIZE,
        &g_prospero.primary_defaults);
    if (ret != AGC_OK) {
        printf("    [defaults] ERROR: carve primary failed: 0x%x (ddid size=0x%zx)\n",
               ret, g_prospero.ddid.size);
        return ret;
    }

    ret = agcProsperoCarveSubRegion(
        &g_prospero.ddid, AGC_DDID_INTERNAL_OFFSET, layout.internal_blob_size,
        &g_prospero.internal_defaults);
    if (ret != AGC_OK) {
        printf("    [defaults] ERROR: carve internal failed: 0x%x\n", ret);
        return ret;
    }

    ret = agcRegisterDefaultsBuild(
        g_prospero.primary_defaults.cpu_addr,
        g_prospero.primary_defaults.size,
        g_prospero.primary_defaults.gpu_addr,
        primary_groups,
        primary_count,
        layout.primary_cx_length,
        layout.primary_sh_length,
        layout.primary_uc_length);
    if (ret != AGC_OK) {
        printf("    [defaults] ERROR: build primary failed: 0x%x (required=0x%zx, blob=0x%zx)\n",
               ret, primary_size, g_prospero.primary_defaults.size);
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
        layout.internal_cx_length,
        layout.internal_sh_length,
        layout.internal_uc_length);
    if (ret != AGC_OK) {
        printf("    [defaults] ERROR: build internal failed: 0x%x (required=0x%zx, blob=0x%zx)\n",
               ret, internal_size, g_prospero.internal_defaults.size);
        agcProsperoFreeRegion(&g_prospero.internal_defaults);
        agcProsperoFreeRegion(&g_prospero.primary_defaults);
        return ret;
    }

    /* The compatibility context-state ABI uses an 8-byte GPU-visible label
     * followed by a flat CX register restore list. Firmware allocates the
     * label as {0, 1}; SceGnmMisc is otherwise unused by OpenAGC and has
     * enough room for all FW 5.50 v8 CX pairs. */
    {
        uint32_t *storage = (uint32_t *)g_prospero.misc.cpu_addr;
        size_t pair_capacity = (g_prospero.misc.size - 8u) /
            (2u * sizeof(uint32_t));
        uint32_t pair_count = 0u;

        storage[0] = 0u;
        storage[1] = 1u;
        for (uint32_t i = 0u; i < primary_count; ++i) {
            const AgcRegisterDefaultsGroup *group = &primary_groups[i];
            if (group->space != kAgcRegisterDefaultSpaceCx)
                continue;
            for (uint32_t j = 0u; j < group->register_count; ++j) {
                if (pair_count >= pair_capacity)
                    return AGC_ERROR_OUT_OF_MEMORY;
                storage[2u + pair_count * 2u] = group->registers[j].offset;
                storage[3u + pair_count * 2u] = group->registers[j].value;
                ++pair_count;
            }
        }
        agcGameCompatConfigureContextState(
            g_prospero.misc.gpu_addr,
            g_prospero.misc.gpu_addr + 8u,
            pair_count,
            1u);
    }

    /*
     * Submit a CLEAR_STATE packet to initialize the GPU context to the
     * primary defaults. The kernel patches CLEAR_STATE (opcode 0x14) via
     * gc_pm4_clearstate_patch; the primary/internal blobs we just built are
     * GPU-visible and consumed during context reset.
     *
     * NOTE: Temporarily disabled — the CLEAR_STATE submission appears to
     * cause a GPU hang that prevents subsequent command buffer submissions
     * from executing. The default state blobs are still built in GPU-visible
     * memory and can be consumed by the kernel during context reset without
     * an explicit CLEAR_STATE packet.
     */
#if 0  /* Temporarily disabled for debugging */
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

    ret = agcProsperoSubmitDcb(&submit);
    /* dcb_region is a sub-region of ddid — don't munmap it, just clear */
    memset(&dcb_region, 0, sizeof(dcb_region));
    if (ret != AGC_OK)
        return ret;
#endif

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
int32_t PS5_SYSV_ABI agcProsperoSdmaCopyLinearBlocking(
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

int32_t PS5_SYSV_ABI agcProsperoSubmitEopFlip(
    void *video_out_handle, uint32_t display_buf_index,
    uint32_t flip_mode, void *present_ptr)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities &
            AGC_DIRECT_CAP_EOP_FLIP) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

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
 * This is OpenAGC's historical one-ID convenience ABI. The FW 5.50 and
 * FW 11.60 Sony exports with similar names are multi-argument nine-dword
 * packet builders and are not ABI-compatible adapters for this helper.
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
    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_WORKLOAD) == 0)
        return AGC_ERROR_NOT_SUPPORTED;
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

    ret = agcProsperoSubmitDcb(&submit);
    /* dcb_region is a sub-region — don't munmap, just clear */
    memset(&dcb_region, 0, sizeof(dcb_region));
    return ret;
}

int32_t PS5_SYSV_ABI agcProsperoSetWorkloadsActive(uint32_t workload_id)
{
    return agcProsperoSubmitWorkload(workload_id, AGC_PM4_SUB_WORKLOAD_BEGIN);
}

int32_t PS5_SYSV_ABI agcProsperoSetWorkloadComplete(uint32_t workload_id)
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
int32_t PS5_SYSV_ABI agcProsperoCreateUserSpecialQueue(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!g_prospero.mem_initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_QUEUE) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

    int index = agcProsperoFindFreeQueue();
    if (index < 0)
        return AGC_ERROR_OUT_OF_MEMORY;

    /* Compute ring buffer address from EOP FIFO base + 0x39000.
     * The EOP FIFO region is 0x3C000 (240 KB), so offset 0x39000
     * (228 KB) is within bounds. */
    uint64_t ring_addr = g_prospero.eop_fifo.gpu_addr +
        g_prospero.profile.eop_ring_offset;

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

    int ret = agcProsperoIoctl(
        g_prospero.direct_profile.queue_create_ioctl, &arg);
    if (ret < 0)
        return AGC_ERROR_INTERNAL;

    g_prospero.queues[index].in_use      = true;
    g_prospero.queues[index].ring_base   = ring_addr;
    g_prospero.queues[index].read_ptr    = 0;  /* no separate read ptr allocation */
    memset(&g_prospero.queues[index].ring_region, 0, sizeof(AgcProsperoRegion));

    /* Return the queue index as the handle so callers can pass it to
     * agcProsperoSubmitAcb as the owner_handle. The generic backend
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
int32_t PS5_SYSV_ABI agcProsperoDestroyUserSpecialQueue(void)
{
    if (!g_prospero.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if ((g_prospero.direct_profile.capabilities & AGC_DIRECT_CAP_QUEUE) == 0)
        return AGC_ERROR_NOT_SUPPORTED;

    /* Find the first in-use queue and destroy it.
     * The SPRX takes no parameters — the kernel tracks the queue. */
    for (int i = 0; i < AGC_PROSPERO_MAX_QUEUES; i++) {
        if (g_prospero.queues[i].in_use) {
            AgcGcQueueDestroyArg arg = {
                .magic1 = AGC_GC_QUEUE_MAGIC1,
                .magic2 = AGC_GC_QUEUE_MAGIC2,
                .magic3 = AGC_GC_QUEUE_MAGIC3,
            };

            int ret = agcProsperoIoctl(
                g_prospero.direct_profile.queue_destroy_ioctl, &arg);
            if (ret < 0)
                return AGC_ERROR_INTERNAL;
            /* No ring_region to free — the ring buffer is carved from
             * the EOP FIFO allocation, not a separate region. */
            g_prospero.queues[i].in_use = false;
            return AGC_OK;
        }
    }

    return AGC_ERROR_NOT_FOUND;
}

int32_t PS5_SYSV_ABI agcProsperoShutdown(void)
{
    int i;

    if (!g_prospero.initialized) {
        return AGC_OK;
    }
    for (i = 0; i < AGC_PROSPERO_MAX_QUEUES; ++i) {
        if (g_prospero.queues[i].in_use) {
            (void)agcProsperoDestroyUserSpecialQueue();
        }
    }

    memset(&g_prospero.multi_trailer, 0, sizeof(g_prospero.multi_trailer));
    memset(&g_prospero.primary_defaults, 0, sizeof(g_prospero.primary_defaults));
    memset(&g_prospero.internal_defaults, 0, sizeof(g_prospero.internal_defaults));
    agcProsperoFreeRegion(&g_prospero.acqrb);
    agcProsperoFreeRegion(&g_prospero.misc);
    agcProsperoFreeRegion(&g_prospero.cwsr);
    agcProsperoFreeRegion(&g_prospero.shadow_reg);
    agcProsperoFreeRegion(&g_prospero.eop_fifo);
    agcProsperoFreeRegion(&g_prospero.ddid);
    agcProsperoFreeRegion(&g_prospero.trap_data);
    agcProsperoFreeRegion(&g_prospero.trap_code);
    agcProsperoFreeRegion(&g_prospero.gpu_info);

    if (g_prospero.mmio_base) {
        (void)munmap(g_prospero.mmio_base, AGC_GC_MMIO_SIZE);
    }
    if (g_prospero.gc_fd >= 0) {
        (void)close(g_prospero.gc_fd);
    }
    memset(&g_prospero, 0, sizeof(g_prospero));
    g_prospero.gc_fd = -1;
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — capture / debug                                          */
/* ===================================================================== */

int32_t PS5_SYSV_ABI agcProsperoRegisterCaptureInterface(void)
{
    /* TODO: Razor ACQ registration via WFDebug ioctls. */
    return AGC_ERROR_NOT_SUPPORTED;
}

int32_t PS5_SYSV_ABI agcProsperoDeregisterCaptureInterface(void)
{
    return AGC_ERROR_NOT_SUPPORTED;
}

int32_t PS5_SYSV_ABI agcProsperoAcquireRazorACQ(void)
{
    /* TODO: WFDebug ioctl nr=0x15. */
    return AGC_ERROR_NOT_SUPPORTED;
}

int32_t PS5_SYSV_ABI agcProsperoReleaseRazorACQ(void)
{
    return AGC_ERROR_NOT_SUPPORTED;
}

int32_t PS5_SYSV_ABI agcProsperoSubmitToRazorACQ(void)
{
    return AGC_ERROR_NOT_SUPPORTED;
}

int32_t PS5_SYSV_ABI agcProsperoSubmitToHDRScopesACQ(void)
{
    return AGC_ERROR_NOT_SUPPORTED;
}

uint32_t PS5_SYSV_ABI agcProsperoGetPaDebugInterfaceVersion(void)
{
    /* FW 5.50 SPRX Pqxglq1oKec at VA 0x2b0 is a permission stub: it logs
     * "permission insufficient" and returns 0x8a6d0001 without an ioctl. */
    return AGC_DRIVER_ERROR_PERMISSION_INSUFFICIENT;
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

const AgcDriverOps agcProsperoDriverOps = {
.name = "prospero-gc-submit16",
    .initialize = agcProsperoInitialize,
    .initialize_internal_memory = agcProsperoInitializeInternalMemory,
    .shutdown = agcProsperoShutdown,
    .submit_multi_command_buffers_direct = agcProsperoSubmitMultiCommandBuffersDirect,
    .submit_dcb = agcProsperoSubmitDcb,
    .submit_acb = agcProsperoSubmitAcb,
    .suspend_point_submit_direct = agcProsperoSuspendPointSubmitDirect,
    .is_suspend_point_in_flight_direct = agcProsperoIsSuspendPointInFlightDirect,
    .internal_suspend_point_submit_final = agcProsperoInternalSuspendPointSubmitFinal,
    .setup_async_graphics = agcProsperoSetupAsyncGraphics,
    .set_tf_ring = agcProsperoSetTFRing,
    .set_tf_ring_direct = agcProsperoSetTFRingDirect,
    .set_hs_offchip_param_direct = agcProsperoSetHsOffchipParamDirect,
    .set_target_ring_for_diag = agcProsperoSetTargetRingForDiag,
    .notify_default_states = agcProsperoNotifyDefaultStates,
    .sdma_copy_linear_blocking = agcProsperoSdmaCopyLinearBlocking,
    .submit_eop_flip = agcProsperoSubmitEopFlip,
    .set_workloads_active = agcProsperoSetWorkloadsActive,
    .set_workload_complete = agcProsperoSetWorkloadComplete,
    .create_user_special_queue = agcProsperoCreateUserSpecialQueue,
    .destroy_user_special_queue = agcProsperoDestroyUserSpecialQueue,
    .register_capture_interface = agcProsperoRegisterCaptureInterface,
    .deregister_capture_interface = agcProsperoDeregisterCaptureInterface,
    .acquire_razor_acq = agcProsperoAcquireRazorACQ,
    .release_razor_acq = agcProsperoReleaseRazorACQ,
    .submit_to_razor_acq = agcProsperoSubmitToRazorACQ,
    .submit_to_hdr_scopes_acq = agcProsperoSubmitToHDRScopesACQ,
    .get_pa_debug_interface_version = agcProsperoGetPaDebugInterfaceVersion,
};

#endif /* OPENAGC_PROSPERO */
