#include <visuals/VisualStyleRegistry.h>

#include <cassert>
#include <cstdio>

namespace visuals
{

// register factory; asserts on duplicate in debug, warns+overwrites in release
void VisualStyleRegistry::registerStyle(const std::string &name, FactoryFn factory)
{
    if (m_factories.count(name) > 0)
    {
#ifdef NDEBUG
        fprintf(
            stderr,
            "[VisualStyleRegistry] warning: style '%s' already registered, overwriting\n",
            name.c_str()
        );
        m_factories[name] = std::move(factory);
#else
        fprintf(
            stderr,
            "[VisualStyleRegistry] error: style '%s' already registered\n",
            name.c_str()
        );
        assert(false && "duplicate style registration");
#endif
        return;
    }

    m_order.push_back(name);
    m_factories[name] = std::move(factory);
}

// look up factory by name, call with palette; returns nullptr on miss
std::unique_ptr<IVisualStyle>
VisualStyleRegistry::create(const std::string &name, const palette::IPalette &palette) const
{
    auto it = m_factories.find(name);
    if (it == m_factories.end())
    {
        fprintf(stderr, "[VisualStyleRegistry] warning: unknown style '%s'\n", name.c_str());
        return nullptr;
    }

    return it->second(palette);
}

// names in the order they were registered
std::vector<std::string> VisualStyleRegistry::listNames() const
{
    return m_order;
}

} // namespace visuals
