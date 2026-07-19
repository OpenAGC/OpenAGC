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
 * openagc — register_defaults.c
 *
 * AGC register default blob builder and parser.
 *
 * The blob layout is recovered from observation
 * (from the HLE reference implementation's AgcExports.cs):
 *
 *   0x00: CX table pointer (uint64)
 *   0x08: SH table pointer (uint64)
 *   0x10: UC table pointer (uint64)
 *   0x18: reserved (8 bytes)
 *   0x20: reserved (8 bytes)
 *   0x28: reserved (8 bytes)
 *   0x30: type table pointer (uint64)
 *   0x38: group count (uint32)
 *   0x40: CX table (cx_length * 8 bytes)
 *         SH table (sh_length * 8 bytes)
 *         UC table (uc_length * 8 bytes)
 *         type table (count * 12 bytes)
 *         register blocks (count * 128 bytes)
 */

#include "agc_context.h"
#include "agc_error.h"

#include <string.h>

_Static_assert(sizeof(AgcRegisterDefaultsHeader) == 0x40,
    "AgcRegisterDefaultsHeader size mismatch");
_Static_assert(sizeof(AgcRegisterDefaultsTypeEntry) == 12,
    "AgcRegisterDefaultsTypeEntry size mismatch");
_Static_assert(sizeof(AgcRegisterDefaultValue) == 8,
    "AgcRegisterDefaultValue size mismatch");

/* Helper: align value up to alignment. */
static size_t agcAlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/* Write a uint32_t at a byte offset into the blob. */
static void agcWriteU32(uint8_t *blob, size_t offset, uint32_t value) {
    memcpy(blob + offset, &value, sizeof(value));
}

/* Write a uint64_t at a byte offset into the blob. */
static void agcWriteU64(uint8_t *blob, size_t offset, uint64_t value) {
    memcpy(blob + offset, &value, sizeof(value));
}

/* ===================================================================== */
/* FW 5.50 primary register defaults (from observation)                     */
/* ===================================================================== */

static const AgcRegisterDefaultValue s_primary_regs_0_3[] = {
    {0x08F, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_0_4[] = {
    {0x08E, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_0_12[] = {
    {0x203, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_0_72[] = {
    {0x318, 0}, {0x31B, 0}, {0x31C, 0}, {0x31D, 0},
    {0x31E, 0x48}, {0x31F, 0}, {0x321, 0}, {0x323, 0},
    {0x324, 0}, {0x325, 0}, {0x390, 0}, {0x398, 0},
    {0x3A0, 0}, {0x3A8, 0}, {0x3B0, 0}, {0x3B8, 0x0006C000},
};
static const AgcRegisterDefaultValue s_primary_regs_0_73[] = {
    {0x00C, 0}, {0x00D, 0x40004000},
};
static const AgcRegisterDefaultValue s_primary_regs_0_74[] = {
    {0x191, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_0_76[] = {
    {0x10F, 0x4E7E0000}, {0x111, 0x4E7E0000}, {0x113, 0x4E7E0000},
    {0x110, 0}, {0x112, 0}, {0x114, 0},
    {0x094, 0x80000000}, {0x095, 0x40004000},
    {0x0B4, 0}, {0x0B5, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_1_13[] = {
    {0x20C, 0}, {0x20D, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_1_14[] = {
    {0x0C8, 0}, {0x0C9, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_1_18[] = {
    {0x008, 0}, {0x009, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_2_3[] = {
    {0x25B, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_2_7[] = {
    {0x262, 0},
};
static const AgcRegisterDefaultValue s_primary_regs_2_12[] = {
    {0x242, 0},
};

static const AgcRegisterDefaultsGroup s_primary_defaults[] = {
    {kAgcRegisterDefaultSpaceCx,  3,  0x0BC65DA4, 1,  s_primary_regs_0_3},
    {kAgcRegisterDefaultSpaceCx,  4,  0x9E5AD592, 1,  s_primary_regs_0_4},
    {kAgcRegisterDefaultSpaceCx,  12, 0x6DE4C312, 1,  s_primary_regs_0_12},
    {kAgcRegisterDefaultSpaceCx,  72, 0x38E92C91, 16, s_primary_regs_0_72},
    {kAgcRegisterDefaultSpaceCx,  73, 0x0B177B43, 2,  s_primary_regs_0_73},
    {kAgcRegisterDefaultSpaceCx,  74, 0x48531062, 1,  s_primary_regs_0_74},
    {kAgcRegisterDefaultSpaceCx,  76, 0x7690AF6F, 10, s_primary_regs_0_76},
    {kAgcRegisterDefaultSpaceSh,  13, 0xC918DF3E, 2,  s_primary_regs_1_13},
    {kAgcRegisterDefaultSpaceSh,  14, 0xC9751C9C, 2,  s_primary_regs_1_14},
    {kAgcRegisterDefaultSpaceSh,  18, 0xC9E01B31, 2,  s_primary_regs_1_18},
    {kAgcRegisterDefaultSpaceUc,  3,  0x105971C2, 1,  s_primary_regs_2_3},
    {kAgcRegisterDefaultSpaceUc,  7,  0x40D49AD1, 1,  s_primary_regs_2_7},
    {kAgcRegisterDefaultSpaceUc,  12, 0x9EBFAB10, 1,  s_primary_regs_2_12},
};

/* ===================================================================== */
/* FW 5.50 internal register defaults (from observation)                    */
/* ===================================================================== */

static const AgcRegisterDefaultValue s_internal_regs_0_0[] = {{0x00E, 0}};
static const AgcRegisterDefaultValue s_internal_regs_0_1[] = {{0x2AF, 0}};
static const AgcRegisterDefaultValue s_internal_regs_0_2[] = {{0x314, 0}};
static const AgcRegisterDefaultValue s_internal_regs_0_3[] = {{0x1B5, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_0[] = {{0x216, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_1[] = {{0x217, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_2[] = {{0x219, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_3[] = {{0x21A, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_4[] = {{0x27D, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_5[] = {{0x22A, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_6[] = {{0x204, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_7[] = {{0x205, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_8[] = {{0x206, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_9[] = {{0x080, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_10[] = {{0x100, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_11[] = {{0x006, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_12[] = {{0x081, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_13[] = {{0x101, 0}};
static const AgcRegisterDefaultValue s_internal_regs_1_14[] = {{0x001, 0}};
static const AgcRegisterDefaultValue s_internal_regs_2_0[] = {{0x24F, 0}};
static const AgcRegisterDefaultValue s_internal_regs_2_1[] = {{0x80003FFF, 0}};
static const AgcRegisterDefaultValue s_internal_regs_2_2[] = {{0x250, 0}};

static const AgcRegisterDefaultsGroup s_internal_defaults[] = {
    {kAgcRegisterDefaultSpaceCx, 0, 0x8FB4EDB5, 1, s_internal_regs_0_0},
    {kAgcRegisterDefaultSpaceCx, 1, 0xB994AD29, 1, s_internal_regs_0_1},
    {kAgcRegisterDefaultSpaceCx, 2, 0xD427322F, 1, s_internal_regs_0_2},
    {kAgcRegisterDefaultSpaceCx, 3, 0xF58FEA31, 1, s_internal_regs_0_3},
    {kAgcRegisterDefaultSpaceSh, 0, 0x6AC156EF, 1, s_internal_regs_1_0},
    {kAgcRegisterDefaultSpaceSh, 1, 0x6AC15610, 1, s_internal_regs_1_1},
    {kAgcRegisterDefaultSpaceSh, 2, 0x6AC15009, 1, s_internal_regs_1_2},
    {kAgcRegisterDefaultSpaceSh, 3, 0x6AC153BA, 1, s_internal_regs_1_3},
    {kAgcRegisterDefaultSpaceSh, 4, 0xBE7DCD73, 1, s_internal_regs_1_4},
    {kAgcRegisterDefaultSpaceSh, 5, 0x0C4B1438, 1, s_internal_regs_1_5},
    {kAgcRegisterDefaultSpaceSh, 6, 0xDB00D71A, 1, s_internal_regs_1_6},
    {kAgcRegisterDefaultSpaceSh, 7, 0xDB00D249, 1, s_internal_regs_1_7},
    {kAgcRegisterDefaultSpaceSh, 8, 0xDB00EC60, 1, s_internal_regs_1_8},
    {kAgcRegisterDefaultSpaceSh, 9, 0x0C4D6FE4, 1, s_internal_regs_1_9},
    {kAgcRegisterDefaultSpaceSh, 10, 0x0C4A80EF, 1, s_internal_regs_1_10},
    {kAgcRegisterDefaultSpaceSh, 11, 0x0DD283E7, 1, s_internal_regs_1_11},
    {kAgcRegisterDefaultSpaceSh, 12, 0xC620E68C, 1, s_internal_regs_1_12},
    {kAgcRegisterDefaultSpaceSh, 13, 0xC67EFACF, 1, s_internal_regs_1_13},
    {kAgcRegisterDefaultSpaceSh, 14, 0xD9E6D9F7, 1, s_internal_regs_1_14},
    {kAgcRegisterDefaultSpaceUc, 0, 0x31F34B9F, 1, s_internal_regs_2_0},
    {kAgcRegisterDefaultSpaceUc, 1, 0xAC0F9E76, 1, s_internal_regs_2_1},
    {kAgcRegisterDefaultSpaceUc, 2, 0x929FD95D, 1, s_internal_regs_2_2},
};

const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetPrimaryGroups(uint32_t *out_count) {
    /* Use the complete v8 register defaults (703 public registers across 127
     * groups) instead of the incomplete HLE-reference-derived data. */
    return agcRegisterDefaultsV8GetPrimaryGroups(out_count);
}

const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetInternalGroups(uint32_t *out_count) {
    /* Use the complete v8 register defaults (25 internal registers across 22
     * groups) instead of the incomplete HLE-reference-derived data. */
    return agcRegisterDefaultsV8GetInternalGroups(out_count);
}

size_t agcRegisterDefaultsComputeSize(
    uint32_t group_count,
    uint32_t cx_table_length,
    uint32_t sh_table_length,
    uint32_t uc_table_length)
{
    size_t cx_table_offset = agcAlignUp(AGC_REGISTER_DEFAULTS_HEADER_SIZE, 8);
    size_t sh_table_offset = cx_table_offset + (size_t)cx_table_length * 8;
    size_t uc_table_offset = sh_table_offset + (size_t)sh_table_length * 8;
    size_t types_offset = agcAlignUp(uc_table_offset + (size_t)uc_table_length * 8, 4);
    size_t blocks_offset = agcAlignUp(types_offset + (size_t)group_count * 12, 8);
    return blocks_offset + (size_t)group_count * AGC_REGISTER_DEFAULTS_BLOCK_SIZE;
}

int32_t agcRegisterDefaultsBuild(
    void *out_blob,
    size_t blob_size,
    uint64_t base_cpu_addr,
    const AgcRegisterDefaultsGroup *groups,
    uint32_t group_count,
    uint32_t cx_table_length,
    uint32_t sh_table_length,
    uint32_t uc_table_length)
{
    if (!out_blob || !groups || group_count == 0)
        return AGC_ERROR_INVALID_ARGUMENT;

    size_t required = agcRegisterDefaultsComputeSize(
        group_count, cx_table_length, sh_table_length, uc_table_length);
    if (blob_size < required)
        return AGC_ERROR_INVALID_ARGUMENT;

    memset(out_blob, 0, required);

    uint8_t *blob = (uint8_t *)out_blob;

    size_t cx_table_offset = agcAlignUp(AGC_REGISTER_DEFAULTS_HEADER_SIZE, 8);
    size_t sh_table_offset = cx_table_offset + (size_t)cx_table_length * 8;
    size_t uc_table_offset = sh_table_offset + (size_t)sh_table_length * 8;
    size_t types_offset = agcAlignUp(uc_table_offset + (size_t)uc_table_length * 8, 4);
    size_t blocks_offset = agcAlignUp(types_offset + (size_t)group_count * 12, 8);

    /* Header */
    agcWriteU64(blob, 0x00, base_cpu_addr + cx_table_offset);
    agcWriteU64(blob, 0x08, base_cpu_addr + sh_table_offset);
    agcWriteU64(blob, 0x10, base_cpu_addr + uc_table_offset);
    agcWriteU64(blob, 0x30, base_cpu_addr + types_offset);
    agcWriteU32(blob, 0x38, group_count);

    for (uint32_t i = 0; i < group_count; i++) {
        const AgcRegisterDefaultsGroup *group = &groups[i];
        if (group->register_count > AGC_REGISTER_DEFAULTS_BLOCK_REGISTERS)
            return AGC_ERROR_INVALID_ARGUMENT;

        size_t table_offset;
        uint32_t table_length;
        switch (group->space) {
        case kAgcRegisterDefaultSpaceCx:
            table_offset = cx_table_offset;
            table_length = cx_table_length;
            break;
        case kAgcRegisterDefaultSpaceSh:
            table_offset = sh_table_offset;
            table_length = sh_table_length;
            break;
        case kAgcRegisterDefaultSpaceUc:
            table_offset = uc_table_offset;
            table_length = uc_table_length;
            break;
        default:
            return AGC_ERROR_INVALID_ARGUMENT;
        }

        if (group->index >= table_length)
            return AGC_ERROR_INVALID_ARGUMENT;

        size_t block_offset = blocks_offset + (size_t)i * AGC_REGISTER_DEFAULTS_BLOCK_SIZE;
        agcWriteU64(blob,
            table_offset + (size_t)group->index * 8,
            base_cpu_addr + block_offset);

        size_t type_entry_offset = types_offset + (size_t)i * 12;
        agcWriteU32(blob, type_entry_offset + 0, group->type_hash);
        agcWriteU32(blob, type_entry_offset + 4, (group->index * 4) + group->space);
        agcWriteU32(blob, type_entry_offset + 8, 0);

        for (uint32_t r = 0; r < group->register_count; r++) {
            size_t reg_offset = block_offset + (size_t)r * 8;
            agcWriteU32(blob, reg_offset + 0, group->registers[r].offset);
            agcWriteU32(blob, reg_offset + 4, group->registers[r].value);
        }
    }

    return AGC_OK;
}

const AgcRegisterDefaultsHeader *agcRegisterDefaultsGetHeader(const void *blob) {
    if (!blob)
        return NULL;
    return (const AgcRegisterDefaultsHeader *)blob;
}

/*
 * The accessors below assume the blob was built with base_cpu_addr equal to
 * the blob's own CPU address. This is true for the host tests and for any
 * blob that is read in place; it is not true for a blob whose pointers were
 * patched to a GPU address before submission.
 */
static size_t agcRegisterDefaultsGetBlocksOffset(const void *blob) {
    const AgcRegisterDefaultsHeader *hdr = blob;
    uint64_t base_addr = (uint64_t)(uintptr_t)blob;
    size_t type_table_offset = (size_t)(hdr->type_table - base_addr);
    size_t end = type_table_offset + (size_t)hdr->group_count * sizeof(AgcRegisterDefaultsTypeEntry);
    return agcAlignUp(end, 8);
}

const uint64_t *agcRegisterDefaultsGetCxTable(const void *blob) {
    if (!blob)
        return NULL;
    const AgcRegisterDefaultsHeader *hdr = blob;
    uint64_t base_addr = (uint64_t)(uintptr_t)blob;
    return (const uint64_t *)((const uint8_t *)blob + (size_t)(hdr->cx_table - base_addr));
}

const uint64_t *agcRegisterDefaultsGetShTable(const void *blob) {
    if (!blob)
        return NULL;
    const AgcRegisterDefaultsHeader *hdr = blob;
    uint64_t base_addr = (uint64_t)(uintptr_t)blob;
    return (const uint64_t *)((const uint8_t *)blob + (size_t)(hdr->sh_table - base_addr));
}

const uint64_t *agcRegisterDefaultsGetUcTable(const void *blob) {
    if (!blob)
        return NULL;
    const AgcRegisterDefaultsHeader *hdr = blob;
    uint64_t base_addr = (uint64_t)(uintptr_t)blob;
    return (const uint64_t *)((const uint8_t *)blob + (size_t)(hdr->uc_table - base_addr));
}

const AgcRegisterDefaultsTypeEntry *agcRegisterDefaultsGetTypeTable(const void *blob) {
    if (!blob)
        return NULL;
    const AgcRegisterDefaultsHeader *hdr = blob;
    uint64_t base_addr = (uint64_t)(uintptr_t)blob;
    return (const AgcRegisterDefaultsTypeEntry *)((const uint8_t *)blob + (size_t)(hdr->type_table - base_addr));
}

const AgcRegisterDefaultValue *agcRegisterDefaultsGetBlock(const void *blob, uint32_t group_index) {
    if (!blob)
        return NULL;
    const AgcRegisterDefaultsHeader *hdr = blob;
    if (group_index >= hdr->group_count)
        return NULL;

    size_t blocks_offset = agcRegisterDefaultsGetBlocksOffset(blob);
    return (const AgcRegisterDefaultValue *)((const uint8_t *)blob + blocks_offset
        + (size_t)group_index * AGC_REGISTER_DEFAULTS_BLOCK_SIZE);
}
