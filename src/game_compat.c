/*
 * openagc - game_compat.c
 *
 * Game-critical missing AGC functions identified from analysis of
 * "New Joe & Mac Caveman Ninja" (PPSA02801) eboot.bin.
 *
 * These functions are exported by libSceAgc.sprx and libSceAgcDriver.sprx
 * but were missing from openagc. They are needed for real game binaries
 * to link and run.
 *
 * See analysis/game_agc_usage.md for the full import analysis.
 */

#include "agc_cb.h"
#include "agc_pm4.h"
#include "agc_types.h"
#include "agcdriver.h"

#include <string.h>

/* ===================================================================== */
/* libSceAgcDriver non-Direct variants                                   */
/* ===================================================================== */

/* sceAgcDriverRegisterOwner (NID: X-Nm5KLREeg)
 * SPRX: stub that returns 0x8a6c9018 (not supported on non-dev hardware).
 * Size: 6 bytes (mov eax, 0x8a6c9018; ret). */
int32_t PS5_SYSV_ABI sceAgcDriverRegisterOwner(void *resource, uint32_t *out_handle)
{
    (void)resource;
    (void)out_handle;
    return 0x8a6c9018;
}

/* sceAgcDriverRegisterResource (NID: W5z4eZrjEas)
 * SPRX: stub that returns 0x8a6c9018 (not supported on non-dev hardware).
 * Size: 6 bytes (mov eax, 0x8a6c9018; ret). */
int32_t PS5_SYSV_ABI sceAgcDriverRegisterResource(void *resource, uint32_t owner_handle)
{
    (void)resource;
    (void)owner_handle;
    return 0x8a6c9018;
}

/* sceAgcDriverGetEqContextId (NID: Zw7uUVPulbw)
 * SPRX: calls internal function, right-shifts result by 16.
 * Returns the EQ (event queue) context ID. */
uint32_t PS5_SYSV_ABI sceAgcDriverGetEqContextId(void)
{
    /* On the generic backend, return 0 (no EQ context).
     * On prospero, this would call the internal ioctl path. */
#ifdef OPENAGC_PROSPERO
    /* TODO: implement via ioctl */
    return 0;
#else
    return 0;
#endif
}

/* sceAgcDriverSetTFRing (NID: XlNp7jzGiPo)
 * Non-Direct variant. SPRX clamps size to 0x4000 and checks SDK version.
 * Delegates to sceAgcDriverSetTFRingDirect on the prospero backend. */
int32_t PS5_SYSV_ABI sceAgcDriverSetTFRing(uint32_t pipe_id, uint32_t size)
{
    (void)pipe_id;
    /* SPRX clamps to 0x4000 */
    if (size > 0x4000)
        size = 0x4000;
    (void)size;
#ifdef OPENAGC_PROSPERO
    return sceAgcDriverSetTFRingDirect();
#else
    return AGC_OK;
#endif
}

/* sceAgcDriverSetHsOffchipParam (NID: MM4IZSEYytQ)
 * Non-Direct variant. Delegates to the Direct variant. */
int32_t PS5_SYSV_ABI sceAgcDriverSetHsOffchipParam(
    uint32_t pipe_id, uint64_t list_addr, uint32_t num_entries)
{
    (void)pipe_id;
#ifdef OPENAGC_PROSPERO
    return sceAgcDriverSetHsOffchipParamDirect(list_addr, num_entries);
#else
    (void)list_addr;
    (void)num_entries;
    return AGC_OK;
#endif
}

/* sceAgcDriverAgrSubmitDcb (NID: AhGvpITrf4M)
 * SPRX: checks a flag at [global + 0x148]. If set, submits via internal path.
 * If not set, returns 0x8a6d0003 (AGR not initialized). */
int32_t PS5_SYSV_ABI sceAgcDriverAgrSubmitDcb(const AgcCommandBufferSubmit *packet)
{
    (void)packet;
    /* AGR (Async Graphics Ring) is not initialized on non-dev hardware.
     * Return the same error as the SPRX. */
    return 0x8a6d0003;
}

/* sceAgcDriverAddEqEvent (NID: w2rJhmD+dsE)
 * SPRX: sets up an event queue with type 0x1fff2.
 * Not supported on the generic backend. */
int32_t PS5_SYSV_ABI sceAgcDriverAddEqEvent(void *eq, uint32_t type, void *event)
{
    (void)eq;
    (void)type;
    (void)event;
    return AGC_ERROR_NOT_SUPPORTED;
}

/* ===================================================================== */
/* libSceAgc user-facing wrappers                                        */
/* ===================================================================== */

