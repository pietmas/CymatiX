#include <palette/MonochromePalette.h>

namespace palette
{

// 4 grayscale stops, white peak down to black silence; sRGB, no gamma
PaletteUBOData MonochromePalette::getUBOData() const
{
    PaletteUBOData data{};
    data.numStops = 4;

    // stop 0: white peak #ffffff
    data.colors[0][0] = 1.000f;
    data.colors[0][1] = 1.000f;
    data.colors[0][2] = 1.000f;
    data.colors[0][3] = 1.0f;

    // stop 1: mid gray #808080
    data.colors[1][0] = 0.502f;
    data.colors[1][1] = 0.502f;
    data.colors[1][2] = 0.502f;
    data.colors[1][3] = 1.0f;

    // stop 2: dark gray #404040
    data.colors[2][0] = 0.251f;
    data.colors[2][1] = 0.251f;
    data.colors[2][2] = 0.251f;
    data.colors[2][3] = 1.0f;

    // stop 3: black silence #000000
    data.colors[3][0] = 0.000f;
    data.colors[3][1] = 0.000f;
    data.colors[3][2] = 0.000f;
    data.colors[3][3] = 1.0f;

    return data;
}

std::string_view MonochromePalette::getName() const
{
    return "Monochrome";
}

} // namespace palette
