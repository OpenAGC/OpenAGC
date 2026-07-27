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
#include "agc_registers.h"
#include "agc_shader.h"
#include "agc_graphics.h"
#include "agc_texture.h"
#include "agc_cb.h"
#include "agc_pm4.h"
#include "agc_error.h"

/* GPU credential bypass */
#include "gpu_credentials.h"

#include <ps5/kernel.h>

/* Embedded shader binaries */
#ifndef AGC_NGG_AMPLIFY
#define AGC_NGG_AMPLIFY 0
#endif
#ifndef AGC_NGG_INPUT_LINES
#define AGC_NGG_INPUT_LINES 0
#endif
#ifndef AGC_NGG_INVOCATIONS
#define AGC_NGG_INVOCATIONS 0
#endif
#if (AGC_NGG_AMPLIFY + AGC_NGG_INPUT_LINES + AGC_NGG_INVOCATIONS) > 1
#error "select only one NGG geometry variant"
#endif
#ifndef AGC_TESSELLATION
#define AGC_TESSELLATION 0
#endif

#ifndef AGC_DEPTH_VALIDATION
#define AGC_DEPTH_VALIDATION 0
#endif

#ifndef AGC_STENCIL_VALIDATION
#define AGC_STENCIL_VALIDATION 0
#endif

#ifndef AGC_MSAA_VALIDATION
#define AGC_MSAA_VALIDATION 0
#endif

#if AGC_STENCIL_VALIDATION && !AGC_DEPTH_VALIDATION
#error "stencil validation requires depth validation"
#endif

#if AGC_MSAA_VALIDATION && !AGC_DEPTH_VALIDATION
#error "MSAA validation requires depth validation"
#endif

#if AGC_MSAA_VALIDATION && AGC_STENCIL_VALIDATION
#error "the isolated MSAA gate keeps stencil disabled"
#endif

#if AGC_DEPTH_VALIDATION && AGC_TESSELLATION
#error "depth validation uses the baseline NGG path"
#endif

#ifndef AGC_TESS_GEOMETRY
#define AGC_TESS_GEOMETRY 0
#endif

#ifndef AGC_TESS_GEOMETRY_INVOCATIONS
#define AGC_TESS_GEOMETRY_INVOCATIONS 0
#endif

#ifndef AGC_TESS_GEOMETRY_LINES
#define AGC_TESS_GEOMETRY_LINES 0
#endif

#if (AGC_TESS_GEOMETRY_INVOCATIONS + AGC_TESS_GEOMETRY_LINES) > 1
#error "select only one tessellation geometry variant"
#endif

#if (AGC_TESS_GEOMETRY_INVOCATIONS || AGC_TESS_GEOMETRY_LINES) && \
    !AGC_TESS_GEOMETRY
#error "tessellation geometry variants require AGC_TESS_GEOMETRY"
#endif

#ifndef AGC_TESS_DISTRIBUTION_MODE
#define AGC_TESS_DISTRIBUTION_MODE 0u
#endif

#if AGC_DEPTH_VALIDATION
#include "shaders/depth_triangle_ngg_front_sb.h"
#include "shaders/depth_triangle_ngg_back_sb.h"
#define NGG_FRONT_DATA depth_triangle_ngg_front_data
#define NGG_BACK_DATA depth_triangle_ngg_back_data
#define NGG_DRAW_VERTEX_COUNT 3u
#define NGG_INPUT_PRIMITIVE_TYPE 4u
#elif AGC_TESSELLATION
#include "shaders/triangle_tess_hs_front_sb.h"
#include "shaders/triangle_tess_hs_back_sb.h"
#if AGC_TESS_GEOMETRY
#if AGC_TESS_GEOMETRY_INVOCATIONS
#include "shaders/triangle_tess_invocations_gs_front_sb.h"
#include "shaders/triangle_tess_invocations_gs_back_sb.h"
#define NGG_FRONT_DATA triangle_tess_invocations_gs_front_data
#define NGG_BACK_DATA triangle_tess_invocations_gs_back_data
#elif AGC_TESS_GEOMETRY_LINES
#include "shaders/triangle_tess_lines_gs_front_sb.h"
#include "shaders/triangle_tess_lines_gs_back_sb.h"
#define NGG_FRONT_DATA triangle_tess_lines_gs_front_data
#define NGG_BACK_DATA triangle_tess_lines_gs_back_data
#else
#include "shaders/triangle_tess_gs_front_sb.h"
#include "shaders/triangle_tess_gs_back_sb.h"
#define NGG_FRONT_DATA triangle_tess_gs_front_data
#define NGG_BACK_DATA triangle_tess_gs_back_data
#endif
#else
#include "shaders/triangle_tess_es_front_sb.h"
#include "shaders/triangle_tess_es_back_sb.h"
#define NGG_FRONT_DATA triangle_tess_es_front_data
#define NGG_BACK_DATA triangle_tess_es_back_data
#endif
#define NGG_DRAW_VERTEX_COUNT 3u
#define NGG_INPUT_PRIMITIVE_TYPE 9u
#elif AGC_NGG_INPUT_LINES
#include "shaders/triangle_ngg_lines_front_sb.h"
#include "shaders/triangle_ngg_lines_back_sb.h"
#define NGG_FRONT_DATA triangle_ngg_lines_front_data
#define NGG_BACK_DATA triangle_ngg_lines_back_data
#define NGG_DRAW_VERTEX_COUNT 2u
#define NGG_INPUT_PRIMITIVE_TYPE 2u
#elif AGC_NGG_INVOCATIONS
#include "shaders/triangle_ngg_invocations_front_sb.h"
#include "shaders/triangle_ngg_invocations_back_sb.h"
#define NGG_FRONT_DATA triangle_ngg_invocations_front_data
#define NGG_BACK_DATA triangle_ngg_invocations_back_data
#define NGG_DRAW_VERTEX_COUNT 3u
#define NGG_INPUT_PRIMITIVE_TYPE 4u
#elif AGC_NGG_AMPLIFY
#include "shaders/triangle_ngg_amplify_front_sb.h"
#include "shaders/triangle_ngg_amplify_back_sb.h"
#define NGG_FRONT_DATA triangle_ngg_amplify_front_data
#define NGG_BACK_DATA triangle_ngg_amplify_back_data
#define NGG_DRAW_VERTEX_COUNT 3u
#define NGG_INPUT_PRIMITIVE_TYPE 4u
#else
#include "shaders/triangle_ngg_front_sb.h"
#include "shaders/triangle_ngg_back_sb.h"
#define NGG_FRONT_DATA triangle_ngg_front_data
#define NGG_BACK_DATA triangle_ngg_back_data
#define NGG_DRAW_VERTEX_COUNT 3u
#define NGG_INPUT_PRIMITIVE_TYPE 4u
#endif
#if AGC_DEPTH_VALIDATION
#include "shaders/depth_triangle_frag_sb.h"
#define FRAGMENT_DATA depth_triangle_frag_data
#elif AGC_NGG_INPUT_LINES || AGC_TESS_GEOMETRY_LINES
#include "shaders/triangle_line_frag_sb.h"
#define FRAGMENT_DATA triangle_line_frag_data
#else
#include "shaders/triangle_frag_sb.h"
#define FRAGMENT_DATA triangle_frag_data
#endif
#if AGC_MSAA_VALIDATION
#include "shaders/depth_resolve_frag_sb.h"
#endif

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
    int memoryType, off_t *directMemoryStart);
int sceKernelMapDirectMemory(
    void **virtualAddress, size_t length, int protection, int flags,
    off_t directMemoryStart, size_t alignment);
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

#define STANDARD_DCB_CAPACITY_BYTES 0x4000u
#define DCB_MARKER_OFFSET  0x7000u
#define DEPTH_DCB_CAPACITY_BYTES DCB_MARKER_OFFSET
#define DCB_MAPPING_BYTES  0x10000u
#define DCB_SECOND_OFFSET  0x8000u
#define VERTEX_DATA_OFFSET 0x8000u
#define VERTEX_DESC_OFFSET 0x9000u
#define INDEX_DATA_OFFSET  0xA000u
#define TEXTURE_DATA_OFFSET 0xB000u
#define TEXTURE_DESC_OFFSET 0xC000u
#define INDEX_TYPE_16      0u
#define DEPTH_SWIZZLE_64KB_Z_X AGC_GFX1013_SWIZZLE_64KB_Z_X
#define DEPTH_HTILE_PROVISIONAL_PIPE_COUNT 8u

#if AGC_TESSELLATION
#define GRAPHICS_POOL_PREFIX 0x30000u
#define TESS_OFFCHIP_OFFSET  0x10000u
#define TESS_FACTOR_OFFSET   0x18000u
#define TESS_RING_TABLE_OFFSET 0x28000u
#else
#define GRAPHICS_POOL_PREFIX 0x10000u
#endif

#define TEXTURE_WIDTH       2u
#define TEXTURE_HEIGHT      2u
#define GFX10_FORMAT_RGBA8_UNORM 56u
#define GFX10_SQ_RSRC_IMG_2D     9u
#define DIAGNOSTIC_CLEAR_COLOR    0xFF202020u
#define FP16_TARGET_WIDTH          1536u
#define FP16_TARGET_HEIGHT         1536u
#define FP16_PREVIEW_DIVISOR       2u
#define FP16_PREVIEW_FRAMES        1800u
#define FP16_CLEAR_SENTINEL        UINT64_C(0x3555355535553555)

#ifndef AGC_VALIDATE_RGBA8_REFERENCE
#define AGC_VALIDATE_RGBA8_REFERENCE 0
#endif

/* RADV's static GFX10 VBO descriptor word 3: identity DST_SEL,
 * FORMAT=32_UINT, RESOURCE_LEVEL=1, OOB_SELECT=structured. Attribute formats
 * and byte offsets are encoded in the shader's typed buffer loads. */
#define GFX10_VBO_DESC_WORD3 0x11014FACu

/* VGT primitive types */
#define VGT_PT_TRILIST         0x04u  /* 4 = triangle list */

/* Shader type constants */
#define AGC_SHADER_TYPE_VS     3
#define AGC_SHADER_TYPE_PS     1

/* GFX10.3 register bits from Linux gc_10_3_0_sh_mask.h. */
#define GFX10_SPI_PS_IN_CONTROL_PS_W32_EN       0x00008000u
#define GFX10_VGT_SHADER_STAGES_EN_GS_W32_EN    0x00400000u

/* ======================================================================== */
/* Types                                                                     */
/* ======================================================================== */

typedef struct {
    float position[2];
    float color[3];
} GraphicsVertex;

typedef struct {
    float position[3];
    float color[3];
} GraphicsDepthVertex;

_Static_assert(sizeof(GraphicsVertex) == 20,
    "interleaved graphics vertex must have a 20-byte stride");
_Static_assert(sizeof(GraphicsDepthVertex) == 24,
    "depth validation vertex must have a 24-byte stride");

typedef struct {
    int32_t handle;
    off_t direct_memory;
    void *mapped;
    size_t mapped_size;
    uint8_t *buffers[BUFFER_COUNT];
    void *compute_buffer;   /* Flexible memory pool for RT + shader code */
    void *render_target;    /* Points into compute_buffer */
    void *msaa_color_surface; /* Optional 4x 64KB_R_X color image */
    size_t msaa_color_surface_size;
    void *depth_surface;    /* Optional uncompressed D32 validation image */
    size_t depth_surface_size;
    void *stencil_surface;  /* Optional separate S8 validation image */
    size_t stencil_surface_size;
    void *htile_surface;    /* Reserved metadata; disabled in the D32 gate */
    size_t htile_surface_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    size_t buffer_stride;
    SceKernelEqueue flipqueue;
} GraphicsTest;

