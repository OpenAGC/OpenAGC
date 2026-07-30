#include "agc_graphics.h"
#include "openagc/runtime.h"

int main(void)
{
    AgcGfx1013TessellationRingTable table = {{0}};
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcRuntimeInfo runtime_info = AGC_RUNTIME_INFO_INIT;
    AgcMemoryStats memory_stats = AGC_MEMORY_STATS_INIT;
    AgcImageLayout image_layout = AGC_IMAGE_LAYOUT_INIT;
    AgcShaderReflection reflection = AGC_SHADER_REFLECTION_INIT;
    AgcGraphicsPipelineDesc pipeline = AGC_GRAPHICS_PIPELINE_DESC_INIT;

    return sizeof(table) == 128u &&
        device_desc.version == AGC_RUNTIME_STRUCTURE_VERSION_1 &&
        runtime_info.struct_size == sizeof(runtime_info) &&
        memory_stats.struct_size == sizeof(memory_stats) &&
        image_layout.struct_size == sizeof(image_layout) &&
        reflection.struct_size == sizeof(reflection) &&
        pipeline.version == AGC_RUNTIME_STRUCTURE_VERSION_2 ? 0 : 1;
}
