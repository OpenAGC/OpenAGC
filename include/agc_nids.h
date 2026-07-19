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

#ifndef _AGC_NIDS_H_
#define _AGC_NIDS_H_

/*
 * Known Gen5 AGC NIDs — cross-referenced from observation and ps5-openagc
 * firmware 5.50 SPRX analysis.
 *
 * Sources:
 *   - HLE reference AgcExports.cs (original identification)
 *   - ps5-openagc include/ps5/internal/agc_nid.h (SPRX NID matching —
 *     NID-to-name mapping only; ps5-openagc's ioctl layouts are NOT trusted,
 *     see analysis/ps5_openagc_audit.md)
 *
 * Total identified: 340 NIDs across libSceAgc (213) and libSceAgcDriver (127),
 * out of 366 total FW 5.50 SPRX exports (92.9% coverage).
 * Sources: KytyPS5 LIB_FUNC, ps5-openagc agc_nid.h, FW 3.20 genstub files,
 * SPRX disassembly (GetSize stubs, error-code stubs).
 * For NIDs from other firmware versions, see agc_nids_version_variants.tsv
 * FW 5.50 export counts: libSceAgc=222, libSceAgcDriver=145, libSceAgcVsh=219.
 */

/* === libSceAgc.sprx === */

#define AGC_NID_SCE_AGC_INIT                         "kW3GLb7QfPg"
#define AGC_NID_SCE_AGC_GET_REGISTER_DEFAULTS2       "2JtWUUiYBXs"
#define AGC_NID_SCE_AGC_GET_REGISTER_DEFAULTS2_INT   "wRbq6ZjNop4"
#define AGC_NID_SCE_AGC_CREATE_SHADER                "f3dg2CSgRKY"
#define AGC_NID_SCE_AGC_GET_DATA_PACKET_PAYLOAD      "V++UgBtQhn0"
#define AGC_NID_SCE_AGC_CB_NOP                       "LtTouSCZjHM"
#define AGC_NID_SCE_AGC_CB_DISPATCH                  "k3GhuSNmBLU"
#define AGC_NID_SCE_AGC_CB_SET_SH_REGISTERS_DIRECT   "UZbQjYAwwXM"

/* ACB builders */
#define AGC_NID_SCE_AGC_ACB_ATOMIC_MEM               "XKKuA6VkSRc"
#define AGC_NID_SCE_AGC_ACB_ACQUIRE_MEM              "KT-hTp-Ch14"
#define AGC_NID_SCE_AGC_ACB_COND_EXEC                "qyM2bxYFPAk"
#define AGC_NID_SCE_AGC_ACB_COPY_DATA                "qzMN2XKGA4k"
#define AGC_NID_SCE_AGC_ACB_DISPATCH_INDIRECT        "j3EtxFkSIhQ"
#define AGC_NID_SCE_AGC_ACB_DMA_DATA                 "-RnpfpxIhec"
#define AGC_NID_SCE_AGC_ACB_EVENT_WRITE              "cFazmnXpJOE"
#define AGC_NID_SCE_AGC_ACB_INIT_DEFAULT_HW_PRE0090  "rPtseVo5ToI"
#define AGC_NID_SCE_AGC_ACB_RESET_QUEUE              "JrtiDtKeS38"
#define AGC_NID_SCE_AGC_ACB_JUMP                     "e1DFTg+Sd8U"
#define AGC_NID_SCE_AGC_ACB_WRITE_DATA               "eZ4+17OQz4Q"
#define AGC_NID_SCE_AGC_ACB_WAIT_REG_MEM             "htn36gPnBk4"
#define AGC_NID_SCE_AGC_ACB_REWIND                   "DwICrVxerkY"
#define AGC_NID_SCE_AGC_ACB_PRIME_UTCL2              "szG7hz2yEhA"
#define AGC_NID_SCE_AGC_ACB_ATOMIC_GDS_PRE0090       "cduV1f0dcGQ"
#define AGC_NID_SCE_AGC_ACB_ATOMIC_GDS               "gQkqkLttcpw"
#define AGC_NID_SCE_AGC_ACB_SET_MARKER               "xAeBOa0A3kk"
#define AGC_NID_SCE_AGC_ACB_PUSH_MARKER              "cpCILPya5Zk"
#define AGC_NID_SCE_AGC_ACB_POP_MARKER               "6mFxkVqdmbQ"
#define AGC_NID_SCE_AGC_ACB_SET_FLIP                 "ebixW91gpPw"
#define AGC_NID_SCE_AGC_ACB_MEM_SEMAPHORE            "q4VuU-QsLOE"
#define AGC_NID_SCE_AGC_ACB_WAIT_SAFE_RENDERING      "GPbUp9jXQa8"
#define AGC_NID_SCE_AGC_ACB_SET_WORKLOADS_ACTIVE     "rVOmPz2RBlg"
#define AGC_NID_SCE_AGC_ACB_SET_WORKLOAD_COMPLETE    "opR1JeJZCBU"
#define AGC_NID_SCE_AGC_ACB_SET_WORKLOAD_STREAM_INACT "FcgdDM3MB+k"

