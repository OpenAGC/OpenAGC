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

#ifndef _AGC_CONTEXT_H_
#define _AGC_CONTEXT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AGC register default blob layout recovered from observation.
 *
 * The firmware builds a structured blob containing CX/SH/UC tables,
 * a type table, and fixed-size register blocks. Each table entry is a
 * pointer to a register block. The blob is what sceAgcGetRegisterDefaults2
 * and sceAgcGetRegisterDefaults2Internal return on PS5.
 *
 * Header size: 0x40 bytes.
 * Register block size: 2048 bytes (up to 256 * (offset:uint32, value:uint32)).
 */

#define AGC_REGISTER_DEFAULTS_HEADER_SIZE  0x40u
#define AGC_REGISTER_DEFAULTS_BLOCK_SIZE   2048u
#define AGC_REGISTER_DEFAULTS_BLOCK_REGISTERS 256u
#define AGC_REGISTER_DEFAULTS_VERSION_7    7u
#define AGC_REGISTER_DEFAULTS_VERSION_8    8u
#define AGC_REGISTER_DEFAULTS_VERSION_10   10u

/* FW 5.50 register-defaults table lengths (from observation). */
#define AGC_PRIMARY_CX_LENGTH   78u
#define AGC_PRIMARY_SH_LENGTH   29u
#define AGC_PRIMARY_UC_LENGTH   20u
#define AGC_INTERNAL_CX_LENGTH  4u
#define AGC_INTERNAL_SH_LENGTH  15u
#define AGC_INTERNAL_UC_LENGTH  3u

/* Register default table indices: which register space the group belongs to. */
typedef enum AgcRegisterDefaultSpace {
    kAgcRegisterDefaultSpaceCx = 0,
    kAgcRegisterDefaultSpaceSh = 1,
    kAgcRegisterDefaultSpaceUc = 2,
} AgcRegisterDefaultSpace;

/*
 * Register default blob header.
 *
 * Offsets:
 *   0x00: cx_table pointer   (uint64)
 *   0x08: sh_table pointer   (uint64)
 *   0x10: uc_table pointer   (uint64)
 *   0x18: reserved (8 bytes)
 *   0x20: reserved (8 bytes)
 *   0x28: reserved (8 bytes)
 *   0x30: type_table pointer (uint64)
 *   0x38: group_count        (uint32)
 */
typedef struct AgcRegisterDefaultsHeader {
    uint64_t cx_table;
    uint64_t sh_table;
    uint64_t uc_table;
    uint64_t reserved0;
    uint64_t reserved1;
    uint64_t reserved2;
    uint64_t type_table;
    uint32_t group_count;
    uint8_t  _pad[0x40 - 0x3C];
} AgcRegisterDefaultsHeader;

/* One entry in the type table: 3 * uint32. */
typedef struct AgcRegisterDefaultsTypeEntry {
    uint32_t type_hash;
    uint32_t packed_index;  /* (index * 4) + space */
    uint32_t reserved;
} AgcRegisterDefaultsTypeEntry;

/* One (offset, value) register pair inside a register block. */
typedef struct AgcRegisterDefaultValue {
    uint32_t offset;
    uint32_t value;
} AgcRegisterDefaultValue;

/*
 * RegisterDefaults structure — matches the SPRX RegisterDefaults layout.
 * This is what sceAgcGetRegisterDefaults2 returns: a pointer to this
 * structure, which contains pointers to per-table register arrays and
 * pointer tables.
 *
 * SPRX layout (from KytyPS5 RegisterDefaults, offsetof(count)==0x38):
 *   0x00: tbl0 (CX) — array of pointers to AgcRegisterDefaultValue arrays
 *   0x08: tbl1 (SH) — array of pointers to AgcRegisterDefaultValue arrays
 *   0x10: tbl2 (UC) — array of pointers (often NULL)
 *   0x18: tbl3 (UC) — array of pointers (used for internal regs)
 *   0x20: tbl0_register_count
 *   0x24: tbl1_register_count
 *   0x28: tbl2_register_count
 *   0x2c: tbl3_register_count
 *   0x30: types — array of uint32 triplets {hash, packed_index, reserved}
 *   0x38: count — number of type entries (= number of groups)
 */
