/*
 * agc_graphics.c — PS5 AGC graphics draw call test (triangle)
 *
 * Phase 7: Submit a real graphics draw call (VS + PS + render target + draw)
 * and display the result on the PS5.
 *
 * Key architectural learnings from compute shader validation:
 * - Render target must be in flexible memory (GPU MMU-mapped), not garlic
 * - SET_SH_REG shader type bit: 0=graphics, 1=compute
 * - Apply FW 5.50 SH register defaults before shader-specific state
 * - After GPU render, copy flexible→garlic for VideoOut display
 *
 * Build: see samples/hw_test/Makefile (target: agc_graphics.elf)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

/* PS5 VideoOut helpers */
#include "ps5_video_out.h"

/* openagc headers */
#include "agcdriver.h"
#include "agc_context.h"
#include "agc_registers.h"
#include "agc_shader.h"
#include "agc_cb.h"
#include "agc_pm4.h"
#include "agc_error.h"

/* GPU credential bypass */
#include "gpu_credentials.h"

#include <ps5/kernel.h>

/* Embedded shader binaries */
#include "shaders/triangle_vert_sb.h"
#include "shaders/triangle_frag_sb.h"

/* Kernel constants fallbacks (if ps5/kernel.h doesn't define them) */
#ifndef SCE_KERNEL_PROT_CPU_READ
#define SCE_KERNEL_PROT_CPU_READ  0x01
#endif
#ifndef SCE_KERNEL_PROT_CPU_RW
#define SCE_KERNEL_PROT_CPU_RW    0x02
#endif
#ifndef SCE_KERNEL_PROT_CPU_WRITE
#define SCE_KERNEL_PROT_CPU_WRITE 0x02
#endif
#ifndef SCE_KERNEL_PROT_GPU_READ
#define SCE_KERNEL_PROT_GPU_READ  0x10
#endif
#ifndef SCE_KERNEL_PROT_GPU_WRITE
#define SCE_KERNEL_PROT_GPU_WRITE 0x20
#endif
#ifndef SCE_KERNEL_WC_GARLIC
#define SCE_KERNEL_WC_GARLIC 3
#endif

/* Kernel function declarations (if not in ps5/kernel.h) */
#ifndef _PS5_KERNEL_DECLS
#define _PS5_KERNEL_DECLS
typedef int SceKernelEqueue;
typedef struct { char _opaque[64]; } SceKernelEvent;
int sceKernelUsleep(unsigned int microseconds);
int sceKernelAllocateDirectMemory(
    off_t searchStart, off_t searchEnd, size_t len, size_t alignment,
    int memoryType, int32_t *directMemoryStart);
int sceKernelMapDirectMemory(
    void **virtualAddress, size_t length, int protection, int flags,
    int32_t directMemoryStart, size_t alignment);
int sceKernelMapNamedSystemFlexibleMemory(
    void **virtualAddress, size_t length, int protection, int flags,
    const char *name);
int sceKernelCreateEqueue(SceKernelEqueue *equeue, const char *name);
int sceKernelWaitEqueue(SceKernelEqueue equeue, SceKernelEvent *events,
    int count, void *timeout, void *result);
#endif

/* ======================================================================== */
/* Constants                                                                */
/* ======================================================================== */

#define BUFFER_COUNT       2
#define BYTES_PER_PIXEL    4
#define DIRECT_MEMORY_ALIGNMENT  0x200000  /* 2MB */
#define PS5_DIRECT_MEM_SEARCH_END  0x1000000000ULL

#define CB_BUFFER_DWORDS   16384

/* CB_COLOR0_INFO format values (AMD surface format enum) */
#define COLOR_8_8_8_8              0x02u
#define COLOR_8_8_8_8_SRGB         0x0Au

/* CB_COLOR0_INFO bit fields */
#define CB_INFO_FORMAT(f)      ((f) << AGC_REG_CB_COLOR0_INFO_FORMAT_SHIFT)
#define CB_INFO_NUM_TYPE(t)    ((t) << AGC_REG_CB_COLOR0_INFO_NUMBER_TYPE_SHIFT)
#define CB_INFO_SWAP(s)        ((s) << 11)  /* bits [12:11] */

/* GnmSurfaceNumber: 0=unsigned norm, 1=signed norm, ... */
#define SURF_NUMBER_UNORM      0
/* GnmSurfaceSwap: 0=standard (RGBA), 1=alt (BGRA) */
#define SURF_SWAP_STD          0
#define SURF_SWAP_ALT          1

/* VGT primitive types */
#define VGT_PT_TRILIST         0x04u  /* 4 = triangle list */

/* VGT_SHADER_STAGES_EN bits (RDNA2) */
#define VGT_SHADER_STAGES_ES_EN  (1u << 8)  /* ES stage = VS without tess */

/* CB_TARGET_MASK: 0xF = write all 4 channels (RGBA) to RT0 */
#define CB_TARGET_MASK_ALL     0x0Fu

/* Shader type constants */
#define AGC_SHADER_TYPE_VS     3
#define AGC_SHADER_TYPE_PS     1

/* ======================================================================== */
/* Types                                                                     */
/* ======================================================================== */

typedef struct {
    int32_t handle;
    int32_t direct_memory;
    void *mapped;
    size_t mapped_size;
    uint8_t *buffers[BUFFER_COUNT];
    void *compute_buffer;   /* Flexible memory pool for RT + shader code */
    void *render_target;    /* Points into compute_buffer */
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    size_t buffer_stride;
    SceKernelEqueue flipqueue;
} GraphicsTest;

typedef struct {
    const AgcShaderRecord *record;
    const uint8_t *code;
    size_t code_size;
    const AgcRegisterValue *sh_regs;
    uint32_t num_sh_regs;
    const AgcRegisterValue *cx_regs;
    uint32_t num_cx_regs;
} ParsedGraphicsShader;

/* ======================================================================== */
/* VideoOut linear tiling patch                                              */
/* ======================================================================== */

int kernel_dynlib_handle(int pid, const char *name, uint32_t *handle);
intptr_t kernel_dynlib_mapbase_addr(int pid, uint32_t handle);
int kernel_mprotect(int pid, intptr_t addr, size_t size, int prot);

