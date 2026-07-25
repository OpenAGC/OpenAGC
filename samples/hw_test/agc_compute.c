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

    /* Allocate command buffer in flexible memory (GPU-visible).
     * Garlic memory (sceKernelMapDirectMemory) is NOT automatically mapped
     * in the GPU's VMID address space. Flexible memory IS automatically
     * mapped in both CPU and GPU address spaces, which is why the AGC SPRX
     * uses it for all internal GPU memory regions. */
    size_t cb_size = cb_buffer_dwords * 4;
    void *cb_addr = NULL;
    int cb_ret = sceKernelMapNamedSystemFlexibleMemory(
        &cb_addr, cb_size, 0x33, 0, "agc_compute_cb");
    if (cb_ret != 0 || !cb_addr) {
        printf("sceKernelMapNamedSystemFlexibleMemory failed for CB: %d\n", cb_ret);
        /* Fallback: use garlic memory (may not work with GPU) */
        cb_buffer = (uint32_t *)((uint8_t *)test->mapped +
                                 test->buffer_stride * BUFFER_COUNT);
        printf("WARNING: CB fallback to garlic memory at %p\n", cb_buffer);
    } else {
        cb_buffer = (uint32_t *)cb_addr;
        printf("Command buffer: %zu bytes at %p (flexible memory, GPU-visible)\n",
               cb_size, cb_buffer);
    }
    printf("Display buffers: %zu bytes each, %d buffers\n",
           test->buffer_stride, BUFFER_COUNT);
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
    /* Place shader code after the last display buffer, within the already
     * mapped garlic memory. The buffer_stride is 8MB but each buffer only
     * uses width*height*4 = ~8MB, so there's no extra space in the stride.
     * Instead, use the second buffer's unused tail space.
     * Actually, buffer_stride = align_up(1920*1080*4, 2MB) = 8MB.
     * The actual buffer uses 1920*1080*4 = 8,294,400 bytes.
     * buffer_stride = 8,388,608 bytes. Difference = 94,208 bytes.
     * That's plenty for 76 bytes of shader code.
     *
     * But we need the shader to be at a GPU-visible address that's not
     * part of the registered display buffer. Let's use the gap between
     * buffer 0 and buffer 1. */
    size_t buf_actual = (size_t)test->width * test->height * BYTES_PER_PIXEL;
    size_t gap = test->buffer_stride - buf_actual;
    if (gap < align_up(code_size, 256)) {
        /* Not enough gap space — use the tail after the last buffer */
        size_t total_used = test->buffer_stride * BUFFER_COUNT;
        size_t remaining = test->mapped_size - total_used;
        if (remaining < code_size) {
            printf("No space for shader code in display memory pool\n");
            return NULL;
        }
        void *shader_addr = (uint8_t *)test->mapped + total_used;
        memcpy(shader_addr, code, code_size);
        __builtin___clear_cache((char *)shader_addr,
                                (char *)shader_addr + code_size);
        printf("Shader code at %p (tail of pool, %zu bytes)\n",
               shader_addr, code_size);
        return shader_addr;
    }

    /* Use gap between buffer 0 and buffer 1 */
    void *shader_addr = (uint8_t *)test->mapped + buf_actual;
    memcpy(shader_addr, code, code_size);
    __builtin___clear_cache((char *)shader_addr,
                            (char *)shader_addr + code_size);
    printf("Shader code at %p (gap in pool, %zu bytes, gap=%zu)\n",
           shader_addr, code_size, gap);
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
static bool dispatch_compute(ComputeTest *test, void *shader_addr,
                             const ParsedShader *shader, uint32_t color) {
    SceAgcCb cb;
    agcCbInit(&cb, cb_buffer, cb_buffer_dwords * 4);

    /* CONTEXT_CONTROL — notify CP of context state transition.
     * RE'd from freegnm CLEAR_STATE_SEQUENCE:
     *   0xc0012800, 0x80000000, 0x80000000
     * This enables loading context from default state. */
    uint32_t *cc = agcCbAllocDwords(&cb, 3);
    if (cc) {
        cc[0] = agcPm4Header3(0x28, 3);  /* CONTEXT_CONTROL */
        cc[1] = 0x80000000u;  /* LOAD_ENABLE_CONTEXT */
        cc[2] = 0x80000000u;
        printf("[Dispatch] CONTEXT_CONTROL: load enable\n");
    }

    /* --- Set compute shader registers ---
     * SET_SH_REG writes CONTIGUOUS registers starting from the base offset.
     * The .offset field of only the FIRST register matters; subsequent
     * registers must be at base+1, base+2, etc.
     * Register groups are non-contiguous, so we need separate calls:
     *   PGM_LO/HI:      0x20C-0x20D (2 contiguous)
     *   RSRC1/2:        0x212-0x213 (2 contiguous)
     *   RSRC3:          0x228       (standalone)
     *   NUM_THREAD_X/Y/Z: 0x207-0x209 (3 contiguous)
     *   USER_DATA_0..5: 0x240-0x245 (6 contiguous)
     */
    /* PGM_LO/HI encode a 48-bit GPU address:
     *   PGM_LO = bits [39:8] of address (value << 8 reconstructs)
     *   PGM_HI = bits [47:40] of address (only lower 8 bits used)
     * The address must be 256-byte aligned (bits [7:0] = 0).
     * RE'd from KytyPS5 pm4Handlers.cpp HwShSetCsRegister. */
    uint32_t shader_addr_lo = (uint32_t)((uintptr_t)shader_addr >> 8);
    uint32_t shader_addr_hi = (uint32_t)((uintptr_t)shader_addr >> 40) & 0xFF;

    /* Extract RSRC values from shader record */
    uint32_t rsrc1 = 0, rsrc2 = 0, rsrc3 = 0;
    for (uint32_t i = 0; i < shader->num_sh_regs; i++) {
        uint32_t off = shader->sh_regs[i].offset;
        if (off == AGC_REG_COMPUTE_PGM_RSRC1) rsrc1 = shader->sh_regs[i].value;
        if (off == AGC_REG_COMPUTE_PGM_RSRC2) rsrc2 = shader->sh_regs[i].value;
        if (off == AGC_REG_COMPUTE_PGM_RSRC3) rsrc3 = shader->sh_regs[i].value;
    }

    /* Group 0: COMPUTE_RESOURCE_LIMITS at 0x215 (1 register)
     * This register limits the number of workgroups that can be dispatched.
     * If it's 0 (default without CLEAR_STATE), no workgroups will execute.
     * Set to max to allow unlimited workgroups.
     * RE'd from KytyPS5 pm4.h: COMPUTE_RESOURCE_LIMITS = 0x215
     *
     * CRITICAL: SET_SH_REG bit 0 = shader type (0=graphics, 1=compute).
     * Without bit 0 set, registers go to the graphics engine, not compute.
     * RE'd from freegnm setshregcompute(): PKT3_SHADER_TYPE_S(1). */
    AgcRegisterValue res_limit_reg[] = {
        { 0x215, 0x3FFFFFFFu },
    };
    printf("[Dispatch] SET_SH_REG RESOURCE_LIMITS (0x215): 0x3FFFFFFF\n");
    uint32_t *cmd = sceAgcCbSetShRegistersDirect(&cb, res_limit_reg, 1);
    if (!cmd) { printf("[Dispatch] ERROR: failed to set RESOURCE_LIMITS\n"); return false; }
    *cmd |= 1u;  /* compute shader type */

    /* Group 1: NUM_THREAD_X/Y/Z at 0x207 (3 contiguous) */
    AgcRegisterValue thread_regs[3] = {
        { AGC_REG_COMPUTE_NUM_THREAD_X, 64 },
        { AGC_REG_COMPUTE_NUM_THREAD_Y, 1 },
        { AGC_REG_COMPUTE_NUM_THREAD_Z, 1 },
    };
    printf("[Dispatch] SET_SH_REG NUM_THREAD (0x207): 64,1,1\n");
    cmd = sceAgcCbSetShRegistersDirect(&cb, thread_regs, 3);
    if (!cmd) { printf("[Dispatch] ERROR: failed to set NUM_THREAD\n"); return false; }
    *cmd |= 1u;  /* compute shader type */

    /* Group 2: PGM_LO/HI at 0x20C (2 contiguous) */
    AgcRegisterValue pgm_regs[2] = {
        { AGC_REG_COMPUTE_PGM_LO, shader_addr_lo },
        { AGC_REG_COMPUTE_PGM_HI, shader_addr_hi },
    };
    printf("[Dispatch] SET_SH_REG PGM_LO/HI (0x20C): 0x%08x, 0x%08x\n",
           shader_addr_lo, shader_addr_hi);
    cmd = sceAgcCbSetShRegistersDirect(&cb, pgm_regs, 2);
    if (!cmd) { printf("[Dispatch] ERROR: failed to set PGM_LO/HI\n"); return false; }
    *cmd |= 1u;  /* compute shader type */

    /* Group 3: RSRC1/2 at 0x212 (2 contiguous) */
    AgcRegisterValue rsrc12_regs[2] = {
        { AGC_REG_COMPUTE_PGM_RSRC1, rsrc1 },
        { AGC_REG_COMPUTE_PGM_RSRC2, rsrc2 },
    };
    printf("[Dispatch] SET_SH_REG RSRC1/2 (0x212): 0x%08x, 0x%08x\n", rsrc1, rsrc2);
    cmd = sceAgcCbSetShRegistersDirect(&cb, rsrc12_regs, 2);
    if (!cmd) { printf("[Dispatch] ERROR: failed to set RSRC1/2\n"); return false; }
    *cmd |= 1u;  /* compute shader type */

    /* Group 4: RSRC3 at 0x228 (standalone) */
    AgcRegisterValue rsrc3_reg[1] = {
        { AGC_REG_COMPUTE_PGM_RSRC3, rsrc3 },
    };
    printf("[Dispatch] SET_SH_REG RSRC3 (0x228): 0x%08x\n", rsrc3);
    cmd = sceAgcCbSetShRegistersDirect(&cb, rsrc3_reg, 1);
    if (!cmd) { printf("[Dispatch] ERROR: failed to set RSRC3\n"); return false; }
    *cmd |= 1u;  /* compute shader type */

    /* --- Set user data registers ---
     * Confirmed from openagc-psbc NIR postprocess output:
     *   %5 = @load_scalar_arg_amd (base=1) → buf ptr low  → USER_DATA_1
     *   %6 = @load_scalar_arg_amd (base=2) → buf ptr high → USER_DATA_2
     *   %7 = @load_scalar_arg_amd (base=3) → total_pixels → USER_DATA_3
     *   %8 = @load_scalar_arg_amd (base=4) → color        → USER_DATA_4
     *   %0 = @load_scalar_arg_amd (base=5) → workgroup_id_x (system, USER_DATA_5)
     * USER_DATA_0 is unused (ring offset placeholder). */
    uint32_t buf_addr_lo = (uint32_t)(uintptr_t)test->buffers[0];
    uint32_t buf_addr_hi = (uint32_t)((uintptr_t)test->buffers[0] >> 32);
    uint32_t total_pixels = test->width * test->height;

    AgcRegisterValue user_data[6];
    user_data[0] = (AgcRegisterValue){ AGC_REG_COMPUTE_USER_DATA_0,     0 };           /* unused (s0) */
    user_data[1] = (AgcRegisterValue){ AGC_REG_COMPUTE_USER_DATA_0 + 1, buf_addr_lo }; /* buffer ptr low (s1) */
    user_data[2] = (AgcRegisterValue){ AGC_REG_COMPUTE_USER_DATA_0 + 2, buf_addr_hi }; /* buffer ptr high (s2) */
    user_data[3] = (AgcRegisterValue){ AGC_REG_COMPUTE_USER_DATA_0 + 3, total_pixels };/* total pixels (s3) */
    user_data[4] = (AgcRegisterValue){ AGC_REG_COMPUTE_USER_DATA_0 + 4, color };       /* fill color (s4) */
    user_data[5] = (AgcRegisterValue){ AGC_REG_COMPUTE_USER_DATA_0 + 5, 0 };           /* workgroup_id_x (system, s5) */

    printf("[Dispatch] SET_SH_REG USER_DATA (0x240): buf=0x%x_%08x pixels=%u color=0x%08x\n",
           buf_addr_hi, buf_addr_lo, total_pixels, color);
    cmd = sceAgcCbSetShRegistersDirect(&cb, user_data, 6);
    if (!cmd) {
        printf("[Dispatch] ERROR: failed to set user data\n");
        return false;
    }
    *cmd |= 1u;  /* compute shader type */

    /* --- Test 1: WRITE_DATA — write a marker value to test GPU execution.
     * We write to TWO targets:
     *   a) The CB buffer itself (flexible memory, known GPU-visible)
     *   b) The display buffer (garlic memory, may not be GPU-visible)
     * This tells us if the GPU is executing commands and whether garlic
     * memory is accessible from the GPU.
     *
     * WRITE_DATA (opcode 0x37) format:
     *   header: agcPm4Header3(0x37, 6)
     *   word 1: control = src_sel(2=inline) | dst_sel(0=mem) | wr_confirm(1)
     *   word 2: dst_addr_lo
     *   word 3: dst_addr_hi
     *   word 4: data
     * Total = 5 dwords (header + 4 body). count = 5-2 = 3. */

    /* Write to flexible memory (CB buffer + offset 0x1000 — past the DCB itself) */
    uint64_t flex_target = (uint64_t)(uintptr_t)cb_buffer + 0x1000;
    uint32_t *wd1 = agcCbAllocDwords(&cb, 5);
    if (wd1) {
        wd1[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 5);
        wd1[1] = (2u << 0) | (0u << 2) | (1u << 8);
        wd1[2] = (uint32_t)flex_target;
        wd1[3] = (uint32_t)(flex_target >> 32);
        wd1[4] = 0x12345678u;  /* marker in flexible memory */
        printf("[Dispatch] WRITE_DATA #1: 0xFLEX1234 → flexible mem 0x%lx (CB+0x1000)\n",
               (unsigned long)flex_target);
    }

    /* Write to garlic memory (display buffer last pixel) */
    uint64_t garlic_target = (uint64_t)(uintptr_t)test->buffers[0] +
                             (size_t)test->width * test->height * 4 - 4;
    uint32_t *wd2 = agcCbAllocDwords(&cb, 5);
    if (wd2) {
        wd2[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 5);
        wd2[1] = (2u << 0) | (0u << 2) | (1u << 8);
        wd2[2] = (uint32_t)garlic_target;
        wd2[3] = (uint32_t)(garlic_target >> 32);
        wd2[4] = 0xCAFEBABE;  /* marker in garlic memory */
        printf("[Dispatch] WRITE_DATA #2: 0xCAFEBABE → garlic mem 0x%lx (buf[last])\n",
               (unsigned long)garlic_target);
    }

    /* Write fill color to first pixel of display buffer via WRITE_DATA */
    uint64_t first_pixel = (uint64_t)(uintptr_t)test->buffers[0];
    uint32_t *wd3 = agcCbAllocDwords(&cb, 5);
    if (wd3) {
        wd3[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 5);
        wd3[1] = (2u << 0) | (0u << 2) | (1u << 8);
        wd3[2] = (uint32_t)first_pixel;
        wd3[3] = (uint32_t)(first_pixel >> 32);
        wd3[4] = color;  /* write fill color to pixel[0] */
        printf("[Dispatch] WRITE_DATA #3: 0x%08x → garlic mem 0x%lx (buf[0])\n",
               color, (unsigned long)first_pixel);
    }

    /* --- Dispatch compute ---
     * DISPATCH_DIRECT also needs the compute shader type bit (bit 0) set,
     * same as SET_SH_REG. Without it, the dispatch goes to the graphics
     * engine and no compute workgroups are launched. */
    uint32_t num_groups_x = (total_pixels + 63) / 64;
    printf("[Dispatch] Dispatching %u workgroups (64 threads each) for %u pixels...\n",
           num_groups_x, total_pixels);
    cmd = sceAgcCbDispatch(&cb, num_groups_x, 1, 1, 0);
    if (!cmd) {
        printf("[Dispatch] ERROR: failed to build dispatch packet\n");
        return false;
    }
    *cmd |= 1u;  /* compute shader type */

    /* WRITE_DATA #4: write marker AFTER dispatch to check if GPU is still alive */
    uint64_t post_dispatch = (uint64_t)(uintptr_t)cb_buffer + 0x1008;
    uint32_t *wd4 = agcCbAllocDwords(&cb, 5);
    if (wd4) {
        wd4[0] = agcPm4Header3(AGC_PM4_OP_WRITE_DATA, 5);
        wd4[1] = (2u << 0) | (0u << 2) | (1u << 8);
        wd4[2] = (uint32_t)post_dispatch;
        wd4[3] = (uint32_t)(post_dispatch >> 32);
        wd4[4] = 0xABCDEF01u;  /* marker after dispatch */
        printf("[Dispatch] WRITE_DATA #4: 0xABCDEF01u → after dispatch\n");
    }

    /* --- ACQUIRE_MEM: flush GPU caches after dispatch ---
     * On GFX10/GFX11, ACQUIRE_MEM (opcode 0x58) flushes the L2 cache
     * so CPU-visible memory writes are committed.
     * Format: header + 5 dwords (coher_cntl, coher_size_lo, coher_size_hi,
     *         engine_sel | coher_base_lo, coher_base_hi)
     * coher_cntl=0x2ec47fc0 from freegnm CLEAR_STATE_SEQUENCE reference. */
    uint32_t *am = agcCbAllocDwords(&cb, 6);
    if (am) {
        am[0] = agcPm4Header3(AGC_PM4_OP_ACQUIRE_MEM, 6);
        am[1] = 0x2ec47fc0u;  /* coher_cntl: flush all caches */
        am[2] = 0xFFFFFFFFu;  /* coher_size_lo */
        am[3] = 0;  /* coher_size_hi */
        am[4] = 0;  /* coher_base_lo */
        am[5] = 0;  /* coher_base_hi */
        printf("[Dispatch] ACQUIRE_MEM: cache flush (coher_cntl=0x2ec47fc0)\n");
    }

    /* Trailing NOP (ACO convention) */
    sceAgcCbNop(&cb, 2);

    uint32_t used_dwords = agcCbUsedDwords(&cb);
    printf("[Dispatch] DCB built: %u dwords\n", used_dwords);

    /* Dump DCB content for debugging */
    printf("[Dispatch] DCB dump (first %u dwords):\n", used_dwords > 64 ? 64 : used_dwords);
    for (uint32_t i = 0; i < used_dwords && i < 64; i++) {
        printf("  [%2u] 0x%08x", i, cb_buffer[i]);
        if ((i & 3) == 3) printf("\n");
    }
    if ((used_dwords & 3) != 0) printf("\n");

    /* Submit the DCB */
    AgcCommandBufferSubmit submit;
    submit.command_address = (uintptr_t)cb_buffer;
    submit.dword_count = used_dwords;
    submit.reserved = 0;

    int32_t err = sceAgcDriverSubmitDcb(&submit);
    printf("[Dispatch] SubmitDcb: 0x%08X (%s)\n", (unsigned)err, errstr(err));
    if (err != AGC_OK) {
        printf("[Dispatch] FAILED: GPU rejected command buffer\n");
        return false;
    }

    /* Wait for GPU to finish — the submit is asynchronous.
     * The GPU processes the DCB in the background. We need to wait
     * for the compute shader to finish before reading back results.
     * Use a simple sleep since we don't have a fence mechanism yet. */
    printf("[Dispatch] Waiting 100ms for GPU to finish...\n");
    sceKernelUsleep(100000);  /* 100ms */

    return true;
}

