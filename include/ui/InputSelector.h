#pragma once

class App;

namespace ui
{

class InputSelector
{
  public:
    explicit InputSelector(App &app);
    void draw();

  private:
    App &m_app;
};

} // namespace ui