/* Patch libSceVideoOut to allow linear tiling without debug setting */
static void patch_videoout_linear(void) {
    uint32_t vo_handle = 0;
    if (kernel_dynlib_handle(-1, "libSceVideoOut.sprx", &vo_handle) == 0 && vo_handle) {
        intptr_t vo_base = kernel_dynlib_mapbase_addr(-1, vo_handle);
        if (vo_base) {
            printf("  libSceVideoOut base: 0x%lx\n", (unsigned long)vo_base);
            intptr_t patch_addr = vo_base + 0x7e61;
            kernel_mprotect(-1, patch_addr & ~0xFFF, 0x2000,
                            SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW | 0x4);
            volatile uint8_t *p = (volatile uint8_t *)patch_addr;
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            kernel_mprotect(-1, patch_addr & ~0xFFF, 0x2000,
                            SCE_KERNEL_PROT_CPU_READ | 0x4);
            printf("  Patched je->nop at offset 0x7e61\n");
        }
    }
}

/* ======================================================================== */
/* Helpers                                                                   */
/* ======================================================================== */

static size_t align_up(size_t value, size_t alignment) {
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

static const char *errstr(int32_t err) {
    if (err == 0) return "OK";
    if (err > 0) return "POSITIVE";
    return "ERROR";
}

/* ======================================================================== */
/* Memory allocation                                                         */
/* ======================================================================== */

static uint32_t *cb_buffer = NULL;  /* Command buffer in flexible memory */

static bool allocate_display_buffers(GraphicsTest *test) {
    test->buffer_stride = align_up(
        (size_t)test->width * test->height * BYTES_PER_PIXEL,
        DIRECT_MEMORY_ALIGNMENT);

    test->mapped_size = test->buffer_stride * BUFFER_COUNT;

    int res = sceKernelAllocateDirectMemory(
        0, (off_t)PS5_DIRECT_MEM_SEARCH_END, test->mapped_size,
        DIRECT_MEMORY_ALIGNMENT, SCE_KERNEL_WC_GARLIC, &test->direct_memory);
    if (res != 0) {
        printf("sceKernelAllocateDirectMemory failed: %d\n", res);
        return false;
    }

    int prot = SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_RW |
               SCE_KERNEL_PROT_GPU_READ | SCE_KERNEL_PROT_GPU_WRITE;
    res = sceKernelMapDirectMemory(
        &test->mapped, test->mapped_size, prot, 0,
        test->direct_memory, DIRECT_MEMORY_ALIGNMENT);
    if (res != 0) {
        printf("sceKernelMapDirectMemory failed: %d\n", res);
        return false;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        test->buffers[i] = (uint8_t *)test->mapped + i * test->buffer_stride;
    }

    /* Allocate flexible memory for command buffer + render target + shader code */
    size_t cb_size = CB_BUFFER_DWORDS * 4;  /* 64KB for CB */
    void *cb_addr = NULL;
    int cb_ret = sceKernelMapNamedSystemFlexibleMemory(
        &cb_addr, cb_size, 0x33, 0, "agc_graphics_cb");
    if (cb_ret != 0 || !cb_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for CB: %d\n", cb_ret);
        return false;
    }
    cb_buffer = (uint32_t *)cb_addr;

    /* Flexible memory pool for render target + shader code.
     * Size must accommodate the render target (width*height*4) plus shader
     * code and 64KB offset. For 4K (3840x2160*4 = ~33MB), use 64MB. */
    size_t rt_size = (size_t)test->width * test->height * BYTES_PER_PIXEL;
    size_t pool_size = align_up(rt_size + 0x100000, 1024 * 1024);  /* rt + 1MB slack */
    void *pool_addr = NULL;
    int pool_ret = sceKernelMapNamedSystemFlexibleMemory(
        &pool_addr, pool_size, 0x33, 0, "agc_graphics_pool");
    if (pool_ret != 0 || !pool_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for pool: %d\n", pool_ret);
        return false;
    }
    test->compute_buffer = pool_addr;

    /* Render target at offset 0x10000 in the pool (after shader code space) */
    test->render_target = (uint8_t *)pool_addr + 0x10000;

    printf("Command buffer: %zu bytes at %p (flexible)\n", cb_size, cb_buffer);
    printf("Compute pool: %zu bytes at %p (flexible)\n", pool_size, pool_addr);
    printf("Render target: at %p (flexible)\n", test->render_target);
    printf("Display buffers: %zu bytes each, %d buffers at %p (garlic)\n",
           test->buffer_stride, BUFFER_COUNT, test->mapped);
    return true;
}

/* ======================================================================== */
/* VideoOut init                                                             */
/* ======================================================================== */

static bool init_videoout(GraphicsTest *test) {
    int32_t user_ids[] = { 0xFF, 0, 1, 2 };
    test->handle = -1;

    for (int i = 0; i < 4; i++) {
        test->handle = sceVideoOutOpen(user_ids[i],
            SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, NULL);
        if (test->handle >= 0) break;
    }
    if (test->handle < 0) {
        printf("sceVideoOutOpen failed: 0x%08x\n", (unsigned)test->handle);
        return false;
    }

    SceVideoOutResolutionStatus status = {0};
    sceVideoOutGetResolutionStatus(test->handle, &status);
    test->width = status.full_width;
    test->height = status.full_height;
    test->pitch_pixels = test->width;
    printf("Resolution: %ux%u\n", test->width, test->height);

    if (!allocate_display_buffers(test)) return false;

    /* Patch libSceVideoOut to allow linear tiling without debug setting */
    patch_videoout_linear();

    /* Register display buffers with VideoOut (linear A8B8G8R8) */
    uint8_t attr_raw[64];
    memset(attr_raw, 0, sizeof(attr_raw));
    *(uint32_t *)(attr_raw + 0)  = 0x80000000;  /* A8B8G8R8_SRGB */
    *(uint32_t *)(attr_raw + 4)  = 1;           /* tiling = linear */
    *(uint32_t *)(attr_raw + 12) = test->width;
    *(uint32_t *)(attr_raw + 16) = test->height;
    *(uint32_t *)(attr_raw + 20) = test->pitch_pixels;

    void *addresses[BUFFER_COUNT] = { test->buffers[0], test->buffers[1] };
    int32_t reg_err = sceVideoOutRegisterBuffers(test->handle, 0, addresses,
        BUFFER_COUNT, (const SceVideoOutBufferAttribute *)attr_raw);
    if (reg_err != 0) {
        printf("sceVideoOutRegisterBuffers failed: 0x%08x\n", (unsigned)reg_err);
        return false;
    }

    sceKernelCreateEqueue(&test->flipqueue, "agc_graphics flips");
    sceVideoOutAddFlipEvent((void *)(uintptr_t)test->flipqueue, test->handle, NULL);
    sceVideoOutSetFlipRate(test->handle, 0);

    return true;
}

/* ======================================================================== */
/* AGC init                                                                  */
/* ======================================================================== */

static bool init_agc(void) {
    int32_t err;
    err = sce_agc_initialize();
    if (err != AGC_OK) { printf("sce_agc_initialize failed: 0x%08x\n", (unsigned)err); return false; }

    err = sce_agc_initialize_internal_memory();
    if (err != AGC_OK) { printf("sce_agc_initialize_internal_memory failed: 0x%08x\n", (unsigned)err); return false; }

    err = sceAgcDriverNotifyDefaultStates(0);
    if (err != AGC_OK) { printf("NotifyDefaultStates failed: 0x%08x\n", (unsigned)err); return false; }

    err = sceAgcDriverSetupAsyncGraphics(1);
    if (err != AGC_OK) { printf("SetupAsyncGraphics failed: 0x%08x\n", (unsigned)err); return false; }

    return true;
}

/* ======================================================================== */
/* Shader parsing                                                            */
/* ======================================================================== */

static bool parse_graphics_shader(ParsedGraphicsShader *out,
                                   const uint8_t *sb, size_t sb_size,
                                   const char *name) {
    if (sb_size < sizeof(AgcShaderRecord)) {
        printf("%s: binary too small (%zu bytes)\n", name, sb_size);
        return false;
    }

    const AgcShaderRecord *rec = (const AgcShaderRecord *)sb;
    if (rec->magic != AGC_SHADER_RECORD_MAGIC) {
        printf("%s: bad magic 0x%08x\n", name, rec->magic);
        return false;
    }

    out->record = rec;
    out->num_sh_regs = rec->num_sh_registers;
    out->sh_regs = (const AgcRegisterValue *)(sb + rec->sh_registers);
    out->code = sb + rec->code;
    out->code_size = sb_size - rec->code;

    /* CX register count = space between cx_registers offset and code offset */
    if (rec->cx_registers > 0 && rec->code > rec->cx_registers) {
        size_t cx_size = rec->code - rec->cx_registers;
        out->num_cx_regs = (uint32_t)(cx_size / sizeof(AgcRegisterValue));
        out->cx_regs = (const AgcRegisterValue *)(sb + rec->cx_registers);
    } else {
        out->num_cx_regs = 0;
        out->cx_regs = NULL;
    }

    printf("%s: type=%u sh_regs=%u cx_regs=%u code_size=%zu\n",
           name, rec->shader_type, out->num_sh_regs, out->num_cx_regs,
           out->code_size);

    for (uint32_t i = 0; i < out->num_sh_regs; i++) {
        printf("  SH[%u]: off=0x%03x val=0x%08x\n",
               i, out->sh_regs[i].offset, out->sh_regs[i].value);
    }
    for (uint32_t i = 0; i < out->num_cx_regs; i++) {
        printf("  CX[%u]: off=0x%03x val=0x%08x\n",
               i, out->cx_regs[i].offset, out->cx_regs[i].value);
    }

    return true;
}

/* Upload shader code to flexible memory. Returns GPU address. */
static void *upload_shader(const uint8_t *code, size_t code_size,
                           void *pool, size_t offset) {
    void *addr = (uint8_t *)pool + offset;
    memcpy(addr, code, code_size);
    __builtin___clear_cache((char *)addr, (char *)addr + code_size);
    return addr;
}

/* ======================================================================== */
/* DCB construction                                                          */
/* ======================================================================== */

/* Apply FW 5.50 SH register defaults (graphics type — bit 0 = 0).
 * Writes each register INDIVIDUALLY because some groups have
 * non-contiguous offsets — the batch SET_SH_REG packet assumes
 * contiguous offsets and would corrupt state. */
static void apply_sh_defaults_graphics(SceAgcCb *cb) {
    uint32_t count = 0;
    const AgcRegisterDefaultsGroup *pgroups = agcRegisterDefaultsV8GetPrimaryGroups(&count);
    uint32_t applied = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (pgroups[i].space == kAgcRegisterDefaultSpaceSh && pgroups[i].register_count > 0) {
            for (uint32_t j = 0; j < pgroups[i].register_count; j++) {
                sceAgcCbSetShRegistersDirect(cb,
                    (const AgcRegisterValue *)&pgroups[i].registers[j], 1);
                applied++;
            }
        }
    }
    const AgcRegisterDefaultsGroup *igroups = agcRegisterDefaultsV8GetInternalGroups(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (igroups[i].space == kAgcRegisterDefaultSpaceSh && igroups[i].register_count > 0) {
            for (uint32_t j = 0; j < igroups[i].register_count; j++) {
                sceAgcCbSetShRegistersDirect(cb,
                    (const AgcRegisterValue *)&igroups[i].registers[j], 1);
                applied++;
            }
        }
    }
    printf("[Dispatch] Applied %u SH register defaults (graphics, individual)\n", applied);
}

