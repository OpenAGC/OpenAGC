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
#include "agc_types.h"
#include "agc_error.h"
#include "agc_pm4.h"

#include <string.h>

/*
 * Default context state.
 *
 * The PS5 firmware initializes a flat context register block that
 * represents the default GPU state. This is loaded at the start
 * of each frame via LOAD_CONTEXT_REG or set inline.
 *
 * TODO: Populate with actual default register values from
 * firmware analysis of sceAgcVshDcbInitializeDefaultHardwareState.
 */
static const AgcContextState g_default_state = { .data = {0} };

int32_t PS5_SYSV_ABI sceAgcGetDefaultState(AgcContextState* out_state) {
    if (!out_state)
        return AGC_ERROR_INVALID_ARGUMENT;
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
