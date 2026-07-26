/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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

_Static_assert(sizeof(AgcShaderSpecialRegister) == 0x08,
    "AgcShaderSpecialRegister size mismatch");
_Static_assert(offsetof(AgcShaderSpecialRegister, register_offset) == 0x00,
    "AgcShaderSpecialRegister register offset mismatch");
_Static_assert(offsetof(AgcShaderSpecialRegister, value) == 0x04,
    "AgcShaderSpecialRegister value offset mismatch");
_Static_assert(sizeof(AgcShaderSpecials) == 0x30,
    "AgcShaderSpecials size mismatch");
_Static_assert(offsetof(AgcShaderSpecials, ge_cntl) == 0x00,
    "AgcShaderSpecials GE_CNTL offset mismatch");
_Static_assert(offsetof(AgcShaderSpecials, vgt_shader_stages_en) == 0x08,
    "AgcShaderSpecials VGT_SHADER_STAGES_EN offset mismatch");
_Static_assert(offsetof(AgcShaderSpecials, reserved_10) == 0x10,
    "AgcShaderSpecials reserved entry offset mismatch");
_Static_assert(offsetof(AgcShaderSpecials, vgt_gs_out_prim_type) == 0x20,
    "AgcShaderSpecials VGT_GS_OUT_PRIM_TYPE offset mismatch");
_Static_assert(offsetof(AgcShaderSpecials, ge_user_vgpr_en) == 0x28,
    "AgcShaderSpecials GE_USER_VGPR_EN offset mismatch");
_Static_assert(sizeof(AgcShaderUserData) == 40,
    "AgcShaderUserData size mismatch");

