#version 450

layout(location = 0) out vec2 outUV;

void main()
{
    // full-screen triangle trick: 3 vertices cover the entire screen without a vertex buffer
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);

    // convert NDC [-1,1] to UV [0,1] for use in the frag shader
    outUV = (positions[gl_VertexIndex] + 1.0) * 0.5;
}
