#pragma once

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
    int m_styleIndex   = 0;
    int m_paletteIndex = 0;
};

} // namespace ui
