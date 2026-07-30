#version 450

/* Dedicated unsigned-integer output fixture for R/RG/RGBA16_UINT. Each lane
 * is an exact, CPU-reproducible function of the pixel coordinate and cycles
 * through every 16-bit value of the form n*257. */
layout(location = 0) out uvec4 frag_color;

void main() {
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    frag_color = uvec4(
        (x & 255u) * 257u,
        (y & 255u) * 257u,
        ((x + y) & 255u) * 257u,
        ((x * 17u + y * 31u) & 255u) * 257u);
}
