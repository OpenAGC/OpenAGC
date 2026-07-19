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

#ifndef _AGC_RE_H_
#define _AGC_RE_H_

#include <stdint.h>

/*
 * Reverse-engineering constants captured from the local firmware/emulator
 * references. These are intentionally separated from stable public API types.
 */

#define AGC_SHADER_FILE_HEADER              0x34333231u
#define AGC_SHADER_HEADER_VERSION_GEN5      0x18u

#define AGC_SHADER_USER_DATA_OFFSET         0x08u
#define AGC_SHADER_CODE_OFFSET              0x10u
#define AGC_SHADER_CX_REGISTERS_OFFSET      0x18u
#define AGC_SHADER_SH_REGISTERS_OFFSET      0x20u
#define AGC_SHADER_SPECIALS_OFFSET          0x28u
#define AGC_SHADER_INPUT_SEMANTICS_OFFSET   0x30u
#define AGC_SHADER_OUTPUT_SEMANTICS_OFFSET  0x38u
#define AGC_SHADER_NUM_INPUT_SEMANTICS      0x50u
#define AGC_SHADER_NUM_OUTPUT_SEMANTICS     0x56u
#define AGC_SHADER_TYPE_OFFSET              0x5Au
#define AGC_SHADER_NUM_SH_REGISTERS_OFFSET  0x5Cu

#define AGC_CB_CURSOR_UP_OFFSET             0x10u
#define AGC_CB_CURSOR_DOWN_OFFSET           0x18u
#define AGC_CB_CALLBACK_OFFSET              0x20u
#define AGC_CB_RESERVED_DW_OFFSET           0x30u

#define AGC_SHADER_SPECIAL_GE_CNTL_OFFSET              0x00u
#define AGC_SHADER_SPECIAL_VGT_SHADER_STAGES_EN_OFFSET 0x08u
#define AGC_SHADER_SPECIAL_VGT_GS_OUT_PRIM_TYPE_OFFSET 0x20u
#define AGC_SHADER_SPECIAL_GE_USER_VGPR_EN_OFFSET      0x28u

#define AGC_REGISTER_DEFAULTS_VERSION_7      7u
#define AGC_REGISTER_DEFAULTS_VERSION_8      8u
#define AGC_REGISTER_DEFAULTS_VERSION_10     10u
#define AGC_REGISTER_DEFAULTS_HEADER_SIZE    0x40u
#define AGC_REGISTER_DEFAULT_BLOCK_SIZE      128u

#define AGC_CB_SET_SH_REGISTER_RANGE_MARKER  0x6875000Du

typedef struct AgcReRegisterDefault {
    uint32_t space;
    uint32_t index;
    uint32_t type;
    uint32_t offset;
    uint32_t value;
} AgcReRegisterDefault;

#endif /* _AGC_RE_H_ */
