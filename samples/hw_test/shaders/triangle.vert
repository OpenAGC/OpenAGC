#version 450

/* Triangle vertex shader — generates a fullscreen triangle from gl_VertexID.
 * No vertex buffer required. Draw with DrawIndexAuto(3).
 *
 * Outputs:
 *   location 0: color (vec3) — red/green/blue for the three vertices
 *
 * Vertex positions (clip space):
 *   0: (-0.5, -0.5, 0.0, 1.0)  bottom-left  (red)
 *   1: ( 0.5, -0.5, 0.0, 1.0)  bottom-right (green)
 *   2: ( 0.0,  0.5, 0.0, 1.0)  top-center   (blue)
 */
layout(location = 0) out vec3 v_color;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2( 0.0,  0.5)
    );
    vec3 colors[3] = vec3[](
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_color = colors[gl_VertexIndex];
}
