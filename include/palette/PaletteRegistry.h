#pragma once

#include <palette/IPalette.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace palette
{

class PaletteRegistry
{
  public:
    using FactoryFn = std::function<std::unique_ptr<IPalette>()>;

    // register a palette factory under a name
    void registerPalette(const std::string &name, FactoryFn factory);

    // create a palette by name, returns nullptr if name unknown
    std::unique_ptr<IPalette> create(const std::string &name) const;

    // names in registration order
    std::vector<std::string> listNames() const;

  private:
    std::unordered_map<std::string, FactoryFn> m_factories;
    std::vector<std::string> m_order;
};

} // namespace palette
