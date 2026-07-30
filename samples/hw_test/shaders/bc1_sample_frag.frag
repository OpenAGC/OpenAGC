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

    if (region == 0) {
        frag_color = texelFetch(bc_texture, ivec3(lane, row, 0), 0);
    } else if (region == 1) {
        frag_color = texelFetch(
            bc_texture, ivec3(min(lane, 2), row, 0), 1);
    } else {
        frag_color = texelFetch(
            bc_texture, ivec3(4, min(row + 3, 6), 1), 0);
    }
}
