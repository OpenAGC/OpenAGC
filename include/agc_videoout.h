/*
 * openagc — SPDX-License-Identifier: Apache-2.0
 *
 * Application-neutral PS5 scanout lifecycle.  Platform and firmware details
 * deliberately remain behind this interface.
 */

#ifndef _AGC_VIDEOOUT_H_
#define _AGC_VIDEOOUT_H_

#include <stdint.h>
#include "agc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGC_VIDEO_OUT_MAX_BUFFERS 16u
#define AGC_VIDEO_OUT_INFINITE_TIMEOUT UINT64_MAX

typedef struct AgcVideoOut AgcVideoOut;

typedef enum AgcVideoOutFormat {
    AGC_VIDEO_OUT_FORMAT_BGRA8_SRGB = 1,
} AgcVideoOutFormat;

typedef struct AgcVideoOutMode {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_millihz;
} AgcVideoOutMode;

typedef struct AgcVideoOutCreateInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    uint32_t buffer_count;
    void *const *buffers;
    AgcVideoOutFormat format;
} AgcVideoOutCreateInfo;

/* The initially qualified FW 5.50 scanout mode is fixed at 1920x1080. */
int32_t PS5_SYSV_ABI agcVideoOutGetDefaultMode(AgcVideoOutMode *mode);

/* Registers caller-owned direct-memory buffers with the main display. */
int32_t PS5_SYSV_ABI agcVideoOutOpen(
    const AgcVideoOutCreateInfo *create_info, AgcVideoOut **video_out);

/* Submits a FIFO/VSYNC flip and waits no longer than timeout_us. */
int32_t PS5_SYSV_ABI agcVideoOutPresent(
    AgcVideoOut *video_out, uint32_t buffer_index, uint64_t frame_id,
    uint64_t timeout_us);

/* Unregisters display state and reports any teardown failure.  Caller-owned
 * buffer memory is never released. */
int32_t PS5_SYSV_ABI agcVideoOutCloseChecked(AgcVideoOut *video_out);

/* Compatibility wrapper.  New ownership-sensitive code must use the checked
 * form before releasing registered buffer memory. */
void PS5_SYSV_ABI agcVideoOutClose(AgcVideoOut *video_out);

#ifdef OPENAGC_GENERIC
/* Host-test fault injection; absent from the Prospero backend. */
void agcVideoOutDebugSetNextCloseResult(int32_t result);
void agcVideoOutDebugSetNextOpenResult(int32_t result);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _AGC_VIDEOOUT_H_ */
