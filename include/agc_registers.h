/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _AGC_REGISTERS_H_
#define _AGC_REGISTERS_H_

#include <stdint.h>

/*
 * Gen5 AGC register offset definitions.
 *
 * These are the context (CX), shader (SH), and user-config (UC) register
 * offsets used by PM4 type-3 SET_CONTEXT_REG / SET_SH_REG / SET_UCONFIG_REG
 * packets. The offsets are relative to each register space's base.
 *
 * Sources: KytyPS5 (MIT) and SharpEmu (GPL-2.0+) register maps, cross-
 * referenced against OpenAGC's register-defaults tables. Bitfield shift/mask
 * constants are provided for registers that OpenAGC needs to decode when
 * building or patching command buffers.
 *
 * Register spaces:
 *   CX  — context registers  (SET_CONTEXT_REG,    opcode 0x69, max 0x3FF)
 *   SH  — shader registers   (SET_SH_REG,         opcode 0x76, max 0x2FF)
 *   UC  — user-config regs   (SET_UCONFIG_REG,    opcode 0x79, max 0x3FFF)
 */

/* ========================================================================
 * Bitfield extraction helper
 * ======================================================================== */

#define AGC_REG_GET(value, reg, field) \
    (((uint32_t)(value) >> AGC_REG_##reg##_##field##_SHIFT) & \
     AGC_REG_##reg##_##field##_MASK)

#define AGC_REG_SET(reg, field, val) \
    (((uint32_t)(val) & AGC_REG_##reg##_##field##_MASK) << \
     AGC_REG_##reg##_##field##_SHIFT)

/* ========================================================================
 * Context registers (CX) — offsets 0x000–0x3FF
 * ========================================================================
 */

/* Depth buffer */
#define AGC_REG_DB_RENDER_CONTROL                    0x000u
#define AGC_REG_DB_COUNT_CONTROL                     0x001u
#define AGC_REG_DB_DEPTH_VIEW                        0x002u
#define AGC_REG_DB_RENDER_OVERRIDE                   0x003u
#define AGC_REG_DB_RENDER_OVERRIDE2                  0x004u
#define AGC_REG_DB_HTILE_DATA_BASE                   0x005u
#define AGC_REG_PS_SHADER_SAMPLE_EXCLUSION_MASK      0x006u
#define AGC_REG_DB_DEPTH_SIZE_XY                     0x007u
#define AGC_REG_DB_DEPTH_BOUNDS_MIN                  0x008u
#define AGC_REG_DB_DEPTH_BOUNDS_MAX                  0x009u
#define AGC_REG_DB_STENCIL_CLEAR                     0x00Au
#define AGC_REG_DB_DEPTH_CLEAR                       0x00Bu
#define AGC_REG_DB_DFSM_CONTROL                      0x00Eu
#define AGC_REG_DB_DEPTH_INFO                        0x00Fu

#define AGC_REG_DB_Z_INFO                            0x010u
#define AGC_REG_DB_Z_INFO_FORMAT_SHIFT               0u
#define AGC_REG_DB_Z_INFO_FORMAT_MASK                0x3u
#define AGC_REG_DB_Z_INFO_NUM_SAMPLES_SHIFT          2u
#define AGC_REG_DB_Z_INFO_NUM_SAMPLES_MASK           0x3u
#define AGC_REG_DB_Z_INFO_ITERATE_FLUSH_SHIFT        11u
#define AGC_REG_DB_Z_INFO_ITERATE_FLUSH_MASK         0x1u
#define AGC_REG_DB_Z_INFO_PARTIALLY_RESIDENT_SHIFT   12u
#define AGC_REG_DB_Z_INFO_PARTIALLY_RESIDENT_MASK    0x1u
#define AGC_REG_DB_Z_INFO_MAXMIP_SHIFT               16u
#define AGC_REG_DB_Z_INFO_MAXMIP_MASK                0xFu
#define AGC_REG_DB_Z_INFO_TILE_MODE_INDEX_SHIFT      20u
#define AGC_REG_DB_Z_INFO_TILE_MODE_INDEX_MASK       0x7u
#define AGC_REG_DB_Z_INFO_DECOMPRESS_ON_N_ZPLANES_SHIFT 23u
#define AGC_REG_DB_Z_INFO_DECOMPRESS_ON_N_ZPLANES_MASK  0xFu
#define AGC_REG_DB_Z_INFO_ALLOW_EXPCLEAR_SHIFT       27u
#define AGC_REG_DB_Z_INFO_ALLOW_EXPCLEAR_MASK        0x1u
#define AGC_REG_DB_Z_INFO_TILE_SURFACE_ENABLE_SHIFT  29u
#define AGC_REG_DB_Z_INFO_TILE_SURFACE_ENABLE_MASK   0x1u
#define AGC_REG_DB_Z_INFO_ZRANGE_PRECISION_SHIFT     31u
#define AGC_REG_DB_Z_INFO_ZRANGE_PRECISION_MASK      0x1u

#define AGC_REG_DB_STENCIL_INFO                     0x011u
#define AGC_REG_DB_Z_READ_BASE                      0x012u
#define AGC_REG_DB_STENCIL_READ_BASE                0x013u
#define AGC_REG_DB_Z_WRITE_BASE                     0x014u
#define AGC_REG_DB_STENCIL_WRITE_BASE               0x015u
#define AGC_REG_DB_DEPTH_SIZE                       0x016u
#define AGC_REG_DB_DEPTH_SLICE                      0x017u
#define AGC_REG_DB_Z_READ_BASE_HI                   0x01Au
#define AGC_REG_DB_STENCIL_READ_BASE_HI             0x01Bu
#define AGC_REG_DB_Z_WRITE_BASE_HI                  0x01Cu
#define AGC_REG_DB_STENCIL_WRITE_BASE_HI            0x01Du
#define AGC_REG_DB_HTILE_DATA_BASE_HI               0x01Eu
#define AGC_REG_DB_RMI_L2_CACHE_CONTROL             0x01Fu

/* Texture / buffer base addresses */
#define AGC_REG_TA_BC_BASE_ADDR                     0x020u
#define AGC_REG_TA_BC_BASE_ADDR_HI                  0x021u

/* Scissor / window registers */
#define AGC_REG_PA_SC_SCREEN_SCISSOR_TL             0x00Cu
#define AGC_REG_PA_SC_SCREEN_SCISSOR_BR             0x00Du
#define AGC_REG_PA_SC_WINDOW_OFFSET                 0x080u
#define AGC_REG_PA_SC_WINDOW_SCISSOR_TL             0x081u
#define AGC_REG_PA_SC_WINDOW_SCISSOR_BR             0x082u
#define AGC_REG_PA_SC_CLIPRECT_RULE                 0x083u
#define AGC_REG_PA_SC_CLIPRECT_0_TL                 0x084u
#define AGC_REG_PA_SC_CLIPRECT_0_BR                 0x085u
#define AGC_REG_PA_SU_HARDWARE_SCREEN_OFFSET        0x08Du
#define AGC_REG_PA_SC_GENERIC_SCISSOR_TL            0x090u
#define AGC_REG_PA_SC_GENERIC_SCISSOR_BR            0x091u
#define AGC_REG_PA_SC_VPORT_SCISSOR_0_TL            0x094u
#define AGC_REG_PA_SC_VPORT_SCISSOR_0_BR            0x095u
#define AGC_REG_PA_SC_VPORT_ZMIN_0                  0x0B4u
#define AGC_REG_PA_SC_VPORT_ZMAX_0                  0x0B5u

/* Color target / blend */
#define AGC_REG_CB_TARGET_MASK                      0x08Eu
#define AGC_REG_CB_SHADER_MASK                      0x08Fu
#define AGC_REG_CB_BLEND_RED                        0x105u
#define AGC_REG_CB_BLEND_GREEN                      0x106u
#define AGC_REG_CB_BLEND_BLUE                       0x107u
#define AGC_REG_CB_BLEND_ALPHA                      0x108u
#define AGC_REG_CB_DCC_CONTROL                      0x109u
#define AGC_REG_CB_RMI_GL2_CACHE_CONTROL            0x104u

#define AGC_REG_CB_BLEND0_CONTROL                   0x1E0u
#define AGC_REG_CB_BLEND0_CONTROL_COLOR_SRCBLEND_SHIFT    0u
#define AGC_REG_CB_BLEND0_CONTROL_COLOR_SRCBLEND_MASK     0x1Fu
#define AGC_REG_CB_BLEND0_CONTROL_COLOR_COMB_FCN_SHIFT    5u
#define AGC_REG_CB_BLEND0_CONTROL_COLOR_COMB_FCN_MASK     0x7u
#define AGC_REG_CB_BLEND0_CONTROL_COLOR_DESTBLEND_SHIFT   8u
#define AGC_REG_CB_BLEND0_CONTROL_COLOR_DESTBLEND_MASK    0x1Fu
#define AGC_REG_CB_BLEND0_CONTROL_ALPHA_SRCBLEND_SHIFT    16u
#define AGC_REG_CB_BLEND0_CONTROL_ALPHA_SRCBLEND_MASK     0x1Fu
#define AGC_REG_CB_BLEND0_CONTROL_ALPHA_COMB_FCN_SHIFT    21u
#define AGC_REG_CB_BLEND0_CONTROL_ALPHA_COMB_FCN_MASK     0x7u
#define AGC_REG_CB_BLEND0_CONTROL_ALPHA_DESTBLEND_SHIFT   24u
#define AGC_REG_CB_BLEND0_CONTROL_ALPHA_DESTBLEND_MASK    0x1Fu
#define AGC_REG_CB_BLEND0_CONTROL_SEPARATE_ALPHA_BLEND_SHIFT 29u
#define AGC_REG_CB_BLEND0_CONTROL_SEPARATE_ALPHA_BLEND_MASK  0x1u
#define AGC_REG_CB_BLEND0_CONTROL_ENABLE_SHIFT            30u
#define AGC_REG_CB_BLEND0_CONTROL_ENABLE_MASK             0x1u

#define AGC_REG_CB_COLOR_CONTROL                     0x202u
#define AGC_REG_CB_COLOR_CONTROL_MODE_SHIFT          4u
#define AGC_REG_CB_COLOR_CONTROL_MODE_MASK           0x7u
#define AGC_REG_CB_COLOR_CONTROL_ROP3_SHIFT          16u
#define AGC_REG_CB_COLOR_CONTROL_ROP3_MASK           0xFFu

/* CB_COLOR0 registers — slot 0 of 8 (stride = 9 per slot) */
#define AGC_REG_CB_COLOR0_BASE                      0x318u
#define AGC_REG_CB_COLOR0_VIEW                      0x31Bu
#define AGC_REG_CB_COLOR0_INFO                      0x31Cu
#define AGC_REG_CB_COLOR0_ATTRIB                    0x31Du
#define AGC_REG_CB_COLOR0_DCC_CONTROL               0x31Eu
#define AGC_REG_CB_COLOR0_CMASK                     0x31Fu
#define AGC_REG_CB_COLOR0_FMASK                     0x321u
#define AGC_REG_CB_COLOR0_CLEAR_WORD0               0x323u
#define AGC_REG_CB_COLOR0_CLEAR_WORD1               0x324u
#define AGC_REG_CB_COLOR0_DCC_BASE                  0x325u
#define AGC_REG_CB_COLOR0_BASE_EXT                  0x390u
#define AGC_REG_CB_COLOR0_CMASK_BASE_EXT            0x398u
#define AGC_REG_CB_COLOR0_FMASK_BASE_EXT            0x3A0u
#define AGC_REG_CB_COLOR0_DCC_BASE_EXT              0x3A8u
#define AGC_REG_CB_COLOR0_ATTRIB2                   0x3B0u
#define AGC_REG_CB_COLOR0_ATTRIB3                   0x3B8u

/* CB_COLOR0_INFO bitfields */
#define AGC_REG_CB_COLOR0_INFO_FORMAT_SHIFT         2u
#define AGC_REG_CB_COLOR0_INFO_FORMAT_MASK          0x1Fu
#define AGC_REG_CB_COLOR0_INFO_NUMBER_TYPE_SHIFT    8u
#define AGC_REG_CB_COLOR0_INFO_NUMBER_TYPE_MASK     0x7u
#define AGC_REG_CB_COLOR0_INFO_COMP_SWAP_SHIFT      11u
#define AGC_REG_CB_COLOR0_INFO_COMP_SWAP_MASK       0x3u
#define AGC_REG_CB_COLOR0_INFO_FAST_CLEAR_SHIFT     13u
#define AGC_REG_CB_COLOR0_INFO_FAST_CLEAR_MASK      0x1u
#define AGC_REG_CB_COLOR0_INFO_COMPRESSION_SHIFT    14u
#define AGC_REG_CB_COLOR0_INFO_COMPRESSION_MASK     0x1u
#define AGC_REG_CB_COLOR0_INFO_BLEND_CLAMP_SHIFT    15u
#define AGC_REG_CB_COLOR0_INFO_BLEND_CLAMP_MASK     0x1u
#define AGC_REG_CB_COLOR0_INFO_BLEND_BYPASS_SHIFT   16u
#define AGC_REG_CB_COLOR0_INFO_BLEND_BYPASS_MASK    0x1u
#define AGC_REG_CB_COLOR0_INFO_ROUND_MODE_SHIFT     18u
#define AGC_REG_CB_COLOR0_INFO_ROUND_MODE_MASK      0x1u
#define AGC_REG_CB_COLOR0_INFO_DCC_ENABLE_SHIFT     28u
#define AGC_REG_CB_COLOR0_INFO_DCC_ENABLE_MASK      0x1u

/* CB_COLOR0_ATTRIB bitfields */
#define AGC_REG_CB_COLOR0_ATTRIB_TILE_MODE_INDEX_SHIFT       0u
#define AGC_REG_CB_COLOR0_ATTRIB_TILE_MODE_INDEX_MASK        0x1Fu
#define AGC_REG_CB_COLOR0_ATTRIB_FMASK_TILE_MODE_INDEX_SHIFT 5u
#define AGC_REG_CB_COLOR0_ATTRIB_FMASK_TILE_MODE_INDEX_MASK  0x1Fu
#define AGC_REG_CB_COLOR0_ATTRIB_NUM_SAMPLES_SHIFT           12u
#define AGC_REG_CB_COLOR0_ATTRIB_NUM_SAMPLES_MASK            0x7u
#define AGC_REG_CB_COLOR0_ATTRIB_NUM_FRAGMENTS_SHIFT         15u
#define AGC_REG_CB_COLOR0_ATTRIB_NUM_FRAGMENTS_MASK          0x3u
#define AGC_REG_CB_COLOR0_ATTRIB_FORCE_DST_ALPHA_1_SHIFT     17u
#define AGC_REG_CB_COLOR0_ATTRIB_FORCE_DST_ALPHA_1_MASK      0x1u

/* CB_COLOR0_ATTRIB2 bitfields */
#define AGC_REG_CB_COLOR0_ATTRIB2_MIP0_HEIGHT_SHIFT  0u
#define AGC_REG_CB_COLOR0_ATTRIB2_MIP0_HEIGHT_MASK   0x3FFFu
#define AGC_REG_CB_COLOR0_ATTRIB2_MIP0_WIDTH_SHIFT   14u
#define AGC_REG_CB_COLOR0_ATTRIB2_MIP0_WIDTH_MASK    0x3FFFu
#define AGC_REG_CB_COLOR0_ATTRIB2_MAX_MIP_SHIFT      28u
#define AGC_REG_CB_COLOR0_ATTRIB2_MAX_MIP_MASK       0xFu

/* CB_COLOR0_ATTRIB3 bitfields */
#define AGC_REG_CB_COLOR0_ATTRIB3_MIP0_DEPTH_SHIFT         0u
#define AGC_REG_CB_COLOR0_ATTRIB3_MIP0_DEPTH_MASK          0x1FFFu
#define AGC_REG_CB_COLOR0_ATTRIB3_COLOR_SW_MODE_SHIFT      14u
#define AGC_REG_CB_COLOR0_ATTRIB3_COLOR_SW_MODE_MASK       0x1Fu
#define AGC_REG_CB_COLOR0_ATTRIB3_RESOURCE_TYPE_SHIFT      24u
#define AGC_REG_CB_COLOR0_ATTRIB3_RESOURCE_TYPE_MASK       0x3u
#define AGC_REG_CB_COLOR0_ATTRIB3_CMASK_PIPE_ALIGNED_SHIFT 26u
#define AGC_REG_CB_COLOR0_ATTRIB3_CMASK_PIPE_ALIGNED_MASK  0x1u
#define AGC_REG_CB_COLOR0_ATTRIB3_DCC_PIPE_ALIGNED_SHIFT   30u
#define AGC_REG_CB_COLOR0_ATTRIB3_DCC_PIPE_ALIGNED_MASK    0x1u

/* CB_COLOR7 (last slot) */
#define AGC_REG_CB_COLOR7_BASE                      0x381u
#define AGC_REG_CB_COLOR7_VIEW                      0x384u
#define AGC_REG_CB_COLOR7_INFO                      0x385u
#define AGC_REG_CB_COLOR7_ATTRIB                    0x386u
#define AGC_REG_CB_COLOR7_DCC_CONTROL               0x387u
#define AGC_REG_CB_COLOR7_CMASK                     0x388u
#define AGC_REG_CB_COLOR7_FMASK                     0x38Au
#define AGC_REG_CB_COLOR7_CLEAR_WORD0               0x38Cu
#define AGC_REG_CB_COLOR7_CLEAR_WORD1               0x38Du
#define AGC_REG_CB_COLOR7_DCC_BASE                  0x38Eu
#define AGC_REG_CB_COLOR7_BASE_EXT                  0x397u
#define AGC_REG_CB_COLOR7_CMASK_BASE_EXT            0x39Fu
#define AGC_REG_CB_COLOR7_FMASK_BASE_EXT            0x3A7u
#define AGC_REG_CB_COLOR7_DCC_BASE_EXT              0x3AFu
#define AGC_REG_CB_COLOR7_ATTRIB2                   0x3B7u
#define AGC_REG_CB_COLOR7_ATTRIB3                   0x3BFu

/* Stride between CB_COLOR slots (for computing N from 0) */
#define AGC_REG_CB_COLOR_SLOT_STRIDE                9u

/* Depth / stencil control */
#define AGC_REG_DB_STENCIL_CONTROL                  0x10Bu
#define AGC_REG_DB_STENCILREFMASK                   0x10Cu
#define AGC_REG_DB_STENCILREFMASK_BF                0x10Du

#define AGC_REG_DB_DEPTH_CONTROL                    0x200u
#define AGC_REG_DB_DEPTH_CONTROL_STENCIL_ENABLE_SHIFT          0u
#define AGC_REG_DB_DEPTH_CONTROL_STENCIL_ENABLE_MASK           0x1u
#define AGC_REG_DB_DEPTH_CONTROL_Z_ENABLE_SHIFT                1u
#define AGC_REG_DB_DEPTH_CONTROL_Z_ENABLE_MASK                 0x1u
#define AGC_REG_DB_DEPTH_CONTROL_Z_WRITE_ENABLE_SHIFT          2u
#define AGC_REG_DB_DEPTH_CONTROL_Z_WRITE_ENABLE_MASK           0x1u
#define AGC_REG_DB_DEPTH_CONTROL_DEPTH_BOUNDS_ENABLE_SHIFT     3u
#define AGC_REG_DB_DEPTH_CONTROL_DEPTH_BOUNDS_ENABLE_MASK      0x1u
#define AGC_REG_DB_DEPTH_CONTROL_ZFUNC_SHIFT                   4u
#define AGC_REG_DB_DEPTH_CONTROL_ZFUNC_MASK                    0x7u
#define AGC_REG_DB_DEPTH_CONTROL_BACKFACE_ENABLE_SHIFT         7u
#define AGC_REG_DB_DEPTH_CONTROL_BACKFACE_ENABLE_MASK          0x1u
#define AGC_REG_DB_DEPTH_CONTROL_STENCILFUNC_SHIFT             8u
#define AGC_REG_DB_DEPTH_CONTROL_STENCILFUNC_MASK              0x7u
#define AGC_REG_DB_DEPTH_CONTROL_STENCILFUNC_BF_SHIFT          20u
#define AGC_REG_DB_DEPTH_CONTROL_STENCILFUNC_BF_MASK           0x7u
#define AGC_REG_DB_DEPTH_CONTROL_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL_SHIFT  30u
#define AGC_REG_DB_DEPTH_CONTROL_ENABLE_COLOR_WRITES_ON_DEPTH_FAIL_MASK   0x1u
#define AGC_REG_DB_DEPTH_CONTROL_DISABLE_COLOR_WRITES_ON_DEPTH_PASS_SHIFT 31u
#define AGC_REG_DB_DEPTH_CONTROL_DISABLE_COLOR_WRITES_ON_DEPTH_PASS_MASK  0x1u

#define AGC_REG_DB_EQAA                              0x201u
#define AGC_REG_DB_SHADER_CONTROL                    0x203u
#define AGC_REG_DB_SHADER_CONTROL_Z_EXPORT_ENABLE_SHIFT       0u
#define AGC_REG_DB_SHADER_CONTROL_Z_EXPORT_ENABLE_MASK        0x1u
#define AGC_REG_DB_SHADER_CONTROL_Z_ORDER_SHIFT               4u
#define AGC_REG_DB_SHADER_CONTROL_Z_ORDER_MASK                0x3u
#define AGC_REG_DB_SHADER_CONTROL_KILL_ENABLE_SHIFT           6u
#define AGC_REG_DB_SHADER_CONTROL_KILL_ENABLE_MASK            0x1u
#define AGC_REG_DB_SHADER_CONTROL_MASK_EXPORT_ENABLE_SHIFT    8u
#define AGC_REG_DB_SHADER_CONTROL_MASK_EXPORT_ENABLE_MASK     0x1u
#define AGC_REG_DB_SHADER_CONTROL_DUAL_EXPORT_ENABLE_SHIFT    9u
#define AGC_REG_DB_SHADER_CONTROL_DUAL_EXPORT_ENABLE_MASK     0x1u
#define AGC_REG_DB_SHADER_CONTROL_ALPHA_TO_MASK_DISABLE_SHIFT 11u
#define AGC_REG_DB_SHADER_CONTROL_ALPHA_TO_MASK_DISABLE_MASK  0x1u
#define AGC_REG_DB_SHADER_CONTROL_CONSERVATIVE_Z_EXPORT_SHIFT 13u
#define AGC_REG_DB_SHADER_CONTROL_CONSERVATIVE_Z_EXPORT_MASK  0x3u

#define AGC_REG_DB_ALPHA_TO_MASK                    0x2DCu
#define AGC_REG_DB_SRESULTS_COMPARE_STATE0          0x2B0u
#define AGC_REG_DB_SRESULTS_COMPARE_STATE1          0x2B1u
#define AGC_REG_DB_HTILE_SURFACE                    0x2AFu

/* PA (Primitive Assembly) — viewport / clip / raster */
#define AGC_REG_PA_CL_CLIP_CNTL                     0x204u
#define AGC_REG_PA_CL_CLIP_CNTL_UCP_ENA_SHIFT                   0u
#define AGC_REG_PA_CL_CLIP_CNTL_UCP_ENA_MASK                    0x3Fu
#define AGC_REG_PA_CL_CLIP_CNTL_CLIP_DISABLE_SHIFT              16u
#define AGC_REG_PA_CL_CLIP_CNTL_CLIP_DISABLE_MASK               0x1u
#define AGC_REG_PA_CL_CLIP_CNTL_DX_CLIP_SPACE_DEF_SHIFT         19u
#define AGC_REG_PA_CL_CLIP_CNTL_DX_CLIP_SPACE_DEF_MASK          0x1u
#define AGC_REG_PA_CL_CLIP_CNTL_DX_RASTERIZATION_KILL_SHIFT     22u
#define AGC_REG_PA_CL_CLIP_CNTL_DX_RASTERIZATION_KILL_MASK      0x1u
#define AGC_REG_PA_CL_CLIP_CNTL_ZCLIP_NEAR_DISABLE_SHIFT        26u
#define AGC_REG_PA_CL_CLIP_CNTL_ZCLIP_NEAR_DISABLE_MASK         0x1u
#define AGC_REG_PA_CL_CLIP_CNTL_ZCLIP_FAR_DISABLE_SHIFT         27u
#define AGC_REG_PA_CL_CLIP_CNTL_ZCLIP_FAR_DISABLE_MASK          0x1u

#define AGC_REG_PA_SU_SC_MODE_CNTL                  0x205u
#define AGC_REG_PA_SU_SC_MODE_CNTL_CULL_FRONT_SHIFT            0u
#define AGC_REG_PA_SU_SC_MODE_CNTL_CULL_FRONT_MASK             0x1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_CULL_BACK_SHIFT             1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_CULL_BACK_MASK              0x1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_FACE_SHIFT                  2u
#define AGC_REG_PA_SU_SC_MODE_CNTL_FACE_MASK                   0x1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLY_MODE_SHIFT             3u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLY_MODE_MASK              0x3u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_FRONT_PTYPE_SHIFT  5u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_FRONT_PTYPE_MASK   0x7u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_BACK_PTYPE_SHIFT   8u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLYMODE_BACK_PTYPE_MASK    0x7u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLY_OFFSET_FRONT_ENABLE_SHIFT  11u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLY_OFFSET_FRONT_ENABLE_MASK   0x1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLY_OFFSET_BACK_ENABLE_SHIFT   12u
#define AGC_REG_PA_SU_SC_MODE_CNTL_POLY_OFFSET_BACK_ENABLE_MASK    0x1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_VTX_WINDOW_OFFSET_ENABLE_SHIFT  16u
#define AGC_REG_PA_SU_SC_MODE_CNTL_VTX_WINDOW_OFFSET_ENABLE_MASK   0x1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST_SHIFT        19u
#define AGC_REG_PA_SU_SC_MODE_CNTL_PROVOKING_VTX_LAST_MASK         0x1u
#define AGC_REG_PA_SU_SC_MODE_CNTL_PERSP_CORR_DIS_SHIFT            20u
#define AGC_REG_PA_SU_SC_MODE_CNTL_PERSP_CORR_DIS_MASK             0x1u

#define AGC_REG_PA_CL_VTE_CNTL                      0x206u
#define AGC_REG_PA_CL_VS_OUT_CNTL                   0x207u
#define AGC_REG_PA_CL_OBJPRIM_ID_CNTL               0x20Du
#define AGC_REG_PA_SU_SMALL_PRIM_FILTER_CNTL        0x20Cu
#define AGC_REG_PA_STEREO_CNTL                      0x210u
#define AGC_REG_PA_STATE_STEREO_X                   0x211u

/* Viewport registers (slot 0 of 16) */
#define AGC_REG_PA_CL_VPORT_XSCALE                  0x10Fu
#define AGC_REG_PA_CL_VPORT_XOFFSET                 0x110u
#define AGC_REG_PA_CL_VPORT_YSCALE                  0x111u
#define AGC_REG_PA_CL_VPORT_YOFFSET                 0x112u
#define AGC_REG_PA_CL_VPORT_ZSCALE                  0x113u
#define AGC_REG_PA_CL_VPORT_ZOFFSET                 0x114u
/* Slot 15: */
#define AGC_REG_PA_CL_VPORT_XSCALE_15               0x169u
#define AGC_REG_PA_CL_VPORT_XOFFSET_15              0x16Au
#define AGC_REG_PA_CL_VPORT_YSCALE_15               0x16Bu
#define AGC_REG_PA_CL_VPORT_YOFFSET_15              0x16Cu
#define AGC_REG_PA_CL_VPORT_ZSCALE_15               0x16Du
#define AGC_REG_PA_CL_VPORT_ZOFFSET_15              0x16Eu
/* Stride = 6 dwords per viewport slot */

/* User clip planes (slot 0 of 6) */
#define AGC_REG_PA_CL_UCP_0_X                       0x16Fu
#define AGC_REG_PA_CL_UCP_0_Y                       0x170u
#define AGC_REG_PA_CL_UCP_0_Z                       0x171u
#define AGC_REG_PA_CL_UCP_0_W                       0x172u

/* Point / line / polygon offset */
#define AGC_REG_PA_SU_POINT_SIZE                    0x280u
#define AGC_REG_PA_SU_POINT_MINMAX                  0x281u
#define AGC_REG_PA_SU_LINE_CNTL                     0x282u
#define AGC_REG_PA_SU_POLY_OFFSET_DB_FMT_CNTL       0x2DEu
#define AGC_REG_PA_SU_POLY_OFFSET_CLAMP             0x2DFu
#define AGC_REG_PA_SU_POLY_OFFSET_FRONT_SCALE       0x2E0u
#define AGC_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET      0x2E1u
#define AGC_REG_PA_SU_POLY_OFFSET_BACK_SCALE        0x2E2u
#define AGC_REG_PA_SU_POLY_OFFSET_BACK_OFFSET       0x2E3u
#define AGC_REG_PA_SU_VTX_CNTL                      0x2F9u

/* Guard band adjustment */
#define AGC_REG_PA_CL_GB_VERT_CLIP_ADJ              0x2FAu
#define AGC_REG_PA_CL_GB_VERT_DISC_ADJ              0x2FBu
#define AGC_REG_PA_CL_GB_HORZ_CLIP_ADJ              0x2FCu
#define AGC_REG_PA_CL_GB_HORZ_DISC_ADJ              0x2FDu

/* Scissor / AA */
#define AGC_REG_PA_SC_MODE_CNTL_0                   0x292u
#define AGC_REG_PA_SC_MODE_CNTL_1                   0x293u
#define AGC_REG_PA_SC_AA_CONFIG                     0x2F8u
#define AGC_REG_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0   0x2FEu
#define AGC_REG_PA_SC_AA_MASK_X0Y0_X1Y0             0x30Eu
#define AGC_REG_PA_SC_AA_MASK_X0Y1_X1Y1             0x30Fu
#define AGC_REG_PA_SC_CENTROID_PRIORITY_0           0x2F5u
#define AGC_REG_PA_SC_CENTROID_PRIORITY_1           0x2F6u
#define AGC_REG_PA_SC_SHADER_CONTROL                0x310u
#define AGC_REG_PA_SC_BINNER_CNTL_0                 0x311u
#define AGC_REG_PA_SC_BINNER_CNTL_1                 0x312u
#define AGC_REG_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL 0x313u
#define AGC_REG_PA_SC_NGG_MODE_CNTL                 0x314u
#define AGC_REG_VGT_VERTEX_REUSE_BLOCK_CNTL         0x316u
#define AGC_REG_PA_SC_FOV_WINDOW_LR                 0x0EBu
#define AGC_REG_PA_SC_FOV_WINDOW_TB                 0x0ECu
#define AGC_REG_PA_SC_FSR_ENABLE                    0x0F0u  /* AGC-custom */
#define AGC_REG_PA_SC_RIGHT_VERT_GRID               0x0E8u
#define AGC_REG_PA_SC_LEFT_VERT_GRID                0x0E9u
#define AGC_REG_PA_SC_HORIZ_GRID                    0x0EAu

/* SPI (Shader Processor Interpolator) */
#define AGC_REG_SPI_PS_INPUT_CNTL_0                 0x191u
#define AGC_REG_SPI_PS_INPUT_CNTL_31                0x1B0u
#define AGC_REG_SPI_VS_OUT_CONFIG                   0x1B1u
#define AGC_REG_SPI_PS_INPUT_ENA                    0x1B3u
#define AGC_REG_SPI_PS_INPUT_ADDR                   0x1B4u
#define AGC_REG_SPI_INTERP_CONTROL_0                0x1B5u
#define AGC_REG_SPI_PS_IN_CONTROL                   0x1B6u
#define AGC_REG_SPI_BARYC_CNTL                      0x1B8u
#define AGC_REG_SPI_TMPRING_SIZE                    0x1BAu
#define AGC_REG_SPI_SHADER_IDX_FORMAT               0x1C2u
#define AGC_REG_SPI_SHADER_POS_FORMAT               0x1C3u
#define AGC_REG_SPI_SHADER_Z_FORMAT                 0x1C4u
#define AGC_REG_SPI_SHADER_COL_FORMAT               0x1C5u

/* VGT (Vector Graphics Tessellator) */
#define AGC_REG_VGT_MULTI_PRIM_IB_RESET_INDX        0x103u
#define AGC_REG_GE_MAX_OUTPUT_PER_SUBGROUP          0x1FFu
#define AGC_REG_VGT_HOS_MAX_TESS_LEVEL              0x286u
#define AGC_REG_VGT_HOS_MIN_TESS_LEVEL              0x287u
#define AGC_REG_VGT_GS_ONCHIP_CNTL                  0x291u
#define AGC_REG_VGT_GS_OUT_PRIM_TYPE                0x29Bu
#define AGC_REG_VGT_PRIMITIVEID_EN                  0x2A1u
#define AGC_REG_VGT_PRIMITIVEID_RESET               0x2A3u
#define AGC_REG_VGT_DRAW_PAYLOAD_CNTL               0x2A6u
#define AGC_REG_VGT_ESGS_RING_ITEMSIZE              0x2ABu
#define AGC_REG_VGT_REUSE_OFF                       0x2ADu
#define AGC_REG_VGT_GS_MAX_VERT_OUT                 0x2CEu
#define AGC_REG_GE_NGG_SUBGRP_CNTL                  0x2D3u
#define AGC_REG_VGT_TESS_DISTRIBUTION               0x2D4u
#define AGC_REG_VGT_SHADER_STAGES_EN                0x2D5u
#define AGC_REG_VGT_LS_HS_CONFIG                    0x2D6u
#define AGC_REG_VGT_TF_PARAM                        0x2DBu
#define AGC_REG_VGT_GS_INSTANCE_CNT                 0x2E4u

/* FSR (AGC-custom, fake register codes >= 0x80000000) */
#define AGC_REG_FSR_RECURSIONS0                     0x800003FCu
#define AGC_REG_FSR_RECURSIONS1                     0x800003FDu
#define AGC_REG_FSR_ENABLE                          0x800003FEu
#define AGC_REG_FSR_EXTEND_SUBPIXEL_ROUNDING        0x80003FF4u
#define AGC_REG_FSR_ALPHA_VALUE0                    0x80003FF5u
#define AGC_REG_FSR_ALPHA_VALUE1                    0x80003FF6u
#define AGC_REG_FSR_CONTROL_POINT0                  0x80003FF7u
#define AGC_REG_FSR_CONTROL_POINT1                  0x80003FF8u
#define AGC_REG_FSR_CONTROL_POINT2                  0x80003FF9u
#define AGC_REG_FSR_CONTROL_POINT3                  0x80003FFAu
#define AGC_REG_FSR_WINDOW0                         0x80003FFBu
#define AGC_REG_FSR_WINDOW1                         0x80003FFCu

/* Sentinel / NOP registers (fake codes, not real HW) */
#define AGC_REG_CX_NOP                              0x800003FFu
#define AGC_REG_CX_NUM                              (0x3FFu + 1u)

/* ========================================================================
 * Shader registers (SH) — offsets 0x000–0x2FF
 * ========================================================================
 */

/* Pixel shader (PS) */
#define AGC_REG_SPI_SHADER_TBA_LO_PS                0x000u
#define AGC_REG_SPI_SHADER_PGM_RSRC4_PS             0x001u
#define AGC_REG_SPI_SHADER_TMA_LO_PS                0x002u
#define AGC_REG_SPI_SHADER_TMA_HI_PS                0x003u
#define AGC_REG_SPI_SHADER_PGM_CHKSUM_PS            0x006u
#define AGC_REG_SPI_SHADER_PGM_RSRC3_PS             0x007u
#define AGC_REG_SPI_SHADER_PGM_LO_PS                0x008u
#define AGC_REG_SPI_SHADER_PGM_HI_PS                0x009u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_PS             0x00Au
#define AGC_REG_SPI_SHADER_PGM_RSRC2_PS             0x00Bu
#define AGC_REG_SPI_SHADER_USER_DATA_PS_0           0x00Cu
#define AGC_REG_SPI_SHADER_USER_DATA_PS_15          0x01Bu
#define AGC_REG_SPI_SHADER_USER_DATA_PS_31          0x02Bu
#define AGC_REG_SPI_SHADER_REQ_CTRL_PS              0x030u
#define AGC_REG_SPI_SHADER_USER_ACCUM_PS_0          0x032u

/* Geometry shader (GS) / Export shader (ES) */
#define AGC_REG_SPI_SHADER_PGM_CHKSUM_GS            0x080u
#define AGC_REG_SPI_SHADER_PGM_RSRC4_GS             0x081u
#define AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_GS     0x082u
#define AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_GS     0x083u
#define AGC_REG_SPI_SHADER_PGM_RSRC3_GS             0x087u
#define AGC_REG_SPI_SHADER_PGM_LO_GS                0x088u
#define AGC_REG_SPI_SHADER_PGM_HI_GS                0x089u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_GS             0x08Au
#define AGC_REG_SPI_SHADER_PGM_RSRC2_GS             0x08Bu
#define AGC_REG_SPI_SHADER_USER_DATA_GS_0           0x08Cu
#define AGC_REG_SPI_SHADER_USER_DATA_GS_15          0x09Bu
#define AGC_REG_SPI_SHADER_USER_DATA_GS_31          0x0ABu
#define AGC_REG_SPI_SHADER_REQ_CTRL_ESGS            0x0B0u
#define AGC_REG_SPI_SHADER_USER_ACCUM_ESGS_0        0x0B2u
#define AGC_REG_SPI_SHADER_PGM_RSRC4_VS             0x041u
#define AGC_REG_SPI_SHADER_PGM_CHKSUM_VS            0x045u
#define AGC_REG_SPI_SHADER_PGM_RSRC3_VS             0x046u
#define AGC_REG_SPI_SHADER_LATE_ALLOC_VS            0x047u
#define AGC_REG_SPI_SHADER_PGM_LO_VS                0x048u
#define AGC_REG_SPI_SHADER_PGM_HI_VS                0x049u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_VS             0x04Au
#define AGC_REG_SPI_SHADER_PGM_RSRC2_VS             0x04Bu
#define AGC_REG_SPI_SHADER_PGM_LO_ES                0x0C8u
#define AGC_REG_SPI_SHADER_PGM_HI_ES                0x0C9u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_ES             0x0CAu
#define AGC_REG_SPI_SHADER_PGM_RSRC2_ES             0x0CBu
#define AGC_REG_SPI_SHADER_USER_DATA_ES_0           0x0CCu

/* Hull shader (HS) / Local shader (LS) */
#define AGC_REG_SPI_SHADER_PGM_CHKSUM_HS            0x100u
#define AGC_REG_SPI_SHADER_PGM_RSRC4_HS             0x101u
#define AGC_REG_SPI_SHADER_USER_DATA_ADDR_LO_HS     0x102u
#define AGC_REG_SPI_SHADER_USER_DATA_ADDR_HI_HS     0x103u
#define AGC_REG_SPI_SHADER_PGM_RSRC3_HS             0x107u
#define AGC_REG_SPI_SHADER_PGM_LO_HS                0x108u
#define AGC_REG_SPI_SHADER_PGM_HI_HS                0x109u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_HS             0x10Au
#define AGC_REG_SPI_SHADER_PGM_RSRC2_HS             0x10Bu
#define AGC_REG_SPI_SHADER_USER_DATA_HS_0           0x10Cu
#define AGC_REG_SPI_SHADER_USER_DATA_HS_15          0x11Bu
#define AGC_REG_SPI_SHADER_USER_DATA_HS_31          0x12Bu
#define AGC_REG_SPI_SHADER_REQ_CTRL_LSHS            0x130u
#define AGC_REG_SPI_SHADER_USER_ACCUM_LSHS_0        0x132u
#define AGC_REG_SPI_SHADER_PGM_LO_LS                0x148u
#define AGC_REG_SPI_SHADER_PGM_HI_LS                0x149u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_LS             0x14Au
#define AGC_REG_SPI_SHADER_PGM_RSRC2_LS             0x14Bu

/* Compute shader (CS) — in SH register space */
#define AGC_REG_COMPUTE_START_X                     0x204u
#define AGC_REG_COMPUTE_START_Y                     0x205u
#define AGC_REG_COMPUTE_START_Z                     0x206u
#define AGC_REG_COMPUTE_NUM_THREAD_X                0x207u
#define AGC_REG_COMPUTE_NUM_THREAD_Y                0x208u
#define AGC_REG_COMPUTE_NUM_THREAD_Z                0x209u
#define AGC_REG_COMPUTE_PGM_LO                      0x20Cu
#define AGC_REG_COMPUTE_PGM_HI                      0x20Du
#define AGC_REG_COMPUTE_PGM_RSRC1                   0x212u
#define AGC_REG_COMPUTE_PGM_RSRC2                   0x213u
#define AGC_REG_COMPUTE_PGM_RSRC3                   0x228u
#define AGC_REG_COMPUTE_RESOURCE_LIMITS             0x215u
#define AGC_REG_COMPUTE_TMPRING_SIZE                0x218u
#define AGC_REG_COMPUTE_USER_ACCUM_0                0x224u
#define AGC_REG_COMPUTE_USER_DATA_0                 0x240u
#define AGC_REG_COMPUTE_USER_DATA_15                0x24Fu
#define AGC_REG_COMPUTE_DISPATCH_TUNNEL             0x27Du

/* RSRC1 bitfields (shared layout for all shader types) */
#define AGC_REG_SPI_SHADER_PGM_RSRC1_VGPRS_SHIFT    0u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_VGPRS_MASK     0x3Fu
#define AGC_REG_SPI_SHADER_PGM_RSRC1_SGPRS_SHIFT    6u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_SGPRS_MASK     0xFu
#define AGC_REG_SPI_SHADER_PGM_RSRC1_PRIORITY_SHIFT 10u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_PRIORITY_MASK  0x3u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_FLOAT_MODE_SHIFT  12u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_FLOAT_MODE_MASK   0xFFu
#define AGC_REG_SPI_SHADER_PGM_RSRC1_DX10_CLAMP_SHIFT  21u
#define AGC_REG_SPI_SHADER_PGM_RSRC1_DX10_CLAMP_MASK   0x1u

/* RSRC2 bitfields (shared layout) */
#define AGC_REG_SPI_SHADER_PGM_RSRC2_SCRATCH_EN_SHIFT      0u
#define AGC_REG_SPI_SHADER_PGM_RSRC2_SCRATCH_EN_MASK       0x1u
#define AGC_REG_SPI_SHADER_PGM_RSRC2_USER_SGPR_SHIFT       1u
#define AGC_REG_SPI_SHADER_PGM_RSRC2_USER_SGPR_MASK        0x1Fu
#define AGC_REG_SPI_SHADER_PGM_RSRC2_USER_SGPR_MSB_SHIFT   27u
#define AGC_REG_SPI_SHADER_PGM_RSRC2_USER_SGPR_MSB_MASK    0x1u

/* Sentinel */
#define AGC_REG_SH_NOP                              0x800002FFu
#define AGC_REG_SH_NUM                              (0x2FFu + 1u)

/* ========================================================================
 * User-config registers (UC) — offsets 0x000–0x3FFF
 * ========================================================================
 */

#define AGC_REG_VGT_PRIMITIVE_TYPE                  0x242u
#define AGC_REG_VGT_PRIMITIVE_TYPE_PRIM_TYPE_SHIFT  0u
#define AGC_REG_VGT_PRIMITIVE_TYPE_PRIM_TYPE_MASK   0x3Fu

#define AGC_REG_VGT_INDEX_TYPE                      0x243u
#define AGC_REG_VGT_OBJECT_ID                       0x248u
#define AGC_REG_GE_MIN_VTX_INDX                     0x249u
#define AGC_REG_GE_INDX_OFFSET                      0x24Au
#define AGC_REG_GE_MULTI_PRIM_IB_RESET_EN           0x24Bu
#define AGC_REG_VGT_TF_RING_SIZE                    0x24Eu
#define AGC_REG_VGT_HS_OFFCHIP_PARAM                0x24Fu
#define AGC_REG_VGT_TF_MEMORY_BASE                  0x250u
#define AGC_REG_VGT_TF_MEMORY_BASE_HI               0x261u
#define AGC_REG_IA_MULTI_VGT_PARAM                  0x258u
#define AGC_REG_GE_MAX_VTX_INDX                     0x259u
#define AGC_REG_GE_CNTL                             0x25Bu
#define AGC_REG_GE_CNTL_PRIM_GRP_SIZE_SHIFT         0u
#define AGC_REG_GE_CNTL_PRIM_GRP_SIZE_MASK          0x1FFu
#define AGC_REG_GE_CNTL_VERT_GRP_SIZE_SHIFT         9u
#define AGC_REG_GE_CNTL_VERT_GRP_SIZE_MASK          0x1FFu
#define AGC_REG_GE_USER_VGPR1                       0x25Cu
#define AGC_REG_GE_USER_VGPR2                       0x25Du
#define AGC_REG_GE_USER_VGPR3                       0x25Eu
#define AGC_REG_GE_STEREO_CNTL                      0x25Fu
#define AGC_REG_GE_USER_VGPR_EN                     0x262u

#define AGC_REG_TA_CS_BC_BASE_ADDR                  0x380u
#define AGC_REG_TA_CS_BC_BASE_ADDR_HI               0x381u
#define AGC_REG_TEXTURE_GRADIENT_FACTORS            0x382u

#define AGC_REG_GDS_OA_CNTL                         0x41Du
#define AGC_REG_GDS_OA_COUNTER                      0x41Eu
#define AGC_REG_GDS_OA_ADDRESS                      0x41Fu

/* AGC-custom UC registers (fake codes >= 0x80000000) */
#define AGC_REG_TEXTURE_GRADIENT_CONTROL            0x80003FFDu
#define AGC_REG_MEMORY_MAPPING_MASK                 0x80003FFEu
#define AGC_REG_UC_NOP                              0x80003FFFu
#define AGC_REG_UC_NUM                              (0x3FFFu + 1u)

/* ========================================================================
 * PM4 packet R (sub-command) codes — encoded in IT_NOP bits 7:2
 * ========================================================================
 */

#define AGC_PM4_R_ZERO              0x00u
#define AGC_PM4_R_DRAW_RESET        0x05u
#define AGC_PM4_R_WAIT_FLIP_DONE    0x06u
#define AGC_PM4_R_DISPATCH_RESET    0x09u
#define AGC_PM4_R_WAIT_MEM_32       0x0Au
#define AGC_PM4_R_PUSH_MARKER       0x0Bu
#define AGC_PM4_R_POP_MARKER        0x0Cu
#define AGC_PM4_R_ACQUIRE_MEM       0x14u
#define AGC_PM4_R_WRITE_DATA        0x15u
#define AGC_PM4_R_WAIT_MEM_64       0x16u
#define AGC_PM4_R_FLIP              0x17u
#define AGC_PM4_R_RELEASE_MEM       0x18u
#define AGC_PM4_R_DMA_DATA          0x19u

#endif /* _AGC_REGISTERS_H_ */
