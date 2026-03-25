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

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// interpolate between palette color stops, same function as lissajous.frag
// (GLSL has no shared includes, so we copy it here)
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

// average a range of FFT magnitude bins
float avgBins(int lo, int hi)
{
    float sum = 0.0;
    for (int i = lo; i <= hi; i++)
        sum += ubo.magnitudes[i];
    return sum / float(hi - lo + 1);
}

void main()
{
    float t = ubo.time;
    vec2 pos = inUV * 2.0 - 1.0;

    // three frequency bands -- each drives one wave source
    float amp0 = clamp(avgBins(1, 10) * 8.0, 0.0, 1.0);   // bass
    float amp1 = clamp(avgBins(11, 79) * 4.0, 0.0, 1.0);  // mid
    float amp2 = clamp(avgBins(80, 200) * 4.0, 0.0, 1.0); // treble

    vec2 src0 = vec2(-0.5, 0.0);
    vec2 src1 = vec2(0.5, 0.3);
    vec2 src2 = vec2(0.0, -0.4);

    float r0 = distance(pos, src0);
    float r1 = distance(pos, src1);
    float r2 = distance(pos, src2);

    // spatial frequency: controls how many wave crests per unit distance
    float k = 18.0;

    // distance decay: exp(-decay * r) makes waves fade naturally away from source
    // like real water surface waves -- value 2.5 means nearly gone at ~0.8 NDC units
    float decay = 2.5;

    // propagating wave: amp * exp(-decay*r) * sin(k*r - speed*t)
    // -- amplitude scales with the audio band, so loud bass = big waves from src0
    // -- speed also nudges up with amplitude so louder = slightly faster propagation
    float wave = 0.0;
    wave += amp0 * exp(-decay * r0) * sin(k * r0 - t * (3.0 + amp0 * 2.0));
    wave += amp1 * exp(-decay * r1) * sin(k * r1 - t * (3.5 + amp1 * 1.5));
    wave += amp2 * exp(-decay * r2) * sin(k * r2 - t * (4.0 + amp2 * 1.5));

    // normalize [-3, 3] → [0, 1]
    wave = wave / 3.0 * 0.5 + 0.5;

    wave = smoothstep(0.4, 0.6, wave);

    outColor = samplePalette(wave);
}
