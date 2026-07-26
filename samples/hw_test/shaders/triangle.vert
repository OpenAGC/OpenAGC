#version 450
/* Triangle vertex shader using an interleaved position/color vertex buffer.
 * Binding 0 has a 20-byte stride: vec2 position followed by vec3 color.
 *
 * Outputs:
 *   location 0: color (vec3) - red/green/blue for the three vertices
 *
 * Vertex positions (clip space):
 *   0: (-0.5, -0.5, 0.0, 1.0)  bottom-left  (red)
 *   1: ( 0.5, -0.5, 0.0, 1.0)  bottom-right (green)
 *   2: ( 0.0,  0.5, 0.0, 1.0)  top-center   (blue)
 */
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec3 v_color;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    v_color = in_color;
}