bool agcShaderRecordIsValid(const AgcShaderRecord *record) {
    if (!record)
        return false;
    if (record->magic != AGC_SHADER_RECORD_MAGIC)
        return false;
    if (record->version != AGC_SHADER_RECORD_VERSION_GEN5)
        return false;
    if (record->shader_type > kAgcShaderTypeLs)
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
static bool agcPatchShaderRegisterAddress(
    AgcShaderRegister *regs, uint32_t num_regs,
    uint32_t lo_offset, uint64_t address)
{
    AgcShaderRegister *lo = agcFindShaderRegister(regs, num_regs, lo_offset, 0);
    if (!lo)
        return false;

    /* Find the hi register immediately after lo (offset == lo_offset + 1). */
    AgcShaderRegister *hi = NULL;
    if (lo + 1 < regs + num_regs && (lo + 1)->offset == lo_offset + 1)
        hi = lo + 1;
    if (!hi)
        return false;

    lo->value = (uint32_t)((address >> 8) & 0xFFFFFFFF);
    hi->value &= 0xFFFFFF00;
    hi->value |= (uint32_t)((address >> 40) & 0xFF);
    return true;
}

static uint32_t agcMaxU32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static uint32_t agcMinU32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t agcMergeMaxField(
    uint32_t dst, uint32_t src, uint32_t shift, uint32_t field_mask)
{
    uint32_t mask = field_mask << shift;
    uint32_t value = agcMaxU32((dst >> shift) & field_mask,
                               (src >> shift) & field_mask);
    return (dst & ~mask) | (value << shift);
}

static uint32_t agcCopyMasked(uint32_t dst, uint32_t src, uint32_t mask) {
    return (dst & ~mask) | (src & mask);
}

/* FW 5.50 fd5Bp5tGTgo computes RSRC2[31:28] from the combined resource
 * pressure of the two halves instead of taking the legacy field maximum. */
static uint32_t agcComputeRsrc2High0200(
    uint32_t front_rsrc1, uint32_t front_rsrc2,
    uint32_t back_rsrc1, uint32_t back_rsrc2)
{
    uint32_t front_base = ((front_rsrc1 & 0x3Fu) << 3u) + 8u;
    uint32_t back_base = ((back_rsrc1 & 0x3Fu) << 3u) + 8u;
    uint32_t front_total = (front_base >> 1u) +
        (((front_rsrc2 >> 28u) & 0xFu) << 3u);
    uint32_t back_total = (back_base >> 1u) +
        (((back_rsrc2 >> 28u) & 0xFu) << 3u);
    uint32_t max_total = agcMaxU32(front_total, back_total);

    if ((agcMaxU32(front_base, back_base) >> 1u) >= max_total)
        return 0;

    uint32_t delta = max_total - agcMinU32(front_total, back_total);
    return ((delta << 22u) + 0x01C00000u) & 0xF0000000u;
}

static bool agcCopyShaderRegisterOccurrences(
    AgcShaderRegister *dst, uint32_t dst_count,
    const AgcShaderRegister *src, uint32_t src_count,
    uint32_t offset, uint32_t count)
{
    for (uint32_t occurrence = 0; occurrence < count; occurrence++) {
        AgcShaderRegister *dst_reg = agcFindShaderRegister(
            dst, dst_count, offset, occurrence);
        AgcShaderRegister *src_reg = agcFindShaderRegister(
            (AgcShaderRegister *)(uintptr_t)src, src_count, offset, occurrence);
        if (!dst_reg || !src_reg)
            return false;
        dst_reg->value = src_reg->value;
    }
    return true;
}

static bool agcMergeShaderResources(
    AgcShaderRegister *dst, uint32_t dst_count,
    const AgcShaderRegister *front, uint32_t front_count,
    uint32_t rsrc1_offset, uint32_t rsrc2_offset,
    bool is_gs, bool revision_0200)
{
    AgcShaderRegister *dst_rsrc1 = agcFindShaderRegister(
        dst, dst_count, rsrc1_offset, 0);
    AgcShaderRegister *dst_rsrc2 = agcFindShaderRegister(
        dst, dst_count, rsrc2_offset, 0);
    AgcShaderRegister *front_rsrc1 = agcFindShaderRegister(
        (AgcShaderRegister *)(uintptr_t)front, front_count, rsrc1_offset, 0);
    AgcShaderRegister *front_rsrc2 = agcFindShaderRegister(
        (AgcShaderRegister *)(uintptr_t)front, front_count, rsrc2_offset, 0);
    if (!dst_rsrc1 || !dst_rsrc2 || !front_rsrc1 || !front_rsrc2)
        return false;

    uint32_t dst1 = dst_rsrc1->value;
    uint32_t dst2 = dst_rsrc2->value;
    uint32_t front1 = front_rsrc1->value;
    uint32_t front2 = front_rsrc2->value;

    dst1 = agcMergeMaxField(dst1, front1, 0u, 0x3Fu);
    if (revision_0200) {
        uint32_t high = agcComputeRsrc2High0200(front1, front2,
                                                dst_rsrc1->value,
                                                dst_rsrc2->value);
        dst2 = (dst2 & 0x0FFFFFFFu) | high;
    } else {
        dst2 = agcMergeMaxField(dst2, front2, 28u, 0xFu);
    }

    dst1 = agcMergeMaxField(dst1, front1, is_gs ? 29u : 28u, 0x3u);
    if (is_gs)
        dst2 = agcMergeMaxField(dst2, front2, 16u, 0x3u);

    dst2 = agcCopyMasked(dst2, front2, 0x0800003Eu);
    if (is_gs)
        dst2 = agcCopyMasked(dst2, front2, 0x00040000u);

    dst_rsrc1->value = dst1;
    dst_rsrc2->value = dst2;
    return true;
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

int32_t PS5_SYSV_ABI sceAgcGetFusedShaderSize_0080(
    AgcSizeAlign *dst, const AgcShaderRecord *front,
    const AgcShaderRecord *back)
{
    return sceAgcGetFusedShaderSize(dst, front, back);
}

static int32_t agcFuseShaderHalvesImpl(
    AgcShaderRecord *fused_result, const AgcShaderRecord *front,
    const AgcShaderRecord *back, void *scratch_mem, bool revision_0200)
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
        ? (uint8_t)kAgcShaderBinaryTypeGs
        : (uint8_t)kAgcShaderBinaryTypeHs;

    /* The firmware dereferences both Specials blocks unconditionally. Return
     * INVALID_HALVES instead of reproducing its invalid-input crash. */
    if (!front->specials || !back->specials)
        return AGC_ERROR_SHADER_INVALID_HALVES;
    const AgcShaderSpecials *front_sp =
        (const AgcShaderSpecials *)(uintptr_t)front->specials;
    const AgcShaderSpecials *back_sp =
        (const AgcShaderSpecials *)(uintptr_t)back->specials;
    uint32_t mismatch_bit = (front_type == kAgcShaderBinaryTypeGsFront)
        ? (1u << 22u) : (1u << 21u);
    if (((front_sp->vgt_shader_stages_en.value ^
          back_sp->vgt_shader_stages_en.value) & mismatch_bit) != 0)
        return AGC_ERROR_SHADER_INVALID_HALVES;

    /* If scratch_mem is provided, copy back's SH registers there. */
    if (scratch_mem) {
        if (!back->sh_registers && back->num_sh_registers != 0)
            return AGC_ERROR_SHADER_INVALID_HALVES;
        AgcShaderRegister *sh_regs = (AgcShaderRegister *)scratch_mem;
        if (back->num_sh_registers != 0) {
            memcpy(sh_regs, (const void *)(uintptr_t)back->sh_registers,
                   (size_t)back->num_sh_registers * sizeof(AgcShaderRegister));
        }
        fused_result->sh_registers = (uint64_t)(uintptr_t)sh_regs;
    }

    AgcShaderRegister *fused_regs = (AgcShaderRegister *)(uintptr_t)fused_result->sh_registers;
    uint32_t fused_reg_count = fused_result->num_sh_registers;
    uint32_t front_reg_count = front->num_sh_registers;
    AgcShaderRegister *front_regs = (AgcShaderRegister *)(uintptr_t)front->sh_registers;
    bool is_gs = front_type == kAgcShaderBinaryTypeGsFront;
    uint32_t checksum_offset = is_gs ? AGC_SPI_SHADER_PGM_CHKSUM_GS
                                     : AGC_SPI_SHADER_PGM_CHKSUM_HS;
    uint32_t rsrc1_offset = is_gs ? AGC_SPI_SHADER_PGM_RSRC1_GS
                                  : AGC_SPI_SHADER_PGM_RSRC1_HS;
    uint32_t rsrc2_offset = is_gs ? AGC_SPI_SHADER_PGM_RSRC2_GS
                                  : AGC_SPI_SHADER_PGM_RSRC2_HS;
    uint32_t program_lo_offset = is_gs ? AGC_SPI_SHADER_PGM_LO_ES
                                       : AGC_SPI_SHADER_PGM_LO_LS;

    if (!agcCopyShaderRegisterOccurrences(fused_regs, fused_reg_count,
            front_regs, front_reg_count, checksum_offset, 2) ||
        !agcMergeShaderResources(fused_regs, fused_reg_count,
            front_regs, front_reg_count, rsrc1_offset, rsrc2_offset,
            is_gs, revision_0200) ||
        !agcPatchShaderRegisterAddress(fused_regs, fused_reg_count,
            program_lo_offset, front->code))
        return AGC_ERROR_SHADER_INVALID_HALVES;

    fused_result->user_data = revision_0200 ? 0 : front->user_data;

    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcFuseShaderHalves(
    AgcShaderRecord *fused_result, const AgcShaderRecord *front,
    const AgcShaderRecord *back, void *scratch_mem)
{
    return agcFuseShaderHalvesImpl(
        fused_result, front, back, scratch_mem, false);
}

int32_t PS5_SYSV_ABI sceAgcFuseShaderHalves_0200(
    AgcShaderRecord *fused_result, const AgcShaderRecord *front,
    const AgcShaderRecord *back, void *scratch_mem)
{
    return agcFuseShaderHalvesImpl(
        fused_result, front, back, scratch_mem, true);
}