/* Apply FW 5.50 CX register defaults.
 * Writes each register INDIVIDUALLY because some groups (e.g. group 72
 * with 128 CB_COLOR0 registers) have non-contiguous offsets. */
static void apply_cx_defaults(SceAgcCb *cb) {
    uint32_t count = 0;
    const AgcRegisterDefaultsGroup *pgroups = agcRegisterDefaultsV8GetPrimaryGroups(&count);
    uint32_t applied = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (pgroups[i].space == kAgcRegisterDefaultSpaceCx && pgroups[i].register_count > 0) {
            for (uint32_t j = 0; j < pgroups[i].register_count; j++) {
                sceAgcCbSetCxRegistersDirect(cb,
                    (const AgcRegisterValue *)&pgroups[i].registers[j], 1);
                applied++;
            }
        }
    }
    const AgcRegisterDefaultsGroup *igroups = agcRegisterDefaultsV8GetInternalGroups(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (igroups[i].space == kAgcRegisterDefaultSpaceCx && igroups[i].register_count > 0) {
            for (uint32_t j = 0; j < igroups[i].register_count; j++) {
                sceAgcCbSetCxRegistersDirect(cb,
                    (const AgcRegisterValue *)&igroups[i].registers[j], 1);
                applied++;
            }
        }
    }
    printf("[Dispatch] Applied %u CX register defaults (individual)\n", applied);
}

