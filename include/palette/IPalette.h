#pragma once

#include <app/SharedTypes.h>
#include <string_view>

namespace palette
{

class IPalette
{
  public:
    virtual ~IPalette() = default;

    virtual PaletteUBOData getUBOData() const = 0;
    virtual std::string_view getName() const = 0;
};

} // namespace palette