static void wait_for_flip(ComputeTest *test) {
    SceKernelEvent events[1];
    int out = 0;
    sceKernelWaitEqueue(test->flipqueue, events, 1, &out, NULL);
}

int main(void) {
    ComputeTest test = {
        .handle = -1,
        .direct_memory = -1,
    };

    printf("=== openagc Compute Dispatch Test ===\n");

    /* Step 0: GPU credentials */
    printf("\n--- Step 0: GPU credential bypass ---\n");
    int cred_err = set_gpu_credentials();
    printf("GPU credentials: %s\n", cred_err == 0 ? "OK" : "FAILED");
    if (cred_err != 0) {
        printf("FATAL: cannot set GPU credentials\n");
        return 1;
    }

    /* Step 1: AGC init */
    printf("\n--- Step 1: AGC initialization ---\n");
    if (!init_agc()) {
        printf("FATAL: AGC init failed\n");
        return 1;
    }

    /* Step 2: VideoOut init */
    printf("\n--- Step 2: VideoOut initialization ---\n");
    if (!init_videoout(&test)) {
        printf("FATAL: VideoOut init failed\n");
        return 1;
    }

    /* Step 3: Parse and upload shader */
    printf("\n--- Step 3: Shader loading ---\n");
    ParsedShader shader;
    if (!parse_shader(&shader)) {
        printf("FATAL: shader parse failed\n");
        return 1;
    }

    void *shader_gpu_addr = upload_shader_code(shader.code, shader.code_size, &test);
    if (!shader_gpu_addr) {
        printf("FATAL: shader upload failed\n");
        return 1;
    }

    /* Step 4: Dispatch compute to fill display buffer */
    printf("\n--- Step 4: Compute dispatch ---\n");
    /* Fill with green (ABGR: A=0xFF, B=0x00, G=0xFF, R=0x00 = 0xFF00FF00) */
    uint32_t fill_color = 0xFF00FF00;  /* Green in ABGR */

    /* Pre-fill buffer 0 with a marker pattern to detect if shader executes */
    uint32_t *buf0_pre = (uint32_t *)test.buffers[0];
    for (uint32_t i = 0; i < test.width * test.height; i++) {
        buf0_pre[i] = 0xDEADBEEF;
    }
    printf("[Pre-fill] Buffer 0 filled with 0xDEADBEEF marker\n");

    if (!dispatch_compute(&test, shader_gpu_addr, &shader, fill_color)) {
        printf("FATAL: compute dispatch failed\n");
        return 1;
    }

    /* Step 5: Wait for GPU to finish, verify output, then flip */
    printf("\n--- Step 5: Verify GPU output ---\n");
    /* Give the GPU some time to finish */
    sceKernelUsleep(200000);  /* 200ms */

    /* CPU readback: check if the shader actually wrote pixels */
    uint32_t *buf0 = (uint32_t *)test.buffers[0];
    uint32_t expected = fill_color;
    uint32_t total_pixels = test.width * test.height;
    uint32_t match_count = 0;
    uint32_t mismatch_count = 0;
    uint32_t first_mismatch_idx = 0xFFFFFFFF;
    uint32_t first_mismatch_val = 0;

    /* Check WRITE_DATA markers */
    uint32_t *flex_marker = (uint32_t *)((uint8_t *)cb_buffer + 0x1000);
    printf("[Readback] WRITE_DATA flexible mem (CB+0x1000): 0x%08x (expecting 0x12345678)\n",
           *flex_marker);
    uint32_t *post_marker = (uint32_t *)((uint8_t *)cb_buffer + 0x1008);
    printf("[Readback] WRITE_DATA post-dispatch (CB+0x1008): 0x%08x (expecting 0xABCDEF01u)\n",
           *post_marker);
    printf("[Readback] WRITE_DATA garlic mem (buf[last]): 0x%08x (expecting 0xCAFEBABE)\n",
           buf0[total_pixels - 1]);

    printf("[Readback] Checking display buffer 0 (expecting 0x%08x)...\n", expected);
    uint32_t sample_indices[] = {0, 1, 100, 1000, 1920, 10000, 100000, 500000, 1000000, 2073598};
    for (int i = 0; i < (int)(sizeof(sample_indices)/sizeof(sample_indices[0])); i++) {
        uint32_t idx = sample_indices[i];
        if (idx >= total_pixels) continue;
        printf("  pixel[%u] = 0x%08x %s\n", idx, buf0[idx],
               buf0[idx] == expected ? "OK" : "MISMATCH");
    }
    /* Full scan */
    for (uint32_t i = 0; i < total_pixels; i++) {
        if (buf0[i] == expected) {
            match_count++;
        } else {
            if (mismatch_count == 0) {
                first_mismatch_idx = i;
                first_mismatch_val = buf0[i];
            }
            mismatch_count++;
        }
    }
    printf("[Readback] %u/%u pixels match (expected 0x%08x)\n",
           match_count, total_pixels, expected);
    if (mismatch_count > 0) {
        printf("[Readback] First mismatch at pixel %u: got 0x%08x (expected 0x%08x)\n",
               first_mismatch_idx, first_mismatch_val, expected);
        if (match_count == 0) {
            printf("[Readback] WARNING: No pixels match — shader may not have executed,\n");
            printf("              or user data layout is wrong, or buffer address is wrong.\n");
        } else {
            printf("[Readback] Partial match — some pixels written correctly.\n");
        }
    } else {
        printf("[Readback] SUCCESS: All pixels match the expected color!\n");
    }

    /* Submit flip to show the GPU-filled buffer */
    int32_t err = sceVideoOutSubmitFlip(test.handle, 0, SCE_VIDEO_OUT_FLIP_MODE_VSYNC, 0);
    printf("sceVideoOutSubmitFlip(0): 0x%x\n", err);
    if (err == 0) {
        wait_for_flip(&test);
        printf("Flip 0 done — GPU-rendered frame should be visible\n");
    }

    /* Keep displaying for 5 seconds */
    printf("\n--- Step 6: Hold display for 5 seconds ---\n");
    sceKernelUsleep(5000000);

    /* Submit second flip with a CPU-rendered red buffer for visual comparison */
    printf("\n--- Step 7: Second flip (CPU-rendered red) ---\n");
    /* Clear buffer 1 to red using CPU for comparison */
    uint32_t *buf1 = (uint32_t *)test.buffers[1];
    for (uint32_t i = 0; i < test.width * test.height; i++) {
        buf1[i] = 0xFF0000FF;  /* Red in ABGR */
    }

    err = sceVideoOutSubmitFlip(test.handle, 1, SCE_VIDEO_OUT_FLIP_MODE_VSYNC, 1);
    printf("sceVideoOutSubmitFlip(1): 0x%x\n", err);
    if (err == 0) {
        wait_for_flip(&test);
        printf("Flip 1 done — CPU-rendered red frame should be visible\n");
    }

    sceKernelUsleep(3000000);

    /* Cleanup */
    printf("\n--- Cleanup ---\n");
    sceVideoOutSubmitFlip(test.handle, -1, 0, 0);
    sceKernelDeleteEqueue(test.flipqueue);
    sceVideoOutClose(test.handle);
    if (test.direct_memory >= 0)
        sceKernelReleaseDirectMemory(test.direct_memory, test.mapped_size);

    printf("\n=== Summary ===\n");
    printf("  AGC initialized:     yes\n");
    printf("  VideoOut initialized: yes\n");
    printf("  Shader loaded:       yes\n");
    printf("  Compute dispatched:  yes\n");
    printf("  Display flipped:     yes\n");
    printf("=== Done ===\n");

    return 0;
}
