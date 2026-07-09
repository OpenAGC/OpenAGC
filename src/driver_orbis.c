/*
 * openagc — driver_orbis.c
 *
 * Native PS5 (orbis) backend for AGC driver functions.
 * Talks directly to the kernel /dev/gc driver via ioctls using the
 * constants and struct layouts from include/agc_ioctl.h.
 *
 * This file is only compiled when OPENAGC_ORBIS is defined. It cannot be
 * built or tested on the host — it requires the ps5-payload-sdk toolchain
 * and PS5 hardware for validation.
 *
 * RE sources:
 * - Kernel dump gc_ioctl_internal at 0x6ed39c (BST + 4 jump tables)
 * - gc_submit_with_pid at 0x6e65c0, gc_frame_submit_internal at 0xb7da90
 * - Sibling ps5-openagc project's agc_driver.c / agc_submit.c / agc_queue.c
 * - SharpEmu HLE for userspace ioctl call patterns
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
#include <string.h>

#ifdef OPENAGC_ORBIS

/* /dev/gc device path */
#define AGC_GC_DEVICE_PATH "/dev/gc"

/* Maximum number of GPU command queues */
#define AGC_ORBIS_MAX_QUEUES 32

/*
 * PS5 kernel memory type constants. These are normally provided by the
 * ps5-payload-sdk libkernel headers. We declare them here so the host
 * build (which never enters this #ifdef block) stays SDK-free. If the
 * SDK headers are included before this file, the header definitions take
 * precedence.
 */
#ifndef SCE_KERNEL_WB_ONION
#define SCE_KERNEL_WB_ONION  0
#endif
#ifndef SCE_KERNEL_WC_GARLIC
#define SCE_KERNEL_WC_GARLIC 1
#endif
#ifndef SCE_KERNEL_WB_GARLIC
#define SCE_KERNEL_WB_GARLIC 3
#endif

#ifndef SCE_KERNEL_PROT_CPU_RW
#define SCE_KERNEL_PROT_CPU_RW 0x04
#endif
#ifndef SCE_KERNEL_PROT_GPU_RW
#define SCE_KERNEL_PROT_GPU_RW 0x08
#endif

/* libkernel direct-memory API (orbis only). */
extern int32_t sceKernelAllocateDirectMemory(
    int64_t searchStart, int64_t searchEnd, size_t length,
    uint64_t alignment, int memoryType, off_t *physicalAddrOut);
extern int32_t sceKernelMapDirectMemory(
    void **virtualAddr, size_t length, int prot, int flags,
    off_t physicalAddr, uint64_t alignment);
extern int32_t sceKernelMunmap(void *addr, size_t len);
extern int32_t sceKernelReleaseDirectMemory(off_t physicalAddr, size_t length);

/* Queue types (bits [10:9] of queue create ioctl arg) */
enum {
    AGC_ORBIS_QUEUE_GFX      = 0,
    AGC_ORBIS_QUEUE_COMPUTE  = 1,
    AGC_ORBIS_QUEUE_DMA      = 2,
};

/*
 * Queue create/destroy ioctl argument (nr=0x2a / 0x2b, size=4).
 * Packed as a 32-bit value:
 *   bits [0:8]   = queue index (9 bits, 0-31 used)
 *   bits [9:10]  = queue type (2 bits)
 *   bits [11:31] = reserved
 */
typedef union {
    uint32_t value;
    struct {
        uint32_t index    : 9;
        uint32_t type     : 2;
        uint32_t reserved : 21;
    };
} AgcOrbisQueueArg;

/* Per-queue state */
typedef struct {
    uint32_t index;
    uint32_t type;
    bool     in_use;
} AgcOrbisQueue;

/* Named internal memory region allocated by sce_agc_initialize_internal_memory */
typedef struct {
    void    *cpu_addr;
    off_t    physical_addr;
    uint64_t gpu_addr;
    size_t   size;
} AgcOrbisRegion;

/*
 * Orbis backend context.
 *
 * The generic backend uses global statics; the orbis backend needs a
 * /dev/gc file descriptor and queue tracking. We keep it in a single
 * static struct for simplicity — the PS5 AGC driver is singleton.
 */
