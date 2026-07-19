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
