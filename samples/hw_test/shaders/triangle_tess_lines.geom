#version 450

layout(triangles) in;
layout(line_strip, max_vertices = 4) out;

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color;

void main()
{
    const int vertex_index[4] = int[4](0, 1, 2, 0);
    for (int i = 0; i < 4; ++i) {
        int source = vertex_index[i];
        gl_Position = gl_in[source].gl_Position;
        out_color = in_color[source];
        EmitVertex();
    }
    EndPrimitive();
}