typedef struct {
    int              gc_fd;          /* /dev/gc file descriptor */
    bool             initialized;    /* sce_agc_initialize succeeded */
    bool             mem_initialized;/* sce_agc_initialize_internal_memory succeeded */
    bool             defaults_notified;/* sceAgcDriverNotifyDefaultStates succeeded */
    AgcOrbisQueue    queues[AGC_ORBIS_MAX_QUEUES];
    AgcOrbisRegion   ddid;
    AgcOrbisRegion   cwsr;
    AgcOrbisRegion   eop_fifo;
    AgcOrbisRegion   shadow_reg;
    AgcOrbisRegion   trap_code;
    AgcOrbisRegion   trap_data;
    AgcOrbisRegion   gpu_info;
    AgcOrbisRegion   workload;
    AgcOrbisRegion   primary_defaults;
    AgcOrbisRegion   internal_defaults;
} AgcOrbisContext;

static AgcOrbisContext g_orbis = {
    .gc_fd = -1,
};

/*
 * Low-level ioctl wrapper.
 * Returns 0 on success, negative errno on ioctl failure.
 */
static int agcOrbisIoctl(uint32_t cmd, void *arg)
{
    if (g_orbis.gc_fd < 0)
        return -1;
    return ioctl(g_orbis.gc_fd, cmd, arg);
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
static void agcOrbisBuildCbDescriptor(AgcGcCommandBuffer *cb,
                                       uint64_t gpu_addr,
                                       uint32_t size_dwords,
                                       bool is_const)
{
    if (is_const)
        cb->header = AGC_GC_CB_HEADER_IB | ((uint64_t)size_dwords << 32);
    else
        cb->header = AGC_GC_CB_HEADER_IB_CNST | ((uint64_t)size_dwords << 32);

    /* ib_base: GPU VA in lower 52 bits, VMID = 0 (kernel will insert) */
    cb->ib_base = gpu_addr & AGC_GC_IB_VMASK;
}

/* Find a free queue slot. Returns index >= 0, or -1 if all slots in use. */
static int agcOrbisFindFreeQueue(void)
{
    for (int i = 0; i < AGC_ORBIS_MAX_QUEUES; i++) {
        if (!g_orbis.queues[i].in_use)
            return i;
    }
    return -1;
}

/*
 * Allocate, map, and GPU-map a single named internal region.
 *
 *   1. sceKernelAllocateDirectMemory for physical pages
 *   2. sceKernelMapDirectMemory for CPU VA
 *   3. MAKESYSMAP_8 ioctl to assign a GPU VA
 *
 * On failure, any partial allocation is unwound and the region is zeroed.
 */
static int32_t agcOrbisAllocRegion(AgcOrbisRegion *region, size_t size, int mem_type)
{
    if (!region || size == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    memset(region, 0, sizeof(*region));

    size_t aligned_size = (size + 0xFFFu) & ~0xFFFu;
    int prot = SCE_KERNEL_PROT_CPU_RW | SCE_KERNEL_PROT_GPU_RW;
    off_t physical_addr = 0;

    int32_t ret = sceKernelAllocateDirectMemory(
        0, 0x10000000000LL, aligned_size, 0x1000u, mem_type, &physical_addr);
    if (ret != 0 || physical_addr == 0)
        return AGC_ERROR_OUT_OF_MEMORY;

    void *cpu_addr = NULL;
    ret = sceKernelMapDirectMemory(
        &cpu_addr, aligned_size, prot, 0, physical_addr, 0x1000u);
    if (ret != 0 || !cpu_addr) {
        sceKernelReleaseDirectMemory(physical_addr, aligned_size);
        return AGC_ERROR_OUT_OF_MEMORY;
    }

    AgcGcMakesysmapArg8 map_arg;
    map_arg.addr = (uint64_t)(uintptr_t)cpu_addr;
    int ioctl_ret = agcOrbisIoctl(AGC_GC_IOCTL_MAKESYSMAP_8, &map_arg);
    if (ioctl_ret < 0) {
        sceKernelMunmap(cpu_addr, aligned_size);
        sceKernelReleaseDirectMemory(physical_addr, aligned_size);
        return AGC_ERROR_INTERNAL;
    }

    region->cpu_addr = cpu_addr;
    region->physical_addr = physical_addr;
    region->gpu_addr = map_arg.addr;
    region->size = aligned_size;
    return AGC_OK;
}

/*
 * Free a single named internal region.
 */
static void agcOrbisFreeRegion(AgcOrbisRegion *region)
{
    if (!region || region->size == 0)
        return;

    if (region->cpu_addr)
        sceKernelMunmap(region->cpu_addr, region->size);
    if (region->physical_addr)
        sceKernelReleaseDirectMemory(region->physical_addr, region->size);

    memset(region, 0, sizeof(*region));
}

/* ===================================================================== */
/* Public API — initialization                                           */
/* ===================================================================== */

/*
 * Open /dev/gc and initialize the AGC context.
 *
 * Corresponds to the firmware's sce_agc_initialize() which:
 * 1. Opens /dev/gc
 * 2. Calls FRAME_OPEN ioctl to get a GPU context
 */
int32_t PS5_SYSV_ABI sce_agc_initialize(void)
{
    if (g_orbis.initialized)
        return AGC_OK;

    g_orbis.gc_fd = open(AGC_GC_DEVICE_PATH, O_RDWR);
    if (g_orbis.gc_fd < 0)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Call FRAME_OPEN ioctl to initialize the GPU context */
    AgcGcFrameOpenArg frame_arg = {0};
    int ret = agcOrbisIoctl(AGC_GC_IOCTL_FRAME_OPEN, &frame_arg);
    if (ret < 0) {
        close(g_orbis.gc_fd);
        g_orbis.gc_fd = -1;
        return AGC_ERROR_NOT_INITIALIZED;
    }

    g_orbis.initialized = true;
    return AGC_OK;
}

/*
 * Allocate internal memory regions.
 *
 * Corresponds to sce_agc_initialize_internal_memory() which allocates
 * DDID, CWSR, EOP FIFO, register shadow, trap handler, GPU info, and
 * workload tracking areas. These are mapped into GPU VA space via
 * gc_makesysmap_8 ioctl.
 *
 * Each region is allocated via sceKernelAllocateDirectMemory, mapped into
 * CPU space via sceKernelMapDirectMemory, and then mapped into GPU VA
 * space via agcOrbisIoctl(AGC_GC_IOCTL_MAKESYSMAP_8). The region sizes and
 * memory types are documented in the sibling ps5-openagc project's
 * src/agc_driver.c.
 */
int32_t PS5_SYSV_ABI sce_agc_initialize_internal_memory(void)
{
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (g_orbis.mem_initialized)
        return AGC_OK;

    /*
     * Named internal memory regions (sizes and memory types from GNM driver
     * string analysis and the sibling ps5-openagc project).
     */
    struct {
        AgcOrbisRegion *region;
        size_t          size;
        int             mem_type;
        const char     *name;
    } regions[] = {
        { &g_orbis.ddid,       0x1000, SCE_KERNEL_WB_ONION,  "DDID"       },
        { &g_orbis.cwsr,       0x10000, SCE_KERNEL_WB_GARLIC, "CWSR"       },
        { &g_orbis.eop_fifo,   0x1000, SCE_KERNEL_WC_GARLIC, "EOP FIFO"   },
        { &g_orbis.shadow_reg, 0x4000, SCE_KERNEL_WB_GARLIC, "Shadow regs"},
        { &g_orbis.trap_code,  0x4000, SCE_KERNEL_WC_GARLIC, "Trap code"  },
        { &g_orbis.trap_data,  0x4000, SCE_KERNEL_WB_GARLIC, "Trap data"  },
        { &g_orbis.gpu_info,   0x1000, SCE_KERNEL_WB_ONION,  "GPU info"   },
        { &g_orbis.workload,   0x1000, SCE_KERNEL_WB_ONION,  "Workload"   },
    };

    for (int i = 0; i < (int)(sizeof(regions) / sizeof(regions[0])); i++) {
        int32_t ret = agcOrbisAllocRegion(regions[i].region,
                                          regions[i].size,
                                          regions[i].mem_type);
        if (ret != AGC_OK) {
            /* Clean up any regions we already allocated. */
            for (int j = 0; j < i; j++)
                agcOrbisFreeRegion(regions[j].region);
            return ret;
        }
    }

    g_orbis.mem_initialized = true;
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
    if (!g_orbis.initialized)
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
        uint32_t size_dwords = dcb_sizes_in_bytes[i] / 4;
        agcOrbisBuildCbDescriptor(&cb_descs[cb_idx],
                                   (uint64_t)(uintptr_t)dcb_gpu_addrs[i],
                                   size_dwords, false);
        cb_idx++;
    }

    for (uint32_t i = 0; i < count && cb_idx < total_cbs; i++) {
        if (!acb_gpu_addrs || !acb_gpu_addrs[i])
            continue;
        uint32_t size_dwords = acb_sizes_in_bytes[i] / 4;
        /* ACBs use the const IB type */
        agcOrbisBuildCbDescriptor(&cb_descs[cb_idx],
                                   (uint64_t)(uintptr_t)acb_gpu_addrs[i],
                                   size_dwords, true);
        cb_idx++;
    }

    /* Build submit ioctl arg */
    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.pid = 0;  /* 0 = current process */
    submit_arg.num_cbs = total_cbs;
    submit_arg.cb_array = (uint64_t)(uintptr_t)cb_descs;

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SUBMIT_PID, &submit_arg);
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
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    AgcGcCommandBuffer cb_desc;
    agcOrbisBuildCbDescriptor(&cb_desc,
                               (uint64_t)packet->command_address,
                               packet->dword_count, false);

    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.pid = 0;
    submit_arg.num_cbs = 1;
    submit_arg.cb_array = (uint64_t)(uintptr_t)&cb_desc;

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SUBMIT_PID, &submit_arg);
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
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (!packet || packet->command_address == 0 || packet->dword_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (owner_handle >= AGC_ORBIS_MAX_QUEUES)
        return AGC_ERROR_CB_INVALID_QUEUE;

    AgcGcCommandBuffer cb_desc;
    agcOrbisBuildCbDescriptor(&cb_desc,
                               (uint64_t)packet->command_address,
                               packet->dword_count, true);

    AgcGcSubmitArgs submit_arg = {0};
    submit_arg.pid = 0;
    submit_arg.num_cbs = 1;
    submit_arg.cb_array = (uint64_t)(uintptr_t)&cb_desc;

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SUBMIT_PID, &submit_arg);
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
    if (!g_orbis.initialized)
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

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SUSPEND_16, &arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;

    return AGC_OK;
}

