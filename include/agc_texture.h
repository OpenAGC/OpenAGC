#ifndef _AGC_TEXTURE_H_
#define _AGC_TEXTURE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AgcSwizzleMode {
    kAgcSwModeLinear       = 0,
    kAgcSwMode256B_S       = 1,
    kAgcSwMode4KB_S        = 2,
    kAgcSwMode64KB_S       = 3,
    kAgcSwMode256KB_S      = 4,
} AgcSwizzleMode;

typedef enum AgcDataFormat {
    kAgcDataFormatInvalid      = 0,
    kAgcDataFormat8            = 1,
    kAgcDataFormat8_8          = 2,
    kAgcDataFormat8_8_8_8      = 3,
    kAgcDataFormat16           = 4,
    kAgcDataFormat16_16        = 5,
    kAgcDataFormat16_16_16_16  = 6,
    kAgcDataFormat32           = 7,
    kAgcDataFormat32_32        = 8,
    kAgcDataFormat32_32_32_32  = 9,
    kAgcDataFormatBc1          = 10,
    kAgcDataFormatBc3          = 11,
    kAgcDataFormatBc7          = 12,
} AgcDataFormat;

typedef enum AgcNumberType {
    kAgcNumberUnorm = 0,
    kAgcNumberSnorm = 1,
    kAgcNumberUint  = 2,
    kAgcNumberSint  = 3,
    kAgcNumberFloat = 4,
} AgcNumberType;

typedef struct AgcTextureDescriptor {
    uint64_t base_address;
    uint32_t width_minus1;
    uint32_t height_minus1;
    uint32_t depth_minus1;
    uint32_t pitch_minus1;
    uint32_t format;
    uint32_t dst_sel_x : 3;
    uint32_t dst_sel_y : 3;
    uint32_t dst_sel_z : 3;
    uint32_t dst_sel_w : 3;
    uint32_t img_type  : 4;
    uint32_t sw_mode   : 4;
    uint32_t reserved  : 12;
} AgcTextureDescriptor;

typedef struct AgcBufferDescriptor {
    uint64_t base_address;
    uint32_t stride      : 14;
    uint32_t dst_sel_x   : 3;
    uint32_t dst_sel_y   : 3;
    uint32_t dst_sel_z   : 3;
    uint32_t dst_sel_w   : 3;
    uint32_t reserved    : 6;
    uint32_t num_records : 16;
    uint32_t format      : 16;
} AgcBufferDescriptor;

typedef struct AgcSamplerDescriptor {
    uint32_t words[4];
} AgcSamplerDescriptor;

void agcTextureDescriptorInit(AgcTextureDescriptor *desc);
void agcTextureDescriptorSetDimensions(
    AgcTextureDescriptor *desc, uint32_t width, uint32_t height, uint32_t depth);
void agcTextureDescriptorSetFormat(
    AgcTextureDescriptor *desc, AgcDataFormat fmt, AgcNumberType ntype);
void agcTextureDescriptorSetSwizzleMode(AgcTextureDescriptor *desc, AgcSwizzleMode mode);
void agcTextureDescriptorSetBaseAddress(AgcTextureDescriptor *desc, uint64_t gpu_addr);

void agcBufferDescriptorInit(AgcBufferDescriptor *desc);
void agcBufferDescriptorSetAddress(AgcBufferDescriptor *desc, uint64_t gpu_addr);
void agcBufferDescriptorSetStride(AgcBufferDescriptor *desc, uint32_t stride_bytes);
void agcBufferDescriptorSetNumRecords(AgcBufferDescriptor *desc, uint32_t num_records);

#ifdef __cplusplus
}
#endif

#endif /* _AGC_TEXTURE_H_ */
