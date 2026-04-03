#pragma once

#include <palette/IPalette.h>
#include <visuals/IVisualStyle.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace visuals
{

class VisualStyleRegistry
{
  public:
    using FactoryFn = std::function<std::unique_ptr<IVisualStyle>(const palette::IPalette &)>;

    // register a style factory under a name; asserts on duplicate in debug
    void registerStyle(const std::string &name, FactoryFn factory);

    // create a style by name with the given palette; returns nullptr if name unknown
    std::unique_ptr<IVisualStyle>
    create(const std::string &name, const palette::IPalette &palette) const;

    // names in registration order
    std::vector<std::string> listNames() const;

  private:
    std::unordered_map<std::string, FactoryFn> m_factories;
    std::vector<std::string> m_order; // insertion order for listNames()
};

} // namespace visuals
