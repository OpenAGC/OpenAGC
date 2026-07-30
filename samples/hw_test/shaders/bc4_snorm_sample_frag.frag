#version 450

layout(set = 0, binding = 0) uniform sampler2DArray bc_texture;
layout(location = 0) out vec4 frag_color;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int local_x = pixel.x % 12;
    int lane = local_x & 3;
    int row = pixel.y & 3;
    int region = local_x / 4;
    float value;

    if (region == 0) {
        value = texelFetch(bc_texture, ivec3(lane, row, 0), 0).r;
    } else if (region == 1) {
        value = texelFetch(
            bc_texture, ivec3(min(lane, 1), min(row, 2), 0), 1).r;
    } else {
        value = texelFetch(
            bc_texture, ivec3(4, min(row + 3, 6), 1), 0).r;
    }
    frag_color = vec4(value * 0.5 + 0.5, 0.0, 0.0, 1.0);
}
