#version 450

/* Triangle fragment shader — validates a sampled GFX1013 texture.
 *
 * Input:
 *   location 0: v_color.xy supplies interpolated texture coordinates
 *   set 0, binding 0: combined 2D image and sampler descriptor
 * Output:
 *   location 0: frag_color (vec4) — interpolated RGBA8 vertex color
 */
layout(location = 0) in vec3 v_color;
layout(set = 0, binding = 0) uniform sampler2D color_texture;
layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = texture(color_texture, v_color.xy);
}
