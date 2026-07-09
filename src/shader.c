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
