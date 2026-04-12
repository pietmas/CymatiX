#include <palette/CyberpunkPalette.h>

namespace palette
{

// 5 cyberpunk color stops; sRGB, no gamma
PaletteUBOData CyberpunkPalette::getUBOData() const
{
    PaletteUBOData data{};
    data.numStops = 5;

    // void black #0d0d0d
    data.colors[0][0] = 0.051f;
    data.colors[0][1] = 0.051f;
    data.colors[0][2] = 0.051f;
    data.colors[0][3] = 1.0f;

    // hot pink #ff006e
    data.colors[1][0] = 1.000f;
    data.colors[1][1] = 0.000f;
    data.colors[1][2] = 0.431f;
    data.colors[1][3] = 1.0f;

    // electric blue #3a86ff
    data.colors[2][0] = 0.227f;
    data.colors[2][1] = 0.525f;
    data.colors[2][2] = 1.000f;
    data.colors[2][3] = 1.0f;

    // ultraviolet #8338ec
    data.colors[3][0] = 0.514f;
    data.colors[3][1] = 0.220f;
    data.colors[3][2] = 0.925f;
    data.colors[3][3] = 1.0f;

    // acid yellow #ffbe0b
    data.colors[4][0] = 1.000f;
    data.colors[4][1] = 0.745f;
    data.colors[4][2] = 0.043f;
    data.colors[4][3] = 1.0f;

    return data;
}

std::string_view CyberpunkPalette::getName() const
{
    return "Cyberpunk";
}

} // namespace palette
