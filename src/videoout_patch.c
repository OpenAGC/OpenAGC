/* openagc — SPDX-License-Identifier: Apache-2.0 */

#include "videoout_patch.h"

static const AgcVideoOutLinearPatch s_linear_patches[] = {
    {
        0x0550u,
        0x7e61u,
        {0x0f, 0x84, 0x15, 0x02, 0x00, 0x00},
    },
    {
        0x1160u,
        0x9922u,
        {0x0f, 0x84, 0x3c, 0x02, 0x00, 0x00},
    },
};

const AgcVideoOutLinearPatch *agcVideoOutFindLinearPatch(
    uint32_t raw_firmware_version)
{
    const uint32_t firmware_abi_key = raw_firmware_version >> 16;

    for (size_t i = 0;
         i < sizeof(s_linear_patches) / sizeof(s_linear_patches[0]); ++i) {
        if (s_linear_patches[i].firmware_abi_key == firmware_abi_key)
            return &s_linear_patches[i];
    }
    return NULL;
}
