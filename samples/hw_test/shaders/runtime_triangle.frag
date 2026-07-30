#version 450

layout(location = 0) in vec3 v_color;
layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec4 frag_aux;

void main()
{
    frag_color = vec4(v_color, 1.0);
    frag_aux = vec4(1.0 - v_color, 1.0);
}
