/*
 * openagc - SPDX-License-Identifier: Apache-2.0
 *
 * Private state shared by the game-compatibility packet layer and native
 * backend. This is not part of the Sony-compatible public ABI.
 */

#ifndef OPENAGC_GAME_COMPAT_INTERNAL_H
#define OPENAGC_GAME_COMPAT_INTERNAL_H

#include <stdint.h>

void agcGameCompatConfigureContextState(
    uint64_t sync_label_gpu_address,
    uint64_t restore_list_gpu_address,
    uint32_t restore_count,
    uint32_t append_restore);

#endif /* OPENAGC_GAME_COMPAT_INTERNAL_H */
