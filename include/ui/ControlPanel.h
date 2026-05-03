#pragma once

#include <ui/InputSelector.h>

#include <memory>

class App;

namespace ui
{

class ControlPanel
{
  public:
    explicit ControlPanel(App &app);
    void draw();

  private:
    App &m_app;
    int m_styleIndex = 0;
    int m_paletteIndex = 0;
    std::unique_ptr<InputSelector> m_inputSelector;
};

} // namespace ui
