/*
 * openagc — shader.c
 *
 * AGC shader record parser (PS5 Gen5). The layout is recovered from
 * HLE reference and cross-referenced with the offsets in
 * include/agc_re.h. Only observed fields are exposed; unknown areas are
 * preserved as padding.
 */

#include "agc_shader.h"
#include "agc_types.h"
#include "agc_error.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(AgcShaderRecord) == 0x60,
    "AgcShaderRecord size mismatch");
_Static_assert(offsetof(AgcShaderRecord, magic) == 0x00,
    "AgcShaderRecord magic offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, version) == 0x04,
    "AgcShaderRecord version offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, user_data) == 0x08,
    "AgcShaderRecord user_data offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, code) == 0x10,
    "AgcShaderRecord code offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, cx_registers) == 0x18,
    "AgcShaderRecord cx_registers offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, sh_registers) == 0x20,
    "AgcShaderRecord sh_registers offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, specials) == 0x28,
    "AgcShaderRecord specials offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, input_semantics) == 0x30,
    "AgcShaderRecord input_semantics offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, output_semantics) == 0x38,
    "AgcShaderRecord output_semantics offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, num_input_semantics) == 0x50,
    "AgcShaderRecord num_input_semantics offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, num_output_semantics) == 0x56,
    "AgcShaderRecord num_output_semantics offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, shader_type) == 0x5A,
    "AgcShaderRecord shader_type offset mismatch");
_Static_assert(offsetof(AgcShaderRecord, num_sh_registers) == 0x5C,
    "AgcShaderRecord num_sh_registers offset mismatch");

_Static_assert(sizeof(AgcShaderSpecials) == 16,
    "AgcShaderSpecials size mismatch");
_Static_assert(sizeof(AgcShaderUserData) == 40,
    "AgcShaderUserData size mismatch");

bool agcShaderRecordIsValid(const AgcShaderRecord *record) {
    if (!record)
        return false;
    if (record->magic != AGC_SHADER_RECORD_MAGIC)
        return false;
    if (record->version != AGC_SHADER_RECORD_VERSION_GEN5)
        return false;
    if (record->shader_type > kAgcShaderTypeCs)
        return false;
    return true;
}

const void *agcShaderRecordGetUserData(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    return (const void *)(uintptr_t)record->user_data;
}

const void *agcShaderRecordGetCode(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    return (const void *)(uintptr_t)record->code;
}

const void *agcShaderRecordGetCxRegisters(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    return (const void *)(uintptr_t)record->cx_registers;
}

const void *agcShaderRecordGetShRegisters(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    return (const void *)(uintptr_t)record->sh_registers;
}

const void *agcShaderRecordGetSpecials(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    return (const void *)(uintptr_t)record->specials;
}

const void *agcShaderRecordGetInputSemantics(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    return (const void *)(uintptr_t)record->input_semantics;
}

const void *agcShaderRecordGetOutputSemantics(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    return (const void *)(uintptr_t)record->output_semantics;
}

AgcShaderType agcShaderRecordGetType(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return kAgcShaderTypePs;
    return (AgcShaderType)record->shader_type;
}

uint32_t agcShaderRecordGetNumInputSemantics(const AgcShaderRecord *record) {
    uint32_t value = 0;
    if (!record || !agcShaderRecordIsValid(record))
        return 0;
    memcpy(&value, record->num_input_semantics, sizeof(value));
    return value;
}

uint32_t agcShaderRecordGetNumOutputSemantics(const AgcShaderRecord *record) {
    uint32_t value = 0;
    if (!record || !agcShaderRecordIsValid(record))
        return 0;
    memcpy(&value, record->num_output_semantics, sizeof(value));
    return value;
}

uint8_t agcShaderRecordGetNumShRegisters(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return 0;
    return record->num_sh_registers;
}

const AgcShaderSpecials *agcShaderRecordGetSpecialsTyped(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    if (record->specials == 0)
        return NULL;
    return (const AgcShaderSpecials *)(uintptr_t)record->specials;
}

const AgcShaderUserData *agcShaderRecordGetUserDataTyped(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    if (record->user_data == 0)
        return NULL;
    return (const AgcShaderUserData *)(uintptr_t)record->user_data;
}

const uint32_t *agcShaderRecordGetShRegisterValues(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    if (record->sh_registers == 0)
        return NULL;
    return (const uint32_t *)(uintptr_t)record->sh_registers;
}

