#include <palette/BioluminescentPalette.h>

namespace palette
{

// 6 bioluminescent color stops; sRGB, no gamma
PaletteUBOData BioluminescentPalette::getUBOData() const
{
    PaletteUBOData data{};
    data.numStops = 6;

    // stop 0: midnight deep blue #050a1a
    data.colors[0][0] = 0.020f;
    data.colors[0][1] = 0.039f;
    data.colors[0][2] = 0.102f;
    data.colors[0][3] = 1.0f;

    // stop 1: ocean blue #0077b6
    data.colors[1][0] = 0.000f;
    data.colors[1][1] = 0.467f;
    data.colors[1][2] = 0.714f;
    data.colors[1][3] = 1.0f;

    // stop 2: cyan #00b4d8
    data.colors[2][0] = 0.000f;
    data.colors[2][1] = 0.706f;
    data.colors[2][2] = 0.847f;
    data.colors[2][3] = 1.0f;

    // stop 3: light cyan #90e0ef
    data.colors[3][0] = 0.565f;
    data.colors[3][1] = 0.878f;
    data.colors[3][2] = 0.937f;
    data.colors[3][3] = 1.0f;

    // stop 4: bioluminescent magenta #c77dff
    data.colors[4][0] = 0.780f;
    data.colors[4][1] = 0.490f;
    data.colors[4][2] = 1.000f;
    data.colors[4][3] = 1.0f;

    // stop 5: white core #ffffff
    data.colors[5][0] = 1.000f;
    data.colors[5][1] = 1.000f;
    data.colors[5][2] = 1.000f;
    data.colors[5][3] = 1.0f;

    return data;
}

std::string_view BioluminescentPalette::getName() const
{
    return "Bioluminescent";
}

} // namespace palette
