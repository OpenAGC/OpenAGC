#ifndef OPENAGC_HW_TEST_GFX1013_TESS_RING_H
#define OPENAGC_HW_TEST_GFX1013_TESS_RING_H

#include <stdint.h>

/* gfx1013 uses the GFX10 descriptor ABI, not the GFX10.3 variant. */
#define GFX1013_TESS_FACTOR_RING_SLOT       5u
#define GFX1013_TESS_OFFCHIP_RING_SLOT      6u
#define GFX1013_RING_DESCRIPTOR_DWORDS      4u
#define GFX1013_TESS_RING_TABLE_DWORDS      32u

/* One 8K-dword off-chip workgroup is sufficient for the isolated test. */
#define GFX1013_TESS_OFFCHIP_RING_SIZE      0x8000u
#define GFX1013_TESS_FACTOR_RING_SIZE       0x10000u
#define GFX1013_TESS_OFFCHIP_PARAM          0u

/* 8 patches, 3 input/output control points, 2 mapped LS/HS output slots,
 * 512-byte TCS memory attribute stride, triangle primitive mode. */
#define GFX1013_TESS_OFFCHIP_LAYOUT         0x21042108u

/* GFX10 raw R32_FLOAT descriptor:
 * identity DST_SEL, FORMAT=22, RESOURCE_LEVEL=1, OOB_SELECT=RAW.
 */
#define GFX1013_RAW_R32_DESCRIPTOR_WORD3    0x31016FACu

static inline void gfx1013BuildRawRingDescriptor(uint32_t desc[4],
                                                  uint64_t address,
                                                  uint32_t size)
{
    desc[0] = (uint32_t)address;
    desc[1] = (uint32_t)(address >> 32);
    desc[2] = size;
    desc[3] = GFX1013_RAW_R32_DESCRIPTOR_WORD3;
}

static inline void gfx1013BuildTessRingTable(uint32_t table[32],
                                             uint64_t offchip_address,
                                             uint64_t factor_address)
{
    uint32_t i;

    for (i = 0; i < GFX1013_TESS_RING_TABLE_DWORDS; ++i)
        table[i] = 0;

    gfx1013BuildRawRingDescriptor(
        &table[GFX1013_TESS_FACTOR_RING_SLOT * GFX1013_RING_DESCRIPTOR_DWORDS],
        factor_address, GFX1013_TESS_FACTOR_RING_SIZE);
    gfx1013BuildRawRingDescriptor(
        &table[GFX1013_TESS_OFFCHIP_RING_SLOT * GFX1013_RING_DESCRIPTOR_DWORDS],
        offchip_address, GFX1013_TESS_OFFCHIP_RING_SIZE);
}

#endif