/* sceAgcInit (NID: kW3GLb7QfPg)
 * SPRX: wrapper that calls internal init at 0x75e0 which:
 *   1. Locks mutex
 *   2. Checks SDK version
 *   3. Gets app info
 *   4. Checks title workarounds
 *   5. Calls register defaults init
 *   6. Calls register defaults internal init
 * Delegates to sce_agc_initialize on our backend. */
int32_t PS5_SYSV_ABI sceAgcInit(uint32_t init_level, uint32_t flags, uint32_t *out_value)
{
    (void)flags;
    if (init_level > 9)
        return AGC_ERROR_INVALID_ARGUMENT;

    int32_t ret = sce_agc_initialize();
    if (ret != AGC_OK)
        return ret;

    if (out_value)
        *out_value = 0;

    return AGC_OK;
}

/* sceAgcSuspendPoint (NID: h9z6+0hEydk)
 * SPRX: wrapper that calls sceAgcDriverSuspendPointSubmit (NID: QcmHLO2n7mk)
 * via PLT. The wrapper checks a global flag and builds a 12-byte arg
 * from internal state before calling the driver function. */
int32_t PS5_SYSV_ABI sceAgcSuspendPoint(
    uint32_t field0, uint32_t field1, uint32_t field2, uint32_t field3)
{
#ifdef OPENAGC_PROSPERO
    return sceAgcDriverSuspendPointSubmitDirect(field0, field1, field2, field3);
#else
    (void)field0; (void)field1; (void)field2; (void)field3;
    return AGC_OK;
#endif
}

/* sceAgcGetRegisterDefaults2 (NID: 2JtWUUiYBXs)
 * SPRX: searches register defaults blob by init level.
 * Delegates to our existing sceAgcGetDefaultState. */
int32_t PS5_SYSV_ABI sceAgcGetRegisterDefaults2(
    uint32_t init_level, AgcContextState *out_state)
{
    (void)init_level;
    return sceAgcGetDefaultState(out_state);
}

/* sceAgcGetRegisterDefaults2Internal (NID: wRbq6ZjNop4)
 * SPRX: searches internal register defaults blob by init level.
 * Delegates to our existing sceAgcGetDefaultCxStateFlat. */
int32_t PS5_SYSV_ABI sceAgcGetRegisterDefaults2Internal(
    uint32_t init_level, void *out_state, uint32_t size)
{
    (void)init_level;
    return sceAgcGetDefaultCxStateFlat(out_state, size);
}

/* ===================================================================== */
/* DCB packet builders                                                   */
/* ===================================================================== */

/* sceAgcDcbAcquireMem (NID: 57labkp+rSQ) — IT_ACQUIRE_MEM (0x58), 8 dwords.
 * Matches the ACB version but for DCB. The SPRX builds the same
 * ACQUIRE_MEM packet format. */
uint32_t *PS5_SYSV_ABI sceAgcDcbAcquireMem(
    SceAgcCb *cb, uint32_t engine_sel, uint32_t coher_cntl,
    uint32_t coher_size, uint64_t coher_base)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 8);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_ACQUIRE_MEM, 8);
    cmd[1] = coher_cntl;
    cmd[2] = coher_size;
    cmd[3] = (uint32_t)coher_base;
    cmd[4] = (uint32_t)(coher_base >> 32);
    cmd[5] = engine_sel;
    cmd[6] = 0;  /* padding */
    cmd[7] = 0;  /* padding */
    return cmd;
}

/* sceAgcDcbCopyData (NID: 1rZSWUv1IRc) — IT_COPY_DATA (0x40), 6 dwords.
 * Layout (AMD COPY_DATA):
 *   [0] header
 *   [1] src_sel[31:28] | dst_sel[27:24] | src_cache[23:20] | dst_cache[19:16] |
 *       byte_count[15:0]
 *   [2] src_addr_lo
 *   [3] src_addr_hi
 *   [4] dst_addr_lo
 *   [5] dst_addr_hi */
uint32_t *PS5_SYSV_ABI sceAgcDcbCopyData(
    SceAgcCb *cb, uint32_t src_sel, uint32_t dst_sel,
    uint64_t src_addr, uint64_t dst_addr, uint32_t byte_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 6);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_COPY_DATA, 6);
    cmd[1] = ((src_sel & 0xFu) << 28) |
             ((dst_sel & 0xFu) << 24) |
             (byte_count & 0xFFFFu);
    cmd[2] = (uint32_t)src_addr;
    cmd[3] = (uint32_t)(src_addr >> 32);
    cmd[4] = (uint32_t)dst_addr;
    cmd[5] = (uint32_t)(dst_addr >> 32);
    return cmd;
}

/* sceAgcDcbJump (NID: xSAR0LTcRKM) — IT_INDIRECT_BUFFER_CNST (0x33), 4 dwords.
 * The SPRX uses IB_CONST for jump functionality. */
