/*
 * emu_sharpemu.c — Comprehensive AGC HLE test for SharpEmu
 *
 * Tests OpenAGC's ABI compatibility against SharpEmu's HLE implementations.
 * Uses NID-based imports (ps5-payload-sdk stubs) that SharpEmu resolves via
 * the NID fallback mechanism.
 *
 * Build: make emu_sharpemu
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* === sceAgc* imports (NID-based, resolved by SharpEmu HLE) === */

extern int32_t sceAgcInit(void *state, uint32_t version);
extern int32_t sceAgcGetRegisterDefaults2(uint32_t version, void *out_buf);
extern int32_t sceAgcGetRegisterDefaults2Internal(uint32_t version, void *out_buf);
extern int printf(const char *fmt, ...);

/* SceAgcCb — proper cursor layout (0x38 bytes) */
typedef struct {
    uintptr_t reserved0;    /* 0x00: buffer base */
    uintptr_t reserved1;    /* 0x08: 0 */
    uintptr_t cursor_up;    /* 0x10: write cursor */
    uintptr_t cursor_down;  /* 0x18: end of buffer */
    uintptr_t callback;     /* 0x20: 0 */
    uintptr_t reserved2;    /* 0x28: 0 */
    uint32_t  reserved_dw;  /* 0x30: 0 */
    uint32_t  reserved3;    /* 0x34: 0 */
} SceAgcCb;

_Static_assert(offsetof(SceAgcCb, cursor_up) == 0x10, "cursor_up");
_Static_assert(offsetof(SceAgcCb, cursor_down) == 0x18, "cursor_down");
_Static_assert(sizeof(SceAgcCb) == 0x38, "SceAgcCb size");

/* AgcCommandBufferSubmit */
typedef struct {
    uintptr_t command_address;
    uint32_t  dword_count;
    uint32_t  reserved;
} AgcCommandBufferSubmit;
_Static_assert(sizeof(AgcCommandBufferSubmit) == 0x10, "submit size");

/* === CB builders (cursor-based, take SceAgcCb* in rdi) === */

extern uint32_t *sceAgcCbNop(SceAgcCb *cb, uint32_t dword_count);
extern uint32_t *sceAgcCbDispatch(SceAgcCb *cb, uint32_t gx, uint32_t gy, uint32_t gz, uint32_t modifier);
extern uint32_t *sceAgcCbSetShRegistersDirect(SceAgcCb *cb, const void *regs, uint32_t count);
extern uint32_t *sceAgcCbSetShRegisterRangeDirect(SceAgcCb *cb, uint32_t offset, const void *values, uint32_t count);
extern uint32_t *sceAgcCbReleaseMem(SceAgcCb *cb, uint32_t action, uint16_t gcr_ctrl,
    uint32_t dst, uint32_t cache_policy, uintptr_t dst_addr,
    uint64_t data_sel, uint64_t data, uint64_t gds_off, uint64_t gds_size,
    uint64_t intr, uint64_t intr_ctx);

/* === DCB builders === */

extern uint32_t *sceAgcDcbDrawIndexAuto(SceAgcCb *cb, uint32_t index_count, uint32_t modifier);
extern uint32_t *sceAgcDcbDrawIndex(SceAgcCb *cb, uint32_t index_count, uintptr_t index_addr, uint32_t modifier);
extern uint32_t *sceAgcDcbDrawIndexOffset(SceAgcCb *cb, uint32_t index_offset, uint32_t index_count, uint32_t flags);
extern uint32_t *sceAgcDcbDrawIndexIndirect(SceAgcCb *cb, uint32_t data_offset, uint32_t modifier);
extern uint32_t *sceAgcDcbSetIndexBuffer(SceAgcCb *cb, uintptr_t index_buf_addr, uint32_t index_count);
extern uint32_t *sceAgcDcbSetIndexCount(SceAgcCb *cb, uint32_t index_count);
extern uint32_t *sceAgcDcbSetIndexSize(SceAgcCb *cb, uint32_t index_size, uint32_t cache_policy);
extern uint32_t *sceAgcDcbSetNumInstances(SceAgcCb *cb, uint32_t instance_count);
extern uint32_t *sceAgcDcbSetPredication(SceAgcCb *cb, uintptr_t address);
extern uint32_t *sceAgcDcbJump(SceAgcCb *cb, uintptr_t target, uint32_t size_dwords);
extern uint32_t *sceAgcDcbResetQueue(SceAgcCb *cb, uint32_t op, uint32_t state);
extern uint32_t *sceAgcDcbEventWrite(SceAgcCb *cb, uint32_t event_type, uintptr_t event_addr);
extern uint32_t *sceAgcDcbSetFlip(SceAgcCb *cb, uint32_t vo_handle, int32_t buf_idx, uint32_t flip_mode, uintptr_t flip_arg);
extern uint32_t *sceAgcDcbWaitUntilSafeForRendering(SceAgcCb *cb, uint32_t vo_handle, uint32_t buf_idx);
extern uint32_t *sceAgcDcbPushMarker(SceAgcCb *cb, const char *marker);
extern uint32_t *sceAgcDcbPopMarker(SceAgcCb *cb);
extern uint32_t *sceAgcDcbDispatchIndirect(SceAgcCb *cb, uint32_t data_offset, uint32_t modifier);
extern uint32_t *sceAgcDcbAcquireMem(SceAgcCb *cb, uint32_t engine, uint32_t cb_db_op,
    uint32_t gcr_ctrl, uintptr_t base_addr, uintptr_t size_bytes, uint32_t poll_cycles);

/* DCB builders with stack args */
extern uint32_t *sceAgcDcbWriteData(SceAgcCb *cb, uint32_t dest, uint32_t cache_policy,
    uintptr_t dst_addr, uintptr_t data_addr, uint32_t dword_count,
    uint32_t increment, uint32_t write_confirm);