/* Vsh DCB builders */
#define AGC_NID_SCE_AGC_VSH_CB_MEM_SEMAPHORE         "vHX9guneRBY"
#define AGC_NID_SCE_AGC_VSH_DCB_CONTEXT_STATE_OP     "qj7QZpgr9Uw"
#define AGC_NID_SCE_AGC_VSH_DCB_CONTEXT_STATE_OP_PRE0100 "HabmgqPwPw0"
#define AGC_NID_SCE_AGC_VSH_DCB_INIT_HW_PRE0090      "Xvcgh9xNNkY"
#define AGC_NID_SCE_AGC_VSH_DCB_RESET_QUEUE          "TRO721eVt4g"
#define AGC_NID_SCE_AGC_VSH_DCB_ATOMIC_GDS_PRE0090   "pH3-dfRpfA0"
#define AGC_NID_SCE_AGC_VSH_DCB_ATOMIC_GDS           "zARR5aCmkoY"
#define AGC_NID_SCE_AGC_VSH_DCB_SET_FLIP             "YUeqkyT7mEQ"
#define AGC_NID_SCE_AGC_DCB_SET_FLIP                 AGC_NID_SCE_AGC_VSH_DCB_SET_FLIP
#define AGC_NID_SCE_AGC_VSH_DCB_WAIT_SAFE_RENDERING  "MWiElSNE8j8"
#define AGC_NID_SCE_AGC_VSH_DCB_MEM_SEMAPHORE        "G0jrLdvEqDw"
#define AGC_NID_SCE_AGC_VSH_DCB_SET_WORKLOADS_ACTIVE "LFSPFmGc9Hg"
#define AGC_NID_SCE_AGC_VSH_DCB_SET_WORKLOAD_COMPLETE "hEK26Wdny6s"
#define AGC_NID_SCE_AGC_VSH_DCB_SET_WORKLOAD_STREAM_INACT "FneFypEDRgY"

/* DCB builders (HLE reference-identified, not in ps5-openagc NID table) */
#define AGC_NID_SCE_AGC_DCB_WRITE_DATA               "i1jyy49AjXU"
#define AGC_NID_SCE_AGC_DCB_WAIT_REG_MEM             "VmW0Tdpy420"
#define AGC_NID_SCE_AGC_DCB_DMA_DATA                 "WmAc2MEj6Io"
#define AGC_NID_SCE_AGC_DCB_GET_LOD_STATS_SIZE       "rUuVjyR+Rd4"
#define AGC_NID_SCE_AGC_DCB_GET_LOD_STATS            "vuSXe69VILM"
#define AGC_NID_SCE_AGC_DCB_SET_BASE_INDIRECT_ARGS   "RmaJwLtc8rY"
#define AGC_NID_SCE_AGC_DCB_DISPATCH_INDIRECT        "CtB+A9-VxO0"
#define AGC_NID_SCE_AGC_DCB_PUSH_MARKER              "+kSrjIVxKFE"
#define AGC_NID_SCE_AGC_DCB_POP_MARKER               "H7uZqCoNuWk"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX_AUTO          "Yw0jKSqop+E"
#define AGC_NID_SCE_AGC_DCB_SET_INDEX_BUFFER         "l4fM9K-Lyks"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX_OFFSET        "B+aG9DUnTKA"

/* DCB builders — newly identified from SPRX disassembly (FW 5.50) */
#define AGC_NID_SCE_AGC_DCB_INDIRECT_BUFFER          "w1KFAHVqpaU"
#define AGC_NID_SCE_AGC_DCB_INDIRECT_BUFFER_CONST     "xSAR0LTcRKM"
#define AGC_NID_SCE_AGC_DCB_SET_CONTEXT_REG           "7toV+elXqNM"
#define AGC_NID_SCE_AGC_DCB_SET_CONFIG_REG            "BVFg3CWU6Eo"
#define AGC_NID_SCE_AGC_DCB_SET_SH_REG                "n2fD4A+pb+g"
#define AGC_NID_SCE_AGC_DCB_SET_SH_REG_DIRECT         "UZbQjYAwwXM"
#define AGC_NID_SCE_AGC_DCB_SET_UCONFIG_REG           "MDLD5Ly94Xk"
#define AGC_NID_SCE_AGC_DCB_RELEASE_MEM               "wr23dPKyWc0"
/* NID "1-gUn1PI4Sw" is sceAgcDcbAtomicMem (SPRX-confirmed), not SET_WORKLOAD.
 * The workload begin/end functions use driver ordinals UM9b9NunSrE/i6bfTi13ApA. */
#define AGC_NID_SCE_AGC_DCB_ATOMIC_MEM               "1-gUn1PI4Sw"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDIRECT             "1rZSWUv1IRc"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX_2              "q88lQ+GP5Yk"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDIRECT_MULTI       "kUlvghKs-mA"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX_INDIRECT       "t1vNu082-jM"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX_INDIRECT_MULTI  "ypVBz4uPKcQ"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDIRECT_COUNT_MULTI  "1q1titRBL6o"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX_INDIRECT_COUNT_MULTI "Rlx+bykm0r0"
#define AGC_NID_SCE_AGC_DCB_SET_PREDICATION           "bbFueFP+J4k"
#define AGC_NID_SCE_AGC_DCB_EVENT_WRITE               "aJf+j5yntiU"
#define AGC_NID_SCE_AGC_DCB_PRIME_UTCL2               "jt3pl7EN17o"
#define AGC_NID_SCE_AGC_DCB_SET_VGT_CONTROL           "hvUfkUIQcOE"
#define AGC_NID_SCE_AGC_DCB_COND_EXEC_EX              "Aozdh0bCcO0"
#define AGC_NID_SCE_AGC_DCB_WAIT_FLIP                 "pdEV7bI6COI"
#define AGC_NID_SCE_AGC_DCB_WAIT_FLIP_EOS             "SbuY2jN+axQ"
#define AGC_NID_SCE_AGC_DCB_INSERT_WAIT_FLIP_DONE     "k0E7vkgqAuE"
#define AGC_NID_SCE_AGC_CB_VALIDATE_PM4_HEADER        "3KDcnM3lrcU"
#define AGC_NID_SCE_AGC_CREATE_SHADER                 "f3dg2CSgRKY"
#define AGC_NID_SCE_AGC_GET_SHADER_REGISTER_DEFAULTS  "D9sr1xGUriE"

/* State queries */
#define AGC_NID_SCE_AGC_GET_DEFAULT_CX_STATE_FLAT    "AAeX-U5-P3M"
#define AGC_NID_SCE_AGC_SUSPEND_POINT                "h9z6+0hEydk"
#define AGC_NID_SCE_AGC_SUSPEND_POINT_CHECK_STATUS   "b+fis+WZ3Ig"
#define AGC_NID_SCE_AGC_GET_GAME_DEFAULT_STATE_1     "Wi82ArQtAwg"
#define AGC_NID_SCE_AGC_GET_GAME_DEFAULT_STATE_2     "uIwxsqDlHRc"


