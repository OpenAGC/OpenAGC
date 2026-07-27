#ifndef OPENAGC_SAMPLE_TRIANGLE_H
#define OPENAGC_SAMPLE_TRIANGLE_H

#include <stdint.h>

#include "agc_graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal public-API render pass used by the homebrew triangle example.
 * Shader records, code addresses, render-target memory, and the completion
 * word must all refer to GPU-visible allocations owned by the application. */
typedef struct OpenAgcTrianglePass {
    AgcGfx1013FrameState frame;
    AgcGfx1013BaselineDrawState draw;
    uint64_t completion_address;
    uint32_t completion_value;
} OpenAgcTrianglePass;

int32_t PS5_SYSV_ABI openagcTriangleRecord(
    SceAgcCb *cb, const OpenAgcTrianglePass *pass);

#ifdef __cplusplus
}
#endif

#endif /* OPENAGC_SAMPLE_TRIANGLE_H */
