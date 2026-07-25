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

#define CB_BUFFER_DWORDS   4096

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
    size_t cb_size = CB_BUFFER_DWORDS * 4;
    void *cb_addr = NULL;
    int cb_ret = sceKernelMapNamedSystemFlexibleMemory(
        &cb_addr, cb_size, 0x33, 0, "agc_graphics_cb");
    if (cb_ret != 0 || !cb_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for CB: %d\n", cb_ret);
        return false;
    }
    cb_buffer = (uint32_t *)cb_addr;

    /* 16MB flexible memory pool for render target + shader code */
    size_t pool_size = 16 * 1024 * 1024;
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

/* Apply FW 5.50 SH register defaults (graphics type — bit 0 = 0) */
static void apply_sh_defaults_graphics(SceAgcCb *cb) {
    uint32_t count = 0;
    const AgcRegisterDefaultsGroup *pgroups = agcRegisterDefaultsV8GetPrimaryGroups(&count);
    uint32_t applied = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (pgroups[i].space == kAgcRegisterDefaultSpaceSh && pgroups[i].register_count > 0) {
            uint32_t *cmd = sceAgcCbSetShRegistersDirect(cb,
                (const AgcRegisterValue *)pgroups[i].registers,
                pgroups[i].register_count);
            if (cmd) {
                /* Graphics shader type: bit 0 = 0 (no need to set it) */
                applied++;
            }
        }
    }
    const AgcRegisterDefaultsGroup *igroups = agcRegisterDefaultsV8GetInternalGroups(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (igroups[i].space == kAgcRegisterDefaultSpaceSh && igroups[i].register_count > 0) {
            uint32_t *cmd = sceAgcCbSetShRegistersDirect(cb,
                (const AgcRegisterValue *)igroups[i].registers,
                igroups[i].register_count);
            if (cmd) {
                applied++;
            }
        }
    }
    printf("[Dispatch] Applied %u SH register default groups (graphics)\n", applied);
}

/* Apply FW 5.50 CX register defaults */
static void apply_cx_defaults(SceAgcCb *cb) {
    uint32_t count = 0;
    const AgcRegisterDefaultsGroup *pgroups = agcRegisterDefaultsV8GetPrimaryGroups(&count);
    uint32_t applied = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (pgroups[i].space == kAgcRegisterDefaultSpaceCx && pgroups[i].register_count > 0) {
            uint32_t *cmd = sceAgcCbSetCxRegistersDirect(cb,
                (const AgcRegisterValue *)pgroups[i].registers,
                pgroups[i].register_count);
            if (cmd) applied++;
        }
    }
    const AgcRegisterDefaultsGroup *igroups = agcRegisterDefaultsV8GetInternalGroups(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (igroups[i].space == kAgcRegisterDefaultSpaceCx && igroups[i].register_count > 0) {
            uint32_t *cmd = sceAgcCbSetCxRegistersDirect(cb,
                (const AgcRegisterValue *)igroups[i].registers,
                igroups[i].register_count);
            if (cmd) applied++;
        }
    }
    printf("[Dispatch] Applied %u CX register default groups\n", applied);
}

