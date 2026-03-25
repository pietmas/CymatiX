#pragma once

#include <palette/IPalette.h>

namespace palette
{

class BioluminescentPalette : public IPalette
{
  public:
    BioluminescentPalette() = default;
    ~BioluminescentPalette() = default;
    BioluminescentPalette(const BioluminescentPalette &) = default;
    BioluminescentPalette &operator=(const BioluminescentPalette &) = default;
    BioluminescentPalette(BioluminescentPalette &&) = default;
    BioluminescentPalette &operator=(BioluminescentPalette &&) = default;

    PaletteUBOData getUBOData() const override;
    std::string_view getName() const override;
};

} // namespace palette
