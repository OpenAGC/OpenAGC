#version 450

layout(triangles, invocations = 2) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color;

void main()
{
    float x_offset = gl_InvocationID == 0 ? -0.3 : 0.3;

    for (int i = 0; i < 3; ++i) {
        vec4 position = gl_in[i].gl_Position;
        gl_Position = vec4(position.xy * 0.5 + vec2(x_offset, 0.0),
                           position.z, position.w);
        out_color = in_color[i];
        EmitVertex();
    }
    EndPrimitive();
}