const uint32_t *agcShaderRecordGetCxRegisterValues(const AgcShaderRecord *record) {
    if (!record || !agcShaderRecordIsValid(record))
        return NULL;
    if (record->cx_registers == 0)
        return NULL;
    return (const uint32_t *)(uintptr_t)record->cx_registers;
}

/*
 * sceAgcShaderLinkHsGs — combine an HS/LS shader record with a CS shader
 * record into a GS shader record. Matches the SPRX disassembly of ordinal
 * 131 (fd5Bp5tGTgo) in libSceAgc.sprx:
 *
 *   mov cl, byte ptr [rsi + 0x5a]   ; source shader type
 *   cmp cl, 5 (LS) / 4 (HS)
 *   mov cl, byte ptr [rdx + 0x5a]   ; CS shader type
 *   cmp cl, 6 (CS)
 *   vmovups ymm0/1/2, [rdx + 0/0x20/0x40]  ; copy 0x60 bytes
 *   vmovups [rdi + 0x40/0x20/0], ymm2/1/0
 *   mov byte ptr [rdi + 0x5a], 2    ; output type = GS
 *
 * The SPRX returns error 0x8a6c0008 on type mismatch.
 */
int32_t agcShaderLinkHsGs(AgcShaderRecord *dst,
    const AgcShaderRecord *hs_or_ls, const AgcShaderRecord *cs)
{
    if (!dst || !hs_or_ls || !cs)
        return AGC_ERROR_INVALID_ARGUMENT;

    /* Source shader must be HS(4) or LS(5). */
    uint8_t src_type = hs_or_ls->shader_type;
    if (src_type != kAgcShaderTypeHs && src_type != kAgcShaderTypeLs)
        return AGC_ERROR_SHADER_INVALID_TYPE;

    /* CS shader must be CS(6). */
    if (cs->shader_type != kAgcShaderTypeCs)
        return AGC_ERROR_SHADER_INVALID_TYPE;

    /* Copy the full 0x60-byte CS shader record to dst. The SPRX uses three
     * 32-byte ymm copies; memcpy is the portable equivalent. */
    memcpy(dst, cs, sizeof(*dst));

    /* Set the output shader type to GS(2). */
    dst->shader_type = kAgcShaderTypeGs;

    return AGC_OK;
}

/* ===================================================================== */
/* Fused shader support (reference-confirmed)                              */
/* ===================================================================== */

/* Find a shader register by offset, with occurrence count for duplicates. */
static AgcShaderRegister *agcFindShaderRegister(
    AgcShaderRegister *regs, uint32_t num_regs,
    uint32_t offset, uint32_t occurrence)
{
    if (!regs)
        return NULL;
    for (uint32_t i = 0; i < num_regs; i++) {
        if (regs[i].offset != offset)
            continue;
        if (occurrence == 0)
            return &regs[i];
        occurrence--;
    }
    return NULL;
}

/* Patch a 64-bit address into a lo/hi register pair (GPU address format:
 * lo = bits [31:8], hi = bits [39:32] in the low byte). */
static void agcPatchShaderRegisterAddress(
    AgcShaderRegister *regs, uint32_t num_regs,
    uint32_t lo_offset, uint64_t address)
{
    AgcShaderRegister *lo = agcFindShaderRegister(regs, num_regs, lo_offset, 0);
    if (!lo)
        return;

    /* Find the hi register immediately after lo (offset == lo_offset + 1). */
    AgcShaderRegister *hi = NULL;
    if (lo + 1 < regs + num_regs && (lo + 1)->offset == lo_offset + 1)
        hi = lo + 1;
    if (!hi)
        return;

    lo->value = (uint32_t)((address >> 8) & 0xFFFFFFFF);
    hi->value &= 0xFFFFFF00;
    hi->value |= (uint32_t)((address >> 40) & 0xFF);
}