bool PS5_SYSV_ABI sceAgcDriverIsSuspendPointInFlightDirect(uint32_t value)
{
    (void)value;
    if (!g_orbis.initialized)
        return false;

    /*
     * Query the gfx queue status via the 4-byte read ioctl (nr=0x27).
     * The exact suspend-point bit layout is still pending RE; until then
     * treat any non-zero status as "in flight".
     */
    uint32_t status = 0;
    int ret = agcOrbisIoctl(AGC_GC_IOCTL_QUEUE_STAT_16, &status);
    if (ret < 0)
        return false;

    return status != 0;
}

int32_t PS5_SYSV_ABI sce_agc_internal_suspend_point_submit_final(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
    if (!g_orbis.initialized)
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

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SUSPEND_39, &arg);
    if (ret < 0)
        return AGC_ERROR_SUBMIT_FAILED;
    return AGC_OK;
}

/* ===================================================================== */
/* Public API — async graphics / TF ring / HS offchip                    */
/* ===================================================================== */

/*
 * Setup async graphics queue.
 * Uses ioctl nr=0x1f (SETUP_ASYNC, 4-byte RW).
 */
int32_t PS5_SYSV_ABI sceAgcDriverSetupAsyncGraphics(void)
{
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    uint32_t arg = 0;
    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SETUP_ASYNC, &arg);
    return (ret < 0) ? AGC_ERROR_INTERNAL : AGC_OK;
}

