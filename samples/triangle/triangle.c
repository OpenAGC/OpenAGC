#include "triangle.h"

#include <stddef.h>

#include "agc_error.h"

int32_t PS5_SYSV_ABI openagcTriangleRecord(
    SceAgcCb *cb, const OpenAgcTrianglePass *pass)
{
    AgcGfx1013BaselineDrawState draw;
    AgcGfx1013GraphicsDefaultStats stats;
    AgcGfx1013ResourceTransition present;
    SceAgcCb checkpoint;
    int32_t error;

    if (cb == NULL || pass == NULL)
        return AGC_ERROR_INVALID_ARGUMENT;

    checkpoint = *cb;
    draw = pass->draw;
    draw.frame = &pass->frame;

    error = agcGfx1013BuildFramePrologue(cb, &pass->frame, &stats);
    if (error == AGC_OK)
        error = agcGfx1013DrawBaselineIndexAuto(cb, &draw);
    if (error == AGC_OK) {
        present.before = AGC_GFX1013_RESOURCE_USAGE_RENDER_TARGET;
        present.after = AGC_GFX1013_RESOURCE_USAGE_PRESENT;
        present.completion_address = pass->completion_address;
        present.completion_value = pass->completion_value;
        error = agcGfx1013TransitionResource(cb, &present);
    }

    if (error != AGC_OK)
        *cb = checkpoint;
    return error;
}
