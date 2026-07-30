#ifndef OPENAGC_INDIRECT_DRAW_H
#define OPENAGC_INDIRECT_DRAW_H

#include <stdint.h>

uint32_t agcIndirectDrawInitiatorForFirmware(
    uint64_t modifier, uint16_t firmware_abi_key);

#endif
