#version 450

/* Dedicated signed-integer output fixture for R/RG/RGBA16_SINT. Each lane
 * is an exact, CPU-reproducible function of the pixel coordinate and spans
 * the complete signed 16-bit range without producing the 0xdead sentinel. */
layout(location = 0) out ivec4 frag_color;

void main() {
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    frag_color = ivec4(
        (x & 255) * 257 - 32768,
        (y & 255) * 257 - 32768,
        ((x + y) & 255) * 257 - 32768,
        ((x * 17 + y * 31) & 255) * 257 - 32768);
}
