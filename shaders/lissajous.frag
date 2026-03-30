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
    float _pad0;
    float _pad1;
    float _pad2;
}
palette;

layout(location = 0) in float inT;
layout(location = 0) out vec4 outColor;

// interpolate smoothly between palette color stops based on t in [0, 1]
vec4 samplePalette(float t)
{
    t = clamp(t, 0.0, 1.0);

    int n = palette.numStops;
    if (n <= 1)
        return palette.colors[0];

    float scaled = t * float(n - 1);
    int lo = int(floor(scaled));
    int hi = lo + 1;

    if (hi >= n)
        return palette.colors[n - 1];

    float frac = scaled - float(lo);
    return mix(palette.colors[lo], palette.colors[hi], frac);
}

void main()
{
    // slowly animate the color along the curve over time
    float animT = fract(inT + ubo.time * 0.1);
    vec4 color = samplePalette(animT);

    // fade alpha at the start and end of the curve for a smooth look
    float alpha = sin(inT * 3.14159265);
    outColor = vec4(color.rgb, color.a * alpha * 0.9);
}
