/*
 * Optional host integration: consume actual openagc-psbc output through the
 * public runtime contract.  It deliberately knows no shader-register layout.
 */

#include "openagc_psbc.h"

#include <stdio.h>
#include <stdlib.h>

#include "openagc/runtime.h"

static uint32_t *read_spirv(const char *path, size_t *size_out)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint32_t *data;

    if (!file || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || (length & 3) != 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        if (file)
            fclose(file);
        return NULL;
    }
    data = malloc((size_t)length);
    if (!data || fread(data, 1u, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size_out = (size_t)length;
    return data;
}

static int create_shader(AgcDevice device, const OpenAgcPsbcOutput *output,
    AgcShader *shader_out)
{
    AgcShaderDesc desc = AGC_SHADER_DESC_INIT;

    desc.stage = output->metadata.stage;
    desc.code = output->shader.data;
    desc.code_size = output->shader.size;
    desc.front_code = output->front_shader.data;
    desc.front_code_size = output->front_shader.size;
    desc.reflection = &output->metadata;
    return agcCreateShader(device, &desc, shader_out) == AGC_OK;
}

static int compile_stage(OpenAgcPsbcStage stage, const uint32_t *spirv,
    size_t spirv_size, const OpenAgcPsbcPipelineContext *pipeline,
    OpenAgcPsbcOutput *output)
{
    const OpenAgcPsbcCompileInfo info = {
        .api_version = OPENAGC_PSBC_API_VERSION,
        .stage = stage,
        .spirv = spirv,
        .spirv_size = spirv_size,
        .entry_point = "main",
        .pipeline = pipeline,
        .optimize = true,
    };
    OpenAgcPsbcResult result = openagcPsbcCompile(&info, output);

    if (result != OPENAGC_PSBC_SUCCESS)
        fprintf(stderr, "stage %u compiler error: %s\n", (unsigned)stage,
            openagcPsbcResultString(result));
    return result == OPENAGC_PSBC_SUCCESS;
}

int main(int argc, char **argv)
{
    const OpenAgcPsbcVertexAttribute vertex_attributes[] = {
        {0u, 0u, 0u, 20u, AGC_SHADER_VERTEX_FORMAT_R32G32_SFLOAT,
         AGC_SHADER_VERTEX_INPUT_RATE_VERTEX, 0u, 0x3u},
        {1u, 0u, 8u, 20u, AGC_SHADER_VERTEX_FORMAT_R32G32B32_SFLOAT,
         AGC_SHADER_VERTEX_INPUT_RATE_VERTEX, 0u, 0x7u},
    };
    const OpenAgcPsbcPipelineContext graphics_context = {
        .vertex_attributes = vertex_attributes,
        .vertex_attribute_count = 2u,
        .color_attachment_count = 1u,
        .enable_ngg = true,
        .wave32 = true,
    };
    const OpenAgcPsbcPipelineContext fragment_context = {
        .color_attachment_count = 1u,
        .wave32 = true,
    };
    const OpenAgcPsbcDescriptorBinding compute_binding = {
        .set = 1u,
        .binding = 3u,
        .type = OPENAGC_PSBC_DESCRIPTOR_STORAGE_BUFFER,
        .array_size = 1u,
    };
    const OpenAgcPsbcPipelineContext compute_context = {
        .descriptor_bindings = &compute_binding,
        .descriptor_binding_count = 1u,
        .push_constant_size = 4u,
        .wave32 = true,
    };
    AgcColorBlendAttachmentState attachment =
        AGC_COLOR_BLEND_ATTACHMENT_STATE_INIT;
    AgcDeviceDesc device_desc = AGC_DEVICE_DESC_INIT;
    AgcGraphicsPipelineDesc graphics_desc = AGC_GRAPHICS_PIPELINE_DESC_INIT;
    AgcComputePipelineDesc compute_desc = AGC_COMPUTE_PIPELINE_DESC_INIT;
    OpenAgcPsbcOutput vertex_output = {0};
    OpenAgcPsbcOutput fragment_output = {0};
    OpenAgcPsbcOutput compute_output = {0};
    AgcDevice device = NULL;
    AgcShader vertex = NULL;
    AgcShader fragment = NULL;
    AgcShader compute = NULL;
    AgcGraphicsPipeline graphics = NULL;
    AgcComputePipeline compute_pipeline = NULL;
    uint32_t *vertex_spirv = NULL;
    uint32_t *fragment_spirv = NULL;
    uint32_t *compute_spirv = NULL;
    size_t vertex_size = 0u;
    size_t fragment_size = 0u;
    size_t compute_size = 0u;
    int32_t runtime_result;
    int result = 1;

    if (argc != 4)
        return 2;
    vertex_spirv = read_spirv(argv[1], &vertex_size);
    fragment_spirv = read_spirv(argv[2], &fragment_size);
    compute_spirv = read_spirv(argv[3], &compute_size);
    if (!vertex_spirv || !fragment_spirv || !compute_spirv ||
        !compile_stage(OPENAGC_PSBC_STAGE_VERTEX, vertex_spirv, vertex_size,
            &graphics_context, &vertex_output) ||
        !compile_stage(OPENAGC_PSBC_STAGE_FRAGMENT, fragment_spirv,
            fragment_size, &fragment_context, &fragment_output) ||
        !compile_stage(OPENAGC_PSBC_STAGE_COMPUTE, compute_spirv, compute_size,
            &compute_context, &compute_output)) {
        fputs("compiler output creation failed\n", stderr);
        goto done;
    }
    if (agcCreateDevice(&device_desc, &device) != AGC_OK) {
        fputs("runtime device creation failed\n", stderr);
        goto done;
    }
    if (!create_shader(device, &vertex_output, &vertex)) {
        fputs("compiler vertex shader was rejected\n", stderr);
        goto done;
    }
    if (!create_shader(device, &fragment_output, &fragment)) {
        fputs("compiler fragment shader was rejected\n", stderr);
        goto done;
    }
    if (!create_shader(device, &compute_output, &compute)) {
        fputs("compiler compute shader was rejected\n", stderr);
        goto done;
    }
    graphics_desc.vertex_shader = vertex;
    graphics_desc.pixel_shader = fragment;
    graphics_desc.vertex_inputs = vertex_output.metadata.vertex_inputs;
    graphics_desc.vertex_input_count = vertex_output.metadata.vertex_input_count;
    attachment.format = AGC_FORMAT_RGBA8_UNORM;
    graphics_desc.color_attachments = &attachment;
    graphics_desc.color_attachment_count = 1u;
    runtime_result = agcCreateGraphicsPipeline(device, &graphics_desc,
        &graphics);
    if (runtime_result != AGC_OK) {
        fprintf(stderr, "compiler graphics pipeline was rejected: 0x%08x\n",
            (unsigned)runtime_result);
        fprintf(stderr,
            "vertex flags=0x%x wave=%u scratch=%u lds=%u sgprs=%u; "
            "fragment wave=%u scratch=%u lds=%u sgprs=%u\n",
            vertex_output.metadata.flags, vertex_output.metadata.wave_size,
            vertex_output.metadata.scratch_bytes_per_wave,
            vertex_output.metadata.lds_size,
            vertex_output.metadata.user_sgpr_count,
            fragment_output.metadata.wave_size,
            fragment_output.metadata.scratch_bytes_per_wave,
            fragment_output.metadata.lds_size,
            fragment_output.metadata.user_sgpr_count);
        for (uint32_t i = 0u; i < vertex_output.metadata.user_sgpr_count; ++i) {
            const AgcShaderUserSgpr *sgpr =
                &vertex_output.metadata.user_sgprs[i];
            fprintf(stderr, "vertex sgpr[%u]: kind=%u reg=0x%x dwords=%u\n",
                i, (unsigned)sgpr->kind, sgpr->register_offset,
                sgpr->dword_count);
        }
        goto done;
    }
    compute_desc.shader = compute;
    compute_desc.local_size_x = compute_output.metadata.local_size_x;
    compute_desc.local_size_y = compute_output.metadata.local_size_y;
    compute_desc.local_size_z = compute_output.metadata.local_size_z;
    compute_desc.descriptor_mappings = compute_output.metadata.descriptor_mappings;
    compute_desc.descriptor_mapping_count =
        compute_output.metadata.descriptor_mapping_count;
    compute_desc.push_constant_ranges = compute_output.metadata.push_constant_ranges;
    compute_desc.push_constant_range_count =
        compute_output.metadata.push_constant_range_count;
    runtime_result = agcCreateComputePipeline(device, &compute_desc,
        &compute_pipeline);
    if (runtime_result != AGC_OK) {
        fprintf(stderr, "compiler compute pipeline was rejected: 0x%08x\n",
            (unsigned)runtime_result);
        goto done;
    }
    result = 0;
done:
    if (compute_pipeline)
        agcDestroyComputePipeline(compute_pipeline);
    if (graphics)
        agcDestroyGraphicsPipeline(graphics);
    if (compute)
        agcDestroyShader(compute);
    if (fragment)
        agcDestroyShader(fragment);
    if (vertex)
        agcDestroyShader(vertex);
    if (device)
        agcDestroyDevice(device);
    openagcPsbcFreeOutput(&compute_output);
    openagcPsbcFreeOutput(&fragment_output);
    openagcPsbcFreeOutput(&vertex_output);
    free(compute_spirv);
    free(fragment_spirv);
    free(vertex_spirv);
    if (result != 0)
        fputs("openagc-psbc/runtime integration failed\n", stderr);
    return result;
}