/*
 * Set tessellation factor ring.
 * Uses ioctl nr=0x20 (SET_TF_RING, 16-byte RW).
 */
int32_t PS5_SYSV_ABI sceAgcDriverSetTFRingDirect(void)
{
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    uint32_t arg[4] = {0};
    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SET_TF_RING, arg);
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
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    AgcGcSetHsOffchipArg arg = {0};
    arg.list_addr = list_addr;
    arg.num_entries = num_entries;

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_SET_HS_OFFCHIP, &arg);
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
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;
    if (g_orbis.defaults_notified)
        return AGC_OK;
    if (!g_orbis.mem_initialized)
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

    int32_t ret = agcOrbisAllocRegion(&g_orbis.primary_defaults, primary_size,
                                      SCE_KERNEL_WB_GARLIC);
    if (ret != AGC_OK)
        return ret;

    ret = agcOrbisAllocRegion(&g_orbis.internal_defaults, internal_size,
                              SCE_KERNEL_WB_GARLIC);
    if (ret != AGC_OK) {
        agcOrbisFreeRegion(&g_orbis.primary_defaults);
        return ret;
    }

    ret = agcRegisterDefaultsBuild(
        g_orbis.primary_defaults.cpu_addr,
        g_orbis.primary_defaults.size,
        g_orbis.primary_defaults.gpu_addr,
        primary_groups,
        primary_count,
        AGC_PRIMARY_CX_LENGTH,
        AGC_PRIMARY_SH_LENGTH,
        AGC_PRIMARY_UC_LENGTH);
    if (ret != AGC_OK) {
        agcOrbisFreeRegion(&g_orbis.internal_defaults);
        agcOrbisFreeRegion(&g_orbis.primary_defaults);
        return ret;
    }

    ret = agcRegisterDefaultsBuild(
        g_orbis.internal_defaults.cpu_addr,
        g_orbis.internal_defaults.size,
        g_orbis.internal_defaults.gpu_addr,
        internal_groups,
        internal_count,
        AGC_INTERNAL_CX_LENGTH,
        AGC_INTERNAL_SH_LENGTH,
        AGC_INTERNAL_UC_LENGTH);
    if (ret != AGC_OK) {
        agcOrbisFreeRegion(&g_orbis.internal_defaults);
        agcOrbisFreeRegion(&g_orbis.primary_defaults);
        return ret;
    }

    /*
     * Submit a CLEAR_STATE packet to initialize the GPU context to the
     * primary defaults. The kernel patches CLEAR_STATE (opcode 0x14) via
     * gc_pm4_clearstate_patch; the primary/internal blobs we just built are
     * GPU-visible and consumed during context reset.
     */
    AgcOrbisRegion dcb_region = {0};
    ret = agcOrbisAllocRegion(&dcb_region, 8, SCE_KERNEL_WB_GARLIC);
    if (ret != AGC_OK) {
        agcOrbisFreeRegion(&g_orbis.internal_defaults);
        agcOrbisFreeRegion(&g_orbis.primary_defaults);
        return ret;
    }

    uint32_t *dcb = (uint32_t *)dcb_region.cpu_addr;
    dcb[0] = agcPm4Header3(AGC_PM4_OP_CLEAR_STATE, 2);
    dcb[1] = 0;

    AgcCommandBufferSubmit submit = {0};
    submit.command_address = (uintptr_t)dcb_region.gpu_addr;
    submit.dword_count = 2;

    ret = sceAgcDriverSubmitDcb(&submit);
    agcOrbisFreeRegion(&dcb_region);
    if (ret != AGC_OK) {
        agcOrbisFreeRegion(&g_orbis.internal_defaults);
        agcOrbisFreeRegion(&g_orbis.primary_defaults);
        return ret;
    }

    g_orbis.defaults_notified = true;
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
/* Public API — user special queue management                            */
/* ===================================================================== */

