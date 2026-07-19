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
 * openagc — context_state.c
 *
 * AGC context state management.
 * Provides default state initialization and query functions.
 */

#include "agcdriver.h"
#include "agc_context.h"
#include "agc_types.h"
#include "agc_error.h"
#include "agc_pm4.h"

#include <string.h>
#include <stdbool.h>

/*
 * Default context state.
 *
 * The PS5 firmware initializes a flat context register block that
 * represents the default GPU state. This is loaded at the start
 * of each frame via LOAD_CONTEXT_REG or set inline.
 *
 * Populated from the v8 register defaults (FW 5.50) primary groups.
 * Each group contains (offset, value) pairs for CX/SH/UC registers.
 */
static AgcContextState g_default_state;
static bool g_default_state_initialized;

static void init_default_state(void) {
    memset(&g_default_state, 0, sizeof(g_default_state));

    uint32_t group_count = 0;
    const AgcRegisterDefaultsGroup *groups =
        agcRegisterDefaultsGetPrimaryGroupsForVersion(8, &group_count);

    for (uint32_t i = 0; i < group_count; i++) {
        const AgcRegisterDefaultsGroup *group = &groups[i];
        for (uint32_t r = 0; r < group->register_count; r++) {
            uint32_t offset = group->registers[r].offset;
            uint32_t value = group->registers[r].value;
            if (offset < sizeof(g_default_state.data))
                g_default_state.data[offset] = value;
        }
    }
    g_default_state_initialized = true;
}

int32_t PS5_SYSV_ABI sceAgcGetDefaultState(AgcContextState* out_state) {
    if (!out_state)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (!g_default_state_initialized)
        init_default_state();
    memcpy(out_state, &g_default_state, sizeof(AgcContextState));
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcGetGameDefaultState(AgcContextState* out_state) {
    if (!out_state)
        return AGC_ERROR_INVALID_ARGUMENT;
    /* Game default state is the same as default state for now.
     * On real PS5, this may include game-specific optimizations. */
    memcpy(out_state, &g_default_state, sizeof(AgcContextState));
    return AGC_OK;
}

int32_t PS5_SYSV_ABI sceAgcGetDefaultCxStateFlat(void* out_state, uint32_t size) {
    if (!out_state)
        return AGC_ERROR_INVALID_ARGUMENT;
    if (size > sizeof(AgcContextState))
        size = sizeof(AgcContextState);
    memcpy(out_state, &g_default_state, size);
    return AGC_OK;
}
