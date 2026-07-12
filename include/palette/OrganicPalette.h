#pragma once

#include <palette/IPalette.h>

namespace palette
{

class OrganicPalette : public IPalette
{
  public:
    OrganicPalette() = default;
    ~OrganicPalette() = default;
    OrganicPalette(const OrganicPalette &) = default;
    OrganicPalette &operator=(const OrganicPalette &) = default;
    OrganicPalette(OrganicPalette &&) = default;
    OrganicPalette &operator=(OrganicPalette &&) = default;

    PaletteUBOData getUBOData() const override;
    std::string_view getName() const override;
};

} // namespace palette
