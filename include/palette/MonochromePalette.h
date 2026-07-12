#pragma once

#include <palette/IPalette.h>

namespace palette
{

class MonochromePalette : public IPalette
{
  public:
    MonochromePalette() = default;
    ~MonochromePalette() = default;
    MonochromePalette(const MonochromePalette &) = default;
    MonochromePalette &operator=(const MonochromePalette &) = default;
    MonochromePalette(MonochromePalette &&) = default;
    MonochromePalette &operator=(MonochromePalette &&) = default;

    PaletteUBOData getUBOData() const override;
    std::string_view getName() const override;
};

} // namespace palette
