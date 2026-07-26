#version 450

layout(vertices = 3) out;

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color[];

void main()
{
    const vec4 positions[3] = vec4[](
        vec4(-0.50, -0.4330127, 0.0, 1.0),
        vec4( 0.50, -0.4330127, 0.0, 1.0),
        vec4( 0.00,  0.4330127, 0.0, 1.0)
    );
    const vec3 colors[3] = vec3[](
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0)
    );
    gl_out[gl_InvocationID].gl_Position = positions[gl_InvocationID];
    out_color[gl_InvocationID] = colors[gl_InvocationID];

    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = 4.0;
        gl_TessLevelOuter[1] = 4.0;
        gl_TessLevelOuter[2] = 4.0;
        gl_TessLevelInner[0] = 4.0;
    }
}
