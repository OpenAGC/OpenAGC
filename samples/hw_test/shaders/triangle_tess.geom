#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color;

void main()
{
    vec4 center = (gl_in[0].gl_Position +
                   gl_in[1].gl_Position +
                   gl_in[2].gl_Position) / 3.0;

    for (int i = 0; i < 3; ++i) {
        gl_Position = mix(center, gl_in[i].gl_Position, 0.78);
        out_color = in_color[i];
        EmitVertex();
    }
    EndPrimitive();
}
