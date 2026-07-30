#version 450

/* Reusable signed-normalized output fixture for R/RG/RGBA16_SNORM.  Every
 * lane spans [-1, 1], and the four functions have distinct native sequences
 * so wider tuples can detect missing, duplicated, or reordered exports. */
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 frag_color;

void main() {
    float t = clamp(v_color.x, 0.0, 1.0);
    float ramp = 2.0 * t - 1.0;
    float triangle = 2.0 * abs(ramp) - 1.0;
    frag_color = vec4(ramp, -ramp, triangle, -triangle);
}