/* --- Additional NIDs from 3.20 stubs and SPRX analysis (5.50 verified) --- */

#define AGC_NID_AGCACBACQUIREMEMGETSIZE                    "ewobAQeMo5k"    /* sceAgcAcbAcquireMemGetSize */
#define AGC_NID_AGCACBCONDEXECGETSIZE                      "ozKzBP4aki4"    /* sceAgcAcbCondExecGetSize */
#define AGC_NID_AGCACBWAITONADDRESSGETSIZE                 "idlaArvdXEs"    /* sceAgcAcbWaitOnAddressGetSize */
#define AGC_NID_AGCASYNCREWINDPATCHSETREWINDSTATE          "eWaWyFegzgQ"    /* sceAgcAsyncRewindPatchSetRewindState */
#define AGC_NID_AGCBRANCHPATCHSETCOMPAREADDRESS            "GXBlM-ekzrI"    /* sceAgcBranchPatchSetCompareAddress */
#define AGC_NID_AGCBRANCHPATCHSETELSETARGET                "QmfvaYpsOcI"    /* sceAgcBranchPatchSetElseTarget */
#define AGC_NID_AGCBRANCHPATCHSETTHENTARGET                "xb8VgcXQhvI"    /* sceAgcBranchPatchSetThenTarget */
#define AGC_NID_AGCCBDISPATCHGETSIZE                       "Abendgtz+3o"    /* sceAgcCbDispatchGetSize */
#define AGC_NID_AGCCBNOPGETSIZE                            "t7PlZ9nt5Lc"    /* sceAgcCbNopGetSize */
#define AGC_NID_AGCCBQUEUEENDOFPIPEACTIONGETSIZE           "hL7C0IRpWZI"    /* sceAgcCbQueueEndOfPipeActionGetSize */
#define AGC_NID_AGCCBSETSHREGISTERRANGEDIRECTGETSIZE       "bxGoVxpdSPQ"    /* sceAgcCbSetShRegisterRangeDirectGetSize */
#define AGC_NID_AGCCBSETSHREGISTERSDIRECTGETSIZE           "yUBESvCCJ4I"    /* sceAgcCbSetShRegistersDirectGetSize */
#define AGC_NID_AGCCBSETUCREGISTERRANGEDIRECTGETSIZE       "JOWmDrl+j20"    /* sceAgcCbSetUcRegisterRangeDirectGetSize */
#define AGC_NID_AGCCBSETUCREGISTERSDIRECTGETSIZE           "TGEZzUWLbrc"    /* sceAgcCbSetUcRegistersDirectGetSize */
#define AGC_NID_AGCCONDEXECPATCHSETCOMMANDADDRESS          "YWTKOju587o"    /* sceAgcCondExecPatchSetCommandAddress */
#define AGC_NID_AGCCONDEXECPATCHSETEND                     "ORWsxIbk4TE"    /* sceAgcCondExecPatchSetEnd */
#define AGC_NID_AGCCREATEINTERPOLANTMAPPING                "HV4j+E0MBHE"    /* sceAgcCreateInterpolantMapping */
#define AGC_NID_AGCDCBACQUIREMEMGETSIZE                    "-vnlTPPXPrw"    /* sceAgcDcbAcquireMemGetSize */
#define AGC_NID_AGCDCBBEGINOCCLUSIONQUERYGETSIZE           "ms1xVoZ-Vwc"    /* sceAgcDcbBeginOcclusionQueryGetSize */
#define AGC_NID_AGCDCBCONDEXECGETSIZE                      "ou16V5hh5sg"    /* sceAgcDcbCondExecGetSize */
#define AGC_NID_AGCDCBCONTEXTSTATEOPGETSIZE                "H6vHS5cidSA"    /* sceAgcDcbContextStateOpGetSize */
#define AGC_NID_AGCDCBDISPATCHINDIRECTGETSIZE              "w8HVkEeXPv8"    /* sceAgcDcbDispatchIndirectGetSize */
#define AGC_NID_AGCDCBDRAWINDEXAUTOGETSIZE                 "WrdP9Zxx3lQ"    /* sceAgcDcbDrawIndexAutoGetSize */
#define AGC_NID_AGCDCBDRAWINDEXGETSIZE                     "6ee9Hd3EWXQ"    /* sceAgcDcbDrawIndexGetSize */
#define AGC_NID_AGCDCBDRAWINDEXINDIRECTMULTIGETSIZE        "r98I08t+LOg"    /* sceAgcDcbDrawIndexIndirectMultiGetSize */
#define AGC_NID_AGCDCBDRAWINDEXMULTIINSTANCEDGETSIZE       "mR9j7+SfM34"    /* sceAgcDcbDrawIndexMultiInstancedGetSize */
#define AGC_NID_AGCDCBDRAWINDEXOFFSETGETSIZE               "qMlfB1ZhMDc"    /* sceAgcDcbDrawIndexOffsetGetSize */
#define AGC_NID_AGCDCBDRAWINDIRECTGETSIZE                  "cxPZ4Wgvdj8"    /* sceAgcDcbDrawIndirectGetSize */
#define AGC_NID_AGCDCBDRAWINDIRECTMULTIGETSIZE             "pYoKs3lPy88"    /* sceAgcDcbDrawIndirectMultiGetSize */
#define AGC_NID_AGCDCBEVENTWRITEGETSIZE                    "C4l9fB17t8w"    /* sceAgcDcbEventWriteGetSize */
#define AGC_NID_AGCDCBJUMPGETSIZE                          "VEGu4dixjUg"    /* sceAgcDcbJumpGetSize */
#define AGC_NID_AGCDCBREWINDGETSIZE                        "QIXCsbipds0"    /* sceAgcDcbRewindGetSize */
#define AGC_NID_AGCDCBSETCXREGISTERDIRECTGETSIZE           "1DeUNpRIDDA"    /* sceAgcDcbSetCxRegisterDirectGetSize */
#define AGC_NID_AGCDCBSETCXREGISTERSINDIRECT               "ZvwO9euwYzc"    /* sceAgcDcbSetCxRegistersIndirect */
#define AGC_NID_AGCDCBSETNUMINSTANCESGETSIZE               "6DFuRKT4C9w"    /* sceAgcDcbSetNumInstancesGetSize */
#define AGC_NID_AGCDCBSETSHREGISTERSINDIRECT               "-HOOCn0JY48"    /* sceAgcDcbSetShRegistersIndirect */
#define AGC_NID_AGCDCBWAITONADDRESSGETSIZE                 "43WJ08sSugE"    /* sceAgcDcbWaitOnAddressGetSize */
#define AGC_NID_AGCDCBWRITEDATAGETSIZE                     "p9tI+yTvx68"    /* sceAgcDcbWriteDataGetSize */
#define AGC_NID_AGCDMADATAPATCHSETDSTADDRESSOROFFSET       "IxYiarKlXxM"    /* sceAgcDmaDataPatchSetDstAddressOrOffset */
#define AGC_NID_AGCFUSESHADERHALVES                        "fd5Bp5tGTgo"    /* sceAgcFuseShaderHalves */
#define AGC_NID_AGCGETDATAPACKETPAYLOADADDRESS             "CQsSq6l6+kA"    /* sceAgcGetDataPacketPayloadAddress */
#define AGC_NID_AGCGETDATAPACKETPAYLOADRANGE               "s+VGAMDQ0AQ"    /* sceAgcGetDataPacketPayloadRange */
#define AGC_NID_AGCGETFUSEDSHADERSIZE                      "dolOmWH+huQ"    /* sceAgcGetFusedShaderSize */
#define AGC_NID_AGCGETPACKETSIZE                           "Lkf86B98qPc"    /* sceAgcGetPacketSize */
#define AGC_NID_AGCINIT                                    "23LRUSvYu1M"    /* sceAgcInit */
#define AGC_NID_AGCJUMPPATCHSETTARGET                      "2BS4EtAaF28"    /* sceAgcJumpPatchSetTarget */
#define AGC_NID_AGCLINKSHADERS                             "MqAdbRMdNz4"    /* sceAgcLinkShaders */
#define AGC_NID_AGCQUEUEENDOFPIPEACTIONPATCHADDRESS        "0fWWK5uG9rQ"    /* sceAgcQueueEndOfPipeActionPatchAddress */
#define AGC_NID_AGCQUEUEENDOFPIPEACTIONPATCHDATA           "MlEw1feXcjg"    /* sceAgcQueueEndOfPipeActionPatchData */
#define AGC_NID_AGCQUEUEENDOFPIPEACTIONPATCHGCRCNTL        "J8YCgfKAMQs"    /* sceAgcQueueEndOfPipeActionPatchGcrCntl */
#define AGC_NID_AGCQUEUEENDOFPIPEACTIONPATCHTYPE           "T9fjQIINoeE"    /* sceAgcQueueEndOfPipeActionPatchType */
#define AGC_NID_AGCREWINDPATCHSETREWINDSTATE               "ziVA3whp3p4"    /* sceAgcRewindPatchSetRewindState */
#define AGC_NID_AGCSETCXREGINDIRECTPATCHSETNUMREGISTERS    "whb1RL7K4Ss"    /* sceAgcSetCxRegIndirectPatchSetNumRegisters */
#define AGC_NID_AGCSETPACKETPREDICATION                    "w6Dj1VJt5qY"    /* sceAgcSetPacketPredication */
#define AGC_NID_AGCSETRANGEPREDICATION                     "n8vgpaQg6dA"    /* sceAgcSetRangePredication */
#define AGC_NID_AGCSETSHREGINDIRECTPATCHSETNUMREGISTERS    "nCUgItdN2ms"    /* sceAgcSetShRegIndirectPatchSetNumRegisters */
#define AGC_NID_AGCSETSUBMITMODE                           "-DtvmQ-tgEA"    /* sceAgcSetSubmitMode */
#define AGC_NID_AGCSETUCREGINDIRECTPATCHSETNUMREGISTERS    "fRG-JOH5+sI"    /* sceAgcSetUcRegIndirectPatchSetNumRegisters */
#define AGC_NID_AGCUNKNOWNGETSIZE_U6DKSLWM2O               "+u6dKSLWM2o"    /* sceAgcUnknownGetSize_+u6dKSLWM2o */
#define AGC_NID_AGCUNKNOWNGETSIZE_0ZOG0JC9NRG              "0ZOG0jc9nRg"    /* sceAgcUnknownGetSize_0ZOG0jc9nRg */
#define AGC_NID_AGCUNKNOWNGETSIZE_1TB0XKLNJCW              "1tB0xkLNjcw"    /* sceAgcUnknownGetSize_1tB0xkLNjcw */
#define AGC_NID_AGCUNKNOWNGETSIZE_2CCJZ9LQI_W              "2ccJz9LQI+w"    /* sceAgcUnknownGetSize_2ccJz9LQI+w */
#define AGC_NID_AGCUNKNOWNGETSIZE_9S4NOWRUI0S              "9S4noWrUI0s"    /* sceAgcUnknownGetSize_9S4noWrUI0s */
#define AGC_NID_AGCUNKNOWNGETSIZE_AFIH8SQKYLQ              "AFIh8SQkYlQ"    /* sceAgcUnknownGetSize_AFIh8SQkYlQ */
#define AGC_NID_AGCUNKNOWNGETSIZE_CBQH3DKMSNO              "CbQh3DKMSno"    /* sceAgcUnknownGetSize_CbQh3DKMSno */
#define AGC_NID_AGCUNKNOWNGETSIZE_F8NLHWVFEMI              "F8NLhWvFemI"    /* sceAgcUnknownGetSize_F8NLhWvFemI */
#define AGC_NID_AGCUNKNOWNGETSIZE_FUVBKYKLF_S              "FuVbkyKlf+s"    /* sceAgcUnknownGetSize_FuVbkyKlf+s */
#define AGC_NID_AGCUNKNOWNGETSIZE_GBCH3ZCIHOU              "GBCh3zCihoU"    /* sceAgcUnknownGetSize_GBCh3zCihoU */
#define AGC_NID_AGCUNKNOWNGETSIZE_KJPEVDUZ6JU              "KjPeVduz6jU"    /* sceAgcUnknownGetSize_KjPeVduz6jU */
#define AGC_NID_AGCUNKNOWNGETSIZE_M0TTM8H7SKA              "M0ttm8h7SKA"    /* sceAgcUnknownGetSize_M0ttm8h7SKA */
#define AGC_NID_AGCUNKNOWNGETSIZE_MMLMJAL7N5W              "MMlmJAL7N5w"    /* sceAgcUnknownGetSize_MMlmJAL7N5w */
#define AGC_NID_AGCUNKNOWNGETSIZE_P1CUGZ99UZC              "P1CugZ99Uzc"    /* sceAgcUnknownGetSize_P1CugZ99Uzc */
#define AGC_NID_AGCUNKNOWNGETSIZE_PXKWV2FVAPS              "PxKWV2fVAps"    /* sceAgcUnknownGetSize_PxKWV2fVAps */
#define AGC_NID_AGCUNKNOWNGETSIZE_QHPDD513V0W              "QhPDD513V0w"    /* sceAgcUnknownGetSize_QhPDD513V0w */
#define AGC_NID_AGCUNKNOWNGETSIZE_UQGTW4XRLCM              "UQGTw4xRlcM"    /* sceAgcUnknownGetSize_UQGTw4xRlcM */
#define AGC_NID_AGCUNKNOWNGETSIZE_XN_IUU7XSM8              "XN+Iuu7XsM8"    /* sceAgcUnknownGetSize_XN+Iuu7XsM8 */
#define AGC_NID_AGCUNKNOWNGETSIZE_Y_5VNEIBTZK              "Y-5vneiBtzk"    /* sceAgcUnknownGetSize_Y-5vneiBtzk */
#define AGC_NID_AGCUNKNOWNGETSIZE_AP1KI9G3_4               "aP1Ki9G3++4"    /* sceAgcUnknownGetSize_aP1Ki9G3++4 */
#define AGC_NID_AGCUNKNOWNGETSIZE_B_OYSN_G2TE              "b-oySn+G2tE"    /* sceAgcUnknownGetSize_b-oySn+G2tE */
#define AGC_NID_AGCUNKNOWNGETSIZE_B5U0JZM8TF8              "b5u0Jzm8TF8"    /* sceAgcUnknownGetSize_b5u0Jzm8TF8 */
#define AGC_NID_AGCUNKNOWNGETSIZE_CA4KPVP0QLQ              "ca4KPvp0qLQ"    /* sceAgcUnknownGetSize_ca4KPvp0qLQ */
#define AGC_NID_AGCUNKNOWNGETSIZE_DA1SM8_QDOU              "da1Sm8-QDoU"    /* sceAgcUnknownGetSize_da1Sm8-QDoU */
#define AGC_NID_AGCUNKNOWNGETSIZE_ECJKAQEEQ5S              "eCjKaqeeQ5s"    /* sceAgcUnknownGetSize_eCjKaqeeQ5s */
#define AGC_NID_AGCUNKNOWNGETSIZE_HCIXS8PMXF4              "hcIxS8pmXF4"    /* sceAgcUnknownGetSize_hcIxS8pmXF4 */
#define AGC_NID_AGCUNKNOWNGETSIZE_J4EMHHNDCPY              "j4emHHndCPY"    /* sceAgcUnknownGetSize_j4emHHndCPY */
#define AGC_NID_AGCUNKNOWNGETSIZE_MSTUVI0ZOTC              "mStuvI0zOtc"    /* sceAgcUnknownGetSize_mStuvI0zOtc */
#define AGC_NID_AGCUNKNOWNGETSIZE_MLJZUGDZRQ4              "mljzuGDZRQ4"    /* sceAgcUnknownGetSize_mljzuGDZRQ4 */
#define AGC_NID_AGCUNKNOWNGETSIZE_NNLUTDDDVZ0              "nNlUtdDDvZ0"    /* sceAgcUnknownGetSize_nNlUtdDDvZ0 */
#define AGC_NID_AGCUNKNOWNGETSIZE_OZ6ZQQ1JWCE              "oz6zQq1JwCE"    /* sceAgcUnknownGetSize_oz6zQq1JwCE */
#define AGC_NID_AGCUNKNOWNGETSIZE_UZW_MQSXKRM              "uZW-mqsxkrM"    /* sceAgcUnknownGetSize_uZW-mqsxkrM */
#define AGC_NID_AGCUNKNOWNGETSIZE_VLRBL8DQIZ8              "vLrBL8DQiz8"    /* sceAgcUnknownGetSize_vLrBL8DQiz8 */
#define AGC_NID_AGCUNKNOWNGETSIZE_YHEJGN_AY_A              "yheJGN-ay+A"    /* sceAgcUnknownGetSize_yheJGN-ay+A */
#define AGC_NID_AGCUNKNOWNGETSIZE_ZG6U_N6OTXS              "zg6u-N6Otxs"    /* sceAgcUnknownGetSize_zg6u-N6Otxs */
#define AGC_NID_AGCUNKNOWNIKFDTRIQCE                       "Ikfdt-rIqCE"    /* sceAgcUnknownIkfdtRIqCE */
#define AGC_NID_AGCUPDATEPRIMSTATE                         "Y3ymLfZ1384"    /* sceAgcUpdatePrimState */