/* Write shader SH registers (PGM_LO/HI patched with code address) */
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
        /* PGM_LO is the first register with value 0 that's at a PGM_LO offset */
        if (!found_lo) {
            /* VS uses ES: 0x0C8, PS uses 0x008 */
            if (off == 0x0C8 || off == 0x008) {
                pgm_lo_off = off;
                found_lo = true;
            }
        }
        if (!found_hi) {
            if (off == 0x0C9 || off == 0x009) {
                pgm_hi_off = off;
                found_hi = true;
            }
        }
    }

    /* Write all SH registers, patching PGM_LO/HI values */
    for (uint32_t i = 0; i < shader->num_sh_regs; i++) {
        uint32_t off = shader->sh_regs[i].offset;
        uint32_t val = shader->sh_regs[i].value;

        if (found_lo && off == pgm_lo_off) val = pgm_lo_val;
        if (found_hi && off == pgm_hi_off) val = pgm_hi_val;

        AgcRegisterValue reg = { off, val };
        uint32_t *cmd = sceAgcCbSetShRegistersDirect(cb, &reg, 1);
        /* Graphics: bit 0 = 0 (no need to set it) */
        (void)cmd;
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
     * Layout matches GnmRenderTarget registers 0-13. */
    uint32_t rt_regs[14];
    memset(rt_regs, 0, sizeof(rt_regs));

    /* reg 0: CB_COLOR0_BASE — base address >> 8 */
    rt_regs[0] = (uint32_t)((uintptr_t)rt_addr >> 8);

    /* reg 1: pitch — tilemax = pitch_pixels - 1 (for linear, pitch in tiles) */
    /* For linear mode, this is (pitch_pixels / 8 - 1) for 8-pixel tiles.
     * Actually for the AGC/GNM model, pitch.tilemax = (pitch_pixels - 1).
     * But for linear surfaces, the pitch is in elements. */
    rt_regs[1] = (width - 1) & 0x7FF;  /* tilemax (11 bits) */

    /* reg 2: slice — tilemax = (height * pitch / tile_size) - 1 */
    rt_regs[2] = ((height * width / 64) - 1) & 0x3FFFFF;  /* slice tilemax (22 bits) */

    /* reg 3: view — no slice view */
    rt_regs[3] = 0;

    /* reg 4: CB_COLOR0_INFO — format + number type + swap
     * Format = COLOR_8_8_8_8 (0x02) at bits [6:2]
     * Number type = UNORM (0) at bits [10:8]
     * Swap = ALT (1) at bits [12:11] to match A8B8G8R8 display buffer */
    rt_regs[4] = CB_INFO_FORMAT(COLOR_8_8_8_8) |
                 CB_INFO_NUM_TYPE(SURF_NUMBER_UNORM) |
                 CB_INFO_SWAP(SURF_SWAP_ALT);

    /* reg 5: CB_COLOR0_ATTRIB — tile mode = linear (0), num_samples = 1, num_fragments = 1 */
    rt_regs[5] = 0x00000000;  /* tilemode_index=0 (linear), fmask_tilemode=0, num_samples=1 */
    /* Actually num_samples is at bits [12:10], value 1 means 1 sample */
    rt_regs[5] = (1u << 10);  /* num_samples = 1 */

    /* reg 6: DCC_CONTROL — disabled */
    rt_regs[6] = 0;

    /* reg 7-13: CMask, FMask, clear words, DCC base — all 0 */
    /* Already zeroed by memset */

    /* Write 14 contiguous CX registers starting at CB_COLOR0_BASE (0x318) */
    /* Build SET_CONTEXT_REG packet manually for 14 contiguous registers */
    uint32_t *cmd = agcCbAllocDwords(cb, 14 + 2);
    if (cmd) {
        cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_CONTEXT_REG, 14 + 2);
        cmd[1] = AGC_REG_CB_COLOR0_BASE;  /* base offset */
        for (int i = 0; i < 14; i++) {
            cmd[2 + i] = rt_regs[i];
        }
    }
    printf("[RT] CB_COLOR0_BASE=0x%08x INFO=0x%08x ATTRIB=0x%08x\n",
           rt_regs[0], rt_regs[4], rt_regs[5]);
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
static void setup_primitive_type(SceAgcCb *cb) {
    AgcRegisterValue prim = { AGC_REG_VGT_PRIMITIVE_TYPE, VGT_PT_TRILIST };
    sceAgcCbSetUcRegistersDirect(cb, &prim, 1);
}

/* Set VGT_SHADER_STAGES_EN — enable ES stage as VS */
static void setup_shader_stages(SceAgcCb *cb) {
    /* For VS+PS without tessellation: ES stage runs the vertex shader.
     * VGT_SHADER_STAGES_EN at 0x2D5 (CX space).
     * ES_EN = 1 at bits [11:8] enables ES as the vertex shader. */
    AgcRegisterValue stages = { AGC_REG_VGT_SHADER_STAGES_EN,
                                 VGT_SHADER_STAGES_ES_EN };
    sceAgcCbSetCxRegistersDirect(cb, &stages, 1);
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

    /* Clear render target to black (CPU fill) */
    uint32_t *rt = (uint32_t *)test->render_target;
    for (uint32_t i = 0; i < test->width * test->height; i++) {
        rt[i] = 0xFF000000;  /* black with full alpha */
    }

    /* Build DCB */
    SceAgcCb cb;
    agcCbInit(&cb, cb_buffer, CB_BUFFER_DWORDS);

    /* 1. Apply FW 5.50 register defaults */
    apply_sh_defaults_graphics(&cb);
    apply_cx_defaults(&cb);

    /* 2. Set up render target */
    setup_render_target(&cb, test->render_target, test->width, test->height);

    /* 3. Set up viewport, scissor, target mask */
    setup_viewport(&cb, test->width, test->height);
    setup_scissor(&cb, test->width, test->height);
    setup_target_mask(&cb);

    /* 4. Set up shader stages (ES as VS) */
    setup_shader_stages(&cb);

    /* 5. Write VS shader SH + CX registers */
    write_shader_sh_regs(&cb, vs, vs_code);
    write_shader_cx_regs(&cb, vs);

    /* 6. Write PS shader SH + CX registers */
    write_shader_sh_regs(&cb, ps, ps_code);
    write_shader_cx_regs(&cb, ps);

    /* 7. Set primitive type (TRILIST) */
    setup_primitive_type(&cb);

    /* 8. Draw — IT_DRAW_INDEX_AUTO with 3 vertices */
    sceAgcDcbDrawIndexAuto(&cb, 3, 0);
    printf("[Draw] DrawIndexAuto(3)\n");

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

    /* 10. Copy render target (flexible) to display buffer (garlic) */
    printf("[Draw] Copying render target to display buffer...\n");
    memcpy(test->buffers[0], test->render_target,
           (size_t)test->width * test->height * BYTES_PER_PIXEL);

    /* Verify a few pixels */
    uint32_t *buf0 = (uint32_t *)test->buffers[0];
    printf("[Readback] Sample pixels from display buffer:\n");
    uint32_t sample_indices[] = {0, 1, 100, 1000, 1920, 10000, 100000, 500000, 1000000, 2073599};
    for (int i = 0; i < (int)(sizeof(sample_indices)/sizeof(sample_indices[0])); i++) {
        uint32_t idx = sample_indices[i];
        if (idx >= test->width * test->height) continue;
        printf("  pixel[%u] = 0x%08x\n", idx, buf0[idx]);
    }

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
