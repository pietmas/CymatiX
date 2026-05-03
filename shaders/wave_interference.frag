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

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// interpolate between palette color stops
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
    vec2 pos = inUV * 2.0 - 1.0;

    float mid = ubo._pad0;
    float treble = ubo._pad1;

    // mid controls how fast rings expand: quiet = lazy, loud = energetic
    float speed = mix(0.4, 1.4, mid);
    float fadeRate = 1.5;

    float wave = 0.0;

    // drops packed at vec4 indices 64..75
    for (int i = 0; i < 12; i++)
    {
        vec4 drop = ubo.magnitudes[64 + i]; // (x, y, hitTime, sigma)
        float age = ubo.time - drop.z;
        if (age < 0.0)
            continue;

        float sig = drop.w;
        float dr = distance(pos, drop.xy) - age * speed;
        wave += exp(-dr * dr / (sig * sig)) * exp(-fadeRate * age);
    }

    vec4 col = samplePalette(wave);

    // treble boosts scene brightness
    float brightness = 1.0 + treble * 1.5;
    outColor = vec4(col.rgb * brightness, 1.0);
}