/* === libSceAgcDriver.sprx === */

#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_DCB            "UglJIZjGssM"
#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_ACB            "gSRnr79F8tQ"
#define AGC_NID_SCE_AGC_DRIVER_ADD_EQ_EVENT          "w2rJhmD+dsE"
#define AGC_NID_SCE_AGC_DRIVER_DELETE_EQ_EVENT       "DL2RXaXOy88"
#define AGC_NID_SCE_AGC_DRIVER_GET_PA_DEBUG_VERSION  "Pqxglq1oKec"
#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_MULTI_CB_DIRECT "xmWi73o1BR0"
#define AGC_NID_SCE_AGC_DRIVER_SUSPEND_POINT_SUBMIT  "ZV04pRl7cWU"
#define AGC_NID_SCE_AGC_DRIVER_ACQUIRE_RAZOR_ACQ     "MetMOQVd8HY"
#define AGC_NID_SCE_AGC_DRIVER_RELEASE_RAZOR_ACQ     "TEOAw-eLjNo"
#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_TO_RAZOR_ACQ   "jJyVJyhi5h8"
#define AGC_NID_SCE_AGC_DRIVER_CREATE_USER_SPECIAL_Q "dwoD-LJDQy8"
#define AGC_NID_SCE_AGC_DRIVER_DESTROY_USER_SPECIAL_Q "NRO47jBXLiI"
#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_TO_HDR_SCOPES "lOYHtoUcJD4"
#define AGC_NID_SCE_AGC_DRIVER_NOTIFY_DEFAULT_STATES "nR6xhiFsOoc"
#define AGC_NID_SCE_AGC_DRIVER_SETUP_ASYNC_GRAPHICS  "Vlaj1gwmIFA"
#define AGC_NID_SCE_AGC_DRIVER_SET_TARGET_RING_DIAG  "l0Jxfl0DEdo"
#define AGC_NID_SCE_AGC_DRIVER_IS_SUSPEND_IN_FLIGHT  "I6elAJxk6Jo"
#define AGC_NID_SCE_AGC_DRIVER_SET_TF_RING_DIRECT    "16IjQxB-Heo"
#define AGC_NID_SCE_AGC_DRIVER_SET_HS_OFFCHIP_PARAM  "MM4IZSEYytQ"
#define AGC_NID_SCE_AGC_DRIVER_SDMA_COPY_LINEAR_BLOCK "bQ+En9GY3PM"
#define AGC_NID_SCE_AGC_DRIVER_REGISTER_CAPTURE_INTF "oz3Yd--lAEE"
#define AGC_NID_SCE_AGC_DRIVER_DEREGISTER_CAPTURE_INTF "qR+S4WqJvUM"