/* Helper to identify PGM_LO and PGM_HI register offsets across all stages:
 * PS: 0x008, VS: 0x048, GS: 0x088, ES: 0x0C8, HS: 0x108, LS: 0x148, CS: 0x20C
 */
static bool is_pgm_lo_off(uint32_t off) {
    return off == 0x008 || off == 0x048 || off == 0x088 ||
           off == 0x0C8 || off == 0x108 || off == 0x148 || off == 0x20C;
}

static bool is_pgm_hi_off(uint32_t off) {
    return off == 0x009 || off == 0x049 || off == 0x089 ||
           off == 0x0C9 || off == 0x109 || off == 0x149 || off == 0x20D;
}

/* Write shader SH registers (PGM_LO/HI patched with code address).
 * For VS shaders compiled by psbc, the SH registers are at ES stage offsets
 * (0x0C7-0x0CB). On RDNA2 without NGG, we remap these to VS stage offsets
 * (0x047-0x04B) so the shader runs as a real VS (vsEn=VsReal). */
static void write_shader_sh_regs(SceAgcCb *cb,
                                  const ParsedGraphicsShader *shader,
                                  void *code_addr) {
    /* Patch PGM_LO/HI with the shader code address */
    uint32_t pgm_lo_val = (uint32_t)((uintptr_t)code_addr >> 8);
    uint32_t pgm_hi_val = (uint32_t)((uintptr_t)code_addr >> 40);

    /* Find PGM_LO and PGM_HI offsets in the SH register block */
    uint32_t pgm_lo_off = 0, pgm_hi_off = 0;
    bool found_lo = false, found_hi = false;
    for (uint32_t i = 0; i < shader->num_sh_regs; i++) {
        uint32_t off = shader->sh_regs[i].offset;
        if (!found_lo && is_pgm_lo_off(off)) {
            pgm_lo_off = off;
            found_lo = true;
        }
        if (!found_hi && is_pgm_hi_off(off)) {
            pgm_hi_off = off;
            found_hi = true;
        }
    }

    if (found_lo) {
        printf("[Shader] Patched PGM_LO (0x%03X) = 0x%08X (code %p)\n",
               pgm_lo_off, pgm_lo_val, code_addr);
    } else {
        printf("[Shader] WARNING: PGM_LO not found in shader SH registers!\n");
    }

    /* Write all SH registers, patching PGM_LO/HI values.
     * On RDNA2, VS shaders compiled by psbc use ES stage offsets (0x0C8-0x0CB)
     * which is correct — the ES stage feeds NGG passthrough. */
    for (uint32_t i = 0; i < shader->num_sh_regs; i++) {
        uint32_t off = shader->sh_regs[i].offset;
        uint32_t val = shader->sh_regs[i].value;

        if (found_lo && off == pgm_lo_off) val = pgm_lo_val;
        if (found_hi && off == pgm_hi_off) val = pgm_hi_val;

        AgcRegisterValue reg = { off, val };
        sceAgcCbSetShRegistersDirect(cb, &reg, 1);
    }
}

/* Write shader CX registers (from the shader record's CX block) */
static void write_shader_cx_regs(SceAgcCb *cb,
                                  const ParsedGraphicsShader *shader) {
    if (shader->num_cx_regs == 0) return;

    /* Write CX registers as a contiguous block if possible, or one by one */
    /* The CX block may contain non-contiguous offsets, so write individually */
    for (uint32_t i = 0; i < shader->num_cx_regs; i++) {
        AgcRegisterValue reg = { shader->cx_regs[i].offset,
                                  shader->cx_regs[i].value };
        sceAgcCbSetCxRegistersDirect(cb, &reg, 1);
    }
}