typedef struct {
    AgcShaderRecord relocated;
    const AgcShaderRecord *record;
    const uint8_t *code;
    size_t code_size;
    const AgcRegisterValue *sh_regs;
    uint32_t num_sh_regs;
    const AgcRegisterValue *cx_regs;
    uint32_t num_cx_regs;
    const AgcShaderSpecials *specials;
    const AgcShaderSemantic *input_semantics;
    uint32_t num_input_semantics;
    const AgcShaderSemantic *output_semantics;
    uint32_t num_output_semantics;
} ParsedGraphicsShader;

typedef struct {
    void *address;
    uint32_t width;
    uint32_t height;
    uint32_t color_format;
    uint32_t number_type;
    uint32_t component_swap;
    bool fp16;
    const char *name;
} RenderTargetConfig;

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
    size_t cb_size = DCB_MAPPING_BYTES;  /* two distinct DCB regions */
    void *cb_addr = NULL;
    int cb_ret = sceKernelMapNamedSystemFlexibleMemory(
        &cb_addr, cb_size, 0x33, 0, "agc_graphics_cb");
    if (cb_ret != 0 || !cb_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for CB: %d\n", cb_ret);
        return false;
    }
    cb_buffer = (uint32_t *)cb_addr;

    /* The offscreen FP16 target is independent of the VideoOut dimensions. */
#if AGC_DEPTH_VALIDATION
    const AgcGfx1013DepthSurfaceLayoutInput depth_input = {
        .width = test->width, .height = test->height, .layer_count = 1u,
        .mip_level_count = 1u,
        .sample_count = AGC_MSAA_VALIDATION ? 4u : 1u,
        .format = AGC_STENCIL_VALIDATION ?
            AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT :
            AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT,
        .depth_swizzle_mode = DEPTH_SWIZZLE_64KB_Z_X,
        .stencil_swizzle_mode = AGC_STENCIL_VALIDATION ?
            DEPTH_SWIZZLE_64KB_Z_X : 0u,
    };
    AgcGfx1013DepthSurfaceLayout depth_layout;
    int32_t layout_ret = agcGfx1013GetDepthSurfaceLayout(
        &depth_input, &depth_layout);
    if (layout_ret != AGC_OK || depth_layout.depth.allocation_size > SIZE_MAX) {
        printf("D32 layout query failed: 0x%08x\n", (unsigned)layout_ret);
        return false;
    }
    size_t rt_size = (size_t)depth_layout.depth.allocation_size;
    size_t rt_alignment = depth_layout.depth.alignment;
    test->depth_surface_size = rt_size;
#if AGC_MSAA_VALIDATION
    const AgcGfx1013ColorSurfaceLayoutInput color_input = {
        .width = test->width, .height = test->height, .layer_count = 1u,
        .mip_level_count = 1u, .sample_count = 4u,
        .format = AGC_GFX1013_RT_FORMAT_RGBA8_UNORM,
        .swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_R_X,
    };
    AgcGfx1013ColorSurfaceLayout color_layout;
    layout_ret = agcGfx1013GetColorSurfaceLayout(&color_input, &color_layout);
    if (layout_ret != AGC_OK || color_layout.allocation_size > SIZE_MAX) {
        printf("4x RGBA8 layout query failed: 0x%08x\n",
               (unsigned)layout_ret);
        return false;
    }
    size_t msaa_color_size = (size_t)color_layout.allocation_size;
    size_t msaa_color_alignment = color_layout.alignment;
    test->msaa_color_surface_size = msaa_color_size;
#else
    size_t msaa_color_size = 0u;
    size_t msaa_color_alignment = 1u;
#endif
    size_t stencil_size = (size_t)depth_layout.stencil.allocation_size;
    size_t stencil_alignment = AGC_STENCIL_VALIDATION ?
        depth_layout.stencil.alignment : 1u;
    test->stencil_surface_size = stencil_size;
    const AgcGfx1013HtileLayoutInput htile_input = {
        .width = test->width, .height = test->height, .layer_count = 1u,
        .mip_level_count = 1u,
        .first_mip_in_tail = depth_layout.depth.first_mip_in_tail,
        .pipe_count = DEPTH_HTILE_PROVISIONAL_PIPE_COUNT,
        .swizzle_mode = DEPTH_SWIZZLE_64KB_Z_X,
    };
    AgcGfx1013HtileLayout htile_layout;
    layout_ret = agcGfx1013GetHtileLayout(&htile_input, &htile_layout);
    if (layout_ret != AGC_OK || htile_layout.allocation_size > SIZE_MAX) {
        printf("HTILE layout query failed: 0x%08x\n", (unsigned)layout_ret);
        return false;
    }
    size_t htile_size = (size_t)htile_layout.allocation_size;
    size_t htile_alignment = htile_layout.alignment;
    test->htile_surface_size = htile_size;
#else
    size_t msaa_color_size = 0u;
    size_t msaa_color_alignment = 1u;
    size_t rt_size = (size_t)FP16_TARGET_WIDTH * FP16_TARGET_HEIGHT *
                     sizeof(uint64_t);
    size_t rt_alignment = 1u;
    size_t stencil_size = 0u;
    size_t stencil_alignment = 1u;
    size_t htile_size = 0u;
    size_t htile_alignment = 1u;
#endif
    size_t pool_size = align_up(
                                GRAPHICS_POOL_PREFIX +
                                msaa_color_alignment - 1u + msaa_color_size +
                                rt_alignment - 1u + rt_size +
                                stencil_alignment - 1u + stencil_size +
                                htile_alignment - 1u + htile_size,
                                1024 * 1024);
    void *pool_addr = NULL;
    int pool_ret = sceKernelMapNamedSystemFlexibleMemory(
        &pool_addr, pool_size, 0x33, 0, "agc_graphics_pool");
    if (pool_ret != 0 || !pool_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for pool: %d\n", pool_ret);
        return false;
    }
    test->compute_buffer = pool_addr;

    /* Render target follows shader data and any tessellation rings. */
    test->render_target = (uint8_t *)pool_addr + GRAPHICS_POOL_PREFIX;
#if AGC_DEPTH_VALIDATION
#if AGC_MSAA_VALIDATION
    test->msaa_color_surface = (void *)(uintptr_t)align_up(
        (size_t)(uintptr_t)test->render_target, msaa_color_alignment);
    test->depth_surface = (void *)(uintptr_t)align_up(
        (size_t)(uintptr_t)test->msaa_color_surface +
            test->msaa_color_surface_size,
        rt_alignment);
#else
    test->depth_surface = (void *)(uintptr_t)align_up(
        (size_t)(uintptr_t)test->render_target, rt_alignment);
#endif
    test->stencil_surface = (void *)(uintptr_t)align_up(
        (size_t)(uintptr_t)test->depth_surface + test->depth_surface_size,
        stencil_alignment);
    test->htile_surface = (void *)(uintptr_t)align_up(
        (size_t)(uintptr_t)test->stencil_surface +
            test->stencil_surface_size,
        htile_alignment);
    if (test->stencil_surface_size != 0u)
        memset(test->stencil_surface, 0, test->stencil_surface_size);
    memset(test->htile_surface, 0, test->htile_surface_size);
#endif

    printf("Command buffer: %zu bytes at %p (flexible)\n", cb_size, cb_buffer);
    printf("Compute pool: %zu bytes at %p (flexible)\n", pool_size, pool_addr);
    printf("Render target: at %p (flexible)\n", test->render_target);
#if AGC_DEPTH_VALIDATION
    printf("Depth surface: %zu bytes at %p (D32, swizzle=%u, HTILE off)\n",
           test->depth_surface_size, test->depth_surface,
           DEPTH_SWIZZLE_64KB_Z_X);
#if AGC_MSAA_VALIDATION
    printf("MSAA color: %zu bytes at %p (RGBA8, 4x, 64KB_R_X)\n",
           test->msaa_color_surface_size, test->msaa_color_surface);
#endif
#if AGC_STENCIL_VALIDATION
    printf("Stencil surface: %zu bytes at %p (S8, swizzle=%u)\n",
           test->stencil_surface_size, test->stencil_surface,
           DEPTH_SWIZZLE_64KB_Z_X);
#endif
    printf("HTILE reserve: %zu bytes at %p (provisional pipes=%u, disabled)\n",
           test->htile_surface_size, test->htile_surface,
           DEPTH_HTILE_PROVISIONAL_PIPE_COUNT);
#endif
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
    printf("VideoOut native resolution: %ux%u\n",
           status.full_width, status.full_height);
    /* Match the hardware-proven linear VideoOut path. The system compositor
     * scales this 1080p scanout to the active display mode. */
    test->width = 1920;
    test->height = 1080;
    test->pitch_pixels = test->width;
    printf("VideoOut registered resolution: %ux%u\n",
           test->width, test->height);

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
    if (!out || !sb || sb_size < sizeof(AgcShaderRecord)) {
        printf("%s: binary too small (%zu bytes)\n", name, sb_size);
        return false;
    }

    memset(out, 0, sizeof(*out));
    const AgcShaderRecord *file_record = (const AgcShaderRecord *)sb;
    if (file_record->magic != AGC_SHADER_RECORD_MAGIC ||
        file_record->version != AGC_SHADER_RECORD_VERSION_GEN5) {
        printf("%s: invalid record magic/version\n", name);
        return false;
    }
    if (file_record->code < sizeof(*file_record) ||
        file_record->code > sb_size) {
        printf("%s: invalid code offset 0x%llx\n", name,
               (unsigned long long)file_record->code);
        return false;
    }

    uint32_t num_inputs = 0;
    uint32_t num_outputs = 0;
    memcpy(&num_inputs, file_record->num_input_semantics, sizeof(num_inputs));
    memcpy(&num_outputs, file_record->num_output_semantics, sizeof(num_outputs));

    if ((file_record->num_sh_registers != 0 &&
         (file_record->sh_registers < sizeof(*file_record) ||
          file_record->sh_registers +
              (uint64_t)file_record->num_sh_registers *
                  sizeof(AgcRegisterValue) > sb_size)) ||
        (file_record->specials != 0 &&
         (file_record->specials < sizeof(*file_record) ||
          file_record->specials + sizeof(AgcShaderSpecials) > sb_size)) ||
        (num_inputs != 0 &&
         (file_record->input_semantics < sizeof(*file_record) ||
          file_record->input_semantics +
              (uint64_t)num_inputs * sizeof(AgcShaderSemantic) > sb_size)) ||
        (num_outputs != 0 &&
         (file_record->output_semantics < sizeof(*file_record) ||
          file_record->output_semantics +
              (uint64_t)num_outputs * sizeof(AgcShaderSemantic) > sb_size))) {
        printf("%s: record sub-block is out of bounds\n", name);
        return false;
    }

    size_t cx_end = (size_t)file_record->code;
    const uint64_t following_offsets[] = {
        file_record->specials,
        file_record->input_semantics,
        file_record->output_semantics,
        file_record->code,
    };
    if (file_record->cx_registers != 0) {
        if (file_record->cx_registers < sizeof(*file_record) ||
            file_record->cx_registers >= sb_size) {
            printf("%s: invalid CX offset\n", name);
            return false;
        }
        for (uint32_t i = 0;
             i < sizeof(following_offsets) / sizeof(following_offsets[0]);
             i++) {
            uint64_t candidate = following_offsets[i];
            if (candidate > file_record->cx_registers &&
                candidate < cx_end) {
                cx_end = (size_t)candidate;
            }
        }
        if ((cx_end - (size_t)file_record->cx_registers) %
                sizeof(AgcRegisterValue) != 0) {
            printf("%s: invalid CX block size\n", name);
            return false;
        }
        out->cx_regs = (const AgcRegisterValue *)
            (sb + file_record->cx_registers);
        out->num_cx_regs = (uint32_t)
            ((cx_end - (size_t)file_record->cx_registers) /
             sizeof(AgcRegisterValue));
    }

    out->relocated = *file_record;
    out->record = &out->relocated;
    out->code = sb + file_record->code;
    out->code_size = sb_size - (size_t)file_record->code;
    out->num_sh_regs = file_record->num_sh_registers;
    out->sh_regs = file_record->sh_registers
        ? (const AgcRegisterValue *)(sb + file_record->sh_registers)
        : NULL;
    out->specials = file_record->specials
        ? (const AgcShaderSpecials *)(sb + file_record->specials)
        : NULL;
    out->input_semantics = num_inputs
        ? (const AgcShaderSemantic *)(sb + file_record->input_semantics)
        : NULL;
    out->num_input_semantics = num_inputs;
    out->output_semantics = num_outputs
        ? (const AgcShaderSemantic *)(sb + file_record->output_semantics)
        : NULL;
    out->num_output_semantics = num_outputs;

    out->relocated.code = (uint64_t)(uintptr_t)out->code;
    out->relocated.sh_registers =
        (uint64_t)(uintptr_t)out->sh_regs;
    out->relocated.cx_registers =
        (uint64_t)(uintptr_t)out->cx_regs;
    out->relocated.specials =
        (uint64_t)(uintptr_t)out->specials;
    out->relocated.input_semantics =
        (uint64_t)(uintptr_t)out->input_semantics;
    out->relocated.output_semantics =
        (uint64_t)(uintptr_t)out->output_semantics;

    printf("%s: type=%u sh=%u cx=%u in=%u out=%u code=%zu specials=%s\n",
           name, out->record->shader_type, out->num_sh_regs,
           out->num_cx_regs, out->num_input_semantics,
           out->num_output_semantics, out->code_size,
           out->specials ? "yes" : "no");
    return true;
}

