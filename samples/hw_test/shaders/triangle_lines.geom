#version 450

layout(lines) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color;

void main()
{
    vec4 bottom_left = gl_in[0].gl_Position;
    vec4 bottom_right = gl_in[1].gl_Position;
    vec4 top = vec4((bottom_left.x + bottom_right.x) * 0.5,
                    -bottom_left.y, bottom_left.z, bottom_left.w);

    gl_Position = bottom_left;
    out_color = in_color[0];
    EmitVertex();

    gl_Position = bottom_right;
    out_color = in_color[1];
    EmitVertex();

    gl_Position = top;
    out_color = vec3(0.0, 0.0, 1.0);
    EmitVertex();
    EndPrimitive();
}
