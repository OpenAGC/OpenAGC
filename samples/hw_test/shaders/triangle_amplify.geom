#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color;

void emit_copy(float x_offset)
{
    for (int i = 0; i < 3; ++i) {
        vec4 position = gl_in[i].gl_Position;
        gl_Position = vec4(position.xy * 0.5 + vec2(x_offset, 0.0),
                           position.z, position.w);
        out_color = in_color[i];
        EmitVertex();
    }
    EndPrimitive();
}

void main()
{
    emit_copy(-0.3);
    emit_copy(0.3);
}
