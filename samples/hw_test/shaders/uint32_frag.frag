#version 450

/* Dedicated unsigned-integer output fixture for R/RG/RGBA32_UINT. Each lane
 * is an exact coordinate function that spans 0x00000000..0xffffffff. */
layout(location = 0) out uvec4 frag_color;

void main() {
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    frag_color = uvec4(
        (x & 255u) * 0x01010101u,
        (y & 255u) * 0x01010101u,
        ((x + y) & 255u) * 0x01010101u,
        ((x * 17u + y * 31u) & 255u) * 0x01010101u);
}
