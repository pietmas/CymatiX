#version 450

layout(set = 0, binding = 0) uniform SpectrumUBO
{
    vec4 magnitudes[256]; // 256 vec4s = 1024 floats, correct std140 stride
    float time;
    float _pad0;
    float _pad1;
    float _pad2;
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
