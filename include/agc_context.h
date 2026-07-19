#ifndef _AGC_CONTEXT_H_
#define _AGC_CONTEXT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AGC register default blob layout recovered from SharpEmu.
 *
 * The firmware builds a structured blob containing CX/SH/UC tables,
 * a type table, and fixed-size register blocks. Each table entry is a
 * pointer to a register block. The blob is what sceAgcGetRegisterDefaults2
 * and sceAgcGetRegisterDefaults2Internal return on PS5.
 *
 * Header size: 0x40 bytes.
 * Register block size: 128 bytes (16 * (offset:uint32, value:uint32)).
 */

#define AGC_REGISTER_DEFAULTS_HEADER_SIZE  0x40u
#define AGC_REGISTER_DEFAULTS_BLOCK_SIZE   128u
#define AGC_REGISTER_DEFAULTS_BLOCK_REGISTERS 16u
#define AGC_REGISTER_DEFAULTS_VERSION_7    7u
#define AGC_REGISTER_DEFAULTS_VERSION_8    8u
#define AGC_REGISTER_DEFAULTS_VERSION_10   10u

/* FW 5.50 register-defaults table lengths (from SharpEmu). */
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
 * FW 5.50 default group tables (from SharpEmu).
 */
const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcRegisterDefaultsGetInternalGroups(uint32_t *out_count);

/*
 * KytyPS5 version 8 default group tables.
 * These contain the full register set (703 public, 25 internal registers)
 * extracted from KytyPS5/src/libs/agcRegisterDefaults.inc.
 */
const AgcRegisterDefaultsGroup *agcKytyPs5V8GetPrimaryGroups(uint32_t *out_count);
const AgcRegisterDefaultsGroup *agcKytyPs5V8GetInternalGroups(uint32_t *out_count);

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
