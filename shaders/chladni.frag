#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC
{
    float m;
    float n;
    float blend;
    float lineWidth;
    float time;
    float theta;
}
pc;

layout(set = 0, binding = 1) uniform PaletteUBO
{
    vec4 colors[8];
    int numStops;
    float _pad0;
    float _pad1;
    float _pad2;
}
palette;

const float PI = 3.14159265;

// interpolate between palette color stops based on t in [0, 1]
vec4 samplePalette(float t)
{
    t = clamp(t, 0.0, 1.0);

    int n = palette.numStops;
    if (n <= 1)
    {
        return palette.colors[0];
    }

    float scaled = t * float(n - 1);
    int lo = int(floor(scaled));
    int hi = lo + 1;

    if (hi >= n)
    {
        return palette.colors[n - 1];
    }

    float frac = scaled - float(lo);
    return mix(palette.colors[lo], palette.colors[hi], frac);
}

// evaluate Chladni equation and color nodal lines using palette
void main()
{
    float x = fragUV.x;
    float y = fragUV.y;

    // Chladni nodal equation: rotated superposition of degenerate eigenmodes
    float f = cos(pc.theta) * cos(pc.m * PI * x) * cos(pc.n * PI * y) +
              sin(pc.theta) * cos(pc.n * PI * x) * cos(pc.m * PI * y);

    float brightness = 1.0 - smoothstep(0.0, pc.lineWidth, abs(f));

    outColor = vec4(samplePalette(brightness).rgb, 1.0);
}
