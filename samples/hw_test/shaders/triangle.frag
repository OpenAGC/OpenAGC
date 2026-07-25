#version 450

/* Triangle fragment shader — outputs the interpolated vertex color.
 *
 * Input:
 *   location 0: v_color (vec3) — interpolated from VS
 * Output:
 *   location 0: frag_color (vec4) — RGBA8 render target
 */
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(v_color, 1.0);
}
