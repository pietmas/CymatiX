#version 450

#define PI 3.14159265
#define MAX_SOURCES 16

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC
{
    float time;
    int activeCount;
}
pc;

layout(set = 0, binding = 0) uniform SpectrumUBO
{
    float magnitudes[1024];
}
spectrum;

layout(set = 0, binding = 1) uniform PaletteUBO
{
    vec4 colors[8];
}
palette;

layout(set = 0, binding = 2) uniform RippleSourcesUBO
{
    vec4 sources[MAX_SOURCES]; // x=cx, y=cy, z=frequency, w=spawnTime
    int activeCount;
}
ripples;

void main()
{
    float total = 0.0;
    for (int i = 0; i < pc.activeCount; i++)
    {
        float cx = ripples.sources[i].x;
        float cy = ripples.sources[i].y;
        float freq = ripples.sources[i].z;
        float spawnTime = ripples.sources[i].w;
        float elapsed = pc.time - spawnTime;
        if (elapsed < 0.0)
        {
            continue;
        }
        float dist = length(fragUV - vec2(cx, cy));
        float k = 2.0 * PI * freq / 0.8;
        float omega = 2.0 * PI * freq;
        float envelope = exp(-1.5 * dist) * exp(-0.8 * elapsed);
        total += sin(k * dist - omega * elapsed) * envelope;
    }

    // map total [-2..2] to [0..1] for palette lookup
    float t = clamp(total * 0.25 + 0.5, 0.0, 1.0);
    float idx = t * 7.0;
    int lo = int(idx);
    float frac = fract(idx);
    lo = clamp(lo, 0, 6);
    vec3 col = mix(palette.colors[lo].rgb, palette.colors[lo + 1].rgb, frac);
    outColor = vec4(col, 1.0);
}
