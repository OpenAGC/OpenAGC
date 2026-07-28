/*
 * agc_compute.c — PS5 AGC compute dispatch test
 *
 * Tests real GPU command execution:
 *   1. Initialize AGC context + internal memory
 *   2. Open VideoOut and allocate display buffers
 *   3. Load a compute shader binary into GPU memory
 *   4. Set compute shader registers (PGM_LO/HI, RSRC1/2/3, NUM_THREAD)
 *   5. Set user data (output buffer descriptor)
 *   6. Dispatch compute shader to fill display buffer with a solid color
 *   7. Flip the display to show GPU-rendered output
 *
 * The compute shader (fill_color.comp) writes a solid color to every pixel
 * of the display buffer via an SSBO binding.
 *
 * Deploy: make agc_compute.elf && make deploy_agc_compute
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "ps5_video_out.h"
#include "agcdriver.h"
#include "agc_cb.h"
#include "agc_error.h"
#include "agc_registers.h"
#include "agc_pm4.h"
#include "agc_shader.h"
#include "gpu_credentials.h"
#include <ps5/kernel.h>

/* PS5 kernel memory constants */
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

#define SCE_KERNEL_WC_GARLIC 3
#define PS5_DIRECT_MEM_SEARCH_END  0x300000000ULL
#define PS5_DIRECT_MEM_ALIGNMENT   0x200000

/* Kernel functions */
int sceKernelUsleep(unsigned int microseconds);
int sceKernelAllocateDirectMemory(
    off_t searchStart, off_t searchEnd, size_t len, size_t alignment,
    int memoryType, off_t *directMemoryStart);
int sceKernelMapDirectMemory(
    void **virtualAddress, size_t len, int prot, int flags,
    off_t directMemoryStart, size_t alignment);
int sceKernelReleaseDirectMemory(off_t directMemoryStart, size_t len);
/* Flexible memory: automatically mapped in both CPU and GPU address spaces.
 * This is what the AGC SPRX uses for internal GPU memory regions. */
int sceKernelMapNamedSystemFlexibleMemory(
    void **addr, size_t len, int memoryType, int flags, const char *name);

/* Event queue */
typedef int SceKernelEqueue;
typedef struct { char _opaque[64]; } SceKernelEvent;
int sceKernelCreateEqueue(SceKernelEqueue *equeue, const char *name);
int sceKernelDeleteEqueue(SceKernelEqueue equeue);
int sceKernelWaitEqueue(SceKernelEqueue equeue, SceKernelEvent *events,
                        int numEvents, int *out, void *timeout);

/* VideoOut patch for linear tiling */
int kernel_dynlib_handle(int pid, const char *name, uint32_t *handle);
intptr_t kernel_dynlib_mapbase_addr(int pid, uint32_t handle);

enum {
    BUFFER_COUNT = 2,
    BYTES_PER_PIXEL = 4,
    DIRECT_MEMORY_ALIGNMENT = 0x200000,
};

/* Command buffer: allocated in garlic memory (GPU-visible) at runtime.
 * The GPU cannot access regular CPU memory (.bss/.data), so the CB must
 * be in direct-mapped garlic memory or flexible memory. We carve space
 * from the display buffer pool. */
static uint32_t *cb_buffer = NULL;
static size_t cb_buffer_dwords = 16384;  /* 64KB */

/* Embedded compute shader binary (fill_color.sb).
 * This is a PS5 AgcShaderRecord-format shader that fills a buffer with
 * a solid color. The shader takes 4 push constants:
 *   0: width, 1: height, 2: pitch, 3: color (RGBA8 packed)
 * And one SSBO binding (binding 0) for the output buffer. */
#include "shaders/fill_color_sb.h"

typedef struct {
    int handle;
    SceKernelEqueue flipqueue;
    off_t direct_memory;
    void *mapped;
    size_t mapped_size;
    uint8_t *buffers[BUFFER_COUNT];
    void *compute_buffer;  /* Flexible memory pool for compute shader & output */
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    size_t buffer_stride;
} ComputeTest;


