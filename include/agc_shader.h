#ifndef _AGC_SHADER_H_
#define _AGC_SHADER_H_

#include <stdbool.h>
#include <stdint.h>

#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AGC shader record (PS5 Gen5) parsed from SharpEmu observation.
 *
 * The shader record is a fixed 0x60-byte header that contains pointers to
 * the code body, user-data table, CX/SH register blocks, specials block,
 * and input/output semantics arrays. The layout is firmware-observed;
 * only the fields listed below are exposed through accessor helpers.
 */
#define AGC_SHADER_RECORD_MAGIC       0x34333231u  /* "1234" little-endian */
#define AGC_SHADER_RECORD_VERSION_GEN5 0x18u

/* Shader stage/type values observed in firmware paths (byte at offset 0x5A). */
typedef enum AgcShaderType {
    kAgcShaderTypePs = 0,   /* Pixel shader */
    kAgcShaderTypeVs = 1,   /* Vertex shader */
    kAgcShaderTypeGs = 2,   /* Geometry shader */
    kAgcShaderTypeEs = 3,   /* Export shader */
    kAgcShaderTypeHs = 4,   /* Hull shader */
    kAgcShaderTypeLs = 5,   /* Local shader */
    kAgcShaderTypeCs = 6,   /* Compute shader */
} AgcShaderType;

/*
 * AGC shader record layout recovered from SharpEmu.
 *
 * Offsets (all little-endian):
 *   0x00: magic          (uint32)  — must be AGC_SHADER_RECORD_MAGIC
 *   0x04: version        (uint32)  — must be AGC_SHADER_RECORD_VERSION_GEN5
 *   0x08: user_data      (uint64)  — pointer to user-data table
 *   0x10: code           (uint64)  — pointer to shader code body
 *   0x18: cx_registers   (uint64)  — pointer to CX register block
 *   0x20: sh_registers   (uint64)  — pointer to SH register block
 *   0x28: specials       (uint64)  — pointer to specials block
 *   0x30: input_semantics (uint64) — pointer to input semantics array
 *   0x38: output_semantics (uint64) — pointer to output semantics array
 *   0x50: num_input_semantics (uint32)
 *   0x56: num_output_semantics (uint32)
 *   0x5A: shader_type    (uint8)   — AgcShaderType
 *   0x5C: num_sh_registers (uint8)
 */
typedef struct AgcShaderRecord {
    uint32_t magic;
    uint32_t version;
    uint64_t user_data;
    uint64_t code;
    uint64_t cx_registers;
    uint64_t sh_registers;
    uint64_t specials;
    uint64_t input_semantics;
    uint64_t output_semantics;
    uint8_t  _pad1[0x10];
    /*
     * The scalar fields below are stored as byte arrays because the firmware
     * layout places them at offsets that are not naturally aligned for their
     * types (e.g. num_output_semantics at 0x56). Accessor helpers use memcpy
     * to read them safely and portably.
     */
    uint8_t  num_input_semantics[4];
    uint8_t  _pad2[0x2];
    uint8_t  num_output_semantics[4];
    uint8_t  shader_type;
    uint8_t  _pad3[0x1];
    uint8_t  num_sh_registers;
} AgcShaderRecord;

/* Validation and read-only accessors. */
bool        agcShaderRecordIsValid(const AgcShaderRecord *record);
const void *agcShaderRecordGetUserData(const AgcShaderRecord *record);
const void *agcShaderRecordGetCode(const AgcShaderRecord *record);
const void *agcShaderRecordGetCxRegisters(const AgcShaderRecord *record);
const void *agcShaderRecordGetShRegisters(const AgcShaderRecord *record);
const void *agcShaderRecordGetSpecials(const AgcShaderRecord *record);
const void *agcShaderRecordGetInputSemantics(const AgcShaderRecord *record);
const void *agcShaderRecordGetOutputSemantics(const AgcShaderRecord *record);
AgcShaderType agcShaderRecordGetType(const AgcShaderRecord *record);
uint32_t    agcShaderRecordGetNumInputSemantics(const AgcShaderRecord *record);
uint32_t    agcShaderRecordGetNumOutputSemantics(const AgcShaderRecord *record);
uint8_t     agcShaderRecordGetNumShRegisters(const AgcShaderRecord *record);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_SHADER_H_ */