/* Set up render target (CB_COLOR0 registers, 14 contiguous dwords) */
static void setup_render_target(SceAgcCb *cb, void *rt_addr,
                                 uint32_t width, uint32_t height) {
    /* Build the 14-dword CB_COLOR0 register block.
     * Registers 0x318-0x325 (14 contiguous CX registers):
     *   0x318: BASE      0x319: PITCH     0x31A: SLICE
     *   0x31B: VIEW      0x31C: INFO      0x31D: ATTRIB
     *   0x31E: DCC_CTRL  0x31F: CMASK_BASE 0x320: CMASK_SLICE
     *   0x321: FMASK_BASE 0x322: FMASK_SLICE
     *   0x323: CLEAR_WORD0  0x324: CLEAR_WORD1  0x325: DCC_BASE
     */
    uint32_t rt_regs[14];
    memset(rt_regs, 0, sizeof(rt_regs));

    /* reg 0: CB_COLOR0_BASE — base address >> 8 */
    rt_regs[0] = (uint32_t)((uintptr_t)rt_addr >> 8);

    /* reg 1: CB_COLOR0_PITCH — TILE_MAX (11 bits) + FMASK_TILE_MAX (11 bits)
     * For linear mode, each tile is 8 elements wide.
     * TILE_MAX = (pitch_elements / 8) - 1 */
    uint32_t tiles_per_row = width / 8;
    rt_regs[1] = (tiles_per_row - 1) & 0x7FF;  /* TILE_MAX (11 bits) */

    /* reg 2: CB_COLOR0_SLICE — TILE_MAX (22 bits)
     * For linear mode: total tiles = tiles_per_row * height
     * TILE_MAX = (tiles_per_row * height) - 1 */
    rt_regs[2] = ((tiles_per_row * height) - 1) & 0x3FFFFF;

    /* reg 3: CB_COLOR0_VIEW — no slice view */
    rt_regs[3] = 0;

    /* reg 4: CB_COLOR0_INFO — format + number type + swap
     * Format = COLOR_8_8_8_8 (0x02) at bits [6:2]
     * Number type = UNORM (0) at bits [10:8]
     * Swap = ALT (1) at bits [12:11] to match A8B8G8R8 display buffer */
    rt_regs[4] = CB_INFO_FORMAT(COLOR_8_8_8_8) |
                 CB_INFO_NUM_TYPE(SURF_NUMBER_UNORM) |
                 CB_INFO_SWAP(SURF_SWAP_ALT) |
                 (1u << 16);  /* CB_INFO_BLEND_BYPASS: bypass blend state */

    /* reg 5: CB_COLOR0_ATTRIB — tile mode + num_samples + num_fragments
     * TILE_MODE_INDEX (5 bits at [4:0]) = 31 (kAgcTileDisplay_LinearGeneral)
     *   This is CRITICAL — tile mode 0 is Depth_2DThin_64 (a depth tile mode),
     *   not linear! Using it for a color RT causes the CB to write to wrong
     *   addresses or not write at all.
     * NUM_SAMPLES (3 bits at [14:12]) = 0 (1 sample)
     * NUM_FRAGMENTS (2 bits at [16:15]) = 0 (1 fragment) */
    rt_regs[5] = 0x0000001Fu;  /* tile_mode_index = 31 (LinearGeneral) */

    /* reg 6: DCC_CONTROL — disabled */
    rt_regs[6] = 0;

    /* reg 7-13: CMask, FMask, clear words, DCC base — all 0 */
    /* Already zeroed by memset */

    /* Write 14 contiguous CX registers starting at CB_COLOR0_BASE (0x318) */
    uint32_t *cmd = agcCbAllocDwords(cb, 14 + 2);
    if (cmd) {
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 14 + 2);
        cmd[1] = AGC_REG_CB_COLOR0_BASE;  /* base offset */
        for (int i = 0; i < 14; i++) {
            cmd[2 + i] = rt_regs[i];
        }
    }

    /* CRITICAL: Set CB_COLOR0_BASE_EXT (0x390 CX space) for 64-bit high bits! */
    AgcRegisterValue base_ext = { AGC_REG_CB_COLOR0_BASE_EXT,
                                  (uint32_t)((uintptr_t)rt_addr >> 40) };
    sceAgcCbSetCxRegistersDirect(cb, &base_ext, 1);

    /* CRITICAL: Set CB_COLOR0_ATTRIB2 (0x3B0) for MIP0 dimensions!
     * ATTRIB2 packs MIP0_HEIGHT (bits [13:0]) and MIP0_WIDTH (bits [27:14]).
     * Without MIP0 dimensions, CB clips all writes to 0x0! */
    uint32_t attrib2_val = ((height - 1) & 0x3FFF) |
                           (((width - 1) & 0x3FFF) << 14);
    AgcRegisterValue attrib2 = { AGC_REG_CB_COLOR0_ATTRIB2, attrib2_val };
    sceAgcCbSetCxRegistersDirect(cb, &attrib2, 1);
    /* ATTRIB3 (0x3B8): MIP0_DEPTH (bits [12:0]) = 0 (1 slice) */
    AgcRegisterValue attrib3 = { AGC_REG_CB_COLOR0_ATTRIB3, 0 };
    sceAgcCbSetCxRegistersDirect(cb, &attrib3, 1);

    /* CRITICAL: Set CB_COLOR_CONTROL (0x202 CX space) to enable target 0 write & ROP3 copy!
     * Target write enable (bits 3:0) = 0xF (RT0 enabled).
     * ROP3 (bits 23:16) = 0xCC (copy src to dst). */
    AgcRegisterValue cb_color_ctrl = { AGC_REG_CB_COLOR_CONTROL, 0x00CC000Fu };
    sceAgcCbSetCxRegistersDirect(cb, &cb_color_ctrl, 1);

    printf("[RT] CB_COLOR0_BASE=0x%08x EXT=0x%08x PITCH=0x%08x SLICE=0x%08x INFO=0x%08x ATTRIB=0x%08x CTRL=0x%08x ATTRIB2=0x%08x\n",
           rt_regs[0], base_ext.value, rt_regs[1], rt_regs[2], rt_regs[4], rt_regs[5], cb_color_ctrl.value, attrib2_val);
}

/* Set up viewport (scale + offset + zmin/zmax) */
static void setup_viewport(SceAgcCb *cb, uint32_t width, uint32_t height) {
    /* Viewport: scale = (w/2, -h/2, 0.5), offset = (w/2, h/2, 0.5)
     * This maps clip space [-1, 1] to screen space [0, w] x [0, h] */
    float scale_x = (float)width * 0.5f;
    float scale_y = -(float)height * 0.5f;
    float scale_z = 0.5f;
    float offset_x = (float)width * 0.5f;
    float offset_y = (float)height * 0.5f;
    float offset_z = 0.5f;

    /* PA_CL_VPORT_XSCALE + 5 more (interleaved scale/offset) */
    uint32_t vp_regs[6];
    memcpy(&vp_regs[0], &scale_x, 4);
    memcpy(&vp_regs[1], &offset_x, 4);
    memcpy(&vp_regs[2], &scale_y, 4);
    memcpy(&vp_regs[3], &offset_y, 4);
    memcpy(&vp_regs[4], &scale_z, 4);
    memcpy(&vp_regs[5], &offset_z, 4);

    uint32_t *cmd = agcCbAllocDwords(cb, 6 + 2);
    if (cmd) {
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 6 + 2);
        cmd[1] = AGC_REG_PA_CL_VPORT_XSCALE;
        for (int i = 0; i < 6; i++) cmd[2 + i] = vp_regs[i];
    }

    /* PA_SC_VPORT_ZMIN_0 + ZMAX_0 (2 contiguous) */
    float zmin = 0.0f, zmax = 1.0f;
    uint32_t z_regs[2];
    memcpy(&z_regs[0], &zmin, 4);
    memcpy(&z_regs[1], &zmax, 4);

    cmd = agcCbAllocDwords(cb, 2 + 2);
    if (cmd) {
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 2 + 2);
        cmd[1] = AGC_REG_PA_SC_VPORT_ZMIN_0;
        cmd[2] = z_regs[0];
        cmd[3] = z_regs[1];
    }
}

/* Set up scissor (screen scissors, 2 dwords packed as int16 pairs) */
static void setup_scissor(SceAgcCb *cb, uint32_t width, uint32_t height) {
    /* PA_SC_SCREEN_SCISSOR_TL/BR — packed as (x16 | y16) */
    uint32_t sc_tl = 0;  /* left=0, top=0 */
    uint32_t sc_br = (width & 0xFFFF) | ((height & 0xFFFF) << 16);

    uint32_t *cmd = agcCbAllocDwords(cb, 2 + 2);
    if (cmd) {
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 2 + 2);
        cmd[1] = AGC_REG_PA_SC_SCREEN_SCISSOR_TL;
        cmd[2] = sc_tl;
        cmd[3] = sc_br;
    }
}

/* Set CB_TARGET_MASK (CX register) */
static void setup_target_mask(SceAgcCb *cb) {
    AgcRegisterValue mask = { AGC_REG_CB_TARGET_MASK, CB_TARGET_MASK_ALL };
    sceAgcCbSetCxRegistersDirect(cb, &mask, 1);
}