/* Driver functions — newly identified from SPRX disassembly (FW 5.50) */
#define AGC_NID_SCE_AGC_DRIVER_BEGIN_WORKLOAD          "UM9b9NunSrE"
#define AGC_NID_SCE_AGC_DRIVER_END_WORKLOAD            "i6bfTi13ApA"
#define AGC_NID_SCE_AGC_DRIVER_INITIALIZE_QUEUE        "b4fpgH5ZXxQ"
#define AGC_NID_SCE_AGC_DRIVER_MAP_COMPUTE_QUEUE       "XNbrdwCsZ9A"
#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_COMMAND_BUFFERS  "Hj4eWnDektQ"
#define AGC_NID_SCE_AGC_DRIVER_WAIT_IDLE               "oFb2hMcoJa4"
#define AGC_NID_SCE_AGC_DRIVER_SET_CONFIG_REG          "-vc-xL+G8u0"
#define AGC_NID_SCE_AGC_DRIVER_SET_BASE                "zmw2uVSEj94"
#define AGC_NID_SCE_AGC_DRIVER_SET_CONTEXT_REG         "+b34-CLWc0s"

/* Game-critical NIDs (from Joe & Mac game binary analysis) */

/* libSceAgc.sprx — packet builders missing from original table */
#define AGC_NID_SCE_AGC_DCB_ACQUIRE_MEM              "57labkp+rSQ"
#define AGC_NID_SCE_AGC_DCB_COPY_DATA                "1rZSWUv1IRc"
#define AGC_NID_SCE_AGC_DCB_JUMP                     "xSAR0LTcRKM"
#define AGC_NID_SCE_AGC_DCB_RESET_QUEUE              "TRO721eVt4g"
#define AGC_NID_SCE_AGC_DCB_SET_INDEX_COUNT          "8N2tmT3jmC8"
#define AGC_NID_SCE_AGC_DCB_SET_INDEX_SIZE           "GIIW2J37e70"
#define AGC_NID_SCE_AGC_DCB_SET_NUM_INSTANCES        "tSBxhAPyytQ"
#define AGC_NID_SCE_AGC_DCB_STALL_CB_PARSER          "u2T2DiA5hRI"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX               "q88lQ+GP5Yk"
#define AGC_NID_SCE_AGC_CB_SET_SH_REG_RANGE_DIRECT   "n2fD4A+pb+g"
#define AGC_NID_SCE_AGC_CB_SET_UC_REGISTERS_DIRECT   "03RZmELWWzw"

