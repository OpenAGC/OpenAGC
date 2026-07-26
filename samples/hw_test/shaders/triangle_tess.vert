#version 450

layout(location = 0) out vec3 v_color;

const vec2 positions[3] = vec2[](
    vec2(-0.50, -0.4330127),
    vec2( 0.50, -0.4330127),
    vec2( 0.00,  0.4330127)
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_color = colors[gl_VertexIndex];
}