typedef struct AgcRegisterDefaults {
    const AgcRegisterDefaultValue **tbl0;
    const AgcRegisterDefaultValue **tbl1;
    const AgcRegisterDefaultValue **tbl2;
    const AgcRegisterDefaultValue **tbl3;
    uint32_t tbl0_register_count;
    uint32_t tbl1_register_count;
    uint32_t tbl2_register_count;
    uint32_t tbl3_register_count;
    const uint32_t *types;
    uint32_t count;
} AgcRegisterDefaults;

_Static_assert(sizeof(AgcRegisterDefaults) == 0x40,
    "AgcRegisterDefaults size must be 0x40 (matches SPRX RegisterDefaults)");
_Static_assert(offsetof(AgcRegisterDefaults, count) == 0x38,
    "AgcRegisterDefaults.count offset must be 0x38");

/*
 * Description of a single register default group used to build a blob.
 * The group occupies one register block (128 bytes) in the output blob.
 */
typedef struct AgcRegisterDefaultsGroup {
    AgcRegisterDefaultSpace space;
    uint32_t                index;
    uint32_t                type_hash;
    uint32_t                register_count;
    const AgcRegisterDefaultValue *registers;
} AgcRegisterDefaultsGroup;

/*
 * Build a register-defaults blob into the supplied buffer.
 *
 * The caller must provide a buffer large enough for the computed blob size.
 * The base_cpu_addr is the CPU virtual address where the blob will be placed
 * at runtime; it is used to write absolute pointer values into the tables.
 *
 * Returns AGC_OK on success, or an AGC error code on failure.
 */
int32_t agcRegisterDefaultsBuild(
    void *out_blob,
    size_t blob_size,
    uint64_t base_cpu_addr,
    const AgcRegisterDefaultsGroup *groups,
    uint32_t group_count,
    uint32_t cx_table_length,
    uint32_t sh_table_length,
    uint32_t uc_table_length);

/*
 * Compute the total blob size for a given set of groups and table lengths.
 */
size_t agcRegisterDefaultsComputeSize(
    uint32_t group_count,
    uint32_t cx_table_length,
    uint32_t sh_table_length,
    uint32_t uc_table_length);

/*
 * Default group tables (uses the latest available version, currently v8).
 */
const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetInternalGroups(uint32_t *out_count);

/*
 * Version-selectable group tables.
 * Version mapping (from reference):
 *   0-3 → v0, 4 → v4, 5-6 → v5, 7 → v7, 8 → v8, 9 → v9,
 *   10 → v10, 11 → v11, 12 → v10
 * Versions > 12 fall back to v11.
 */
const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetPrimaryGroupsForVersion(
    uint32_t version, uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetInternalGroupsForVersion(
    uint32_t version, uint32_t *out_count);

/*
 * Per-version accessor functions.
 * Each returns the register defaults for that specific firmware version.
 */
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV0GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV0GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV4GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV4GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV5GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV5GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV7GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV7GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV8GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV8GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV9GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV9GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV10GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV10GetInternalGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV11GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsV11GetInternalGroups(uint32_t *out_count);

/*
 * Read-only accessors over a built blob.
 */
const AgcRegisterDefaultsHeader *agcRegisterDefaultsGetHeader(const void *blob);
const uint64_t *agcRegisterDefaultsGetCxTable(const void *blob);
const uint64_t *agcRegisterDefaultsGetShTable(const void *blob);
const uint64_t *agcRegisterDefaultsGetUcTable(const void *blob);
const AgcRegisterDefaultsTypeEntry *agcRegisterDefaultsGetTypeTable(const void *blob);
const AgcRegisterDefaultValue *agcRegisterDefaultsGetBlock(const void *blob, uint32_t group_index);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_CONTEXT_H_ */
