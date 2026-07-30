#ifndef OPENAGC_HW_TEST_DEFAULTS_H
#define OPENAGC_HW_TEST_DEFAULTS_H

#include <stdint.h>

/* Qualification-only caller choices. These do not select OpenAGC's runtime
 * implementation. FW 5.50 deliberately keeps its hardware-proven V8 choice;
 * other profiles exercise their exact SPRX-proven upper bound. */
static inline uint32_t agcTestDefaultsVersion(uint16_t firmware_abi_key)
{
    switch (firmware_abi_key) {
    case 0x0320u: return 7u;
    case 0x0400u: case 0x0403u: case 0x0450u: case 0x0451u:
        return 8u;
    case 0x0502u: case 0x0510u:
    case 0x0600u: case 0x0602u: case 0x0650u:
    case 0x0701u: case 0x0720u: case 0x0740u: case 0x0760u: case 0x0761u:
    case 0x0800u: case 0x0820u: case 0x0840u: case 0x0860u:
        return 9u;
    case 0x0550u:
        return 8u;
    case 0x0900u: case 0x0905u: case 0x0920u: case 0x0940u: case 0x0960u:
    case 0x1001u: case 0x1020u: case 0x1040u: case 0x1060u:
    case 0x1100u: case 0x1120u: case 0x1140u: case 0x1160u:
    case 0x1200u: case 0x1202u: case 0x1220u: case 0x1240u:
    case 0x1260u: case 0x1270u:
        return 12u;
    default:
        return UINT32_MAX;
    }
}

#endif
