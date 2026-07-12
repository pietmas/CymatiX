#include <palette/OrganicPalette.h>

namespace palette
{

// 6 earthy stops, dark soil up to ivory; sRGB, no gamma
PaletteUBOData OrganicPalette::getUBOData() const
{
    PaletteUBOData data{};
    data.numStops = 6;

    // stop 0: dark soil #1a1209
    data.colors[0][0] = 0.102f;
    data.colors[0][1] = 0.071f;
    data.colors[0][2] = 0.035f;
    data.colors[0][3] = 1.0f;

    // stop 1: terracotta #6b4226
    data.colors[1][0] = 0.420f;
    data.colors[1][1] = 0.259f;
    data.colors[1][2] = 0.149f;
    data.colors[1][3] = 1.0f;

    // stop 2: ochre #a67c52
    data.colors[2][0] = 0.651f;
    data.colors[2][1] = 0.486f;
    data.colors[2][2] = 0.322f;
    data.colors[2][3] = 1.0f;

    // stop 3: golden grass #c8b560
    data.colors[3][0] = 0.784f;
    data.colors[3][1] = 0.710f;
    data.colors[3][2] = 0.376f;
    data.colors[3][3] = 1.0f;

    // stop 4: sage green #8fbc8f
    data.colors[4][0] = 0.561f;
    data.colors[4][1] = 0.737f;
    data.colors[4][2] = 0.561f;
    data.colors[4][3] = 1.0f;

    // stop 5: ivory #f5f0e8
    data.colors[5][0] = 0.961f;
    data.colors[5][1] = 0.941f;
    data.colors[5][2] = 0.910f;
    data.colors[5][3] = 1.0f;

    return data;
}

std::string_view OrganicPalette::getName() const
{
    return "Organic";
}

} // namespace palette
