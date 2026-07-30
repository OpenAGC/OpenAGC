#version 450

/* Four independent full-range float outputs for the RGBA16_UNORM gate.
 * The rasterized vertex input supplies t in [0, 1].  Complementary ramps
 * and triangle waves make every native 16-bit lane observable and prevent
 * a duplicated or missing export lane from satisfying the readback oracle. */
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 frag_color;

void main() {
    float t = clamp(v_color.x, 0.0, 1.0);
    float triangle = abs(2.0 * t - 1.0);
    frag_color = vec4(t, 1.0 - t, triangle, 1.0 - triangle);
}
