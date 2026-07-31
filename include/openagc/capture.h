/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Versioned diagnostic capture stream for the native runtime.
 */

#ifndef OPENAGC_CAPTURE_H
#define OPENAGC_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#include "openagc/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_CAPTURE_FORMAT_VERSION 1u
#define AGC_CAPTURE_FILE_HEADER_SIZE 32u
#define AGC_CAPTURE_RECORD_HEADER_SIZE 16u
#define AGC_CAPTURE_ENDIAN_TAG UINT32_C(0x01020304)
#define AGC_CAPTURE_MAGIC_BYTE_0 'O'
#define AGC_CAPTURE_MAGIC_BYTE_1 'A'
#define AGC_CAPTURE_MAGIC_BYTE_2 'G'
#define AGC_CAPTURE_MAGIC_BYTE_3 'C'
#define AGC_CAPTURE_MAGIC_BYTE_4 'C'
#define AGC_CAPTURE_MAGIC_BYTE_5 'A'
#define AGC_CAPTURE_MAGIC_BYTE_6 'P'
#define AGC_CAPTURE_MAGIC_BYTE_7 '\0'

typedef struct AgcCaptureImpl *AgcCapture;

typedef enum AgcCaptureFlagBits {
    /* Shader bytes remain omitted unless the application explicitly opts in.
     * Hashes and record metadata are captured regardless. */
    AGC_CAPTURE_INCLUDE_SHADER_BYTES_BIT = 1u << 0
} AgcCaptureFlagBits;
typedef uint32_t AgcCaptureFlags;

typedef enum AgcCaptureRecordType {
    AGC_CAPTURE_RECORD_RUNTIME_INFO = 1,
    AGC_CAPTURE_RECORD_OBJECT_CREATE = 2,
    AGC_CAPTURE_RECORD_OBJECT_NAME = 3,
    AGC_CAPTURE_RECORD_OBJECT_DESTROY = 4,
    AGC_CAPTURE_RECORD_COMMAND_BEGIN = 5,
    AGC_CAPTURE_RECORD_COMMAND_END = 6,
    AGC_CAPTURE_RECORD_SUBMISSION = 7,
    AGC_CAPTURE_RECORD_FENCE_RESULT = 8,
    AGC_CAPTURE_RECORD_VALIDATION_MESSAGE = 9,
    AGC_CAPTURE_RECORD_READBACK_HASH = 10,
    AGC_CAPTURE_RECORD_END = 11,
    /* Final post-injection dwords exactly as submitted to the backend. */
    AGC_CAPTURE_RECORD_COMMAND_STREAM = 12
} AgcCaptureRecordType;

typedef enum AgcCaptureObjectType {
    AGC_CAPTURE_OBJECT_DEVICE = 0,
    AGC_CAPTURE_OBJECT_QUEUE = 1,
    AGC_CAPTURE_OBJECT_BUFFER = 2,
    AGC_CAPTURE_OBJECT_IMAGE = 3,
    AGC_CAPTURE_OBJECT_IMAGE_VIEW = 4,
    AGC_CAPTURE_OBJECT_SAMPLER = 5,
    AGC_CAPTURE_OBJECT_SHADER = 6,
    AGC_CAPTURE_OBJECT_GRAPHICS_PIPELINE = 7,
    AGC_CAPTURE_OBJECT_COMPUTE_PIPELINE = 8,
    AGC_CAPTURE_OBJECT_COMMAND_BUFFER = 9,
    AGC_CAPTURE_OBJECT_FENCE = 10,
    AGC_CAPTURE_OBJECT_GPU_LABEL = 11,
    AGC_CAPTURE_OBJECT_PRESENT_CHAIN = 12
} AgcCaptureObjectType;

typedef void (PS5_SYSV_ABI *AgcCaptureWriteFunction)(
    void *user_data, const void *data, size_t size);

typedef struct AgcCaptureDesc {
    uint32_t struct_size;
    uint32_t version;
    AgcCaptureFlags flags;
    uint32_t reserved0;
    AgcCaptureWriteFunction write;
    void *user_data;
    uint64_t reserved[4];
} AgcCaptureDesc;

#define AGC_CAPTURE_DESC_INIT \
    { sizeof(AgcCaptureDesc), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, 0u, \
      NULL, NULL, {0u, 0u, 0u, 0u} }

typedef struct AgcCaptureInfo {
    uint32_t struct_size;
    uint32_t version;
    uint32_t active;
    int32_t status;
    uint64_t record_count;
    uint64_t byte_count;
    uint64_t next_object_id;
    uint64_t reserved[4];
} AgcCaptureInfo;

#define AGC_CAPTURE_INFO_INIT \
    { sizeof(AgcCaptureInfo), AGC_RUNTIME_STRUCTURE_VERSION_1, 0u, AGC_OK, \
      0u, 0u, 0u, {0u, 0u, 0u, 0u} }

_Static_assert(sizeof(AgcCaptureDesc) == 64u,
    "AgcCaptureDesc v1 size mismatch");
_Static_assert(sizeof(AgcCaptureInfo) == 72u,
    "AgcCaptureInfo v1 size mismatch");

int32_t PS5_SYSV_ABI agcCreateCapture(AgcDevice device,
    const AgcCaptureDesc *desc, AgcCapture *capture_out);
int32_t PS5_SYSV_ABI agcDestroyCapture(AgcCapture capture);
int32_t PS5_SYSV_ABI agcBeginCapture(AgcCapture capture);
int32_t PS5_SYSV_ABI agcEndCapture(AgcCapture capture);
int32_t PS5_SYSV_ABI agcGetCaptureInfo(
    AgcCapture capture, AgcCaptureInfo *info);

#ifdef __cplusplus
}
#endif

#endif /* OPENAGC_CAPTURE_H */
