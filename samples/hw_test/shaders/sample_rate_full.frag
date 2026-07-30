#version 450

layout(set = 0, binding = 0, std430) buffer SampleResults {
    uint sample_counts[4];
    uint total_count;
    uint guards[4];
} results;

layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 frag_color;

void main()
{
    atomicAdd(results.sample_counts[gl_SampleID], 1u);
    atomicAdd(results.total_count, 1u);
    frag_color = vec4(v_color, 1.0);
}