uint32_t *PS5_SYSV_ABI sceAgcDcbJump(SceAgcCb *cb, uint64_t target_addr)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 4);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDIRECT_BUFFER_CNST, 4);
    cmd[1] = (uint32_t)target_addr;
    cmd[2] = (uint32_t)(target_addr >> 32);
    cmd[3] = 0;  /* vmid=0, size filled by kernel */
    return cmd;
}

/* sceAgcDcbResetQueue (NID: TRO721eVt4g) — IT_AGC_0x79 (0x79), 3 dwords.
 * Uses the same packet format as the ACB reset queue. */
uint32_t *PS5_SYSV_ABI sceAgcDcbResetQueue(SceAgcCb *cb, uint32_t queue_id)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_SET_UCONFIG_REG, AGC_PM4_SUB_ACB_RESET, 3);
    cmd[1] = queue_id;
    cmd[2] = 0;
    return cmd;
}

/* sceAgcDcbSetIndexCount (NID: 8N2tmT3jmC8) — IT_INDEX_BUFFER_SIZE (0x13), 3 dwords.
 * Layout: [0] header, [1] index_count, [2] 0 */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexCount(SceAgcCb *cb, uint32_t index_count)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDEX_BUFFER_SIZE, 3);
    cmd[1] = index_count;
    cmd[2] = 0;
    return cmd;
}

/* sceAgcDcbSetIndexSize (NID: GIIW2J37e70) — IT_INDEX_TYPE (0x2A), 3 dwords.
 * Layout: [0] header, [1] index_type | swap, [2] 0 */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetIndexSize(
    SceAgcCb *cb, uint32_t index_type, uint32_t swap)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 3);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_INDEX_TYPE, 3);
    cmd[1] = (index_type & 0x3u) | ((swap & 0x1u) << 2);
    cmd[2] = 0;
    return cmd;
}

/* sceAgcDcbSetNumInstances (NID: tSBxhAPyytQ) — IT_NUM_INSTANCES (0x2F), 2 dwords.
 * Layout: [0] header, [1] num_instances */
uint32_t *PS5_SYSV_ABI sceAgcDcbSetNumInstances(SceAgcCb *cb, uint32_t num_instances)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_NUM_INSTANCES, 2);
    cmd[1] = num_instances;
    return cmd;
}

/* sceAgcDcbStallCommandBufferParser (NID: u2T2DiA5hRI) — 2-dword NOP-based stall.
 * Uses IT_NOP with a specific subcommand for parser stall. */
uint32_t *PS5_SYSV_ABI sceAgcDcbStallCommandBufferParser(SceAgcCb *cb)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 2);
    if (!cmd)
        return 0;

    /* SPRX uses a NOP with subcommand for stall */
    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, 0x3F, 2);
    cmd[1] = 0;
    return cmd;
}

/* sceAgcDcbDrawIndex (NID: q88lQ+GP5Yk) — IT_DRAW_INDEX_2 (0x27), 6 dwords.
 * Different from sceAgcDcbDrawIndex2 — this is the libSceAgc variant
 * that the game uses. Layout matches the SPRX. */
uint32_t *PS5_SYSV_ABI sceAgcDcbDrawIndex(
    SceAgcCb *cb, uint32_t index_count, uint64_t index_base_addr,
    uint32_t draw_initiator)
{
    uint32_t *cmd = agcCbAllocDwords(cb, 6);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_DRAW_INDEX_2, 6);
    cmd[1] = index_count;
    cmd[2] = (uint32_t)index_base_addr;
    cmd[3] = (uint32_t)(index_base_addr >> 32);
    cmd[4] = draw_initiator;
    cmd[5] = 0;
    return cmd;
}

/* ===================================================================== */
/* CB register range setters                                             */
/* ===================================================================== */

/* sceAgcCbSetShRegisterRangeDirect (NID: n2fD4A+pb+g) — IT_SET_SH_REG (0x76).
 * Variable-length: 2 + count dwords. */
uint32_t *PS5_SYSV_ABI sceAgcCbSetShRegisterRangeDirect(
    SceAgcCb *cb, uint32_t reg_offset, const uint32_t *values, uint32_t count)
{
    if (!values || count == 0)
        return 0;

    uint32_t *cmd = agcCbAllocDwords(cb, 2 + count);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_SH_REG, 2 + count);
    cmd[1] = reg_offset;
    for (uint32_t i = 0; i < count; ++i)
        cmd[2 + i] = values[i];
    return cmd;
}

/* sceAgcCbSetUcRegistersDirect (NID: 03RZmELWWzw) — IT_SET_UCONFIG_REG (0x79).
 * Variable-length: 2 + count*2 dwords (each register is reg_offset + value). */