int32_t PS5_SYSV_ABI sceAgcGetFusedShaderSize(
    AgcSizeAlign *dst, const AgcShaderRecord *front, const AgcShaderRecord *back)
{
    if (!dst || !front || !back)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint8_t front_type = front->shader_type;
    uint8_t back_type  = back->shader_type;

    /* Valid pairs: GsFront(4)+GsBack(6) or HsFront(5)+HsBack(7). */
    if (!((front_type == kAgcShaderBinaryTypeGsFront &&
           back_type  == kAgcShaderBinaryTypeGsBack) ||
          (front_type == kAgcShaderBinaryTypeHsFront &&
           back_type  == kAgcShaderBinaryTypeHsBack)))
        return AGC_ERROR_SHADER_INVALID_HALVES;

    dst->size  = (uint64_t)back->num_sh_registers * sizeof(AgcShaderRegister);
    dst->align = 4;

    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcFuseShaderHalves(
    AgcShaderRecord *fused_result, const AgcShaderRecord *front,
    const AgcShaderRecord *back, void *scratch_mem)
{
    if (!fused_result || !front || !back)
        return AGC_ERROR_INVALID_ARGUMENT;

    uint8_t front_type = front->shader_type;
    uint8_t back_type  = back->shader_type;

    /* Valid pairs: GsFront(4)+GsBack(6) or HsFront(5)+HsBack(7). */
    if (!((front_type == kAgcShaderBinaryTypeGsFront &&
           back_type  == kAgcShaderBinaryTypeGsBack) ||
          (front_type == kAgcShaderBinaryTypeHsFront &&
           back_type  == kAgcShaderBinaryTypeHsBack)))
        return AGC_ERROR_SHADER_INVALID_HALVES;

    /* Copy the back shader record to fused_result. */
    *fused_result = *back;

    /* Set the fused type to Gs(2) or Hs(3) depending on front type. */
    fused_result->shader_type = (front_type == kAgcShaderBinaryTypeGsFront)
        ? (uint8_t)kAgcShaderTypeGs
        : (uint8_t)kAgcShaderTypeHs;

    /* Validate vgt_shader_stages_en mismatch bit. */
    if (front->specials && back->specials) {
        const AgcShaderSpecials *front_sp = (const AgcShaderSpecials *)(uintptr_t)front->specials;
        const AgcShaderSpecials *back_sp  = (const AgcShaderSpecials *)(uintptr_t)back->specials;
        uint32_t front_stages = front_sp->vgt_shader_stages_en;
        uint32_t back_stages  = back_sp->vgt_shader_stages_en;
        uint32_t mismatch_bit = (front_type == kAgcShaderBinaryTypeGsFront)
            ? (1u << 22u) : (1u << 21u);
        if (((front_stages ^ back_stages) & mismatch_bit) != 0)
            return AGC_ERROR_SHADER_INVALID_HALVES;
    }

    /* If scratch_mem is provided, copy back's SH registers there. */
    if (scratch_mem && back->sh_registers && back->num_sh_registers != 0) {
        AgcShaderRegister *sh_regs = (AgcShaderRegister *)scratch_mem;
        memcpy(sh_regs, (const void *)(uintptr_t)back->sh_registers,
               (size_t)back->num_sh_registers * sizeof(AgcShaderRegister));
        fused_result->sh_registers = (uint64_t)(uintptr_t)sh_regs;
    }

    /* Patch front shader code address and checksum into fused registers. */
    AgcShaderRegister *fused_regs = (AgcShaderRegister *)(uintptr_t)fused_result->sh_registers;
    uint32_t fused_reg_count = fused_result->num_sh_registers;
    uint32_t front_reg_count = front->num_sh_registers;
    AgcShaderRegister *front_regs = (AgcShaderRegister *)(uintptr_t)front->sh_registers;

    if (front_type == kAgcShaderBinaryTypeGsFront) {
        /* GS: patch SPI_SHADER_PGM_CHKSUM_GS from front (2 occurrences). */
        for (uint32_t occ = 0; occ < 2; occ++) {
            AgcShaderRegister *dst_reg = agcFindShaderRegister(
                fused_regs, fused_reg_count, AGC_SPI_SHADER_PGM_CHKSUM_GS, occ);
            const AgcShaderRegister *src_reg = agcFindShaderRegister(
                front_regs, front_reg_count, AGC_SPI_SHADER_PGM_CHKSUM_GS, occ);
            if (dst_reg && src_reg)
                dst_reg->value = src_reg->value;
        }
        /* Patch SPI_SHADER_PGM_LO_ES with front's code address. */
        agcPatchShaderRegisterAddress(fused_regs, fused_reg_count,
            AGC_SPI_SHADER_PGM_LO_ES, front->code);
    } else {
        /* HS: patch SPI_SHADER_PGM_LO_LS with front's code address. */
        agcPatchShaderRegisterAddress(fused_regs, fused_reg_count,
            AGC_SPI_SHADER_PGM_LO_LS, front->code);
    }

    /* Clear user_data pointer in the fused result. */
    fused_result->user_data = 0;

    return AGC_OK;
}
