#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in gl_PerVertex {
    vec4 gl_Position;
} gl_in[];

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color;

void main()
{
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        out_color = in_color[i];
        EmitVertex();
    }
    EndPrimitive();
}