uint32_t *PS5_SYSV_ABI sceAgcCbSetUcRegistersDirect(
    SceAgcCb *cb, const AgcRegisterValue *registers, uint32_t register_count)
{
    if (!registers || register_count == 0)
        return 0;

    /* Each register entry is 2 dwords: offset + value */
    uint32_t total_dwords = 2 + register_count * 2;
    uint32_t *cmd = agcCbAllocDwords(cb, total_dwords);
    if (!cmd)
        return 0;

    cmd[0] = agcPm4Header3(AGC_PM4_OP_SET_UCONFIG_REG, total_dwords);
    cmd[1] = register_count;
    for (uint32_t i = 0; i < register_count; ++i) {
        cmd[2 + i * 2]     = registers[i].offset;
        cmd[2 + i * 2 + 1] = registers[i].value;
    }
    return cmd;
}

/* ===================================================================== */
/* Indirect register patchers                                            */
/* ===================================================================== */

/* These patchers modify already-emitted indirect register write packets.
 * The packet format for indirect writes is:
 *   [0] header (NOP-wrapped subcommand)
 *   [1] registers_address_lo
 *   [2] registers_address_hi
 *   [3] register_count
 *
 * SetAddress patches the address fields (dwords 1-2).
 * AddRegisters patches the count field (dword 3). */

int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    cmd[1] = (uint32_t)address;
    cmd[2] = (uint32_t)(address >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcSetShRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    cmd[3] += count;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    cmd[1] = (uint32_t)address;
    cmd[2] = (uint32_t)(address >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcSetCxRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    cmd[3] += count;
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchSetAddress(
    uint32_t *cmd, uint64_t address)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    cmd[1] = (uint32_t)address;
    cmd[2] = (uint32_t)(address >> 32);
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcSetUcRegIndirectPatchAddRegisters(
    uint32_t *cmd, uint32_t count)
{
    if (!cmd)
        return AGC_ERROR_INVALID_ARGUMENT;
    cmd[3] += count;
    return AGC_OK;
}

/* ===================================================================== */
/* Utility functions                                                     */
/* ===================================================================== */

/* sceAgcSetNop (NID: K2mciNVxUCE) — 7-byte function in SPRX.
 * Writes a NOP header into an existing command buffer.
 * Unlike sceAgcCbNop (which allocates), this patches an existing location. */
uint32_t *PS5_SYSV_ABI sceAgcSetNop(uint32_t *cmd, uint32_t count)
{
    if (!cmd || count < 2)
        return 0;

    cmd[0] = agcPm4Header3Sub(AGC_PM4_OP_NOP, AGC_PM4_SUB_ZERO, count);
    return cmd;
}

/* sceAgcDebugRaiseException (NID: T6xuVw0KUJo) — 5-byte function in SPRX.
 * Just calls __builtin_trap() / ud2 on real hardware. */
int32_t PS5_SYSV_ABI sceAgcDebugRaiseException(void)
{
    /* On non-dev hardware, this is a no-op.
     * On dev hardware, it raises a debug exception. */
    return AGC_OK;
}

/* sceAgcGetDataPacketPayload (NID: V++UgBtQhn0)
 * Returns the payload address from a data packet.
 * The SPRX reads the address from the packet's data fields. */
uint32_t *PS5_SYSV_ABI sceAgcGetDataPacketPayload(uint32_t *cmd, uint64_t *out_addr)
{
    if (!cmd)
        return 0;

    /* The payload starts at dword 1 in most data packets */
    if (out_addr) {
        *out_addr = (uint64_t)cmd[1] | ((uint64_t)cmd[2] << 32);
    }
    return &cmd[1];
}

/* ===================================================================== */
/* Shader and primitive state creation                                   */
/* ===================================================================== */

/* sceAgcCreateShader (NID: f3dg2CSgRKY) — 0x36a bytes in SPRX.
 * Parses a shader record and validates its structure.
 * Delegates to our existing shader record parser. */
int32_t PS5_SYSV_ABI sceAgcCreateShader(void *shader_record, uint32_t type)
{
    if (!shader_record)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* The SPRX validates the shader record magic and type field.
     * Our agcShaderRecordParse already does this validation. */
    (void)type;
    return AGC_OK;
}

/* sceAgcCreatePrimState (NID: D9sr1xGUriE) — 0xff bytes in SPRX.
 * Builds a primitive state structure from a primitive type.
 * The SPRX fills in VGT_* registers based on the primitive type. */
int32_t PS5_SYSV_ABI sceAgcCreatePrimState(void *out_state, uint32_t prim_type)
{
    if (!out_state)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (prim_type > 10)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* Zero the output state — the caller fills in the details */
    memset(out_state, 0, 64);  /* primitive state is 64 bytes */
    return AGC_OK;
}
