#pragma once

#include <cstdint>

// must match GLSL std140 layout declared in all shaders
// SpectrumUBO (set=0, binding=0)
// PaletteUBO  (set=0, binding=1)

constexpr uint32_t MAX_SPECTRUM_BINS = 1024;
constexpr uint32_t MAX_PALETTE_STOPS = 8;

// layout: 1024 floats (4096 bytes) + time (4 bytes) + pad to 16-byte boundary
struct SpectrumUBOData
{
    float magnitudes[MAX_SPECTRUM_BINS];
    float time;
    float _pad[3];
};
// sizeof(SpectrumUBOData) == 4112

// layout: 8 vec4s (128 bytes) + numStops int (4 bytes) + pad (12 bytes)
struct alignas(16) PaletteUBOData
{
    float colors[MAX_PALETTE_STOPS][4]; // each row is a vec4
    int numStops;
    float _pad[3];
};
// sizeof(PaletteUBOData) == 144
