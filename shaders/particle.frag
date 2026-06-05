#version 450

layout(location = 0) in float fragAge;
layout(location = 1) in float fragEnergy;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform PaletteUBO
{
    vec4 colors[8];
}
palette;

void main()
{
    // sample palette by energy
    float t = clamp(fragEnergy * 2.0, 0.0, 1.0);
    float idx = t * 7.0;
    int lo = clamp(int(idx), 0, 6);
    float frac = fract(idx);
    vec3 col = mix(palette.colors[lo].rgb, palette.colors[lo + 1].rgb, frac);

    // quadratic fade-out by age
    float alpha = (1.0 - fragAge) * (1.0 - fragAge);

    outColor = vec4(col, alpha);
}