static size_t align_up(size_t value, size_t alignment) {
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

static const char *errstr(int32_t err) {
    return agcErrorString(err);
}

/* Patch libSceVideoOut to allow linear tiling without debug setting */
static void patch_videoout_linear(void) {
    uint32_t vo_handle = 0;
    if (kernel_dynlib_handle(-1, "libSceVideoOut.sprx", &vo_handle) == 0 && vo_handle) {
        intptr_t vo_base = kernel_dynlib_mapbase_addr(-1, vo_handle);
        if (vo_base) {
            printf("  libSceVideoOut base: 0x%lx\n", (unsigned long)vo_base);
            intptr_t patch_addr = vo_base + 0x7e61;
            kernel_mprotect(-1, patch_addr & ~0xFFF, 0x2000,
                            SCE_KERNEL_PROT_CPU_READ | SCE_KERNEL_PROT_CPU_WRITE | 0x4);
            volatile uint8_t *p = (volatile uint8_t *)patch_addr;
            p[0] = 0x90; p[1] = 0x90; p[2] = 0x90;
            p[3] = 0x90; p[4] = 0x90; p[5] = 0x90;
            kernel_mprotect(-1, patch_addr & ~0xFFF, 0x2000,
                            SCE_KERNEL_PROT_CPU_READ | 0x4);
            printf("  Patched je->nop at offset 0x7e61\n");
        }
    }
}

static bool allocate_display_buffers(ComputeTest *test) {
    const size_t buffer_size =
        (size_t)test->pitch_pixels * test->height * BYTES_PER_PIXEL;
    test->buffer_stride = align_up(buffer_size, DIRECT_MEMORY_ALIGNMENT);
    test->mapped_size = test->buffer_stride * BUFFER_COUNT;

    int res = sceKernelAllocateDirectMemory(
        0, (off_t)PS5_DIRECT_MEM_SEARCH_END, test->mapped_size,
        DIRECT_MEMORY_ALIGNMENT, SCE_KERNEL_WC_GARLIC, &test->direct_memory
    );
    if (res != 0) {
        printf("sceKernelAllocateDirectMemory failed: 0x%x\n", res);
        return false;
    }

    const int prot = 0x33;  /* CPU_RW | GPU_RW */
    res = sceKernelMapDirectMemory(
        &test->mapped, test->mapped_size, prot, 0,
        test->direct_memory, DIRECT_MEMORY_ALIGNMENT
    );
    if (res != 0) {
        printf("sceKernelMapDirectMemory failed: 0x%x\n", res);
        sceKernelReleaseDirectMemory(test->direct_memory, test->mapped_size);
        return false;
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        test->buffers[i] = (uint8_t *)test->mapped + i * test->buffer_stride;
    }

    /* Allocate command buffer AND dedicated compute output buffer in Flexible Memory.
     * Flexible memory is automatically mapped in both CPU and GPU MMU VMID spaces. */
    size_t cb_size = cb_buffer_dwords * 4;
    void *cb_addr = NULL;
    int cb_ret = sceKernelMapNamedSystemFlexibleMemory(
        &cb_addr, cb_size, 0x33, 0, "agc_compute_cb");
    if (cb_ret != 0 || !cb_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for CB: %d\n", cb_ret);
        return false;
    }
    cb_buffer = (uint32_t *)cb_addr;

    /* Allocate 10MB Flexible Memory pool for compute shader output + code */
    size_t pool_size = 16 * 1024 * 1024;
    void *pool_addr = NULL;
    int pool_ret = sceKernelMapNamedSystemFlexibleMemory(
        &pool_addr, pool_size, 0x33, 0, "agc_compute_pool");
    if (pool_ret != 0 || !pool_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for pool: %d\n", pool_ret);
        return false;
    }
    test->compute_buffer = pool_addr;

    printf("Command buffer: %zu bytes at %p (flexible memory, GPU-visible)\n",
           cb_size, cb_buffer);
    printf("Compute buffer: %zu bytes at %p (flexible memory, GPU-visible)\n",
           pool_size, test->compute_buffer);
    printf("Display buffers: %zu bytes each, %d buffers at %p (garlic memory)\n",
           test->buffer_stride, BUFFER_COUNT, test->mapped);
    return true;
}


static bool init_videoout(ComputeTest *test) {
    int32_t user_ids[] = { 0xFF, 0, 1, 2 };
    test->handle = -1;
    for (int i = 0; i < 4; i++) {
        test->handle = sceVideoOutOpen(user_ids[i], SCE_VIDEO_OUT_BUS_TYPE_MAIN, 0, NULL);
        if (test->handle >= 0) {
            printf("sceVideoOutOpen(userId=0x%x) = %d\n", user_ids[i], test->handle);
            break;
        }
    }
    if (test->handle < 0) {
        printf("sceVideoOutOpen failed: 0x%x\n", test->handle);
        return false;
    }

    SceVideoOutResolutionStatus status = {0};
    int res = sceVideoOutGetResolutionStatus(test->handle, &status);
    if (res != 0) {
        printf("sceVideoOutGetResolutionStatus failed: 0x%x\n", res);
        return false;
    }
    printf("Resolution: full=%dx%d pane=%dx%d\n",
           status.full_width, status.full_height,
           status.pane_width, status.pane_height);

    test->width = 1920;
    test->height = 1080;
    test->pitch_pixels = test->width;

    if (!allocate_display_buffers(test)) {
        return false;
    }

    /* Carve shader code space from the end of the display buffer allocation.
     * We have buffer_stride * BUFFER_COUNT bytes mapped, but only use
     * width*height*4 per buffer. The remaining space in each buffer's
     * stride is available for shader code. */
    /* Use space after the last display buffer for shader code */
    /* (handled in upload_shader_code_from_pool) */

    patch_videoout_linear();

    printf("Registering buffers (linear mode)...\n");
    uint8_t attr_raw[64];
    memset(attr_raw, 0, sizeof(attr_raw));
    *(uint32_t *)(attr_raw + 0)  = 0x80000000;  /* pixel format */
    *(uint32_t *)(attr_raw + 4)  = 1;           /* tiling = linear */
    *(uint32_t *)(attr_raw + 8)  = 0;           /* aspect = 16:9 */
    *(uint32_t *)(attr_raw + 12) = test->width;
    *(uint32_t *)(attr_raw + 16) = test->height;
    *(uint32_t *)(attr_raw + 20) = test->pitch_pixels;

    void *addresses[BUFFER_COUNT] = {test->buffers[0], test->buffers[1]};
    res = sceVideoOutRegisterBuffers(
        test->handle, 0, addresses, BUFFER_COUNT,
        (const SceVideoOutBufferAttribute *)attr_raw);
    printf("sceVideoOutRegisterBuffers: 0x%x\n", res);
    if (res < 0) {
        printf("sceVideoOutRegisterBuffers failed: 0x%x\n", res);
        return false;
    }

    res = sceKernelCreateEqueue(&test->flipqueue, "agc_compute flips");
    if (res != 0) {
        printf("sceKernelCreateEqueue failed: 0x%x\n", res);
        return false;
    }
    res = sceVideoOutAddFlipEvent((void *)(uintptr_t)test->flipqueue,
                                  test->handle, NULL);
    if (res != 0) {
        printf("sceVideoOutAddFlipEvent failed: 0x%x\n", res);
        return false;
    }

    sceVideoOutSetFlipRate(test->handle, 0);
    printf("VideoOut: %ux%u pitch=%u stride=%zu\n",
           test->width, test->height, test->pitch_pixels, test->buffer_stride);
    return true;
}

static bool init_agc(void) {
    int32_t err;

    printf("[AGC] sce_agc_initialize()...\n");
    err = sce_agc_initialize();
    printf("[AGC] init result: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("[AGC] FATAL: cannot initialize AGC\n");
        return false;
    }

    printf("[AGC] sce_agc_initialize_internal_memory()...\n");
    err = sce_agc_initialize_internal_memory();
    printf("[AGC] internal memory: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("[AGC] FATAL: cannot allocate internal GPU memory\n");
        return false;
    }

    printf("[AGC] sceAgcDriverNotifyDefaultStates()...\n");
    err = sceAgcDriverNotifyDefaultStates(0);
    printf("[AGC] default states: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK)
        printf("[AGC] WARNING: default state notification failed\n");

    printf("[AGC] sceAgcDriverSetupAsyncGraphics(1)...\n");
    err = sceAgcDriverSetupAsyncGraphics(1);
    printf("[AGC] async graphics: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK)
        printf("[AGC] WARNING: async graphics setup failed\n");

    return true;
}

/* Parse the embedded shader binary and return pointers to its components.
 * The .sb file contains an AgcShaderRecord header (0x60 bytes) followed by
 * the SH register block and code body. The pointers in the record are
 * file-relative offsets. */
typedef struct {
    const AgcShaderRecord *record;
    const uint8_t *code;
    size_t code_size;
    const AgcRegisterValue *sh_regs;
    uint32_t num_sh_regs;
} ParsedShader;

static bool parse_shader(ParsedShader *out) {
    const uint8_t *sb = fill_color_sb;
    size_t sb_size = sizeof(fill_color_sb);

    if (sb_size < sizeof(AgcShaderRecord)) {
        printf("Shader binary too small: %zu bytes\n", sb_size);
        return false;
    }

    const AgcShaderRecord *rec = (const AgcShaderRecord *)sb;
    if (rec->magic != AGC_SHADER_RECORD_MAGIC) {
        printf("Bad shader magic: 0x%08x (expected 0x%08x)\n",
               rec->magic, AGC_SHADER_RECORD_MAGIC);
        return false;
    }
    if (rec->version != AGC_SHADER_RECORD_VERSION_GEN5) {
        printf("Bad shader version: 0x%08x (expected 0x%08x)\n",
               rec->version, AGC_SHADER_RECORD_VERSION_GEN5);
        return false;
    }
    if (rec->shader_type != AGC_SHADER_TYPE_CS) {
        printf("Not a compute shader: type=%u\n", rec->shader_type);
        return false;
    }

    out->record = rec;
    out->num_sh_regs = rec->num_sh_registers;
    out->sh_regs = (const AgcRegisterValue *)(sb + rec->sh_registers);
    out->code = sb + rec->code;
    /* Code size = total file size - code offset */
    out->code_size = sb_size - rec->code;

    printf("Shader: magic=OK version=0x%x type=CS(%u) sh_regs=%u code_size=%zu\n",
           rec->version, rec->shader_type, out->num_sh_regs, out->code_size);

    /* Print SH register entries */
    for (uint32_t i = 0; i < out->num_sh_regs; i++) {
        printf("  SH[%u]: offset=0x%03x value=0x%08x\n",
               i, out->sh_regs[i].offset, out->sh_regs[i].value);
    }

    return true;
}
/* Copy shader code into GPU-accessible memory.
 * Uses space carved from the display buffer pool (after the last buffer). */
static void *upload_shader_code(const uint8_t *code, size_t code_size,
                                ComputeTest *test) {
    /* Upload shader code to the start of test->compute_buffer in Flexible Memory.
     * Flexible memory is mapped in the GPU MMU VMID space so CP can fetch instructions. */
    void *shader_addr = test->compute_buffer;
    memcpy(shader_addr, code, code_size);
    __builtin___clear_cache((char *)shader_addr,
                            (char *)shader_addr + code_size);
    printf("Shader code at %p (flexible memory pool, %zu bytes)\n",
           shader_addr, code_size);
    return shader_addr;
}

/* Build and submit a compute dispatch command buffer.
 *
 * The DCB contains:
 *   1. SET_SH_REG: COMPUTE_PGM_LO/HI (shader address)
 *   2. SET_SH_REG: COMPUTE_PGM_RSRC1/2/3 (from shader record)
 *   3. SET_SH_REG: COMPUTE_NUM_THREAD_X/Y/Z (workgroup size)
 *   4. SET_SH_REG: COMPUTE_USER_DATA_0..5 (user data)
 *   5. DISPATCH_DIRECT: launch compute shader
 *   6. NOP: trailing NOP (ACO convention)
 *
 * User data layout (confirmed from NIR postprocess analysis):
 *   USER_DATA_0: ring_offsets low  (s0, unused — set to 0)
 *   USER_DATA_1: ring_offsets high (s1, unused — set to 0)
 *   USER_DATA_2: buffer ptr low    (s2, inline push const offset 0)
 *   USER_DATA_3: buffer ptr high   (s3, inline push const offset 4)
 *   USER_DATA_4: total_pixels      (s4, inline push const offset 8)
 *   USER_DATA_5: color             (s5, inline push const offset 12)
 *
 * RSRC2 USER_SGPR=6, TGID_X_EN=1:
 *   s0-s5: 6 user SGPRs from USER_DATA_0..5
 *   s6: workgroup_id_x (system, TGID_X)
 *   v0: local_invocation_id_x (system, TIDIG)
 *
 * The shader uses GL_EXT_buffer_reference to access the output buffer
 * via a 64-bit pointer in push constants, avoiding descriptor table
 * indirection. The NIR shows:
 *   base=1,2 → pack_64_2x32_split → 64-bit buffer pointer
 *   base=3   → total_pixels (bounds check)
 *   base=4   → color (stored via @store_global_amd)
 *   base=5   → workgroup_id_x (system)
 *   base=7   → local_invocation_id (system)
 */
#include "agc_graphics.h"

static bool apply_sh_defaults(SceAgcCb *cb)
{
    AgcGfx1013ComputeDefaultStats stats = {0};
    int32_t result = agcGfx1013ApplyComputeDefaultsV8(cb, &stats);

    if (result != AGC_OK) {
        printf("[Dispatch] compute defaults failed: 0x%08x\n",
               (uint32_t)result);
        return false;
    }
    printf("[Dispatch] Applied %u SH defaults in %u packets\n",
           stats.sh_register_count, stats.packet_count);
    return true;
}

static bool dispatch_compute(ComputeTest *test, void *shader_addr,
                             const ParsedShader *shader, uint32_t color)
{
    SceAgcCb cb;
    void *compute_out = (uint8_t *)test->compute_buffer + 0x10000;
    uint32_t total_pixels = test->width * test->height;
    uint32_t user_data[6] = {
        0u,
        0u,
        (uint32_t)(uintptr_t)compute_out,
        (uint32_t)((uintptr_t)compute_out >> 32),
        total_pixels,
        color,
    };
    AgcGfx1013ComputeState state = {
        .record = shader->record,
        .sh_registers = shader->sh_regs,
        .num_sh_registers = shader->num_sh_regs,
        .code_address = (uint64_t)(uintptr_t)shader_addr,
        .user_data = user_data,
        .num_user_data = 6u,
        .local_size_x = 64u,
        .local_size_y = 1u,
        .local_size_z = 1u,
        .group_count_x = (total_pixels + 63u) / 64u,
        .group_count_y = 1u,
        .group_count_z = 1u,
        .modifier = AGC_GFX1013_COMPUTE_DISPATCH_WAVE32,
    };
    uint64_t flex_target = (uint64_t)(uintptr_t)cb_buffer + 0x1000u;
    uint64_t garlic_target = (uint64_t)(uintptr_t)compute_out +
        (size_t)total_pixels * sizeof(uint32_t) - sizeof(uint32_t);
    uint64_t post_dispatch = (uint64_t)(uintptr_t)cb_buffer + 0x1008u;
    volatile uint32_t *completion_fence =
        (volatile uint32_t *)(uintptr_t)post_dispatch;
    uint32_t flex_marker = 0x12345678u;
    uint32_t garlic_marker = 0xcafebabeu;
    uint32_t post_marker = 0xabcdef01u;
    AgcCommandBufferSubmit submit;
    int32_t result;

    agcCbInit(&cb, cb_buffer, cb_buffer_dwords * sizeof(uint32_t));
    *completion_fence = 0u;
    if (!apply_sh_defaults(&cb))
        return false;

    printf("[Dispatch] compute_out=%p pixels=%u color=0x%08x groups=%u\n",
           compute_out, total_pixels, color, state.group_count_x);

    if (!sceAgcDcbWriteData(
            &cb, 2u, 0u, flex_target, &flex_marker, 1u, 0u, 0u) ||
        !sceAgcDcbWriteData(
            &cb, 2u, 0u, garlic_target, &garlic_marker, 1u, 0u, 0u)) {
        printf("[Dispatch] diagnostic WRITE_DATA emission failed\n");
        return false;
    }

    result = agcGfx1013DispatchCompute(&cb, &state);
    if (result != AGC_OK) {
        printf("[Dispatch] compute binding failed: 0x%08x\n",
               (uint32_t)result);
        return false;
    }

    /* Complete compute writes before the CPU validates the output. */
    const AgcGfx1013ResourceTransition completion = {
        .before = AGC_GFX1013_RESOURCE_USAGE_COMPUTE_WRITE,
        .after = AGC_GFX1013_RESOURCE_USAGE_HOST_READ,
        .completion_address = post_dispatch,
        .completion_value = post_marker,
    };
    if (agcGfx1013TransitionResource(&cb, &completion) != AGC_OK) {
        printf("[Dispatch] completion packet emission failed\n");
        return false;
    }

    submit.command_address = (uintptr_t)cb_buffer;
    submit.dword_count = agcCbUsedDwords(&cb);
    submit.reserved = 0u;
    result = sceAgcDriverSubmitDcb(&submit);
    if (result != AGC_OK) {
        printf("[Dispatch] submission failed: 0x%08x\n", (uint32_t)result);
        return false;
    }

    uint32_t waited_us = 0u;
    while (*completion_fence != post_marker && waited_us < 200000u) {
        sceKernelUsleep(1000u);
        waited_us += 1000u;
    }
    if (*completion_fence != post_marker) {
        printf("[Dispatch] GPU completion fence timed out after %u us\n",
               waited_us);
        return false;
    }
    printf("[Dispatch] GPU completion fence reached after %u us\n",
           waited_us);
    return true;
}

static void wait_for_flip(ComputeTest *test) {
    SceKernelEvent events[1];
    int out = 0;
    sceKernelWaitEqueue(test->flipqueue, events, 1, &out, NULL);
}

int main(void) {
    ComputeTest test = { .handle = -1, .direct_memory = -1 };
    init_agc();
    init_videoout(&test);
    /* Step 3: Parse and upload shader */
    printf("\n--- Step 3: Shader loading ---\n");

    ParsedShader shader;
    if (!parse_shader(&shader)) {
        printf("FATAL: shader parse failed\n");
        return 1;
    }

    /* Use full 76-byte GLSL compiled shader from psbc */
    void *shader_gpu_addr = upload_shader_code(shader.code, shader.code_size, &test);
    if (!shader_gpu_addr) {
        printf("FATAL: shader upload failed\n");
        return 1;
    }



    /* Step 4: Compute dispatch */
    printf("\n--- Step 4: Compute dispatch ---\n");
    uint32_t fill_color = 0xFF00FF00;
    uint32_t *compute_out_pre = (uint32_t *)((uint8_t *)test.compute_buffer + 0x10000);
    for (uint32_t i = 0; i < test.width * test.height; i++) compute_out_pre[i] = 0xDEADBEEF;

    if (!dispatch_compute(&test, shader_gpu_addr, &shader, fill_color))
        return 1;

    /* Step 5: Verify GPU output */
    printf("\n--- Step 5: Verify GPU output ---\n");
    uint32_t *buf0 = (uint32_t *)((uint8_t *)test.compute_buffer + 0x10000);
    uint32_t expected = fill_color;
    uint32_t total_pixels = test.width * test.height;

    /* Check WRITE_DATA markers */
    uint32_t *flex_marker = (uint32_t *)((uint8_t *)cb_buffer + 0x1000);
    uint32_t *post_marker = (uint32_t *)((uint8_t *)cb_buffer + 0x1008);
    printf("[Readback] WRITE_DATA #1 flex (CB+0x1000): 0x%08x (expecting 0x12345678)\n", *flex_marker);
    printf("[Readback] WRITE_DATA #4 post (CB+0x1008): 0x%08x (expecting 0xABCDEF01)\n", *post_marker);
    printf("[Readback] WRITE_DATA #2 compute[last]: 0x%08x (expecting 0xCAFEBABE)\n", buf0[total_pixels - 1]);
    printf("[Readback] WRITE_DATA #3 compute[0]:    0x%08x (expecting 0x%08x)\n", buf0[0], expected);

    printf("[Readback] Sample pixels:\n");
    uint32_t sample_indices[] = {0, 1, 63, 64, 127, 128, 1000, 1920, 10000, 100000, 2073598};
    for (int i = 0; i < (int)(sizeof(sample_indices)/sizeof(sample_indices[0])); i++) {
        uint32_t idx = sample_indices[i];
        printf("  pixel[%u] = 0x%08x %s\n", idx, buf0[idx],
               buf0[idx] == expected ? "OK" : "MISMATCH");
    }

    uint32_t match_count = 0;
    for (uint32_t i = 0; i < total_pixels; i++) {
        if (buf0[i] == expected) match_count++;
    }
    printf("[Readback] Total: %u/%u pixels match\n", match_count, total_pixels);
    bool output_complete = match_count == total_pixels;
    if (!output_complete)
        printf("[Readback] FAIL: compute dispatch did not fill the entire buffer\n");


    /* Copy rendered output to display buffer */
    memcpy(test.buffers[0], buf0, total_pixels * 4);

    int flip_result = sceVideoOutSubmitFlip(
        test.handle, 0, SCE_VIDEO_OUT_FLIP_MODE_VSYNC, 0);
    if (flip_result != 0) {
        printf("[Display] sceVideoOutSubmitFlip failed: 0x%x\n", flip_result);
        return 1;
    }
    wait_for_flip(&test);
    printf("[Display] GPU output flip completed\n");
    sceKernelUsleep(1000000);

    return output_complete ? 0 : 1;
}