extern uint32_t *sceAgcDcbWaitRegMem(SceAgcCb *cb, uint32_t size, uint32_t compare_func,
    uint32_t operation, uint32_t cache_policy, uintptr_t address,
    uint64_t reference, uint64_t mask, uint32_t poll_cycles);
extern uint32_t *sceAgcDcbDmaData(SceAgcCb *cb, uint32_t dest, uint32_t dest_cache_policy,
    uint32_t source, uintptr_t dst_addr, uint32_t src_cache_policy,
    uint64_t control4, uintptr_t src_addr, uint32_t byte_count,
    uint64_t control7, uint64_t control8, uint64_t control9);
extern uint32_t *sceAgcDcbSetBaseIndirectArgs(SceAgcCb *cb, uint32_t base_index, uintptr_t address);
extern uint32_t *sceAgcDcbSetCxRegistersIndirect(SceAgcCb *cb, uintptr_t regs_addr, uint32_t reg_count);
extern uint32_t *sceAgcDcbSetShRegistersIndirect(SceAgcCb *cb, uintptr_t regs_addr, uint32_t reg_count);
extern uint32_t *sceAgcDcbSetUcRegistersIndirect(SceAgcCb *cb, uintptr_t regs_addr, uint32_t reg_count);
extern uint32_t *sceAgcDcbStallCommandBufferParser(SceAgcCb *cb, uint32_t size, uintptr_t address, uintptr_t reference);
extern uint32_t *sceAgcDcbGetLodStats(SceAgcCb *cb, uint32_t cache_policy, uintptr_t dst_addr,
    uint32_t control, uint32_t counter_mask, uint32_t reset_counters,
    uint64_t enable, uint64_t counter_select);

/* === ACB builders === */

extern uint32_t *sceAgcAcbResetQueue(SceAgcCb *cb, uint32_t queue_index);
extern uint32_t *sceAgcAcbEventWrite(SceAgcCb *cb, uint32_t event_type, uintptr_t event_addr);
extern uint32_t *sceAgcAcbAcquireMem(SceAgcCb *cb, uint32_t gcr_ctrl, uintptr_t base_addr,
    uintptr_t size_bytes, uint32_t poll_cycles);
extern uint32_t *sceAgcAcbPopMarker(SceAgcCb *cb);
extern uint32_t *sceAgcAcbPushMarker(SceAgcCb *cb, const char *marker);
extern uint32_t *sceAgcAcbDispatchIndirect(SceAgcCb *cb, uintptr_t args_addr, uint32_t modifier);
/* AcbDmaData: HLE reads 4 reg args (rdi-rcx) + 2 stack args (stack[+8], stack[+16]).
 * With 6 args in SysV, all go in registers. The real function has 8 args so
 * args 7-8 land on the stack. Args 5-6 (r8/r9) are unused by the HLE. */
extern uint32_t *sceAgcAcbDmaData(SceAgcCb *cb, uint32_t src_selector, uint32_t dst_selector,
    uintptr_t dst_addr, uint32_t pad_r8, uint32_t pad_r9,
    uint64_t src_or_immediate, uint32_t byte_count);
extern uint32_t *sceAgcAcbWaitRegMem(SceAgcCb *cb, uint32_t size, uint32_t compare_func,
    uint32_t cache_policy, uintptr_t address, uintptr_t reference,
    uint64_t mask, uint32_t poll_cycles);
extern uint32_t *sceAgcAcbWriteData(SceAgcCb *cb, uint32_t dest, uint32_t cache_policy,
    uintptr_t dst_addr, uintptr_t data_addr, uint32_t dword_count,
    uint32_t increment, uint32_t write_confirm);

/* === Driver functions === */

extern int32_t  sceAgcDriverSubmitDcb(void *submit);
extern int32_t  sceAgcDriverSubmitAcb(uint32_t owner, void *submit);
extern int32_t  sceAgcDriverSubmitMultiDcbs(uintptr_t *addr_array, uint32_t *size_array, uint32_t count);
extern int32_t  sceAgcDriverSetTFRing(uintptr_t ring, uint32_t size);
extern int32_t  sceAgcDriverSetHsOffchipParam(uintptr_t buffer, uint32_t param);
extern int32_t  sceAgcGetDataPacketPayloadAddress(uintptr_t *out, uintptr_t cmd_addr, int32_t type);
extern int32_t  sceAgcSuspendPoint(void);
extern int32_t  sceAgcDriverGetDefaultOwner(uint32_t *out_owner);
extern int32_t  sceAgcDriverGetResourceRegistrationMaxNameLength(uint32_t *out_len);
extern int32_t  sceAgcDriverInitResourceRegistration(uintptr_t mem, uintptr_t mem_size, uintptr_t owner_count);
extern int32_t  sceAgcDriverRegisterOwner(uint32_t *out_owner, const char *name);
extern int32_t  sceAgcDriverRegisterResource(uintptr_t resource, uint32_t owner, const char *name, uint32_t type, uint32_t flags);
extern int32_t  sceAgcDriverRegisterDefaultOwner(uint32_t owner);
extern int32_t  sceAgcDriverQueryResourceRegistrationUserMemoryRequirements(uintptr_t *out_size, uintptr_t res_count, uintptr_t owner_count);
extern int32_t  sceAgcDriverUnregisterResource(uint32_t handle);
extern int32_t  sceAgcDriverAddEqEvent(uintptr_t equeue, uintptr_t event_id, uintptr_t user_data);
extern int32_t  sceAgcDriverDeleteEqEvent(uintptr_t equeue, uintptr_t event_id);

/* === GetSize helpers === */

extern uint32_t sceAgcDcbDrawIndexIndirectGetSize(void);
extern uint32_t sceAgcDcbDmaDataGetSize(void);
extern uint32_t sceAgcDcbSetIndexCountGetSize(void);
extern uint32_t sceAgcDcbStallCommandBufferParserGetSize(void);
extern uint32_t sceAgcDcbGetLodStatsGetSize(uint32_t counter_count);
extern uint32_t sceAgcAcbDmaDataGetSize(void);

/* === Patchers === */

