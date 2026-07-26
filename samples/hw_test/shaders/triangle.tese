#version 450

layout(triangles, equal_spacing, ccw) in;

layout(location = 0) in vec3 in_color[];
layout(location = 0) out vec3 out_color;

void main()
{
    const vec4 positions[3] = vec4[](
        vec4(-0.50, -0.4330127, 0.0, 1.0),
        vec4( 0.50, -0.4330127, 0.0, 1.0),
        vec4( 0.00,  0.4330127, 0.0, 1.0)
    );
    gl_Position = gl_TessCoord.x * positions[0] +
                  gl_TessCoord.y * positions[1] +
                  gl_TessCoord.z * positions[2];
    out_color = gl_TessCoord;
}
