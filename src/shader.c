/*
 * openagc — shader.c
 *
 * AGC shader record parser (PS5 Gen5). The layout is recovered from
 * SharpEmu observation and cross-referenced with the offsets in
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
