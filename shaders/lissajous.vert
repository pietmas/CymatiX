#version 450

layout(set = 0, binding = 0) uniform SpectrumUBO
{
    float magnitudes[1024];
    float time;
    float _pad[3];
}
ubo;

layout(set = 0, binding = 1) uniform PaletteUBO
{
    vec4 colors[8];
    int numStops;
    float _pad[3];
}
palette;

// per-vertex position generated on the CPU each frame
layout(location = 0) in vec2 inPosition;

// normalized position along the curve [0, 1], used by frag for color mapping
layout(location = 0) out float outT;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    outT = float(gl_VertexIndex) / 4095.0;
}
