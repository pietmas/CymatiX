#pragma once

#include <palette/IPalette.h>

namespace palette
{

class CyberpunkPalette : public IPalette
{
  public:
    CyberpunkPalette() = default;
    ~CyberpunkPalette() = default;
    CyberpunkPalette(const CyberpunkPalette &) = default;
    CyberpunkPalette &operator=(const CyberpunkPalette &) = default;
    CyberpunkPalette(CyberpunkPalette &&) = default;
    CyberpunkPalette &operator=(CyberpunkPalette &&) = default;

    PaletteUBOData getUBOData() const override;
    std::string_view getName() const override;
};

} // namespace palette
