/* openagc — SPDX-License-Identifier: Apache-2.0 */

#ifndef OPENAGC_VIDEOOUT_PATCH_H
#define OPENAGC_VIDEOOUT_PATCH_H

#include <stddef.h>
#include <stdint.h>

#define AGC_VIDEOOUT_LINEAR_PATCH_SIZE 6u

typedef struct AgcVideoOutLinearPatch {
    uint32_t firmware_abi_key;
    uintptr_t text_offset;
    uint8_t original[AGC_VIDEOOUT_LINEAR_PATCH_SIZE];
} AgcVideoOutLinearPatch;

const AgcVideoOutLinearPatch *agcVideoOutFindLinearPatch(
    uint32_t raw_firmware_version);

#endif /* OPENAGC_VIDEOOUT_PATCH_H */