/* Set VGT_PRIMITIVE_TYPE (UC register) */
/* VGT_SHADER_STAGES_EN (CX 0x2D5) — RDNA2 geometry pipeline configuration.
 *
 * VGT_SHADER_STAGES_EN bit layout (from rpcsx Registers.hpp):
 *   bits [1:0] = lsEn  (LsStage: 0=Off, 1=On, 2=CsOn)
 *   bit  [2]   = hsEn
 *   bits [4:3] = esEn  (EsStage: 0=Off, 1=Ds, 2=Real)
 *   bit  [5]   = gsEn
 *   bits [7:6] = vsEn  (VsStage: 0=Real, 1=Ds, 2=Copy)
 *   bit  [8]   = dynamicHs
 *
 * For a simple VS+PS pipeline (no tessellation/geometry):
 *   vsEn = VsReal (0), esEn = EsOff (0), gsEn = 0, hsEn = 0, lsEn = 0
 *   → VGT_SHADER_STAGES_EN = 0
 *
 * NOTE: The previous code set bit 8 (0x100) thinking it was ES_EN, but bit 8
 * is actually dynamicHs. The real ES enable is at bits [4:3] = EsReal (2) = 0x10.
 * On RDNA2, the ES stage output must go through NGG, which requires a proper
 * NGG passthrough shader. Without NGG, use vsEn=VsReal with the shader at
 * VS PGM (0x048) instead.
 */
static void setup_shader_stages(SceAgcCb *cb) {
    /* On RDNA2 (GFX10.3), the geometry pipeline is NGG-only. The vertex
     * shader runs as the ES (export shader) stage, feeding NGG passthrough.
     * VGT_SHADER_STAGES_EN bit layout (from rpcsx Registers.hpp):
     *   bits [1:0] = lsEn  (0=Off)
     *   bit  [2]   = hsEn  (false)
     *   bits [4:3] = esEn  (2=EsReal) ← this is what we need
     *   bit  [5]   = gsEn  (false, NGG passthrough)
     *   bits [7:6] = vsEn  (0=VsReal, unused when esEn=EsReal)
     *   bit  [8]   = dynamicHs (false)
     * esEn=EsReal (2) = bits [4:3] = 10 = 0x10.
     * DO NOT set bit 8 (dynamicHs) or bit 15 — these caused a kernel panic. */
    AgcRegisterValue stages = { AGC_REG_VGT_SHADER_STAGES_EN, 0x00000010u };
    sceAgcCbSetCxRegistersDirect(cb, &stages, 1);
    printf("[VGT] VGT_SHADER_STAGES_EN = 0x10 (EsReal, NGG passthrough)\n");
}

/* ======================================================================== */
/* Main draw call dispatch                                                   */
/* ======================================================================== */

