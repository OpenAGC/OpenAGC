#version 450

/* Dedicated signed-integer output fixture for R/RG/RGBA32_SINT. Each lane
 * is an exact coordinate function translated by INT32_MIN, spanning the full
 * signed 32-bit range without floating-point conversion. */
layout(location = 0) out ivec4 frag_color;

int signed_lane(uint index) {
    return int((index & 255u) * 0x01010101u - 0x80000000u);
}

void main() {
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    frag_color = ivec4(
        signed_lane(x),
        signed_lane(y),
        signed_lane(x + y),
        signed_lane(x * 17u + y * 31u));
}
