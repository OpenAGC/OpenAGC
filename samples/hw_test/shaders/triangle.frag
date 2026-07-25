#version 450

/* Triangle fragment shader — outputs a constant diagnostic color.
 *
 * Input:
 *   location 0: v_color (vec3) — retained for interface validation
 * Output:
 *   location 0: frag_color (vec4) — RGBA8 render target
 */
layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(1.0, 0.0, 1.0, 1.0);
}