/* libSceAgc.sprx — patchers */
#define AGC_NID_SCE_AGC_SET_SH_REG_IND_PATCH_SET_ADDR    "Qrj4c+61z4A"
#define AGC_NID_SCE_AGC_SET_SH_REG_IND_PATCH_ADD_REGS    "z2duB-hHQSM"
#define AGC_NID_SCE_AGC_SET_CX_REG_IND_PATCH_SET_ADDR    "vcmNN+AAXnY"
#define AGC_NID_SCE_AGC_SET_CX_REG_IND_PATCH_ADD_REGS    "d-6uF9sZDIU"
#define AGC_NID_SCE_AGC_SET_UC_REG_IND_PATCH_SET_ADDR    "6lNcCp+fxi4"
#define AGC_NID_SCE_AGC_SET_UC_REG_IND_PATCH_ADD_REGS    "vRoArM9zaIk"

/* libSceAgc.sprx — additional patcher */
#define AGC_NID_SCE_AGC_DMA_DATA_PATCH_SET_SRC           "cdDRpqcFGbU"

/* libSceAgc.sprx — utility */
#define AGC_NID_SCE_AGC_SET_NOP                      "K2mciNVxUCE"
#define AGC_NID_SCE_AGC_DEBUG_RAISE_EXCEPTION        "T6xuVw0KUJo"
#define AGC_NID_SCE_AGC_CREATE_PRIM_STATE            "D9sr1xGUriE"

