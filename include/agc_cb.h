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

#ifndef _AGC_CB_H_
#define _AGC_CB_H_

#include <stddef.h>
#include <stdint.h>

#include "agc_error.h"
#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void     agcCbInit(SceAgcCb *cb, void *buffer, size_t size_bytes);
void     agcCbReset(SceAgcCb *cb, void *buffer, size_t size_bytes);
uint32_t agcCbCapacityDwords(const SceAgcCb *cb);
uint32_t agcCbUsedDwords(const SceAgcCb *cb);
uint32_t agcCbRemainingDwords(const SceAgcCb *cb);
uint32_t *agcCbAllocDwords(SceAgcCb *cb, uint32_t dword_count);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_CB_H_ */
