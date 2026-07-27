#version 450

layout(set = 0, binding = 0) uniform sampler2DMS color_msaa;
layout(location = 0) out vec4 frag_color;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    frag_color = (texelFetch(color_msaa, pixel, 0) +
                  texelFetch(color_msaa, pixel, 1) +
                  texelFetch(color_msaa, pixel, 2) +
                  texelFetch(color_msaa, pixel, 3)) * 0.25;
}