/*
 * Create a user special queue.
 * Uses ioctl nr=0x2a (QUEUE_CREATE, 4-byte R).
 */
int32_t PS5_SYSV_ABI _sceAgcDriverCreateUserSpecialQueue(void)
{
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    int index = agcOrbisFindFreeQueue();
    if (index < 0)
        return AGC_ERROR_OUT_OF_MEMORY;

    AgcOrbisQueueArg arg = {0};
    arg.index = (uint32_t)index;
    arg.type = AGC_ORBIS_QUEUE_COMPUTE;

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_QUEUE_CREATE, &arg.value);
    if (ret < 0)
        return AGC_ERROR_INTERNAL;

    g_orbis.queues[index].index = (uint32_t)index;
    g_orbis.queues[index].type = AGC_ORBIS_QUEUE_COMPUTE;
    g_orbis.queues[index].in_use = true;

    return AGC_OK;
}

/*
 * Destroy a user special queue.
 * Uses ioctl nr=0x2b (QUEUE_DESTROY, 4-byte W).
 */
int32_t PS5_SYSV_ABI _sceAgcDriverDestroyUserSpecialQueue(void)
{
    if (!g_orbis.initialized)
        return AGC_ERROR_NOT_INITIALIZED;

    /* Find the first in-use compute queue and destroy it.
     * The real API takes a queue handle; this stub destroys the first. */
    for (int i = 0; i < AGC_ORBIS_MAX_QUEUES; i++) {
        if (g_orbis.queues[i].in_use) {
            AgcOrbisQueueArg arg = {0};
            arg.index = g_orbis.queues[i].index;
            arg.type = g_orbis.queues[i].type;

            agcOrbisIoctl(AGC_GC_IOCTL_QUEUE_DESTROY, &arg.value);
            g_orbis.queues[i].in_use = false;
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
    if (!g_orbis.initialized || g_orbis.gc_fd < 0)
        return 0;

    uint32_t version = 0;
    agcOrbisIoctl(AGC_GC_IOCTL_PADEBUG_4, &version);
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
int agcOrbisMakeSysmap(void *cpu_addr, uint64_t *out_gpu_addr)
{
    if (!g_orbis.initialized || !cpu_addr || !out_gpu_addr)
        return -1;

    AgcGcMakesysmapArg8 arg;
    arg.addr = (uint64_t)(uintptr_t)cpu_addr;

    int ret = agcOrbisIoctl(AGC_GC_IOCTL_MAKESYSMAP_8, &arg);
    if (ret < 0)
        return ret;

    *out_gpu_addr = arg.addr;
    return 0;
}

#endif /* OPENAGC_ORBIS */