/* libSceAgc.sprx — init/suspend wrappers */
#define AGC_NID_SCE_AGC_SUSPEND_POINT_WRAPPER        "h9z6+0hEydk"

/* libSceAgcDriver.sprx — non-Direct variants */
#define AGC_NID_SCE_AGC_DRIVER_AGR_SUBMIT_DCB        "AhGvpITrf4M"
#define AGC_NID_SCE_AGC_DRIVER_SET_TF_RING           "XlNp7jzGiPo"
#define AGC_NID_SCE_AGC_DRIVER_GET_EQ_CONTEXT_ID     "Zw7uUVPulbw"
#define AGC_NID_SCE_AGC_DRIVER_ADD_EQ_EVENT_DRV      "w2rJhmD+dsE"
#define AGC_NID_SCE_AGC_DRIVER_REGISTER_OWNER        "X-Nm5KLREeg"
#define AGC_NID_SCE_AGC_DRIVER_REGISTER_RESOURCE     "W5z4eZrjEas"

/* === Newly identified from SPRX disassembly (FW 5.50) — batch 2 === */

/* libSceAgc.sprx — DCB packet builders */
#define AGC_NID_SCE_AGC_DCB_CLEAR_STATE              "PxEFhy0d5v8"
#define AGC_NID_SCE_AGC_DCB_REWIND                   "zfcxg-ewMK8"
#define AGC_NID_SCE_AGC_DCB_COND_EXEC                "BIPexNBSGog"
#define AGC_NID_SCE_AGC_DCB_ATOMIC_GDS               "pH3-dfRpfA0"
#define AGC_NID_SCE_AGC_DCB_MEM_SEMAPHORE            "G0jrLdvEqDw"
#define AGC_NID_SCE_AGC_DCB_PRIME_UTCL2              "jt3pl7EN17o"
#define AGC_NID_SCE_AGC_DCB_SET_INDEX_INDIRECT_ARGS  "0o3VDdtA6nM"
#define AGC_NID_SCE_AGC_DCB_DRAW_INDEX_MULTI_INST    "Rlx+bykm0r0"
#define AGC_NID_SCE_AGC_DCB_SET_MARKER               "QhCbS4X9Rl8"
#define AGC_NID_SCE_AGC_DCB_CONTEXT_STATE_OP         "HabmgqPwPw0"
#define AGC_NID_SCE_AGC_DCB_SET_WORKLOADS_ACTIVE     "LFSPFmGc9Hg"
#define AGC_NID_SCE_AGC_DCB_SET_WORKLOAD_COMPLETE    "hEK26Wdny6s"
#define AGC_NID_SCE_AGC_DCB_SET_WORKLOAD_STREAM_INACT "FneFypEDRgY"

/* libSceAgc.sprx — DCB register direct setters */
#define AGC_NID_SCE_AGC_DCB_SET_CF_REG_DIRECT        "73ZZdojLIgs"
#define AGC_NID_SCE_AGC_DCB_SET_CX_REG_DIRECT        "LHFXRrlTPD8"
/* Note: "pFLArOT53+w" is the single-register DCB variant.
 * "UZbQjYAwwXM" (defined above) is the CB multi-register variant. */
#define AGC_NID_SCE_AGC_DCB_SET_SH_REG_DIRECT_SINGLE "pFLArOT53+w"
#define AGC_NID_SCE_AGC_DCB_SET_UC_REG_DIRECT        "w4-d0n60hdo"
#define AGC_NID_SCE_AGC_DCB_SET_CF_REG_RANGE_DIRECT  "BVFg3CWU6Eo"
#define AGC_NID_SCE_AGC_CB_SET_UC_REG_RANGE_DIRECT   "MDLD5Ly94Xk"

/* libSceAgc.sprx — CB builders */
#define AGC_NID_SCE_AGC_CB_BRANCH                    "w1KFAHVqpaU"
#define AGC_NID_SCE_AGC_CB_COND_WRITE                "7toV+elXqNM"
#define AGC_NID_SCE_AGC_CB_MEM_SEMAPHORE             "vHX9guneRBY"

/* libSceAgc.sprx — WaitRegMem patchers */
#define AGC_NID_SCE_AGC_WAIT_REG_MEM_PATCH_CMP_FUNC  "n485EBnIWmk"
#define AGC_NID_SCE_AGC_WAIT_REG_MEM_PATCH_MASK      "hXAnLgDHCoI"
#define AGC_NID_SCE_AGC_WAIT_REG_MEM_PATCH_REF       "7nOoijNPvEU"


/* --- Additional driver NIDs from 3.20 stubs and SPRX analysis (5.50 verified) --- */