extern int32_t sceAgcWaitRegMemPatchAddress(uintptr_t cmd_addr, uintptr_t address);
extern int32_t sceAgcWaitRegMemPatchCompareFunction(uintptr_t cmd_addr, uint32_t compare_func);
extern int32_t sceAgcWaitRegMemPatchMask(uintptr_t cmd_addr, uintptr_t mask);
extern int32_t sceAgcWaitRegMemPatchReference(uintptr_t cmd_addr, uintptr_t reference);
extern int32_t sceAgcQueueEndOfPipeActionPatchAddress(uintptr_t cmd_addr, uintptr_t address);
extern int32_t sceAgcQueueEndOfPipeActionPatchData(uintptr_t cmd_addr, uintptr_t data);
extern int32_t sceAgcQueueEndOfPipeActionPatchGcrCntl(uintptr_t cmd_addr, uint32_t gcr_cntl);
extern int32_t sceAgcQueueEndOfPipeActionPatchType(uintptr_t cmd_addr, uint32_t type);
extern int32_t sceAgcSetPacketPredication(uintptr_t cmd_addr, uint32_t predication);
extern int32_t sceAgcSetCxRegIndirectPatchAddRegisters(uintptr_t cmd_addr, uint32_t count);
extern int32_t sceAgcSetCxRegIndirectPatchSetAddress(uintptr_t cmd_addr, uintptr_t address);
extern int32_t sceAgcSetShRegIndirectPatchAddRegisters(uintptr_t cmd_addr, uint32_t count);
extern int32_t sceAgcSetShRegIndirectPatchSetAddress(uintptr_t cmd_addr, uintptr_t address);
extern int32_t sceAgcSetUcRegIndirectPatchAddRegisters(uintptr_t cmd_addr, uint32_t count);
extern int32_t sceAgcSetUcRegIndirectPatchSetAddress(uintptr_t cmd_addr, uintptr_t address);
extern int32_t sceAgcDmaDataPatchSetDstAddressOrOffset(uintptr_t cmd_addr, uintptr_t address);
extern int32_t sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate(uintptr_t cmd_addr, uintptr_t address);

/* === Shader / PrimState / InterpolantMapping === */

extern int32_t sceAgcCreateShader(void *dst, void *header, uintptr_t code_addr);
extern int32_t sceAgcCreatePrimState(void *cx_regs, void *uc_regs,
    void *hull_shader, void *geom_shader, uint32_t prim_type);
extern int32_t sceAgcCreateInterpolantMapping(void *regs, void *geom_shader, void *pixel_shader);

/* === Test infrastructure === */

static uint32_t cb_buffer[8192] __attribute__((aligned(64)));
static uint8_t  reg_defaults_buf[0x4000] __attribute__((aligned(64)));
static uint8_t  reg_defaults_internal_buf[0x4000] __attribute__((aligned(64)));
static uint8_t  agc_state_buf[0x1000] __attribute__((aligned(64)));
static uint32_t sh_regs[4] = { 0x12345678, 0xDEADBEEF, 0xCAFEBABE, 0x00000001 };
static uint32_t sh_reg_range[8] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444,
                                     0x55555555, 0x66666666, 0x77777777, 0x88888888 };
static uint32_t data_buf[64] __attribute__((aligned(64)));
static uint8_t  resource_reg_mem[0x1000] __attribute__((aligned(64)));

/* Synthetic shader record for CreateShader / CreatePrimState / CreateInterpolantMapping.
 * Layout matches SharpEmu HLE expectations:
 *   +0x00: file header (0x34333231 = "1234")
 *   +0x04: version (0x18)
 *   +0x08: user data ptr
 *   +0x10: code ptr (patched by CreateShader)
 *   +0x18: cx registers ptr
 *   +0x20: sh registers ptr
 *   +0x28: specials ptr
 *   +0x30: input semantics ptr
 *   +0x38: output semantics ptr
 *   +0x56: num output semantics (u16)
 *   +0x5A: shader type (byte) — 2 or 6 for ES/GS
 */
static uint8_t shader_header[0x80] __attribute__((aligned(64)));
static uint8_t shader_specials[0x40] __attribute__((aligned(64)));
static uint32_t shader_user_data[16] __attribute__((aligned(64)));
static uint32_t shader_cx_regs[16] __attribute__((aligned(64)));
static uint32_t shader_sh_regs[16] __attribute__((aligned(64)));
static uint32_t shader_input_sem[16] __attribute__((aligned(64)));
static uint32_t shader_output_sem[16] __attribute__((aligned(64)));
static uint32_t prim_cx_regs[8] __attribute__((aligned(64)));
static uint32_t prim_uc_regs[8] __attribute__((aligned(64)));
static uint32_t interp_regs[16] __attribute__((aligned(64)));

static int g_pass = 0, g_fail = 0;

static void init_cb(SceAgcCb *cb, void *buffer, size_t size_bytes) {
    cb->reserved0 = (uintptr_t)buffer;
    cb->reserved1 = 0;
    cb->cursor_up = (uintptr_t)buffer;
    cb->cursor_down = (uintptr_t)buffer + size_bytes;
    cb->callback = 0;
    cb->reserved2 = 0;
    cb->reserved_dw = 0;
    cb->reserved3 = 0;
}

static uint32_t cb_used_dwords(const SceAgcCb *cb) {
    return (uint32_t)((cb->cursor_up - cb->reserved0) / sizeof(uint32_t));
}

#define CHECK(name, ptr) do { \
    if (ptr) { g_pass++; printf("  [PASS] %s: off=%ld hdr=0x%08X\n", name, \
        (long)((uint32_t*)(ptr) - cb_buffer), *(uint32_t*)(ptr)); } \
    else { g_fail++; printf("  [FAIL] %s: returned NULL\n", name); } \
} while(0)

