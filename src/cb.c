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
 * openagc - cb.c
 *
 * Sony-style command-buffer cursor helpers.
 */

#include "agc_cb.h"

void agcCbInit(SceAgcCb *cb, void *buffer, size_t size_bytes)
{
    if (!cb)
        return;

    cb->reserved0 = (uintptr_t)buffer;
    cb->reserved1 = 0;
    cb->cursor_up = (uintptr_t)buffer;
    cb->cursor_down = (uintptr_t)buffer + size_bytes;
    cb->callback = 0;
    cb->reserved2 = 0;
    cb->reserved_dw = 0;
    cb->reserved3 = 0;
}

void agcCbReset(SceAgcCb *cb, void *buffer, size_t size_bytes)
{
    agcCbInit(cb, buffer, size_bytes);
}

uint32_t agcCbCapacityDwords(const SceAgcCb *cb)
{
    if (!cb || cb->reserved0 == 0 || cb->cursor_down < cb->reserved0)
        return 0;
    return (uint32_t)((cb->cursor_down - cb->reserved0) / sizeof(uint32_t));
}

uint32_t agcCbUsedDwords(const SceAgcCb *cb)
{
    if (!cb || cb->reserved0 == 0 || cb->cursor_up < cb->reserved0)
        return 0;
    return (uint32_t)((cb->cursor_up - cb->reserved0) / sizeof(uint32_t));
}

uint32_t agcCbRemainingDwords(const SceAgcCb *cb)
{
    if (!cb || cb->cursor_down < cb->cursor_up)
        return 0;

    uint32_t available = (uint32_t)((cb->cursor_down - cb->cursor_up) / sizeof(uint32_t));
    return available > cb->reserved_dw ? available - cb->reserved_dw : 0;
}

uint32_t *agcCbAllocDwords(SceAgcCb *cb, uint32_t dword_count)
{
    if (!cb || dword_count == 0 || dword_count > agcCbRemainingDwords(cb))
        return 0;

    uint32_t *result = (uint32_t *)cb->cursor_up;
    cb->cursor_up += (uintptr_t)dword_count * sizeof(uint32_t);
    return result;
}
