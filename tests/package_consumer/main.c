#include "agc_graphics.h"
#include "openagc/runtime.h"

int main(void)
{
    AgcGfx1013TessellationRingTable table = {{0}};
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;

    return sizeof(table) == 128u &&
        device_desc.version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
        runtime_info.struct_size == sizeof(runtime_info) ? 0 : 1;
}
