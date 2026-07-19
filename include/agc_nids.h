#ifndef _AGC_NIDS_H_
#define _AGC_NIDS_H_

/*
 * Known Gen5 AGC NIDs — cross-referenced from SharpEmu and ps5-openagc
 * firmware 5.50 SPRX analysis.
 *
 * Sources:
 *   - SharpEmu AgcExports.cs (original identification)
 *   - ps5-openagc include/ps5/internal/agc_nid.h (SPRX NID matching —
 *     NID-to-name mapping only; ps5-openagc's ioctl layouts are NOT trusted,
 *     see analysis/ps5_openagc_audit.md)
 *
 * Total identified: 114 NIDs across libSceAgc, libSceAgcDriver, libSceAgcVsh.
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

/* DCB builders (SharpEmu-identified, not in ps5-openagc NID table) */
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

#endif /* _AGC_NIDS_H_ */