#define CHECK_INT(name, val, expected) do { \
    if ((uint32_t)(val) == (uint32_t)(expected)) { g_pass++; \
        printf("  [PASS] %s: 0x%08X\n", name, (unsigned)(val)); } \
    else { g_fail++; printf("  [FAIL] %s: got 0x%08X, expected 0x%08X\n", \
        name, (unsigned)(val), (unsigned)(expected)); } \
} while(0)

#define CHECK_OK(name, val) do { \
    if ((int32_t)(val) == 0) { g_pass++; printf("  [PASS] %s: OK\n", name); } \
    else { g_fail++; printf("  [FAIL] %s: got 0x%08X\n", name, (unsigned)(val)); } \
} while(0)

int main(void) {
    SceAgcCb cb;
    int32_t err;
    uint32_t *p;

    printf("=== OpenAGC SharpEmu HLE Comprehensive Test ===\n\n");

    /* --- Init & Register Defaults --- */
    printf("[Init]\n");
    memset(agc_state_buf, 0, sizeof(agc_state_buf));
    err = sceAgcInit(agc_state_buf, 7);
    CHECK_INT("sceAgcInit", err, 0);

    memset(reg_defaults_buf, 0xCC, sizeof(reg_defaults_buf));
    err = sceAgcGetRegisterDefaults2(7, reg_defaults_buf);
    printf("  sceAgcGetRegisterDefaults2: result=0x%08X\n", (unsigned)err);
    g_pass++;

    memset(reg_defaults_internal_buf, 0xDD, sizeof(reg_defaults_internal_buf));
    err = sceAgcGetRegisterDefaults2Internal(7, reg_defaults_internal_buf);
    printf("  sceAgcGetRegisterDefaults2Internal: result=0x%08X\n", (unsigned)err);
    g_pass++;

    /* --- CB builders --- */
    printf("\n[CB Builders]\n");
    init_cb(&cb, cb_buffer, sizeof(cb_buffer));

    p = sceAgcCbNop(&cb, 4);
    CHECK("sceAgcCbNop(4)", p);

    p = sceAgcCbDispatch(&cb, 64, 1, 1, 0);
    CHECK("sceAgcCbDispatch(64,1,1)", p);

    p = sceAgcCbSetShRegistersDirect(&cb, sh_regs, 4);
    CHECK("sceAgcCbSetShRegistersDirect(4)", p);

    p = sceAgcCbSetShRegisterRangeDirect(&cb, 0x100, sh_reg_range, 8);
    CHECK("sceAgcCbSetShRegisterRangeDirect(0x100,8)", p);

    p = sceAgcCbReleaseMem(&cb, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK("sceAgcCbReleaseMem", p);

    /* --- DCB draw builders --- */
    printf("\n[DCB Draw Builders]\n");

    p = sceAgcDcbDrawIndexAuto(&cb, 6, 0x40000000);
    CHECK("sceAgcDcbDrawIndexAuto(6)", p);

    p = sceAgcDcbDrawIndex(&cb, 6, (uintptr_t)cb_buffer, 0x40000000);
    CHECK("sceAgcDcbDrawIndex(6)", p);

    p = sceAgcDcbDrawIndexOffset(&cb, 0, 6, 0);
    CHECK("sceAgcDcbDrawIndexOffset(0,6)", p);

    p = sceAgcDcbDrawIndexIndirect(&cb, 0, 0x41);
    CHECK("sceAgcDcbDrawIndexIndirect(0)", p);

    /* --- DCB state builders --- */
    printf("\n[DCB State Builders]\n");

    p = sceAgcDcbSetIndexBuffer(&cb, (uintptr_t)cb_buffer, 256);
    CHECK("sceAgcDcbSetIndexBuffer(256)", p);

    p = sceAgcDcbSetIndexCount(&cb, 256);
    CHECK("sceAgcDcbSetIndexCount(256)", p);

    p = sceAgcDcbSetIndexSize(&cb, 1, 0);
    CHECK("sceAgcDcbSetIndexSize(1)", p);

    p = sceAgcDcbSetNumInstances(&cb, 4);
    CHECK("sceAgcDcbSetNumInstances(4)", p);

    p = sceAgcDcbSetPredication(&cb, (uintptr_t)cb_buffer);
    CHECK("sceAgcDcbSetPredication", p);

    p = sceAgcDcbJump(&cb, (uintptr_t)cb_buffer, 16);
    CHECK("sceAgcDcbJump(16)", p);

    p = sceAgcDcbResetQueue(&cb, 0x3FF, 0);
    CHECK("sceAgcDcbResetQueue(0x3FF)", p);

    /* --- DCB event/flip/wait builders --- */
    printf("\n[DCB Event/Flip/Wait]\n");

    p = sceAgcDcbEventWrite(&cb, 0x10, 0);
    CHECK("sceAgcDcbEventWrite(0x10)", p);

    p = sceAgcDcbSetFlip(&cb, 0x10000, 0, 0, 0);
    CHECK("sceAgcDcbSetFlip(0,0)", p);

    p = sceAgcDcbWaitUntilSafeForRendering(&cb, 0x10000, 0);
    CHECK("sceAgcDcbWaitUntilSafeForRendering", p);

    /* --- DCB markers --- */
    printf("\n[DCB Markers]\n");

    p = sceAgcDcbPushMarker(&cb, "test_marker");
    CHECK("sceAgcDcbPushMarker", p);

    p = sceAgcDcbPopMarker(&cb);
    CHECK("sceAgcDcbPopMarker", p);

    /* --- DCB dispatch/acquire --- */
    printf("\n[DCB Dispatch/Acquire]\n");

    p = sceAgcDcbDispatchIndirect(&cb, 0, 0x41);
    CHECK("sceAgcDcbDispatchIndirect(0)", p);

    p = sceAgcDcbAcquireMem(&cb, 0, 0, 0, 0, (uintptr_t)-1, 0);
    CHECK("sceAgcDcbAcquireMem(noSize)", p);

    /* --- DCB builders with stack args --- */
    printf("\n[DCB Stack-Arg Builders]\n");

    /* WriteData: needs non-zero dst_addr and data_addr, dword_count <= 0x3FFD */
    memset(data_buf, 0xAA, sizeof(data_buf));
    p = sceAgcDcbWriteData(&cb, 0, 0, (uintptr_t)cb_buffer, (uintptr_t)data_buf, 4, 0, 0);
    CHECK("sceAgcDcbWriteData(4)", p);

    /* WaitRegMem: size<=1, compare_func<=7, operation<=4, cache_policy<=3 */
    p = sceAgcDcbWaitRegMem(&cb, 0, 0, 0, 0, (uintptr_t)cb_buffer, 0, 0, 0);
    CHECK("sceAgcDcbWaitRegMem", p);

    /* DmaData: byte_count must be non-zero and dword-aligned */
    p = sceAgcDcbDmaData(&cb, 0, 0, 0, (uintptr_t)cb_buffer, 0, 0, (uintptr_t)data_buf, 16, 0, 0, 0);
    CHECK("sceAgcDcbDmaData(16)", p);

    /* SetBaseIndirectArgs */
    p = sceAgcDcbSetBaseIndirectArgs(&cb, 0, (uintptr_t)cb_buffer);
    CHECK("sceAgcDcbSetBaseIndirectArgs", p);

    /* SetCx/Sh/UcRegistersIndirect */
    p = sceAgcDcbSetCxRegistersIndirect(&cb, (uintptr_t)data_buf, 4);
    CHECK("sceAgcDcbSetCxRegistersIndirect(4)", p);

    p = sceAgcDcbSetShRegistersIndirect(&cb, (uintptr_t)data_buf, 4);
    CHECK("sceAgcDcbSetShRegistersIndirect(4)", p);

    p = sceAgcDcbSetUcRegistersIndirect(&cb, (uintptr_t)data_buf, 4);
    CHECK("sceAgcDcbSetUcRegistersIndirect(4)", p);

    /* StallCommandBufferParser: size<=1 */
    p = sceAgcDcbStallCommandBufferParser(&cb, 0, (uintptr_t)cb_buffer, 0);
    CHECK("sceAgcDcbStallCommandBufferParser", p);

    /* GetLodStats */
    p = sceAgcDcbGetLodStats(&cb, 0, (uintptr_t)data_buf, 0, 0, 0, 0, 0);
    CHECK("sceAgcDcbGetLodStats", p);

    /* --- ACB builders --- */
    printf("\n[ACB Builders]\n");

    p = sceAgcAcbResetQueue(&cb, 0);
    CHECK("sceAgcAcbResetQueue(0)", p);

    p = sceAgcAcbEventWrite(&cb, 0x10, 0);
    CHECK("sceAgcAcbEventWrite(0x10)", p);

    p = sceAgcAcbAcquireMem(&cb, 0, 0, (uintptr_t)-1, 0);
    CHECK("sceAgcAcbAcquireMem(noSize)", p);

    p = sceAgcAcbPushMarker(&cb, "acb_marker");
    CHECK("sceAgcAcbPushMarker", p);

    p = sceAgcAcbPopMarker(&cb);
    CHECK("sceAgcAcbPopMarker", p);

    p = sceAgcAcbDispatchIndirect(&cb, (uintptr_t)data_buf, 0x41);
    CHECK("sceAgcAcbDispatchIndirect", p);

    /* AcbDmaData: HLE reads src_or_immediate from stack[+8] and byte_count from
     * stack[+16]. With 8 args, args 7-8 land on stack. Args 5-6 (r8/r9) unused. */
    p = sceAgcAcbDmaData(&cb, 0, 0, (uintptr_t)cb_buffer, 0, 0, (uintptr_t)data_buf, 16);
    CHECK("sceAgcAcbDmaData(16)", p);

    /* AcbWaitRegMem: size<=1, compare_func<=7, cache_policy<=3 */
    p = sceAgcAcbWaitRegMem(&cb, 0, 0, 0, (uintptr_t)cb_buffer, 0, 0, 0);
    CHECK("sceAgcAcbWaitRegMem", p);

    /* AcbWriteData (delegates to DcbWriteData) */
    p = sceAgcAcbWriteData(&cb, 0, 0, (uintptr_t)cb_buffer, (uintptr_t)data_buf, 4, 0, 0);
    CHECK("sceAgcAcbWriteData(4)", p);

    /* --- GetSize helpers --- */
    printf("\n[GetSize Helpers]\n");

    uint32_t sz;
    sz = sceAgcDcbDrawIndexIndirectGetSize();
    CHECK_INT("sceAgcDcbDrawIndexIndirectGetSize", sz, 20);

    sz = sceAgcDcbDmaDataGetSize();
    CHECK_INT("sceAgcDcbDmaDataGetSize", sz, 32);

    sz = sceAgcDcbSetIndexCountGetSize();
    CHECK_INT("sceAgcDcbSetIndexCountGetSize", sz, 28);

    sz = sceAgcDcbStallCommandBufferParserGetSize();
    CHECK_INT("sceAgcDcbStallCommandBufferParserGetSize", sz, 8);

    sz = sceAgcDcbGetLodStatsGetSize(4);
    CHECK_INT("sceAgcDcbGetLodStatsGetSize(4)", sz, 16 + 16);

    sz = sceAgcAcbDmaDataGetSize();
    CHECK_INT("sceAgcAcbDmaDataGetSize", sz, 32);

    /* --- Driver functions --- */
    printf("\n[Driver Functions]\n");

    /* SuspendPoint: returns 0 */
    err = sceAgcSuspendPoint();
    CHECK_OK("sceAgcSuspendPoint", err);

    /* SetTFRing: returns OK */
    err = sceAgcDriverSetTFRing((uintptr_t)cb_buffer, 0x4000);
    CHECK_OK("sceAgcDriverSetTFRing", err);

    /* SetHsOffchipParam: returns OK */
    err = sceAgcDriverSetHsOffchipParam((uintptr_t)cb_buffer, 0x1000);
    CHECK_OK("sceAgcDriverSetHsOffchipParam", err);

    /* GetDefaultOwner: writes owner to output */
    uint32_t owner = 0;
    err = sceAgcDriverGetDefaultOwner(&owner);
    CHECK_OK("sceAgcDriverGetDefaultOwner", err);

    /* GetResourceRegistrationMaxNameLength: writes 256 */
    uint32_t max_name = 0;
    err = sceAgcDriverGetResourceRegistrationMaxNameLength(&max_name);
    CHECK_OK("sceAgcDriverGetResourceRegistrationMaxNameLength", err);

    /* QueryResourceRegistrationUserMemoryRequirements */
    uintptr_t req_size = 0;
    err = sceAgcDriverQueryResourceRegistrationUserMemoryRequirements(&req_size, 16, 4);
    CHECK_OK("sceAgcDriverQueryResourceRegistrationUserMemoryRequirements", err);

    /* InitResourceRegistration */
    err = sceAgcDriverInitResourceRegistration((uintptr_t)resource_reg_mem, sizeof(resource_reg_mem), 4);
    CHECK_OK("sceAgcDriverInitResourceRegistration", err);

    /* RegisterDefaultOwner */
    err = sceAgcDriverRegisterDefaultOwner(0x1234);
    CHECK_OK("sceAgcDriverRegisterDefaultOwner", err);

    /* RegisterOwner: writes owner handle to output */
    uint32_t reg_owner = 0;
    err = sceAgcDriverRegisterOwner(&reg_owner, "test_owner");
    CHECK_OK("sceAgcDriverRegisterOwner", err);

    /* RegisterResource */
    err = sceAgcDriverRegisterResource((uintptr_t)cb_buffer, reg_owner, "test_resource", 0, 0);
    CHECK_OK("sceAgcDriverRegisterResource", err);

    /* UnregisterResource — HLE's RegisterResource is a stub that doesn't track
     * resources, so UnregisterResource always returns NOT_FOUND. This is a
     * SharpEmu HLE limitation, not an OpenAGC ABI bug. Test that the call
     * doesn't crash and returns the expected error. */
    err = sceAgcDriverUnregisterResource(1);
    if ((uint32_t)err == 0x80020003u) {
        g_pass++;
        printf("  [PASS] sceAgcDriverUnregisterResource: NOT_FOUND (expected, HLE stub)\n");
    } else if (err == 0) {
        g_pass++;
        printf("  [PASS] sceAgcDriverUnregisterResource: OK\n");
    } else {
        g_fail++;
        printf("  [FAIL] sceAgcDriverUnregisterResource: got 0x%08X\n", (unsigned)err);
    }

    /* GetDataPacketPayloadAddress — use a NOP packet we built earlier */
    /* The first packet in cb_buffer is CbNop at offset 0 */
    uintptr_t payload_addr = 0;
    err = sceAgcGetDataPacketPayloadAddress(&payload_addr, (uintptr_t)cb_buffer, 0);
    CHECK_OK("sceAgcGetDataPacketPayloadAddress", err);

    /* AddEqEvent / DeleteEqEvent — test with null equeue (expects NOT_FOUND) */
    err = sceAgcDriverAddEqEvent(0, 0x100, 0);
    if ((uint32_t)err == 0x80020002u) {
        g_pass++;
        printf("  [PASS] sceAgcDriverAddEqEvent: NOT_FOUND (expected, null equeue)\n");
    } else if (err == 0) {
        g_pass++;
        printf("  [PASS] sceAgcDriverAddEqEvent: OK\n");
    } else {
        g_fail++;
        printf("  [FAIL] sceAgcDriverAddEqEvent: got 0x%08X\n", (unsigned)err);
    }

    err = sceAgcDriverDeleteEqEvent(0, 0x100);
    if ((uint32_t)err == 0x80020002u) {
        g_pass++;
        printf("  [PASS] sceAgcDriverDeleteEqEvent: NOT_FOUND (expected, null equeue)\n");
    } else if (err == 0) {
        g_pass++;
        printf("  [PASS] sceAgcDriverDeleteEqEvent: OK\n");
    } else {
        g_fail++;
        printf("  [FAIL] sceAgcDriverDeleteEqEvent: got 0x%08X\n", (unsigned)err);
    }

    /* --- Submit --- */
    printf("\n[Submit]\n");
    uint32_t total_dwords = cb_used_dwords(&cb);
    printf("  Total CB dwords: %u\n", total_dwords);

    AgcCommandBufferSubmit submit = {
        .command_address = (uintptr_t)cb_buffer,
        .dword_count = total_dwords,
        .reserved = 0,
    };
    err = sceAgcDriverSubmitDcb(&submit);
    CHECK_OK("sceAgcDriverSubmitDcb", err);

    /* SubmitAcb — use a small separate buffer */
    SceAgcCb acb;
    static uint32_t acb_buffer[64] __attribute__((aligned(64)));
    init_cb(&acb, acb_buffer, sizeof(acb_buffer));
    sceAgcAcbResetQueue(&acb, 0);
    AgcCommandBufferSubmit acb_submit = {
        .command_address = (uintptr_t)acb_buffer,
        .dword_count = cb_used_dwords(&acb),
        .reserved = 0,
    };
    err = sceAgcDriverSubmitAcb(0, &acb_submit);
    CHECK_OK("sceAgcDriverSubmitAcb", err);

    /* SubmitMultiDcbs — submit 2 small buffers */
    static uint32_t multi_buf1[16] __attribute__((aligned(64)));
    static uint32_t multi_buf2[16] __attribute__((aligned(64)));
    SceAgcCb mcb1, mcb2;
    init_cb(&mcb1, multi_buf1, sizeof(multi_buf1));
    init_cb(&mcb2, multi_buf2, sizeof(multi_buf2));
    sceAgcCbNop(&mcb1, 4);
    sceAgcCbNop(&mcb2, 4);
    uintptr_t addr_array[2] = { (uintptr_t)multi_buf1, (uintptr_t)multi_buf2 };
    uint32_t  size_array[2] = { cb_used_dwords(&mcb1), cb_used_dwords(&mcb2) };
    err = sceAgcDriverSubmitMultiDcbs(addr_array, size_array, 2);
    CHECK_OK("sceAgcDriverSubmitMultiDcbs", err);

    /* --- Patchers --- */
    /* Build packets first, then patch them */
    printf("\n[Patchers]\n");

    /* Build a WaitRegMem packet for patching */
    static uint32_t wrm_buf[16] __attribute__((aligned(64)));
    SceAgcCb wrm_cb;
    init_cb(&wrm_cb, wrm_buf, sizeof(wrm_buf));
    uint32_t *wrm_pkt = sceAgcDcbWaitRegMem(&wrm_cb, 0, 0, 0, 0, (uintptr_t)cb_buffer, 0, 0, 0);
    if (wrm_pkt) {
        err = sceAgcWaitRegMemPatchAddress((uintptr_t)wrm_pkt, (uintptr_t)data_buf);
        CHECK_OK("sceAgcWaitRegMemPatchAddress", err);

        err = sceAgcWaitRegMemPatchCompareFunction((uintptr_t)wrm_pkt, 3);
        CHECK_OK("sceAgcWaitRegMemPatchCompareFunction", err);

        err = sceAgcWaitRegMemPatchMask((uintptr_t)wrm_pkt, 0xFFFFFFFF);
        CHECK_OK("sceAgcWaitRegMemPatchMask", err);

        err = sceAgcWaitRegMemPatchReference((uintptr_t)wrm_pkt, 0x1234);
        CHECK_OK("sceAgcWaitRegMemPatchReference", err);
    } else {
        g_fail += 4;
        printf("  [FAIL] WaitRegMem patchers: couldn't build base packet\n");
    }

    /* Build a ReleaseMem packet for EOP patching */
    static uint32_t eop_buf[16] __attribute__((aligned(64)));
    SceAgcCb eop_cb;
    init_cb(&eop_cb, eop_buf, sizeof(eop_buf));
    uint32_t *eop_pkt = sceAgcCbReleaseMem(&eop_cb, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (eop_pkt) {
        err = sceAgcQueueEndOfPipeActionPatchAddress((uintptr_t)eop_pkt, (uintptr_t)data_buf);
        CHECK_OK("sceAgcQueueEndOfPipeActionPatchAddress", err);

        err = sceAgcQueueEndOfPipeActionPatchData((uintptr_t)eop_pkt, 0xDEAD);
        CHECK_OK("sceAgcQueueEndOfPipeActionPatchData", err);

        err = sceAgcQueueEndOfPipeActionPatchGcrCntl((uintptr_t)eop_pkt, 0x1234);
        CHECK_OK("sceAgcQueueEndOfPipeActionPatchGcrCntl", err);

        err = sceAgcQueueEndOfPipeActionPatchType((uintptr_t)eop_pkt, 1);
        CHECK_OK("sceAgcQueueEndOfPipeActionPatchType", err);
    } else {
        g_fail += 4;
        printf("  [FAIL] EOP patchers: couldn't build base packet\n");
    }

    /* Build indirect register packets for patching */
    static uint32_t cx_buf[16] __attribute__((aligned(64)));
    SceAgcCb cx_cb;
    init_cb(&cx_cb, cx_buf, sizeof(cx_buf));
    uint32_t *cx_pkt = sceAgcDcbSetCxRegistersIndirect(&cx_cb, (uintptr_t)data_buf, 4);
    if (cx_pkt) {
        err = sceAgcSetCxRegIndirectPatchSetAddress((uintptr_t)cx_pkt, (uintptr_t)cb_buffer);
        CHECK_OK("sceAgcSetCxRegIndirectPatchSetAddress", err);

        err = sceAgcSetCxRegIndirectPatchAddRegisters((uintptr_t)cx_pkt, 8);
        CHECK_OK("sceAgcSetCxRegIndirectPatchAddRegisters", err);
    } else {
        g_fail += 2;
        printf("  [FAIL] CxRegIndirect patchers: couldn't build base packet\n");
    }

    static uint32_t sh_buf[16] __attribute__((aligned(64)));
    SceAgcCb sh_cb;
    init_cb(&sh_cb, sh_buf, sizeof(sh_buf));
    uint32_t *sh_pkt = sceAgcDcbSetShRegistersIndirect(&sh_cb, (uintptr_t)data_buf, 4);
    if (sh_pkt) {
        err = sceAgcSetShRegIndirectPatchSetAddress((uintptr_t)sh_pkt, (uintptr_t)cb_buffer);
        CHECK_OK("sceAgcSetShRegIndirectPatchSetAddress", err);

        err = sceAgcSetShRegIndirectPatchAddRegisters((uintptr_t)sh_pkt, 8);
        CHECK_OK("sceAgcSetShRegIndirectPatchAddRegisters", err);
    } else {
        g_fail += 2;
        printf("  [FAIL] ShRegIndirect patchers: couldn't build base packet\n");
    }

    static uint32_t uc_buf[16] __attribute__((aligned(64)));
    SceAgcCb uc_cb;
    init_cb(&uc_cb, uc_buf, sizeof(uc_buf));
    uint32_t *uc_pkt = sceAgcDcbSetUcRegistersIndirect(&uc_cb, (uintptr_t)data_buf, 4);
    if (uc_pkt) {
        err = sceAgcSetUcRegIndirectPatchSetAddress((uintptr_t)uc_pkt, (uintptr_t)cb_buffer);
        CHECK_OK("sceAgcSetUcRegIndirectPatchSetAddress", err);

        err = sceAgcSetUcRegIndirectPatchAddRegisters((uintptr_t)uc_pkt, 8);
        CHECK_OK("sceAgcSetUcRegIndirectPatchAddRegisters", err);
    } else {
        g_fail += 2;
        printf("  [FAIL] UcRegIndirect patchers: couldn't build base packet\n");
    }

    /* Build a DmaData packet for patching */
    static uint32_t dma_buf[16] __attribute__((aligned(64)));
    SceAgcCb dma_cb;
    init_cb(&dma_cb, dma_buf, sizeof(dma_buf));
    uint32_t *dma_pkt = sceAgcDcbDmaData(&dma_cb, 0, 0, 0, (uintptr_t)cb_buffer, 0, 0, (uintptr_t)data_buf, 16, 0, 0, 0);
    if (dma_pkt) {
        err = sceAgcDmaDataPatchSetDstAddressOrOffset((uintptr_t)dma_pkt, (uintptr_t)data_buf);
        CHECK_OK("sceAgcDmaDataPatchSetDstAddressOrOffset", err);

        err = sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate((uintptr_t)dma_pkt, (uintptr_t)cb_buffer);
        CHECK_OK("sceAgcDmaDataPatchSetSrcAddressOrOffsetOrImmediate", err);
    } else {
        g_fail += 2;
        printf("  [FAIL] DmaData patchers: couldn't build base packet\n");
    }

    /* SetPacketPredication — patch a draw packet */
    static uint32_t pred_buf[16] __attribute__((aligned(64)));
    SceAgcCb pred_cb;
    init_cb(&pred_cb, pred_buf, sizeof(pred_buf));
    uint32_t *pred_pkt = sceAgcDcbDrawIndexAuto(&pred_cb, 6, 0x40000000);
    if (pred_pkt) {
        err = sceAgcSetPacketPredication((uintptr_t)pred_pkt, 1);
        CHECK_OK("sceAgcSetPacketPredication", err);
    } else {
        g_fail++;
        printf("  [FAIL] SetPacketPredication: couldn't build base packet\n");
    }

    /* --- Shader / PrimState / InterpolantMapping --- */
    printf("\n[Shader / PrimState / InterpolantMapping]\n");

    /* Build a synthetic shader header matching HLE's expected layout.
     * HLE's RelocatePointerField treats pointer fields as relative offsets
     * from the field address — it writes back fieldAddress + relativeOffset.
     * So we store relative offsets, not absolute addresses. */
    memset(shader_header, 0, sizeof(shader_header));
    *(uint32_t *)(shader_header + 0x00) = 0x34333231;  /* file header "1234" */
    *(uint32_t *)(shader_header + 0x04) = 0x18;         /* version */
    /* Pointer fields as relative offsets from each field address */
    *(uintptr_t *)(shader_header + 0x08) = (uintptr_t)shader_user_data - (uintptr_t)(shader_header + 0x08);
    *(uintptr_t *)(shader_header + 0x10) = 0;           /* code ptr (patched by CreateShader) */
    *(uintptr_t *)(shader_header + 0x18) = (uintptr_t)shader_cx_regs - (uintptr_t)(shader_header + 0x18);
    *(uintptr_t *)(shader_header + 0x20) = (uintptr_t)shader_sh_regs - (uintptr_t)(shader_header + 0x20);
    *(uintptr_t *)(shader_header + 0x28) = (uintptr_t)shader_specials - (uintptr_t)(shader_header + 0x28);
    *(uintptr_t *)(shader_header + 0x30) = (uintptr_t)shader_input_sem - (uintptr_t)(shader_header + 0x30);
    *(uintptr_t *)(shader_header + 0x38) = (uintptr_t)shader_output_sem - (uintptr_t)(shader_header + 0x38);
    *(uint16_t *)(shader_header + 0x56) = 4;            /* num output semantics */
    shader_header[0x5A] = 2;                             /* shader type = ES geometry */
    shader_header[0x5C] = 2;                             /* num SH registers (>= 2) */

    /* SH registers: first reg must be SpiShaderPgmLoEs (0xC8), second must be
     * SpiShaderPgmHiEs (0xC9) for shader type 2 (ES). Each reg entry is 8 bytes
     * (4-byte register offset + 4-byte value). */
    memset(shader_sh_regs, 0, sizeof(shader_sh_regs));
    shader_sh_regs[0] = 0xC8;  /* SpiShaderPgmLoEs register offset */
    shader_sh_regs[2] = 0xC9;  /* SpiShaderPgmHiEs register offset (at +8 bytes) */

    /* Fill specials with recognizable values for PrimState verification */
    memset(shader_specials, 0, sizeof(shader_specials));
    *(uint32_t *)(shader_specials + 0x00) = 0xAAAAAAAA;  /* GE_CNTL */
    *(uint32_t *)(shader_specials + 0x08) = 0xBBBBBBBB;  /* VGT_SHADER_STAGES_EN */
    *(uint32_t *)(shader_specials + 0x20) = 0xCCCCCCCC;  /* VGT_GS_OUT_PRIM_TYPE */
    *(uint32_t *)(shader_specials + 0x28) = 0xDDDDDDDD;  /* GE_USER_VGPR_EN */

    /* CreateShader: valid header + code address */
    err = sceAgcCreateShader(NULL, shader_header, (uintptr_t)cb_buffer);
    CHECK_OK("sceAgcCreateShader", err);

    /* CreatePrimState: needs ES/GS shader type (2 or 6), non-null specials.
     * After CreateShader, the specials pointer at header+0x28 has been
     * relocated to an absolute address, so we can reuse the header. */
    memset(prim_cx_regs, 0, sizeof(prim_cx_regs));
    memset(prim_uc_regs, 0, sizeof(prim_uc_regs));
    err = sceAgcCreatePrimState(prim_cx_regs, prim_uc_regs, NULL, shader_header, 1);
    CHECK_OK("sceAgcCreatePrimState", err);

    /* CreateInterpolantMapping: needs geom shader with output semantics */
    memset(interp_regs, 0, sizeof(interp_regs));
    err = sceAgcCreateInterpolantMapping(interp_regs, shader_header, NULL);
    CHECK_OK("sceAgcCreateInterpolantMapping", err);

    /* --- Summary --- */
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    printf("=== Done ===\n");

    return g_fail == 0 ? 0 : 1;
}