#define AGC_NID_AGCDRIVERCREATEQUEUE                       "zP4ZNlXLBVg"    /* sceAgcDriverCreateQueue */
#define AGC_NID_AGCDRIVERCWSRRESUMEACQ                     "1DXIHxWHZAQ"    /* sceAgcDriverCwsrResumeAcq */
#define AGC_NID_AGCDRIVERCWSRSUSPENDACQ                    "SOAMmdlyaIc"    /* sceAgcDriverCwsrSuspendAcq */
#define AGC_NID_AGCDRIVERDEBUGHARDWARESTATUS               "1BUTwixUG5Y"    /* sceAgcDriverDebugHardwareStatus */
#define AGC_NID_AGCDRIVERGETDEFAULTOWNER                   "F0ZXt5q0ZTA"    /* sceAgcDriverGetDefaultOwner */
#define AGC_NID_AGCDRIVERGETEQEVENTTYPE                    "5CdQTZIQPxM"    /* sceAgcDriverGetEqEventType */
#define AGC_NID_AGCDRIVERGETHSOFFCHIPPARAM                 "r28hEh6cNH0"    /* sceAgcDriverGetHsOffchipParam */
#define AGC_NID_AGCDRIVERGETREGSHADOWINFO                  "CP-kVAMmWVw"    /* sceAgcDriverGetRegShadowInfo */
#define AGC_NID_AGCDRIVERGETREGSHADOWINFOAGR               "ME1eUot7+Qw"    /* sceAgcDriverGetRegShadowInfoAgr */
#define AGC_NID_AGCDRIVERGETRESERVEDDMEMFORAGC             "Um-jkyDy9rI"    /* sceAgcDriverGetReservedDmemForAgc */
#define AGC_NID_AGCDRIVERGETRESOURCEREGISTRATIONMAXNAMELENGTH "uJziRsODk1c"    /* sceAgcDriverGetResourceRegistrationMaxNameLength */
#define AGC_NID_AGCDRIVERGETSETFLIPPACKETSIZEINDWORDS      "2PrsbRYyZi4"    /* sceAgcDriverGetSetFlipPacketSizeInDwords */
#define AGC_NID_AGCDRIVERGETSETWORKLOADCOMPLETEPACKETSIZE  "WNyjOWq8-Vk"    /* sceAgcDriverGetSetWorkloadCompletePacketSize */
#define AGC_NID_AGCDRIVERGETSETWORKLOADSACTIVEPACKETSIZE   "gyVTZWyySpM"    /* sceAgcDriverGetSetWorkloadsActivePacketSize */
#define AGC_NID_AGCDRIVERGETTFRING                         "e-YMQ+2tj9M"    /* sceAgcDriverGetTFRing */
#define AGC_NID_AGCDRIVERGETWAITRENDERINGPACKETSIZEINDWORDS "0MtUJ3BpGhE"    /* sceAgcDriverGetWaitRenderingPacketSizeInDwords */
#define AGC_NID_AGCDRIVERGETWORKLOADSTREAMINFO             "t8PLXbBCiRA"    /* sceAgcDriverGetWorkloadStreamInfo */
#define AGC_NID_AGCDRIVERIDHSSUBMIT                        "C2yjkNdzbW4"    /* sceAgcDriverIDHSSubmit */
#define AGC_NID_AGCDRIVERINITRESOURCEREGISTRATION          "F0Y42t-3e18"    /* sceAgcDriverInitResourceRegistration */
#define AGC_NID_AGCDRIVERISCAPTUREINPROGRESS               "Ddwk4gLT5j0"    /* sceAgcDriverIsCaptureInProgress */
#define AGC_NID_AGCDRIVERMODULEREGISTRATION                "04JRU1Uf8Ms"    /* sceAgcDriverModuleRegistration */
#define AGC_NID_AGCDRIVERPASSINFODOWNWARD                  "aCfbPzyjU90"    /* sceAgcDriverPassInfoDownward */
#define AGC_NID_AGCDRIVERPATCHCLEARSTATE                   "lYz7vbL4W4A"    /* sceAgcDriverPatchClearState */
#define AGC_NID_AGCDRIVERQUERYRESOURCEREGISTRATIONUSERMEMORYREQUIREMENTS "AOLcoIkQDgM"    /* sceAgcDriverQueryResourceRegistrationUserMemoryRequirements */
#define AGC_NID_AGCDRIVERREGISTERWORKLOADSTREAM            "3AyTaWcF-H8"    /* sceAgcDriverRegisterWorkloadStream */
#define AGC_NID_AGCDRIVERSETFLIP                           "cwbxjPSJ7WQ"    /* sceAgcDriverSetFlip */
#define AGC_NID_AGCDRIVERSETHSOFFCHIPPARAMDIRECT           "DPcAnsOlTQs"    /* sceAgcDriverSetHsOffchipParamDirect */
#define AGC_NID_AGCDRIVERSUBMITMULTIACBS                   "HF3YllT3mXU"    /* sceAgcDriverSubmitMultiAcbs */
#define AGC_NID_AGCDRIVERSUBMITMULTICOMMANDBUFFERS         "Fj7r9EHzF38"    /* sceAgcDriverSubmitMultiCommandBuffers */
#define AGC_NID_AGCDRIVERSUBMITMULTIDCBS                   "6UzEidRZwkg"    /* sceAgcDriverSubmitMultiDcbs */
#define AGC_NID_AGCDRIVERSUSPENDPOINTSUBMIT                "QcmHLO2n7mk"    /* sceAgcDriverSuspendPointSubmit */
#define AGC_NID_AGCDRIVERSYSENABLESUBMITDONE45EXCEPTION    "x3K61sY5m8Q"    /* sceAgcDriverSysEnableSubmitDone45Exception */
#define AGC_NID_AGCDRIVERSYSGETCLIENTNUMBER                "kuE1uTiWfuk"    /* sceAgcDriverSysGetClientNumber */
#define AGC_NID_AGCDRIVERSYSISGAMECLOSED                   "ftf-xlfBQpo"    /* sceAgcDriverSysIsGameClosed */
#define AGC_NID_AGCDRIVERTMPINITIDHS                       "UM8rn9hRWrY"    /* sceAgcDriverTmpInitIdhs */
#define AGC_NID_AGCDRIVERUNKNOWNU9UEYEHSKF4                "U9ueyEhSkF4"    /* sceAgcDriverUnknownU9ueyEhSkF4 */
#define AGC_NID_AGCDRIVERUNREGISTERRESOURCE                "pWLG7WOpVcw"    /* sceAgcDriverUnregisterResource */
#define AGC_NID_AGCDRIVERUNREGISTERWORKLOADSTREAM          "n5ElQVYsU1A"    /* sceAgcDriverUnregisterWorkloadStream */
#define AGC_NID_AGCDRIVERUSERDATAGETPACKETSIZE             "VhLnEiTuuWo"    /* sceAgcDriverUserDataGetPacketSize */
#define AGC_NID_AGCDRIVERUSERDATAWRITEPOPMARKER            "LEnn-4ARRJM"    /* sceAgcDriverUserDataWritePopMarker */
#define AGC_NID_AGCDRIVERWAITUNTILSAFEFORRENDERING         "u8BkdHb1+Po"    /* sceAgcDriverWaitUntilSafeForRendering */

#endif /* _AGC_NIDS_H_ */