static bool dispatch_graphics(GraphicsTest *test,
                               const ParsedGraphicsShader *vs,
                               const ParsedGraphicsShader *ps) {
    /* Upload shader code to flexible memory pool */
    void *vs_code = upload_shader(vs->code, vs->code_size,
                                   test->compute_buffer, 0x0000);
    void *ps_code = upload_shader(ps->code, ps->code_size,
                                   test->compute_buffer, 0x1000);
    printf("VS code at %p (%zu bytes)\n", vs_code, vs->code_size);
    printf("PS code at %p (%zu bytes)\n", ps_code, ps->code_size);

    /* Render to flexible memory (GPU MMU-mapped).
     * The Color Buffer hardware writes to flexible memory reliably.
     * CPU will copy from flexible memory to garlic display buffer after draw. */
    void *rt_addr = (uint8_t *)test->compute_buffer + 0x10000;
    printf("Render target at %p (flexible memory)\n", rt_addr);

    /* Clear render target to black (CPU fill) */
    uint32_t *rt = (uint32_t *)rt_addr;
    for (uint32_t i = 0; i < test->width * test->height; i++) {
        rt[i] = 0xFF000000;  /* black with full alpha */
    }

    /* Build DCB */
    SceAgcCb cb;
    agcCbInit(&cb, cb_buffer, CB_BUFFER_DWORDS);

    /* 0. CONTEXT_CONTROL — notify CP to load context state.
     * Same as compute sample: opcode 0x28, 3 dwords. */
    uint32_t *cc = agcCbAllocDwords(&cb, 3);
    if (cc) {
        cc[0] = agcPm4Header3(0x28, 3);  /* CONTEXT_CONTROL */
        cc[1] = 0x80000000u;  /* LOAD_ENABLE_CONTEXT */
        cc[2] = 0x80000000u;
    }
    printf("[Dispatch] CONTEXT_CONTROL: load enable\n");

    /* 1. Apply FW 5.50 register defaults */
    apply_sh_defaults_graphics(&cb);
    apply_cx_defaults(&cb);

    /* 2. Set up render target */
    setup_render_target(&cb, rt_addr, test->width, test->height);

    /* 2b. Disable depth buffer — will be re-applied AFTER shader CX regs
     * because the PS CX block writes 0x00F (DB_DEPTH_INFO) and 0x203
     * (DB_SHADER_CONTROL) which re-enable depth testing and Z export. */
    /* (moved to after PS CX registers below) */

    /* 3. Set up viewport, scissor, target mask */
    setup_viewport(&cb, test->width, test->height);
    setup_scissor(&cb, test->width, test->height);
    setup_target_mask(&cb);

    /* 4. Enable ES shader stage (VGT_SHADER_STAGES_EN = 0x100) */
    setup_shader_stages(&cb);

    /* 5. Write VS shader SH + CX registers */
    write_shader_sh_regs(&cb, vs, vs_code);
    write_shader_cx_regs(&cb, vs);

    /* 6. Write PS shader SH + CX registers */
    write_shader_sh_regs(&cb, ps, ps_code);
    write_shader_cx_regs(&cb, ps);

    /* 6b. CRITICAL: Override depth/stencil registers AFTER shader CX blocks!
     * The PS CX block writes:
     *   0x00F (DB_DEPTH_INFO) = 0x0F — enables depth testing
     *   0x203 (DB_SHADER_CONTROL) = 0x10 — enables Z_EXPORT in PS
     * These re-enable depth even though we set DB_Z_INFO=0 earlier.
     * With no depth buffer bound, depth testing discards ALL fragments.
     * Must override AFTER the PS CX block to take effect. */
    AgcRegisterValue db_depth_info = { 0x00F, 0x00000000 };  /* DB_DEPTH_INFO = 0 */
    sceAgcCbSetCxRegistersDirect(&cb, &db_depth_info, 1);
    AgcRegisterValue db_z_info = { AGC_REG_DB_Z_INFO, 0 };     /* DB_Z_INFO = 0 */
    sceAgcCbSetCxRegistersDirect(&cb, &db_z_info, 1);
    AgcRegisterValue db_stencil_info = { 0x011, 0 };           /* DB_STENCIL_INFO = 0 */
    sceAgcCbSetCxRegistersDirect(&cb, &db_stencil_info, 1);
    AgcRegisterValue db_shader_ctrl = { AGC_REG_DB_SHADER_CONTROL, 0x00000000 };
    sceAgcCbSetCxRegistersDirect(&cb, &db_shader_ctrl, 1);     /* DB_SHADER_CONTROL = 0 (no Z_EXPORT) */
    AgcRegisterValue db_depth_ctrl = { AGC_REG_DB_DEPTH_CONTROL, 0x00000000 };
    sceAgcCbSetCxRegistersDirect(&cb, &db_depth_ctrl, 1);      /* DB_DEPTH_CONTROL = 0 (no Z test) */
    printf("[DB] DB_DEPTH_INFO=0, DB_Z_INFO=0, DB_STENCIL_INFO=0, DB_SHADER_CONTROL=0, DB_DEPTH_CONTROL=0\n");

    /* 6c. Set SPI_SHADER_COL_FORMAT and SPI_SHADER_POS_FORMAT — NOT in
     * shader record or defaults! Without these, the PS doesn't export color
     * and the PA can't process vertex positions.
     * SPI_SHADER_POS_FORMAT (0x1C3): 1 = 4_32_32_32_32 (vec4 position)
     * SPI_SHADER_Z_FORMAT (0x1C4): 0 = no Z export
     * SPI_SHADER_COL_FORMAT (0x1C5): 4 = hardware-capture matched value */
    AgcRegisterValue pos_format = { AGC_REG_SPI_SHADER_POS_FORMAT, 0x1 };
    sceAgcCbSetCxRegistersDirect(&cb, &pos_format, 1);
    AgcRegisterValue z_format = { AGC_REG_SPI_SHADER_Z_FORMAT, 0 };
    sceAgcCbSetCxRegistersDirect(&cb, &z_format, 1);
    /* Fine-tune Primitive Assembler and Clipper registers:
     * - PA_CL_VS_OUT_CNTL (0x207): Set VS_OUT_PARAM_COUNT=1 (0x00010000) so PA expects 1 param export (v_color).
     *   The shader CX block had 0x01400300 (expecting 64 params!), causing PA to stall.
     * - PA_CL_CLIP_CNTL (0x204): Set CLIP_DISABLE=1 (0x00010000, bit 16) and UCPs=0 to bypass clipping.
     * - PA_SU_SC_MODE_CNTL (0x205): Set 0 (no front/back face culling).
     * - SPI_VS_OUT_CONFIG (0x1B1): Set 0 (VS_EXPORT_COUNT=0 for 1 export param).
     */
    AgcRegisterValue vs_out_cntl = { AGC_REG_PA_CL_VS_OUT_CNTL, 0x00000000 };  /* No clip/cull/point_size — prevent cull distance from killing primitives */
    sceAgcCbSetCxRegistersDirect(&cb, &vs_out_cntl, 1);
    AgcRegisterValue clip_cntl = { AGC_REG_PA_CL_CLIP_CNTL, 0x00000000 };  /* Clipping enabled (default) — CLIP_DISABLE may break NGG rasterization */
    sceAgcCbSetCxRegistersDirect(&cb, &clip_cntl, 1);
    AgcRegisterValue sc_mode = { AGC_REG_PA_SU_SC_MODE_CNTL, 0x00000000 };
    sceAgcCbSetCxRegistersDirect(&cb, &sc_mode, 1);
    AgcRegisterValue vs_out_cfg = { AGC_REG_SPI_VS_OUT_CONFIG, 0x00000000 };
    sceAgcCbSetCxRegistersDirect(&cb, &vs_out_cfg, 1);
    AgcRegisterValue col_format = { AGC_REG_SPI_SHADER_COL_FORMAT, 0x1 };  /* 1 = 8_8_8_8, matches CB_COLOR0_INFO format */
    sceAgcCbSetCxRegistersDirect(&cb, &col_format, 1);
    AgcRegisterValue cb_shader_mask = { AGC_REG_CB_SHADER_MASK, 0x0F };
    sceAgcCbSetCxRegistersDirect(&cb, &cb_shader_mask, 1);
    AgcRegisterValue input_ctl = { AGC_REG_SPI_PS_INPUT_CNTL_0, 0x00000000 };  /* OFFSET=0: PS input 0 reads from VS param export 0 (v_color) */
    sceAgcCbSetCxRegistersDirect(&cb, &input_ctl, 1);
    printf("[PA/SPI] VS_OUT_CNTL=0, CLIP_CNTL=0, SC_MODE=0, COL_FORMAT=1 (8_8_8_8), PS_INPUT_CNTL_0=0x00000000 (OFFSET=0)\n");

    /* 7. Set primitive type (TRILIST) in both UCONFIG and CONTEXT spaces */
    AgcRegisterValue prim = { AGC_REG_VGT_PRIMITIVE_TYPE, VGT_PT_TRILIST };
    sceAgcCbSetUcRegistersDirect(&cb, &prim, 1);
    sceAgcCbSetCxRegistersDirect(&cb, &prim, 1);

    /* 7b. Set VGT_GS_OUT_PRIM_TYPE = TRILIST (4) to match input prim type.
     * In NGG passthrough mode, the output prim type must match the input.
     * Default is 2 (triangle strip) which is wrong for triangle list. */
    AgcRegisterValue gs_out_prim = { AGC_REG_VGT_GS_OUT_PRIM_TYPE, VGT_PT_TRILIST };
    sceAgcCbSetCxRegistersDirect(&cb, &gs_out_prim, 1);

    /* 7c. Set GE_CNTL — controls geometry engine batching for NGG.
     * PRIM_GRP_SIZE (bits [8:0]) = 256: primitives per group
     * VERT_GRP_SIZE (bits [17:9]) = 256: vertices per group
     * Without this, the geometry engine may not batch any primitives. */
    uint32_t ge_cntl_val = (256u & 0x1FF) | ((256u & 0x1FF) << 9);
    AgcRegisterValue ge_cntl = { AGC_REG_GE_CNTL, ge_cntl_val };
    sceAgcCbSetUcRegistersDirect(&cb, &ge_cntl, 1);
    printf("[GE] VGT_GS_OUT_PRIM_TYPE=4 (TRILIST), GE_CNTL=0x%08X (PRIM_GRP=256, VERT_GRP=256)\n", ge_cntl_val);

    /* 8. Draw — IT_DRAW_INDEX_AUTO with 3 vertices */
    sceAgcDcbDrawIndexAuto(&cb, 3, 0);
    printf("[Draw] DrawIndexAuto(3)\n");

    /* 8b. WRITE_DATA marker — verify GPU is alive after draw.
     * Write a marker value to CB_BUFFER + 0x1000 (flexible memory). */
    uint64_t marker_target = (uint64_t)(uintptr_t)cb_buffer + 0x1000;
    uint32_t *wd = agcCbAllocDwords(&cb, 5);
    if (wd) {
        wd[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 5);
        wd[1] = (2u << 0) | (0u << 2) | (1u << 8);  /* dst=memory, verify=1 */
        wd[2] = (uint32_t)marker_target;
        wd[3] = (uint32_t)(marker_target >> 32);
        wd[4] = 0xDEADCAFEu;
    }
    printf("[Draw] WRITE_DATA marker at 0x%llx\n", (unsigned long long)marker_target);

    /* 9. ACQUIRE_MEM — flush GPU caches */
    uint32_t *am = agcCbAllocDwords(&cb, 6);
    if (am) {
        am[0] = agcPm4Header3(AGC_PM4_OP_ACQUIRE_MEM, 6);
        am[1] = 0x2ec47fc0u;  /* coher_cntl */
        am[2] = 0xFFFFFFFFu;
        am[3] = 0;
        am[4] = 0;
        am[5] = 0;
    }

    /* Trailing NOP */
    sceAgcCbNop(&cb, 2);

    /* Submit DCB */
    AgcCommandBufferSubmit submit;
    submit.command_address = (uintptr_t)cb_buffer;
    submit.dword_count = agcCbUsedDwords(&cb);
    submit.reserved = 0;

    printf("[Draw] DCB: %u dwords, submitting...\n", submit.dword_count);
    int32_t err = sceAgcDriverSubmitDcb(&submit);
    printf("[Draw] SubmitDcb: 0x%08x (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) return false;

    /* Wait for GPU to finish */
    printf("[Draw] Waiting 200ms for GPU...\n");
    sceKernelUsleep(200000);

    /* Check WRITE_DATA marker — if present, GPU is alive after draw */
    uint32_t *marker = (uint32_t *)((uintptr_t)cb_buffer + 0x1000);
    printf("[Marker] WRITE_DATA marker = 0x%08x (expected 0xDEADCAFE)\n", *marker);

    /* Verify pixels in flexible render target */
    uint32_t *rt_buf = (uint32_t *)rt_addr;
    printf("[Readback] Sample pixels from flexible render target:\n");
    uint32_t sample_indices[] = {0, 1, 100, 1000, 1920, 10000, 100000, 500000, 1000000, 2073599};
    for (int i = 0; i < (int)(sizeof(sample_indices)/sizeof(sample_indices[0])); i++) {
        uint32_t idx = sample_indices[i];
        if (idx >= test->width * test->height) continue;
        printf("  pixel[%u] = 0x%08x\n", idx, rt_buf[idx]);
    }

    /* Count non-black pixels to see if GPU rendered anything */
    uint32_t non_black = 0;
    for (uint32_t i = 0; i < test->width * test->height; i++) {
        if (rt_buf[i] != 0xFF000000) non_black++;
    }
    printf("[Readback] Non-black pixels: %u / %u\n", non_black, test->width * test->height);

    /* Copy rendered image from flexible memory to garlic display buffer */
    memcpy(test->buffers[0], rt_addr, test->width * test->height * 4);
    printf("[Copy] Copied flexible render target to garlic display buffer 0\n");

    return true;
}

/* ======================================================================== */
/* Flip helper                                                               */
/* ======================================================================== */

static void wait_for_flip(GraphicsTest *test) {
    sceVideoOutSubmitFlip(test->handle, 0, SCE_VIDEO_OUT_FLIP_MODE_VSYNC, 0);
    SceKernelEvent events[1];
    for (;;) {
        int ret = sceKernelWaitEqueue(test->flipqueue, events, 1, NULL, NULL);
        if (ret == 0) break;
    }
}

/* ======================================================================== */
/* Main                                                                      */
/* ======================================================================== */

int main(void) {
    GraphicsTest test = { .handle = -1, .direct_memory = -1 };

    printf("=== openagc Graphics Draw Call Test ===\n");

    /* Step 0: GPU credentials */
    printf("\n--- Step 0: GPU credential bypass ---\n");
    int cred_err = set_gpu_credentials();
    printf("GPU credentials: %s\n", cred_err == 0 ? "OK" : "FAILED");
    if (cred_err != 0) return 1;

    /* Step 1: AGC init */
    printf("\n--- Step 1: AGC initialization ---\n");
    if (!init_agc()) return 1;

    /* Step 2: VideoOut init */
    printf("\n--- Step 2: VideoOut initialization ---\n");
    if (!init_videoout(&test)) return 1;

    /* Step 3: Parse shaders */
    printf("\n--- Step 3: Shader loading ---\n");
    ParsedGraphicsShader vs, ps;
    if (!parse_graphics_shader(&vs, triangle_vert_data, sizeof(triangle_vert_data), "VS")) {
        printf("FATAL: VS parse failed\n");
        return 1;
    }
    if (!parse_graphics_shader(&ps, triangle_frag_data, sizeof(triangle_frag_data), "PS")) {
        printf("FATAL: PS parse failed\n");
        return 1;
    }

    /* Step 4: Draw call */
    printf("\n--- Step 4: Graphics draw call ---\n");
    if (!dispatch_graphics(&test, &vs, &ps)) {
        printf("FATAL: draw call failed\n");
        return 1;
    }

    /* Step 5: Flip display */
    printf("\n--- Step 5: Display flip ---\n");
    wait_for_flip(&test);
    printf("Flipped! Triangle should be visible on display.\n");

    /* Keep displaying for a few seconds */
    for (int i = 0; i < 60; i++) {
        sceKernelUsleep(100000);  /* 100ms */
    }

    printf("\nDone.\n");
    return 0;
}
