#ifndef _AGC_NIDS_H_
#define _AGC_NIDS_H_

/*
 * Known Gen5 AGC NIDs from local SharpEmu reference analysis.
 * Keep this as a small RE index; do not treat it as a complete firmware map.
 */

#define AGC_NID_SCE_AGC_INIT                         "23LRUSvYu1M"
#define AGC_NID_SCE_AGC_GET_REGISTER_DEFAULTS2       "2JtWUUiYBXs"
#define AGC_NID_SCE_AGC_CREATE_SHADER                "f3dg2CSgRKY"
#define AGC_NID_SCE_AGC_GET_DATA_PACKET_PAYLOAD      "V++UgBtQhn0"
#define AGC_NID_SCE_AGC_CB_NOP                       "LtTouSCZjHM"
#define AGC_NID_SCE_AGC_CB_DISPATCH                  "k3GhuSNmBLU"
#define AGC_NID_SCE_AGC_CB_SET_SH_REGISTERS_DIRECT   "UZbQjYAwwXM"
#define AGC_NID_SCE_AGC_ACB_RESET_QUEUE              "JrtiDtKeS38"
#define AGC_NID_SCE_AGC_ACB_EVENT_WRITE              "cFazmnXpJOE"
#define AGC_NID_SCE_AGC_ACB_ACQUIRE_MEM              "KT-hTp-Ch14"
#define AGC_NID_SCE_AGC_ACB_WAIT_REG_MEM             "htn36gPnBk4"
#define AGC_NID_SCE_AGC_ACB_WRITE_DATA               "eZ4+17OQz4Q"
#define AGC_NID_SCE_AGC_ACB_DISPATCH_INDIRECT        "j3EtxFkSIhQ"
#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_ACB            "gSRnr79F8tQ"
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
#define AGC_NID_SCE_AGC_DCB_WAIT_SAFE_RENDERING      "MWiElSNE8j8"
#define AGC_NID_SCE_AGC_DCB_SET_FLIP                 "YUeqkyT7mEQ"
#define AGC_NID_SCE_AGC_DRIVER_SUBMIT_DCB            "UglJIZjGssM"
#define AGC_NID_SCE_AGC_DRIVER_ADD_EQ_EVENT          "w2rJhmD+dsE"
#define AGC_NID_SCE_AGC_DRIVER_DELETE_EQ_EVENT       "DL2RXaXOy88"
#define AGC_NID_SCE_AGC_SUSPEND_POINT                "h9z6+0hEydk"

#endif /* _AGC_NIDS_H_ */
