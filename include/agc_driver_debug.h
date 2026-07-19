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

#ifndef _AGC_DRIVER_DEBUG_H_
#define _AGC_DRIVER_DEBUG_H_

#include <stdint.h>

#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const AgcCommandBufferSubmit *agcDriverDebugLastDcbSubmit(void);
const AgcCommandBufferSubmit *agcDriverDebugLastAcbSubmit(uint32_t *owner_handle);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_DRIVER_DEBUG_H_ */
