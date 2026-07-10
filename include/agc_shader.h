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

/* Shader type macro constants — mirror the AgcShaderType enum values for
 * use in preprocessor contexts and switch/case labels. Values match the
 * byte at offset 0x5A of the shader record (SPRX-confirmed). */
#define AGC_SHADER_TYPE_PS   0
#define AGC_SHADER_TYPE_VS   1
#define AGC_SHADER_TYPE_GS   2
#define AGC_SHADER_TYPE_DS   3
#define AGC_SHADER_TYPE_HS   4
#define AGC_SHADER_TYPE_LS   5
#define AGC_SHADER_TYPE_CS   6

/*
 * AGC shader record layout recovered from SharpEmu and cross-referenced
 * with ps5-openagc (NID/field name reference only — NOT proven working) and
 * shadPS4.
 *
 * The shader record is a fixed 0x60-byte header that contains pointers to
 * the code body, user-data table, CX/SH register blocks, specials block,
 * and input/output semantics arrays. The layout is firmware-observed;
 * only the fields listed below are exposed through accessor helpers.
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
 *   0x40-0x4F: padding (reserved)
 *   0x50: num_input_semantics (uint32)
 *   0x54: padding (2 bytes)
 *   0x56: num_output_semantics (uint32)
 *   0x5A: shader_type    (uint8)   — AgcShaderType
 *   0x5B: padding (1 byte)
 *   0x5C: num_sh_registers (uint8)
 *   0x5D-0x5F: padding (3 bytes)
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

/*
 * AGC shader Specials block — pointed to by AgcShaderRecord.specials.
 * Contains graphics-engine (GE) and vertex-generating-team (VGT) register
 * values that configure the shader pipeline. Recovered from SharpEmu
 * observation and cross-referenced with shadPS4/RPCSX register definitions.
 *
 * The specials block is a variable-length array of register value pairs.
 * The exact layout depends on shader type, but common fields are:
 */
typedef struct AgcShaderSpecials {
    uint32_t ge_cntl;              /* GE_CNTL — geometry engine control */
    uint32_t vgt_shader_stages_en; /* VGT_SHADER_STAGES_EN — stage enable bits */
    uint32_t vgt_gs_out_prim_type; /* VGT_GS_OUT_PRIM_TYPE — GS output primitive */
    uint32_t ge_user_vgpr_en;      /* GE_USER_VGPR_EN — user VGPR enable */
} AgcShaderSpecials;

/*
 * AGC shader User Data Table — pointed to by AgcShaderRecord.user_data.
 * Contains pointers to resource descriptors (T#, V#, S#) and inline
 * constants. The table is an array of 64-bit entries; the first 5 entries
 * are commonly used for shader-specific resource bindings.
 */
typedef struct AgcShaderUserData {
    uint64_t entries[5];  /* Variable-length in practice; 5 is the common minimum */
} AgcShaderUserData;

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

/* Typed accessors for sub-blocks (return NULL if record is invalid). */
const AgcShaderSpecials *agcShaderRecordGetSpecialsTyped(const AgcShaderRecord *record);
const AgcShaderUserData *agcShaderRecordGetUserDataTyped(const AgcShaderRecord *record);

/* SH/CX register accessors — return raw uint32 pointer to the register block. */
const uint32_t *agcShaderRecordGetShRegisterValues(const AgcShaderRecord *record);
const uint32_t *agcShaderRecordGetCxRegisterValues(const AgcShaderRecord *record);

/*
 * Shader linking — combine an HS/LS shader record with a CS shader record
 * into a GS shader record. Matches the SPRX behavior of
 * sceAgcShaderLinkHsGs (ordinal 131, libSceAgc.sprx):
 *
 *   - Reads shader_type at byte offset 0x5A from the source (hs_or_ls);
 *     it must be HS(4) or LS(5), otherwise returns
 *     AGC_ERROR_SHADER_INVALID_TYPE (0x8a6c0008).
 *   - Reads shader_type at byte offset 0x5A from the CS shader; it must be
 *     CS(6), otherwise returns AGC_ERROR_SHADER_INVALID_TYPE.
 *   - Copies the full 0x60-byte CS shader record to dst (the SPRX uses
 *     three 32-byte ymm copies: [0..0x1F], [0x20..0x3F], [0x40..0x5F]).
 *   - Sets dst->shader_type to GS(2).
 *   - Returns AGC_OK (0) on success.
 *
 * Returns AGC_ERROR_INVALID_ARGUMENT if any pointer is NULL.
 */
int32_t agcShaderLinkHsGs(AgcShaderRecord *dst,
    const AgcShaderRecord *hs_or_ls, const AgcShaderRecord *cs);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_SHADER_H_ */
