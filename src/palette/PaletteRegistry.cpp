#include <palette/PaletteRegistry.h>

#include <cassert>
#include <cstdio>

namespace palette
{

// register factory; asserts on duplicate in debug, warns+overwrites in release
void PaletteRegistry::registerPalette(const std::string &name, FactoryFn factory)
{
    if (m_factories.count(name) > 0)
    {
#ifdef NDEBUG
        fprintf(
            stderr,
            "[PaletteRegistry] warning: palette '%s' already registered, overwriting\n",
            name.c_str()
        );
        m_factories[name] = std::move(factory);
#else
        fprintf(stderr, "[PaletteRegistry] error: palette '%s' already registered\n", name.c_str());
        assert(false && "duplicate palette registration");
#endif
        return;
    }

    m_order.push_back(name);
    m_factories[name] = std::move(factory);
}

// look up factory by name, call it; returns nullptr on miss
std::unique_ptr<IPalette> PaletteRegistry::create(const std::string &name) const
{
    auto it = m_factories.find(name);
    if (it == m_factories.end())
    {
        fprintf(stderr, "[PaletteRegistry] warning: unknown palette '%s'\n", name.c_str());
        return nullptr;
    }

    return it->second();
}

// names in the order they were registerd
std::vector<std::string> PaletteRegistry::listNames() const
{
    return m_order;
}

} // namespace palette