static bool shader_cx_register(const ParsedGraphicsShader *shader,
                               uint32_t offset, uint32_t *value)
{
    for (uint32_t i = 0; i < shader->num_cx_regs; i++) {
        if (shader->cx_regs[i].offset == offset) {
            *value = shader->cx_regs[i].value;
            return true;
        }
    }
    return false;
}

static bool validate_shader_records(const ParsedGraphicsShader *ngg,
                                    const ParsedGraphicsShader *ps)
{
    uint32_t stages = ngg->specials
        ? ngg->specials->vgt_shader_stages_en.value
        : 0;
    uint32_t ps_control = 0;
    const bool ngg_wave32 =
        ngg->specials &&
        ngg->specials->vgt_shader_stages_en.register_offset ==
            AGC_REG_VGT_SHADER_STAGES_EN &&
        (stages & GFX10_VGT_SHADER_STAGES_EN_GS_W32_EN) != 0;
    const bool ps_wave32 =
        shader_cx_register(ps, AGC_REG_SPI_PS_IN_CONTROL, &ps_control) &&
        (ps_control & GFX10_SPI_PS_IN_CONTROL_PS_W32_EN) != 0;

    printf("[Wave] records: NGG=%s stages=0x%08x PS=Wave32 "
           "control=0x%08x: %s\n",
           ngg_wave32 ? "Wave32" : "Wave64", stages, ps_control,
           ps_wave32 ? "PASS" : "FAIL");
    return ps_wave32;
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

/* Build the merged ES+GS/NGG primitive state from the fused shader record. */
#if !AGC_TESSELLATION
static void setup_shader_stages(
    AgcGfx1013Wave32VsPsState *state,
    const ParsedGraphicsShader *ngg,
    void *ngg_code, const ParsedGraphicsShader *ps, void *ps_code)
{
    *state = (AgcGfx1013Wave32VsPsState){
        .primitive = {
            ngg->record, ngg->sh_regs, ngg->num_sh_regs,
            ngg->cx_regs, ngg->num_cx_regs,
            (uint64_t)(uintptr_t)ngg_code,
        },
        .pixel = {
            ps->record, ps->sh_regs, ps->num_sh_regs,
            ps->cx_regs, ps->num_cx_regs,
            (uint64_t)(uintptr_t)ps_code,
        },
        .primitive_back_code_address = (uint64_t)(uintptr_t)ngg_code,
        .primitive_type = NGG_INPUT_PRIMITIVE_TYPE,
    };
}
#endif

#if AGC_TESSELLATION
static void setup_tess_shader_stages(
    AgcGfx1013Wave32TessVsPsState *state,
    const ParsedGraphicsShader *hull,
    void *hull_back_code,
    const ParsedGraphicsShader *primitive,
    void *primitive_back_code,
    const ParsedGraphicsShader *ps, void *ps_code,
    uint64_t ring_descriptor_address, uint64_t vertex_table_address,
    uint64_t texture_table_address)
{
    *state = (AgcGfx1013Wave32TessVsPsState){
        .hull = {
            hull->record, hull->sh_regs, hull->num_sh_regs,
            hull->cx_regs, hull->num_cx_regs,
            (uint64_t)(uintptr_t)hull_back_code,
        },
        .primitive = {
            primitive->record, primitive->sh_regs, primitive->num_sh_regs,
            primitive->cx_regs, primitive->num_cx_regs,
            (uint64_t)(uintptr_t)primitive_back_code,
        },
        .pixel = {
            ps->record, ps->sh_regs, ps->num_sh_regs,
            ps->cx_regs, ps->num_cx_regs,
            (uint64_t)(uintptr_t)ps_code,
        },
        .hull_back_code_address = (uint64_t)(uintptr_t)hull_back_code,
        .primitive_back_code_address =
            (uint64_t)(uintptr_t)primitive_back_code,
        .ring_descriptor_address = ring_descriptor_address,
        .tcs_offchip_layout = AGC_GFX1013_TESS_OFFCHIP_LAYOUT,
        .primitive_type = 9u,
    };
    (void)vertex_table_address;
    (void)texture_table_address;
}
#endif

/* ======================================================================== */
/* Main draw call dispatch                                                   */
/* ======================================================================== */

static bool dump_launch_registers(const uint32_t *dcb, uint32_t dword_count)
{
    uint32_t sh[0x400] = {0};
    uint32_t cx[0x400] = {0};
    uint32_t uc[0x400] = {0};

    for (uint32_t pos = 0; pos < dword_count;) {
        uint32_t header = dcb[pos];
        uint32_t length = agcPm4Length(header);
        if (length == 0 || length > dword_count - pos)
            break;

        uint32_t *space = NULL;
        uint32_t opcode = agcPm4Opcode(header);
        if (opcode == AGC_PM4_OP_SET_SH_REG)
            space = sh;
        else if (opcode == AGC_PM4_OP_SET_CONTEXT_REG)
            space = cx;
        else if (opcode == AGC_PM4_OP_SET_UCONFIG_REG)
            space = uc;

        if (space && length >= 3) {
            uint32_t start = dcb[pos + 1] & 0xffffu;
            for (uint32_t i = 0; i < length - 2 && start + i < 0x400; i++)
                space[start + i] = dcb[pos + 2 + i];
        }
        pos += length;
    }

    printf("[PM4 Audit] GS: R4=%08x R3=%08x PGM=%08x:%08x R1=%08x R2=%08x UD=%08x:%08x\n",
           sh[AGC_REG_SPI_SHADER_PGM_RSRC4_GS],
           sh[AGC_REG_SPI_SHADER_PGM_RSRC3_GS],
           sh[AGC_REG_SPI_SHADER_PGM_HI_GS],
           sh[AGC_REG_SPI_SHADER_PGM_LO_GS],
           sh[AGC_REG_SPI_SHADER_PGM_RSRC1_GS],
           sh[AGC_REG_SPI_SHADER_PGM_RSRC2_GS],
           sh[AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 1u],
           sh[AGC_REG_SPI_SHADER_USER_DATA_GS_0]);
    printf("[PM4 Audit] ES: PGM=%08x:%08x stages=%08x posfmt=%08x\n",
           sh[AGC_REG_SPI_SHADER_PGM_HI_ES],
           sh[AGC_REG_SPI_SHADER_PGM_LO_ES],
           cx[AGC_REG_VGT_SHADER_STAGES_EN],
           cx[AGC_REG_SPI_SHADER_POS_FORMAT]);
#if AGC_TESSELLATION
    printf("[PM4 Audit] LS: PGM=%08x:%08x R1=%08x R2=%08x VBO=%08x\n",
           sh[AGC_REG_SPI_SHADER_PGM_HI_LS],
           sh[AGC_REG_SPI_SHADER_PGM_LO_LS],
           sh[AGC_REG_SPI_SHADER_PGM_RSRC1_LS],
           sh[AGC_REG_SPI_SHADER_PGM_RSRC2_LS],
           sh[0x10Eu]);
    printf("[PM4 Audit] HS: PGM=%08x:%08x R1=%08x R2=%08x "
           "ring=%08x:%08x\n",
           sh[AGC_REG_SPI_SHADER_PGM_HI_HS],
           sh[AGC_REG_SPI_SHADER_PGM_LO_HS],
           sh[AGC_REG_SPI_SHADER_PGM_RSRC1_HS],
           sh[AGC_REG_SPI_SHADER_PGM_RSRC2_HS],
           sh[AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_HS],
           sh[AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_HS]);
    printf("[PM4 Audit] Tess: GS-ring=%08x:%08x LS_HS=%08x TF=%08x "
           "ring-size=%08x offchip=%08x base=%08x:%08x\n",
           sh[AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_GS],
           sh[AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_GS],
           cx[AGC_REG_VGT_LS_HS_CONFIG], cx[AGC_REG_VGT_TF_PARAM],
           uc[AGC_REG_VGT_TF_RING_SIZE],
           uc[AGC_REG_VGT_HS_OFFCHIP_PARAM],
           uc[AGC_REG_VGT_TF_MEMORY_BASE_HI],
           uc[AGC_REG_VGT_TF_MEMORY_BASE]);
    printf("[PM4 Audit] Tess context: max=%08x min=%08x ESGS=%08x "
           "distribution=%08x\n",
           cx[AGC_REG_VGT_HOS_MAX_TESS_LEVEL],
           cx[AGC_REG_VGT_HOS_MIN_TESS_LEVEL],
           cx[AGC_REG_VGT_ESGS_RING_ITEMSIZE],
           cx[AGC_REG_VGT_TESS_DISTRIBUTION]);
#endif
    printf("[PM4 Audit] GS addr(s0:s1)=%08x:%08x user: "
           "VBO(s2)=%08x base(s3)=%08x "
           "ESGS(s14)=%08x LDS(s15)=%08x NEXT(s16)=%08x\n",
           sh[AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_GS],
           sh[AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_GS],
           sh[AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 2u],
           sh[AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 3u],
           sh[AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 14u],
           sh[AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 15u],
           sh[AGC_REG_SPI_SHADER_USER_DATA_GS_0 + 16u]);
    printf("[PM4 Audit] UC: prim=%08x min=%08x max=%08x ge=%08x pc=%08x\n",
           uc[AGC_REG_VGT_PRIMITIVE_TYPE],
           uc[AGC_REG_GE_MIN_VTX_INDX],
           uc[AGC_REG_GE_MAX_VTX_INDX],
           uc[AGC_REG_GE_CNTL], uc[0x260]);
    const bool ngg_wave32 =
        (cx[AGC_REG_VGT_SHADER_STAGES_EN] &
         GFX10_VGT_SHADER_STAGES_EN_GS_W32_EN) != 0;
    const bool ps_wave32 =
        (cx[AGC_REG_SPI_PS_IN_CONTROL] &
         GFX10_SPI_PS_IN_CONTROL_PS_W32_EN) != 0;
    printf("[PM4 Audit] Wave: NGG=%s PS=%s: %s\n",
           ngg_wave32 ? "yes" : "no", ps_wave32 ? "yes" : "no",
           ps_wave32 ? "PASS" : "FAIL");
    return ps_wave32;
}

static bool dispatch_graphics(GraphicsTest *test,
                               const ParsedGraphicsShader *front,
                               const ParsedGraphicsShader *back,
                               const ParsedGraphicsShader *ps,
                               const RenderTargetConfig *target) {
#ifndef AGC_NGG_OUT_PRIM_OVERRIDE
#define AGC_NGG_OUT_PRIM_OVERRIDE 0
#endif
#ifndef AGC_NGG_WGP_OVERRIDE
#define AGC_NGG_WGP_OVERRIDE 0
#endif
#if AGC_TESSELLATION
    ParsedGraphicsShader hs_front;
    ParsedGraphicsShader hs_back;
    if (!parse_graphics_shader(
            &hs_front, triangle_tess_hs_front_data,
            sizeof(triangle_tess_hs_front_data), "HS front") ||
        !parse_graphics_shader(
            &hs_back, triangle_tess_hs_back_data,
            sizeof(triangle_tess_hs_back_data), "HS back")) {
        return false;
    }
    if (hs_back.num_sh_regs > 16u) {
        printf("HS back has too many SH registers\n");
        return false;
    }
    void *hs_front_code = upload_shader(
        hs_front.code, hs_front.code_size, test->compute_buffer, 0x2000);
    void *hs_back_code = upload_shader(
        hs_back.code, hs_back.code_size, test->compute_buffer, 0x3000);
    AgcShaderRecord hs_front_record = *hs_front.record;
    AgcShaderRecord hs_back_record = *hs_back.record;
    AgcShaderRecord fused_hull_record;
    AgcRegisterValue fused_hull_regs[16] = {0};
    hs_front_record.code = (uint64_t)(uintptr_t)hs_front_code;
    hs_back_record.code = (uint64_t)(uintptr_t)hs_back_code;
    int32_t hull_fuse_err = sceAgcFuseShaderHalves_0200(
        &fused_hull_record, &hs_front_record, &hs_back_record,
        fused_hull_regs);
    if (hull_fuse_err != AGC_OK) {
        printf("FuseShaderHalves(HS) failed: 0x%08x\n",
               (unsigned)hull_fuse_err);
        return false;
    }
    ParsedGraphicsShader hull = hs_back;
    hull.relocated = fused_hull_record;
    hull.record = &hull.relocated;
    hull.sh_regs = fused_hull_regs;
    hull.num_sh_regs = fused_hull_record.num_sh_registers;
    printf("HS front ACO code at %p (%zu bytes)\n",
           hs_front_code, hs_front.code_size);
    printf("HS back ACO code at %p (%zu bytes)\n",
           hs_back_code, hs_back.code_size);
#endif
    void *front_code = upload_shader(
        front->code, front->code_size, test->compute_buffer, 0x0000);
    void *back_code = upload_shader(
        back->code, back->code_size, test->compute_buffer, 0x1000);
    void *ps_code = upload_shader(
        ps->code, ps->code_size, test->compute_buffer, 0x4000);
#if AGC_MSAA_VALIDATION
    ParsedGraphicsShader resolve_ps;
    if (!parse_graphics_shader(
            &resolve_ps, depth_resolve_frag_data,
            sizeof(depth_resolve_frag_data), "4x resolve PS"))
        return false;
    void *resolve_ps_code = upload_shader(
        resolve_ps.code, resolve_ps.code_size, test->compute_buffer, 0x5000);
#endif
    printf("NGG front ACO code at %p (%zu bytes)\n",
           front_code, front->code_size);
    printf("NGG back ACO code at %p (%zu bytes)\n",
           back_code, back->code_size);
    printf("PS code at %p (%zu bytes)\n", ps_code, ps->code_size);

    if (back->num_sh_regs > 24u) {
        printf("NGG back has too many SH registers\n");
        return false;
    }
    AgcShaderRecord front_record = *front->record;
    AgcShaderRecord back_record = *back->record;
    AgcShaderRecord fused_record;
    AgcRegisterValue fused_regs[24] = {0};
    front_record.code = (uint64_t)(uintptr_t)front_code;
    back_record.code = (uint64_t)(uintptr_t)back_code;
    int32_t fuse_err = sceAgcFuseShaderHalves_0200(
        &fused_record, &front_record, &back_record, fused_regs);
    if (fuse_err != AGC_OK) {
        printf("FuseShaderHalves failed: 0x%08x\n", (unsigned)fuse_err);
        return false;
    }

    uint32_t front_rsrc1 = 0, front_rsrc2 = 0;
    uint32_t raw_rsrc1 = 0, raw_rsrc2 = 0;
    uint32_t fused_rsrc1 = 0, fused_rsrc2 = 0;
    for (uint32_t i = 0; i < front->num_sh_regs; i++) {
        if (front->sh_regs[i].offset == AGC_REG_SPI_SHADER_PGM_RSRC1_GS)
            front_rsrc1 = front->sh_regs[i].value;
        else if (front->sh_regs[i].offset == AGC_REG_SPI_SHADER_PGM_RSRC2_GS)
            front_rsrc2 = front->sh_regs[i].value;
    }
    for (uint32_t i = 0; i < back->num_sh_regs; i++) {
        if (back->sh_regs[i].offset == AGC_REG_SPI_SHADER_PGM_RSRC1_GS)
            raw_rsrc1 = back->sh_regs[i].value;
        else if (back->sh_regs[i].offset == AGC_REG_SPI_SHADER_PGM_RSRC2_GS)
            raw_rsrc2 = back->sh_regs[i].value;
    }
    for (uint32_t i = 0; i < fused_record.num_sh_registers; i++) {
        if (fused_regs[i].offset == AGC_REG_SPI_SHADER_PGM_RSRC1_GS) {
            if (AGC_NGG_WGP_OVERRIDE)
                fused_regs[i].value |= 1u << 27; /* WGP_MODE */
            fused_rsrc1 = fused_regs[i].value;
        } else if (fused_regs[i].offset == AGC_REG_SPI_SHADER_PGM_RSRC2_GS) {
            fused_rsrc2 = fused_regs[i].value;
        }
    }
    printf("[Fusion] GS RSRC front=%08x:%08x back=%08x:%08x "
           "fused=%08x:%08x\n",
           front_rsrc1, front_rsrc2, raw_rsrc1, raw_rsrc2,
           fused_rsrc1, fused_rsrc2);

    uint32_t onchip = 0, max_output = 0, subgroup = 0;
    uint32_t instances = 0, max_vert_out = 0;
    const uint32_t out_prim = back->specials
        ? back->specials->vgt_gs_out_prim_type.value
        : 0u;
    (void)shader_cx_register(back, 0x291u, &onchip);
    (void)shader_cx_register(back, 0x1ffu, &max_output);
    (void)shader_cx_register(back, 0x2d3u, &subgroup);
    (void)shader_cx_register(back, 0x2e4u, &instances);
    (void)shader_cx_register(back, 0x2ceu, &max_vert_out);
    printf("[NGG Config] onchip=%08x max_output=%08x subgroup=%08x "
           "instances=%08x max_vert_out=%08x out_prim=%08x\n",
           onchip, max_output, subgroup, instances, max_vert_out, out_prim);

    ParsedGraphicsShader ngg = *back;
    AgcRegisterValue diagnostic_cx_regs[16];
    if (AGC_NGG_OUT_PRIM_OVERRIDE) {
        if (back->num_cx_regs > 16u) {
            printf("NGG back has too many CX registers\n");
            return false;
        }
        memcpy(diagnostic_cx_regs, back->cx_regs,
               back->num_cx_regs * sizeof(diagnostic_cx_regs[0]));
        for (uint32_t i = 0; i < back->num_cx_regs; i++) {
            if (diagnostic_cx_regs[i].offset == 0x29bu)
                diagnostic_cx_regs[i].value = 2u; /* triangle strip */
        }
        ngg.cx_regs = diagnostic_cx_regs;
    }
    ngg.relocated = fused_record;
    ngg.record = &ngg.relocated;
    ngg.sh_regs = fused_regs;
    ngg.num_sh_regs = fused_record.num_sh_registers;

    void *rt_addr = target->address;
    printf("Render target %s at %p (%ux%u, %s)\n",
           target->name, rt_addr, target->width, target->height,
           target->fp16 ? "RGBA16_FLOAT" : "RGBA8_UNORM");

    /* Use a diagnostic sentinel absent from the texture so every rasterized
     * pixel contributes to the exact indexed-triangle coverage count. */
    const uint32_t target_pixels = target->width * target->height;
    if (target->fp16) {
        uint64_t *rt = (uint64_t *)rt_addr;
        for (uint32_t i = 0; i < target_pixels; i++)
            rt[i] = FP16_CLEAR_SENTINEL;
    } else {
        uint32_t *rt = (uint32_t *)rt_addr;
        for (uint32_t i = 0; i < target_pixels; i++)
            rt[i] = DIAGNOSTIC_CLEAR_COLOR;
    }

#if AGC_DEPTH_VALIDATION
    /* Four independent triangles: full-screen depth initialization, a near
     * green left triangle, the same left triangle farther away (must fail),
     * and a far red right triangle over untouched depth (must pass). */
    static const GraphicsDepthVertex depth_vertices[12] = {
        {{-1.0f, -1.0f, 1.00f}, {0.0f, 0.0f, 0.0f}},
        {{ 3.0f, -1.0f, 1.00f}, {0.0f, 0.0f, 0.0f}},
        {{-1.0f,  3.0f, 1.00f}, {0.0f, 0.0f, 0.0f}},
        {{-0.85f, -0.55f, 0.25f}, {0.0f, 1.0f, 0.0f}},
        {{-0.05f, -0.55f, 0.25f}, {0.0f, 1.0f, 0.0f}},
        {{-0.45f,  0.55f, 0.25f}, {0.0f, 1.0f, 0.0f}},
        {{-0.85f, -0.55f, 0.75f}, {1.0f, 0.0f, 0.0f}},
        {{-0.05f, -0.55f, 0.75f}, {1.0f, 0.0f, 0.0f}},
        {{-0.45f,  0.55f, 0.75f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.05f, -0.55f, 0.75f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.85f, -0.55f, 0.75f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.45f,  0.55f, 0.75f}, {1.0f, 0.0f, 0.0f}},
    };
    GraphicsDepthVertex *gpu_vertices = (GraphicsDepthVertex *)
        ((uint8_t *)test->compute_buffer + VERTEX_DATA_OFFSET);
    uint32_t *vertex_desc = (uint32_t *)
        ((uint8_t *)test->compute_buffer + VERTEX_DESC_OFFSET);
#if AGC_MSAA_VALIDATION
    uint32_t *texture_desc = (uint32_t *)
        ((uint8_t *)test->compute_buffer + TEXTURE_DESC_OFFSET);
#endif
    memcpy(gpu_vertices, depth_vertices, sizeof(depth_vertices));
    for (uint32_t draw = 0u; draw < 4u; ++draw) {
        int32_t descriptor_error = agcGfx1013BufferDescriptorEncode(
            (AgcGfx1013BufferDescriptor *)&vertex_desc[draw * 4u],
            (uint64_t)(uintptr_t)&gpu_vertices[draw * 3u],
            (uint32_t)sizeof(GraphicsDepthVertex), 3u);
        if (descriptor_error != AGC_OK) {
            printf("[Depth] vertex descriptor %u failed: 0x%08x\n",
                   draw, (unsigned)descriptor_error);
            return false;
        }
    }
    uint32_t *depth_words = (uint32_t *)test->depth_surface;
    for (size_t i = 0u; i < test->depth_surface_size / sizeof(uint32_t); ++i)
        depth_words[i] = 0x7fc00000u;
    printf("[Depth] uploaded four float3 position/color triangles at %p\n",
           gpu_vertices);
#else
    /* Upload one interleaved binding: float2 position + float3 color. The
     * compiler uses a single static binding descriptor for both attributes. */
    static const GraphicsVertex vertices[8] = {
        {{-0.5f, -0.4330127f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.4330127f}, {0.0f, 1.0f, 0.0f}},
        {{ 0.0f,  0.4330127f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.4330127f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.4330127f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.4330127f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.4330127f}, {0.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.4330127f}, {0.0f, 0.0f, 1.0f}},
    };
    static const uint16_t indices[3] = {0, 1, 2};
    GraphicsVertex *gpu_vertices = (GraphicsVertex *)
        ((uint8_t *)test->compute_buffer + VERTEX_DATA_OFFSET);
    uint32_t *vertex_desc = (uint32_t *)
        ((uint8_t *)test->compute_buffer + VERTEX_DESC_OFFSET);
    uint16_t *gpu_indices = (uint16_t *)
        ((uint8_t *)test->compute_buffer + INDEX_DATA_OFFSET);
    uint32_t *gpu_texture = (uint32_t *)
        ((uint8_t *)test->compute_buffer + TEXTURE_DATA_OFFSET);
    uint32_t *texture_desc = (uint32_t *)
        ((uint8_t *)test->compute_buffer + TEXTURE_DESC_OFFSET);
    memcpy(gpu_vertices, vertices, sizeof(vertices));
    memcpy(gpu_indices, indices, sizeof(indices));
    /* RGBA8 texels: red, green / blue, white. Bilinear sampling produces a
     * visibly distinct two-dimensional gradient inside the triangle. */
    static const uint32_t texture_pixels[4] = {
        0xFF0000FFu, 0xFF00FF00u,
        0xFFFF0000u, 0xFFFFFFFFu,
    };
    memcpy(gpu_texture, texture_pixels, sizeof(texture_pixels));
    int32_t resource_error = agcGfx1013BufferDescriptorEncode(
        (AgcGfx1013BufferDescriptor *)vertex_desc,
        (uint64_t)(uintptr_t)gpu_vertices,
        (uint32_t)sizeof(GraphicsVertex), 8u);
    if (resource_error != AGC_OK) {
        printf("[Vertex] descriptor encode failed: 0x%08x\n",
               (unsigned)resource_error);
        return false;
    }
    const AgcGfx1013Image2DState image_state = {
        .address = (uint64_t)(uintptr_t)gpu_texture,
        .width = TEXTURE_WIDTH,
        .height = TEXTURE_HEIGHT,
        .format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM,
        .image_type = AGC_GFX1013_IMAGE_TYPE_2D,
        .dst_sel_x = 4u,
        .dst_sel_y = 5u,
        .dst_sel_z = 6u,
        .dst_sel_w = 7u,
    };
    AgcSamplerDescriptor sampler;
    agcSamplerDescriptorInit(&sampler);
    agcSamplerDescriptorSetClampMode(
        &sampler, kAgcClampClamp, kAgcClampClamp, kAgcClampClamp);
    agcSamplerDescriptorSetFilterMode(
        &sampler, kAgcFilterBilinear, kAgcFilterBilinear,
        kAgcMipFilterNone);
    resource_error = agcGfx1013CombinedImageSamplerDescriptorEncode(
        (AgcGfx1013CombinedImageSamplerDescriptor *)texture_desc,
        &image_state, &sampler);
    if (resource_error != AGC_OK) {
        printf("[Texture] descriptor encode failed: 0x%08x\n",
               (unsigned)resource_error);
        return false;
    }

    printf("[Vertex] data=%p stride=%zu records=8 table=%p\n",
           gpu_vertices, sizeof(GraphicsVertex), vertex_desc);
    printf("[Vertex] descriptor=%08x %08x %08x %08x\n",
           vertex_desc[0], vertex_desc[1], vertex_desc[2], vertex_desc[3]);
    printf("[Index] data=%p type=u16 count=3 values={%u,%u,%u}\n",
           gpu_indices, indices[0], indices[1], indices[2]);
    printf("[Texture] data=%p 2x2 RGBA8 table=%p\n",
           gpu_texture, texture_desc);
    printf("[Texture] image=%08x %08x %08x %08x sampler=%08x %08x %08x %08x\n",
           texture_desc[0], texture_desc[1], texture_desc[2], texture_desc[3],
           texture_desc[8], texture_desc[9], texture_desc[10], texture_desc[11]);
#endif

        /* Use a distinct command-buffer address for each hardware submission.
     * Reusing the first IB address immediately can leave the second direct
     * submit indistinguishable from the prior work in the native queue. */
    uint32_t *dispatch_cb = (uint32_t *)((uint8_t *)cb_buffer +
        (target->fp16 ? DCB_SECOND_OFFSET : 0u));

    /* Build DCB */
    SceAgcCb cb;
    agcCbInit(&cb, dispatch_cb,
              AGC_DEPTH_VALIDATION ? DEPTH_DCB_CAPACITY_BYTES :
                                     STANDARD_DCB_CAPACITY_BYTES);

    const AgcGfx1013FrameState frame_state = {
        .color_target = {
            .address = (uint64_t)(uintptr_t)rt_addr,
            .width = target->width,
            .height = target->height,
            .color_format = target->color_format,
            .number_type = target->number_type,
            .component_swap = target->component_swap,
            .sample_count = AGC_MSAA_VALIDATION ? 4u : 1u,
            .fragment_count = AGC_MSAA_VALIDATION ? 4u : 1u,
            .swizzle_mode = AGC_MSAA_VALIDATION ?
                AGC_GFX1013_SWIZZLE_64KB_R_X : 0u,
        },
        .viewport = {target->width, target->height},
        .scissor = {0u, 0u, target->width, target->height},
        .target_mask = AGC_GFX1013_TARGET_MASK_RGBA0,
        .context_load_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE,
        .context_shadow_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE,
        .clear_state_flags = 0u,
        .min_vertex_index = 0u,
        .vertex_index_offset = 0u,
        .max_vertex_index = 0xffffffffu,
        .ngg_mode_control = AGC_GFX1013_NGG_MODE_CONTROL,
        .vertex_reuse_block_control = AGC_GFX1013_VERTEX_REUSE_BLOCK,
        .instance_step_rate = 1u,
        .clip_control = 0u,
        .raster_mode_control = 0u,
    };
    AgcGfx1013GraphicsDefaultStats default_stats;
    int32_t state_error = agcGfx1013BuildFramePrologue(
        &cb, &frame_state, &default_stats);
    if (state_error != AGC_OK) {
        printf("[Dispatch] reusable frame prologue failed: %s\n",
               errstr(state_error));
        return false;
    }
    printf("[Dispatch] CONTEXT_CONTROL: load enable\n");
    printf("[Dispatch] CLEAR_STATE: opcode 0x12, state 0\n");
    printf("[Dispatch] Applied %u SH, %u CX, %u UC register defaults\n",
           default_stats.sh_register_count,
           default_stats.cx_register_count,
           default_stats.uc_register_count);

    printf("[RT] reusable gfx1013 color target: %ux%u format=0x%x\n",
           target->width, target->height, target->color_format);

    /* 4. Derive primitive and interpolant state from fused records. */
#if AGC_TESSELLATION
    void *offchip_ring =
        (uint8_t *)test->compute_buffer + TESS_OFFCHIP_OFFSET;
    void *factor_ring =
        (uint8_t *)test->compute_buffer + TESS_FACTOR_OFFSET;
    AgcGfx1013TessellationRingTable *ring_table =
        (AgcGfx1013TessellationRingTable *)
        ((uint8_t *)test->compute_buffer + TESS_RING_TABLE_OFFSET);
    uint32_t *offchip_words = (uint32_t *)offchip_ring;
    for (uint32_t i = 0; i < AGC_GFX1013_TESS_OFFCHIP_RING_SIZE / 4u; ++i)
        offchip_words[i] = 0xDEADBEEFu;
    memset(factor_ring, 0, AGC_GFX1013_TESS_FACTOR_RING_SIZE);
    int32_t tf_ring_err = sceAgcDriverSetTFRing(
        (uintptr_t)factor_ring, AGC_GFX1013_TESS_FACTOR_RING_SIZE);
    printf("[Tess] FW 5.50 TF-ring address setup: 0x%08x\n",
           (unsigned)tf_ring_err);
    if (tf_ring_err != AGC_OK)
        return false;
    const AgcGfx1013TessellationState tess_state = {
        .offchip_ring_address = (uint64_t)(uintptr_t)offchip_ring,
        .factor_ring_address = (uint64_t)(uintptr_t)factor_ring,
        .offchip_ring_size = AGC_GFX1013_TESS_OFFCHIP_RING_SIZE,
        .factor_ring_size = AGC_GFX1013_TESS_FACTOR_RING_SIZE,
        .offchip_param = AGC_GFX1013_TESS_OFFCHIP_PARAM,
        .max_tess_level = 0x42800000u,
        .min_tess_level = 0u,
        .esgs_ring_itemsize = 1u,
        .distribution = 0xD8181E0Cu,
        .tf_param = 0x00000061u |
            ((AGC_TESS_DISTRIBUTION_MODE & 3u) << 17),
    };
    if (agcGfx1013BuildTessellationRingTable(
            ring_table, &tess_state) != AGC_OK ||
        agcGfx1013SetTessellationRings(&cb, &tess_state) != AGC_OK)
        return false;
    printf("[Tess] offchip=%p size=0x%x factor=%p size=0x%x table=%p\n",
           offchip_ring, AGC_GFX1013_TESS_OFFCHIP_RING_SIZE,
           factor_ring, AGC_GFX1013_TESS_FACTOR_RING_SIZE, ring_table);
    AgcGfx1013Wave32TessVsPsState tess_shaders;
    setup_tess_shader_stages(
            &tess_shaders, &hull, hs_back_code,
            &ngg, back_code, ps, ps_code,
            (uint64_t)(uintptr_t)ring_table,
            (uint64_t)(uintptr_t)vertex_desc,
            (uint64_t)(uintptr_t)texture_desc);
#else
    AgcGfx1013Wave32VsPsState baseline_shaders;
    setup_shader_stages(&baseline_shaders, &ngg, back_code, ps, ps_code);
#endif

    /* 5. Bind the fused state. The ES-front starts the merged wave and uses
     * the address32 AC_UD_NEXT_STAGE_PC value to continue at the GS-back
     * program; ACO supplies the fixed high address dword in the ISA. */
    const AgcGfx1013ResourceTableBinding primitive_resource_table = {
        OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER,
        (uint64_t)(uintptr_t)vertex_desc,
    };
#if !AGC_DEPTH_VALIDATION
    const AgcGfx1013ResourceTableBinding pixel_resource_table = {
        OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
        (uint64_t)(uintptr_t)texture_desc,
    };
#endif

    /* 6b. Post-bind depth and rasterizer overrides remain application state.
     * Shader formats,
     * stage selection, primitive type, and interpolants came from compiler
     * records and the OpenAGC state builders above. */
    /* 7. Establish the NGG stage ABI with canonical auto-generated vertex
     * IDs. Indexed offset semantics are a separate PM4 validation case. */
#if AGC_DEPTH_VALIDATION
    volatile uint32_t *depth_markers = (volatile uint32_t *)
        ((uint8_t *)dispatch_cb + DCB_MARKER_OFFSET);
    static const uint32_t depth_marker_values[4] = {
        0xD3200001u, 0xD3200002u, 0xD3200003u, 0xD3200004u,
    };
    memset((void *)depth_markers, 0, 4u * sizeof(uint32_t));

    if (agcGfx1013BindVsPs(&cb, &baseline_shaders) != AGC_OK ||
        agcGfx1013BindResourceTables(
            &cb, &baseline_shaders.primitive,
            &primitive_resource_table, 1u) != AGC_OK ||
        agcGfx1013ApplyFramePostBind(&cb, &frame_state) != AGC_OK)
        return false;

#if AGC_MSAA_VALIDATION
    const AgcGfx1013SampleState sample_state_4x = {4u, 1u, 0xFu};
    if (agcGfx1013SetSampleState(&cb, &sample_state_4x) != AGC_OK)
        return false;
#endif

    const AgcGfx1013DepthSurfaceState depth_surface = {
        .depth_read_address = (uint64_t)(uintptr_t)test->depth_surface,
        .depth_write_address = (uint64_t)(uintptr_t)test->depth_surface,
        .stencil_read_address = AGC_STENCIL_VALIDATION ?
            (uint64_t)(uintptr_t)test->stencil_surface : 0u,
        .stencil_write_address = AGC_STENCIL_VALIDATION ?
            (uint64_t)(uintptr_t)test->stencil_surface : 0u,
        .width = target->width,
        .height = target->height,
        .format = AGC_STENCIL_VALIDATION ?
            AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT_S8_UINT :
            AGC_GFX1013_DEPTH_FORMAT_D32_FLOAT,
        .depth_swizzle_mode = DEPTH_SWIZZLE_64KB_Z_X,
        .stencil_swizzle_mode = AGC_STENCIL_VALIDATION ?
            DEPTH_SWIZZLE_64KB_Z_X : 0u,
        .mip_level_count = 1u,
        .sample_count = AGC_MSAA_VALIDATION ? 4u : 1u,
        .htile_enable = 0u,
    };
    AgcGfx1013DepthStencilState depth_control = {
        .depth_test_enable = 1u,
        .depth_write_enable = 1u,
        .depth_compare_operation = AGC_GFX1013_COMPARE_ALWAYS,
    };
    AgcGfx1013ColorBlendState blend = {
        .target_count = 1u,
    };
#if AGC_MSAA_VALIDATION
    blend.targets[0].write_mask = 0xfu;
#endif
    if (agcGfx1013SetDepthSurface(&cb, &depth_surface) != AGC_OK ||
        agcGfx1013SetDepthStencilState(&cb, &depth_control) != AGC_OK ||
        agcGfx1013SetColorBlendState(&cb, &blend) != AGC_OK ||
        !sceAgcDcbSetIndexSize(&cb, kAgcIndexSize16, 0u) ||
        !sceAgcDcbSetNumInstances(&cb, 1u) ||
        !sceAgcDcbDrawIndexAuto(&cb, 3u, 0x40000000u) ||
        !sceAgcDcbWriteData(&cb, 2u, 0u,
            (uint64_t)(uintptr_t)&depth_markers[0],
            &depth_marker_values[0], 1u, 0u, 0u))
        return false;

    blend.targets[0].write_mask = 0xfu;
    depth_control.depth_compare_operation = AGC_GFX1013_COMPARE_LESS;
#if AGC_STENCIL_VALIDATION
    depth_control.stencil_test_enable = 1u;
    depth_control.front.compare_operation = AGC_GFX1013_COMPARE_ALWAYS;
    depth_control.front.fail_operation = AGC_GFX1013_STENCIL_KEEP;
    depth_control.front.depth_fail_operation = AGC_GFX1013_STENCIL_KEEP;
    depth_control.front.pass_operation = AGC_GFX1013_STENCIL_REPLACE;
    depth_control.front.reference = 0x5au;
    depth_control.front.compare_mask = 0xffu;
    depth_control.front.write_mask = 0xffu;
#endif
    if (agcGfx1013SetColorBlendState(&cb, &blend) != AGC_OK ||
        agcGfx1013SetDepthStencilState(&cb, &depth_control) != AGC_OK)
        return false;
    for (uint32_t draw = 1u; draw < 4u; ++draw) {
        const AgcGfx1013ResourceTableBinding draw_table = {
            OPENAGC_VERTEX_BUFFER_TABLE_PLACEHOLDER,
            (uint64_t)(uintptr_t)&vertex_desc[draw * 4u],
        };
        if (agcGfx1013BindResourceTables(
                &cb, &baseline_shaders.primitive, &draw_table, 1u) != AGC_OK ||
            !sceAgcDcbDrawIndexAuto(&cb, 3u, 0x40000000u) ||
            !sceAgcDcbWriteData(&cb, 2u, 0u,
                (uint64_t)(uintptr_t)&depth_markers[draw],
                &depth_marker_values[draw], 1u, 0u, 0u))
            return false;
    }
    printf("[Depth%s] emitted init, near-pass, overlap-fail, and far-pass draws\n",
           AGC_STENCIL_VALIDATION ? "+Stencil" : "");
#if AGC_MSAA_VALIDATION
    AgcGfx1013ImageDescriptor *msaa_descriptor =
        (AgcGfx1013ImageDescriptor *)texture_desc;
    const AgcGfx1013Image2DState msaa_image = {
        .address = (uint64_t)(uintptr_t)test->msaa_color_surface,
        .width = target->width,
        .height = target->height,
        .format = AGC_GFX1013_IMAGE_FORMAT_RGBA8_UNORM,
        .image_type = AGC_GFX1013_IMAGE_TYPE_2D_MSAA,
        /* ALT render-target storage exchanges the logical red/blue lanes;
         * undo that exchange when sampling the multisample image. */
        .dst_sel_x = 6u, .dst_sel_y = 5u,
        .dst_sel_z = 4u, .dst_sel_w = 7u,
        .sample_count = 4u,
        .swizzle_mode = AGC_GFX1013_SWIZZLE_64KB_R_X,
    };
    memset(texture_desc, 0, sizeof(AgcGfx1013CombinedImageSamplerDescriptor));
    if (agcGfx1013Image2DDescriptorEncode(
            msaa_descriptor, &msaa_image) != AGC_OK)
        return false;

    AgcGfx1013Wave32VsPsState resolve_shaders;
    setup_shader_stages(&resolve_shaders, &ngg, back_code,
                        &resolve_ps, resolve_ps_code);
    const AgcGfx1013FrameState resolve_frame = {
        .color_target = {
            .address = (uint64_t)(uintptr_t)test->buffers[0],
            .width = target->width,
            .height = target->height,
            .color_format = AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
            .number_type = AGC_GFX1013_SURFACE_NUMBER_UNORM,
            .component_swap = AGC_GFX1013_SURFACE_SWAP_ALT,
            .sample_count = 1u,
            .fragment_count = 1u,
        },
        .viewport = {target->width, target->height},
        .scissor = {0u, 0u, target->width, target->height},
        .target_mask = AGC_GFX1013_TARGET_MASK_RGBA0,
        .context_load_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE,
        .context_shadow_control = AGC_GFX1013_CONTEXT_CONTROL_ENABLE,
        .max_vertex_index = 0xffffffffu,
        .ngg_mode_control = AGC_GFX1013_NGG_MODE_CONTROL,
        .vertex_reuse_block_control = AGC_GFX1013_VERTEX_REUSE_BLOCK,
        .instance_step_rate = 1u,
    };
    const AgcGfx1013ResourceTableBinding resolve_pixel_table = {
        OPENAGC_DESCRIPTOR_SET_PLACEHOLDER(0u),
        (uint64_t)(uintptr_t)texture_desc,
    };
    const AgcGfx1013BaselineDrawState resolve_draw = {
        .shaders = resolve_shaders,
        .frame = &resolve_frame,
        .primitive_resource_tables = &primitive_resource_table,
        .num_primitive_resource_tables = 1u,
        .pixel_resource_tables = &resolve_pixel_table,
        .num_pixel_resource_tables = 1u,
        .index_type = kAgcIndexSize16,
        .instance_count = 1u,
        .vertex_count = 3u,
        .draw_modifier = 0x40000000u,
    };
    const AgcGfx1013ColorResolveState resolve = {
        &frame_state.color_target, &resolve_draw,
    };
    int32_t resolve_error = agcGfx1013ResolveColor4x(&cb, &resolve);
    if (resolve_error != AGC_OK) {
        printf("[MSAA] resolve failed: 0x%08x (%s)\n",
               (unsigned)resolve_error, errstr(resolve_error));
        return false;
    }
    printf("[MSAA] shader-resolved 4x RGBA8 to 1x VideoOut target\n");
#endif
#elif AGC_TESSELLATION
    const AgcGfx1013TessDrawState tess_draw = {
        .shaders = tess_shaders,
        .frame = &frame_state,
        .tessellation = &tess_state,
        .hull_resource_tables = &primitive_resource_table,
        .num_hull_resource_tables = 1u,
        .pixel_resource_tables = &pixel_resource_table,
        .num_pixel_resource_tables = 1u,
        .instance_count = 1u,
        .vertex_count = NGG_DRAW_VERTEX_COUNT,
        .draw_modifier = 0x40000000u,
    };
    state_error = agcGfx1013DrawTessIndexAuto(&cb, &tess_draw);
    printf("[Tess] reusable gfx1013 HS+TES+PS bind: 0x%08x\n",
           (unsigned)state_error);
    printf("[Tess] resource tables/context/instance/draw: 0x%08x\n",
           (unsigned)state_error);
    if (state_error != AGC_OK)
        return false;
    printf("[Draw] NumInstances(1)\n");
    printf("[Draw] DrawIndexAuto(%u)\n", NGG_DRAW_VERTEX_COUNT);
#else
    const AgcGfx1013BaselineDrawState baseline_draw = {
        .shaders = baseline_shaders,
        .frame = &frame_state,
        .primitive_resource_tables = &primitive_resource_table,
        .num_primitive_resource_tables = 1u,
        .pixel_resource_tables = &pixel_resource_table,
        .num_pixel_resource_tables = 1u,
        .index_type = kAgcIndexSize16,
        .index_swap = 0u,
        .instance_count = 1u,
        .vertex_count = NGG_DRAW_VERTEX_COUNT,
        .draw_modifier = 0x40000000u,
    };
    state_error = agcGfx1013DrawBaselineIndexAuto(&cb, &baseline_draw);
    printf("[Draw] reusable baseline bind/index/instance/auto: 0x%08x\n",
           (unsigned)state_error);
    if (state_error != AGC_OK)
        return false;
#endif

    /* 8b. WRITE_DATA marker — verify GPU is alive after draw. Keep it near
     * the end of this 32 KiB allocation, outside the active command stream. */
#if AGC_DEPTH_VALIDATION
    uint64_t marker_target =
                (uint64_t)(uintptr_t)dispatch_cb +
                DCB_MARKER_OFFSET + 0x20u;
    uint32_t marker_value = 0xD32FFFFFu;
#else
    uint64_t marker_target =
            (uint64_t)(uintptr_t)dispatch_cb + DCB_MARKER_OFFSET;
    uint32_t marker_value = 0xDEADCAFEu;
#endif
    volatile uint32_t *marker =
        (volatile uint32_t *)(uintptr_t)marker_target;
    *marker = 0u;
    printf("[Draw] WRITE_DATA marker at 0x%llx\n", (unsigned long long)marker_target);

    /* Complete render-target writes before CPU readback and conversion. */
#if AGC_DEPTH_VALIDATION
    const AgcGfx1013ResourceTransition color_completion = {
        .before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET,
        .after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ,
    };
#endif
    const AgcGfx1013ResourceTransition completion = {
#if AGC_DEPTH_VALIDATION
        .before = AGC_GFX1013_RESOURCE_USAGE_DEPTH_STENCIL_WRITE,
#else
        .before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET,
#endif
        .after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ,
        .completion_address = marker_target,
        .completion_value = marker_value,
    };
#if AGC_DEPTH_VALIDATION
    if (agcGfx1013TransitionResource(&cb, &color_completion) != AGC_OK)
        return false;
#endif
    if (agcGfx1013TransitionResource(&cb, &completion) != AGC_OK)
        return false;

    /* Submit DCB */
    AgcCommandBufferSubmit submit;
    submit.command_address = (uintptr_t)dispatch_cb;
    submit.dword_count = agcCbUsedDwords(&cb);
    submit.reserved = 0;

    if (!dump_launch_registers(dispatch_cb, submit.dword_count))
        return false;

    printf("[Draw] DCB: %u dwords, submitting...\n", submit.dword_count);
    int32_t err = sceAgcDriverSubmitDcb(&submit);
    printf("[Draw] SubmitDcb: 0x%08x (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) return false;

    uint32_t waited_us = 0u;
    while (*marker != marker_value && waited_us < 200000u) {
        sceKernelUsleep(1000u);
        waited_us += 1000u;
    }
    if (*marker != marker_value) {
        printf("[Draw] GPU completion fence timed out after %u us\n",
               waited_us);
        return false;
    }
    printf("[Draw] GPU completion fence reached after %u us\n", waited_us);

    printf("[Marker] WRITE_DATA marker = 0x%08x (expected 0x%08x)\n",
           *marker, marker_value);
#if AGC_DEPTH_VALIDATION
    bool markers_pass = true;
    for (uint32_t i = 0u; i < 4u; ++i) {
        printf("[Depth Marker] stage[%u]=0x%08x expected=0x%08x\n",
               i, depth_markers[i], depth_marker_values[i]);
        markers_pass &= depth_markers[i] == depth_marker_values[i];
    }
#endif
#if AGC_TESSELLATION
    uint32_t offchip_changed = 0;
    uint32_t factor_changed = 0;
    const uint32_t *factor_words = (const uint32_t *)factor_ring;
    for (uint32_t i = 0; i < AGC_GFX1013_TESS_OFFCHIP_RING_SIZE / 4u; ++i)
        offchip_changed += offchip_words[i] != 0xDEADBEEFu;
    for (uint32_t i = 0; i < AGC_GFX1013_TESS_FACTOR_RING_SIZE / 4u; ++i)
        factor_changed += factor_words[i] != 0u;
    printf("[Tess Rings] offchip changed=%u factor changed=%u\n",
           offchip_changed, factor_changed);
    printf("[Tess Rings] offchip[0..3]=%08x %08x %08x %08x "
           "factor[0..3]=%08x %08x %08x %08x\n",
           offchip_words[0], offchip_words[1], offchip_words[2],
           offchip_words[3], factor_words[0], factor_words[1],
           factor_words[2], factor_words[3]);
    uint32_t dumped = 0;
    for (uint32_t i = 0;
         i < AGC_GFX1013_TESS_OFFCHIP_RING_SIZE / 4u && dumped < 32u; ++i) {
        if (offchip_words[i] != 0xDEADBEEFu) {
            printf("[Tess Offchip] word[%u]=%08x\n", i, offchip_words[i]);
            ++dumped;
        }
    }
#endif

#if AGC_DEPTH_VALIDATION
    const uint32_t *color = (const uint32_t *)(AGC_MSAA_VALIDATION ?
        test->buffers[0] : rt_addr);
    const uint32_t *depth = (const uint32_t *)test->depth_surface;
    uint32_t green_pixels = 0u;
    uint32_t red_pixels = 0u;
    uint32_t depth_one = 0u;
    uint32_t depth_near = 0u;
    uint32_t depth_far = 0u;
    const uint32_t expected_red = 0xFFFF0000u;
    for (uint32_t i = 0u; i < target_pixels; ++i) {
        green_pixels += color[i] == 0xFF00FF00u;
        red_pixels += color[i] == expected_red;
    }
    for (size_t i = 0u; i < test->depth_surface_size / sizeof(uint32_t); ++i) {
        depth_one += depth[i] == 0x3f800000u;
        /* The reusable viewport maps clip Z with scale/offset 0.5/0.5. */
        depth_near += depth[i] == 0x3f200000u;
        depth_far += depth[i] == 0x3f600000u;
    }
    const uint32_t left_sample = color[639u * target->width + 717u];
    const uint32_t right_sample = color[639u * target->width + 1203u];
    const bool color_pass = green_pixels > 1000u && red_pixels > 1000u &&
        left_sample == 0xFF00FF00u && right_sample == expected_red;
    const bool depth_pass = depth_one != 0u && depth_near != 0u &&
        depth_far != 0u;
#if AGC_STENCIL_VALIDATION
    const uint8_t *stencil = (const uint8_t *)test->stencil_surface;
    uint32_t stencil_zero = 0u;
    uint32_t stencil_replace = 0u;
    uint32_t stencil_other = 0u;
    for (size_t i = 0u; i < test->stencil_surface_size; ++i) {
        stencil_zero += stencil[i] == 0u;
        stencil_replace += stencil[i] == 0x5au;
        stencil_other += stencil[i] != 0u && stencil[i] != 0x5au;
    }
    const bool stencil_pass = stencil_zero != 0u &&
        stencil_replace > 1000u && stencil_other == 0u;
#else
    const bool stencil_pass = true;
#endif
    printf("[Depth Readback] green=%u red=%u left=%08x right=%08x\n",
           green_pixels, red_pixels, left_sample, right_sample);
    printf("[Depth Readback] raw D32: one=%u near=%u far=%u\n",
           depth_one, depth_near, depth_far);
#if AGC_STENCIL_VALIDATION
    printf("[Stencil Readback] zero=%u replace-5a=%u other=%u\n",
           stencil_zero, stencil_replace, stencil_other);
#endif
    printf("[Depth%s%s Result] markers=%s color=%s raw-depth=%s stencil=%s\n",
           AGC_STENCIL_VALIDATION ? "+Stencil" : "",
           AGC_MSAA_VALIDATION ? "+4xMSAA" : "",
           markers_pass ? "PASS" : "FAIL",
           color_pass ? "PASS" : "FAIL",
           depth_pass ? "PASS" : "FAIL",
           stencil_pass ? "PASS" : "FAIL");
    return markers_pass && color_pass && depth_pass && stencil_pass;
#endif

    if (target->fp16) {
        const uint64_t *rt = (const uint64_t *)rt_addr;
        uint64_t unique_colors[8] = {0};
        uint32_t unique_color_count = 0;
        uint32_t changed = 0;
        uint32_t opaque_samples = 0;
        uint32_t out_of_range_components = 0;
        uint32_t min_x = target->width;
        uint32_t min_y = target->height;
        uint32_t max_x = 0;
        uint32_t max_y = 0;
        for (uint32_t i = 0; i < target_pixels; i++) {
            uint64_t color = rt[i];
            if (color == FP16_CLEAR_SENTINEL)
                continue;
            changed++;
            const uint32_t x = i % target->width;
            const uint32_t y = i / target->width;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
            if ((uint16_t)(color >> 48) == 0x3c00u)
                opaque_samples++;
            for (uint32_t lane = 0; lane < 4u; lane++) {
                uint16_t component = (uint16_t)(color >> (lane * 16u));
                if ((component & 0x8000u) != 0u || component > 0x3c00u)
                    out_of_range_components++;
            }
            if (unique_color_count < 8) {
                bool seen = false;
                for (uint32_t j = 0; j < unique_color_count; j++) {
                    if (unique_colors[j] == color) {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                    unique_colors[unique_color_count++] = color;
            }
        }
        /* Equilateral NDC triangle covers sqrt(3)/16 of a square target.
         * 1774/16384 is a close integer approximation. */
#if AGC_TESS_GEOMETRY_LINES
        /* A level-4 triangular grid has 30 unique edges. Each edge is one
         * quarter of the half-width outer side, for 15/4 target widths of
         * ideal one-pixel line coverage before rasterization endpoint rules. */
        const uint32_t expected_changed =
            (target->width * 15u) / 4u;
#elif AGC_TESS_GEOMETRY_INVOCATIONS
        /* Two invocation-ID-selected half-scale copies cover half the
         * isolated tessellation triangle area. */
        const uint32_t expected_changed = (uint32_t)
            (((uint64_t)target_pixels * 887u) / 16384u);
#elif AGC_TESS_GEOMETRY
        /* The combined TES+GS fixture scales each vertex around the centroid
         * by 0.78, so its rasterized area is 0.78^2 of the TES control. */
        const uint32_t expected_changed = (uint32_t)
            (((uint64_t)target_pixels * 1774u * 1521u) /
             (16384u * 2500u));
#elif AGC_NGG_AMPLIFY || AGC_NGG_INVOCATIONS
        /* Two half-scale copies cover half the pass-through triangle area. */
        const uint32_t expected_changed = (uint32_t)
            (((uint64_t)target_pixels * 887u) / 16384u);
#else
        const uint32_t expected_changed = (uint32_t)
            (((uint64_t)target_pixels * 1774u) / 16384u);
#endif
        const uint32_t coverage_tolerance = target->width;
        printf("[FP16] Changed pixels: %u / %u (expected about %u)\n",
               changed, target_pixels, expected_changed);
        if (changed != 0u) {
            printf("[FP16] Coverage bounds: x=%u..%u y=%u..%u (%ux%u)\n",
                   min_x, max_x, min_y, max_y,
                   max_x - min_x + 1u, max_y - min_y + 1u);
        }
        printf("[FP16] Distinct sampled colors: %u\n", unique_color_count);
        for (uint32_t i = 0; i < unique_color_count; i++)
            printf("  fp16_color[%u] = 0x%016llx\n", i,
                   (unsigned long long)unique_colors[i]);
        printf("[FP16] Opaque samples: %u; out-of-range components: %u\n",
               opaque_samples, out_of_range_components);
#if AGC_NGG_INPUT_LINES || AGC_TESS_GEOMETRY_LINES
        const bool color_pass = unique_color_count == 1u &&
                                unique_colors[0] == 0x3c003c003c003c00ull;
#else
        const bool color_pass = unique_color_count >= 8u;
#endif
        const bool fp16_pass =
                               changed + coverage_tolerance >= expected_changed &&
                               changed <= expected_changed + coverage_tolerance &&
                               color_pass &&
                               opaque_samples != 0u &&
                               out_of_range_components == 0u;
        printf("[FP16] GFX1013 R16G16B16A16_FLOAT target: %s\n",
               fp16_pass ? "PASS" : "FAIL");
        return fp16_pass;
    }

    /* Preserve the exact validation for the registered RGBA8 display RT. */
    uint32_t *rt = (uint32_t *)rt_addr;
    uint32_t changed = 0;
    uint32_t unique_colors[8] = {0};
    uint32_t unique_color_count = 0;
    for (uint32_t i = 0; i < target_pixels; i++) {
        uint32_t color = rt[i];
        if (color == DIAGNOSTIC_CLEAR_COLOR)
            continue;
        changed++;
        if (unique_color_count < 8) {
            bool seen = false;
            for (uint32_t j = 0; j < unique_color_count; j++) {
                if (unique_colors[j] == color) {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                unique_colors[unique_color_count++] = color;
        }
    }
    printf("[Readback] Changed pixels: %u / %u\n", changed, target_pixels);
    printf("[Texture] Distinct sampled colors: %u\n", unique_color_count);
    for (uint32_t i = 0; i < unique_color_count; i++)
        printf("  color[%u] = 0x%08x\n", i, unique_colors[i]);
    const uint32_t viewport_extent =
        target->width < target->height ? target->width : target->height;
#if AGC_TESS_GEOMETRY
    /* The combined TES+GS control shrinks every microtriangle around its
     * centroid by 0.78, reducing ideal RGBA8 coverage by 0.78^2. */
    const uint32_t expected_changed = (uint32_t)
        (((uint64_t)viewport_extent * viewport_extent * 1774u * 1521u) /
         (16384u * 2500u));
#else
    const uint32_t expected_changed = (uint32_t)
        (((uint64_t)viewport_extent * viewport_extent * 1774u) / 16384u);
#endif
    const uint32_t coverage_tolerance = 1024u;
    printf("[Readback] Expected triangle coverage: about %u (+/-%u)\n",
           expected_changed, coverage_tolerance);
    const bool vertex_fetch_pass = changed != 0 && unique_color_count >= 3;
    const bool indexed_draw_pass =
                                   changed + coverage_tolerance >= expected_changed &&
                                   changed <= expected_changed + coverage_tolerance &&
                                   unique_color_count >= 3;
    const bool texture_sampler_pass = indexed_draw_pass &&
                                      unique_color_count >= 8;
    printf("[Vertex] Interleaved buffer fetch: %s\n",
           vertex_fetch_pass ? "PASS" : "FAIL");
    printf("[Index] Bound u16 indexed draw: %s\n",
           indexed_draw_pass ? "PASS" : "FAIL");
    printf("[Texture] gfx1013 image + bilinear sampler: %s\n",
           texture_sampler_pass ? "PASS" : "FAIL");
    return vertex_fetch_pass && indexed_draw_pass && texture_sampler_pass;
}

#if !AGC_VALIDATE_RGBA8_REFERENCE && !AGC_DEPTH_VALIDATION
static uint8_t half_to_unorm8(uint16_t half) {
    uint32_t exponent = (half >> 10) & 0x1fu;
    uint32_t mantissa = half & 0x3ffu;
    if ((half & 0x8000u) != 0u)
        return 0;
    if (exponent == 0u)
        return 0;
    if (exponent >= 15u)
        return 255;
    uint32_t significand = 1024u + mantissa;
    uint32_t shift = 25u - exponent;
    uint32_t scaled = shift < 32u ? (significand * 255u) >> shift : 0u;
    return scaled > 255u ? 255u : (uint8_t)scaled;
}

static void visualize_fp16(GraphicsTest *test) {
    const uint64_t *source = (const uint64_t *)test->render_target;
    uint32_t *display = (uint32_t *)test->buffers[0];
    const uint32_t preview_width = FP16_TARGET_WIDTH / FP16_PREVIEW_DIVISOR;
    const uint32_t preview_height = FP16_TARGET_HEIGHT / FP16_PREVIEW_DIVISOR;
    const uint32_t origin_x = (test->width - preview_width) / 2u;
    const uint32_t origin_y = (test->height - preview_height) / 2u;

    for (uint32_t i = 0; i < test->width * test->height; i++)
        display[i] = DIAGNOSTIC_CLEAR_COLOR;
    for (uint32_t y = 0; y < preview_height; y++) {
        const uint32_t source_y = y * FP16_PREVIEW_DIVISOR;
        for (uint32_t x = 0; x < preview_width; x++) {
            uint64_t pixel = source[source_y * FP16_TARGET_WIDTH +
                                    x * FP16_PREVIEW_DIVISOR];
            if (pixel == FP16_CLEAR_SENTINEL)
                continue;
            uint8_t r = half_to_unorm8((uint16_t)pixel);
            uint8_t g = half_to_unorm8((uint16_t)(pixel >> 16));
            uint8_t b = half_to_unorm8((uint16_t)(pixel >> 32));
            uint8_t a = half_to_unorm8((uint16_t)(pixel >> 48));
            display[(origin_y + y) * test->width + origin_x + x] =
                ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                ((uint32_t)g << 8) | r;
        }
    }
    memcpy(test->buffers[1], test->buffers[0],
           (size_t)test->width * test->height * BYTES_PER_PIXEL);
    printf("[FP16] CPU preview: %ux%u centered on RGBA8 display\n",
           preview_width, preview_height);
}
#endif

/* ======================================================================== */
/* Flip helper                                                               */
/* ======================================================================== */

static bool present_preview(GraphicsTest *test) {
    uint32_t accepted = 0;
    uint32_t completed = 0;
    for (uint32_t frame = 0; frame < FP16_PREVIEW_FRAMES; frame++) {
        const int buffer_index = (int)(frame & 1u);
        int ret = sceVideoOutSubmitFlip(
            test->handle, buffer_index, SCE_VIDEO_OUT_FLIP_MODE_VSYNC,
            (int64_t)frame);
        if (ret != 0) {
            printf("VideoOutSubmitFlip[%u]: 0x%08x\n",
                   frame, (unsigned)ret);
            return false;
        }
        accepted++;

        SceKernelEvent event = {0};
        int out = 0;
        ret = sceKernelWaitEqueue(
            test->flipqueue, &event, 1, &out, NULL);
        if (ret != 0) {
            printf("sceKernelWaitEqueue[%u]: 0x%08x\n",
                   frame, (unsigned)ret);
            return false;
        }
        completed++;
        if ((frame % 60u) == 0u)
            printf("VideoOut displayed frame %u\n", frame);
    }
    printf("VideoOut sustained preview: %u accepted, %u completed\n",
           accepted, completed);
    return completed == FP16_PREVIEW_FRAMES;
}

/* ======================================================================== */
/* Main                                                                      */
/* ======================================================================== */

int main(void) {
    GraphicsTest test = { .handle = -1, .direct_memory = -1 };

    printf("=== openagc NGG Graphics Draw Call Test ===\n");

    printf("\n--- Step 0: GPU credential bypass ---\n");
    int cred_err = set_gpu_credentials();
    printf("GPU credentials: %s\n", cred_err == 0 ? "OK" : "FAILED");
    if (cred_err != 0) return 1;

    printf("\n--- Step 1: AGC initialization ---\n");
    if (!init_agc()) return 1;

    printf("\n--- Step 2: VideoOut initialization ---\n");
    if (!init_videoout(&test)) return 1;

    printf("\n--- Step 3: Shader loading ---\n");
    ParsedGraphicsShader front, back, ps;
    if (!parse_graphics_shader(
            &front, NGG_FRONT_DATA,
            sizeof(NGG_FRONT_DATA), "NGG front")) {
        return 1;
    }
    if (!parse_graphics_shader(
            &back, NGG_BACK_DATA,
            sizeof(NGG_BACK_DATA), "NGG back")) {
        return 1;
    }
    if (!parse_graphics_shader(
            &ps, FRAGMENT_DATA,
            sizeof(FRAGMENT_DATA), "PS")) {
        return 1;
    }
    if (!validate_shader_records(&back, &ps))
        return 1;

#if AGC_DEPTH_VALIDATION
    RenderTargetConfig depth_target = {
        AGC_MSAA_VALIDATION ? test.msaa_color_surface : test.buffers[0],
        test.width, test.height,
        AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
        AGC_GFX1013_SURFACE_NUMBER_UNORM,
        AGC_GFX1013_SURFACE_SWAP_ALT,
        false, AGC_MSAA_VALIDATION ?
            "4x depth validation RGBA8" : "depth validation RGBA8"
    };
    printf("\n--- Step 4: D32%s%s depth pass/fail draw ---\n",
           AGC_STENCIL_VALIDATION ? "+S8 stencil" : "",
           AGC_MSAA_VALIDATION ? "+4x MSAA" : "");
    if (!dispatch_graphics(&test, &front, &back, &ps, &depth_target)) {
        printf("FATAL: D32%s validation failed\n",
               AGC_STENCIL_VALIDATION ? "+S8 stencil" : " depth");
        return 1;
    }
    memcpy(test.buffers[1], test.buffers[0],
           (size_t)test.width * test.height * BYTES_PER_PIXEL);
#elif AGC_VALIDATE_RGBA8_REFERENCE
    RenderTargetConfig rgba8_target = {
        test.buffers[0], test.width, test.height,
        AGC_GFX1013_COLOR_FORMAT_8_8_8_8,
        AGC_GFX1013_SURFACE_NUMBER_UNORM,
        AGC_GFX1013_SURFACE_SWAP_ALT,
        false, "display RGBA8"
    };
    printf("\n--- Step 4: RGBA8 reference draw ---\n");
    if (!dispatch_graphics(&test, &front, &back, &ps, &rgba8_target)) {
        printf("FATAL: RGBA8 reference draw failed\n");
        return 1;
    }
    memcpy(test.buffers[1], test.buffers[0],
           (size_t)test.width * test.height * BYTES_PER_PIXEL);
#else
    RenderTargetConfig fp16_target = {
        test.render_target, FP16_TARGET_WIDTH, FP16_TARGET_HEIGHT,
        AGC_GFX1013_COLOR_FORMAT_16_16_16_16,
        AGC_GFX1013_SURFACE_NUMBER_FLOAT,
        AGC_GFX1013_SURFACE_SWAP_STD,
        true, "offscreen FP16"
    };
    printf("\n--- Step 4: RGBA16F offscreen draw ---\n");
    if (!dispatch_graphics(&test, &front, &back, &ps, &fp16_target)) {
        printf("FATAL: RGBA16F render-target validation failed\n");
        return 1;
    }
    visualize_fp16(&test);
#endif

    printf("\n--- Step 5: Display target preview ---\n");
    if (!present_preview(&test)) {
        printf("FATAL: no VideoOut preview flip was accepted\n");
        return 1;
    }
#if AGC_DEPTH_VALIDATION
    printf("Displayed green depth-pass and red independent-pass triangles%s%s for 30 seconds.\n",
           AGC_STENCIL_VALIDATION ? " with S8 replace validation" : "",
           AGC_MSAA_VALIDATION ? " resolved from 4x MSAA" : "");
#else
    printf("Displayed the compiler-generated NGG triangle for 30 seconds.\n");
#endif

    printf("\nDone.\n");
    return 0;
}
