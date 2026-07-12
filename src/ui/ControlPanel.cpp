#include <app/App.h>
#include <ui/ControlPanel.h>

#include <imgui.h>

#include <vector>

// store app ref, init dropdown indices to match current active style and palette
ui::ControlPanel::ControlPanel(App &app) : m_app(app)
{
    // find index of the active style so the combo starts on the right item
    auto styleNames = app.getStyleNames();
    std::string activStyle = app.getActiveStyleName();
    for (int i = 0; i < (int)styleNames.size(); i++)
    {
        if (styleNames[i] == activStyle)
        {
            m_styleIndex = i;
            break;
        }
    }

    // same for palette
    auto paletteNames = app.getPaletteNames();
    std::string activPalette = app.getActivePaletteName();
    for (int i = 0; i < (int)paletteNames.size(); i++)
    {
        if (paletteNames[i] == activPalette)
        {
            m_paletteIndex = i;
            break;
        }
    }

    m_inputSelector = std::make_unique<InputSelector>(app);
}

// draw controls panel: style combo, palette combo, gain slider
void ui::ControlPanel::draw()
{
    // always track the right edge so resizing the window keeps the panel in place
    ImGuiIO &io = ImGui::GetIO();
    float panelW = io.DisplaySize.x * 0.2f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panelW, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, io.DisplaySize.y), ImGuiCond_Always);

    ImGui::Begin(
        "Controls",
        nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
    );

    auto styleNames = m_app.getStyleNames();
    std::vector<const char *> styleItems;
    styleItems.reserve(styleNames.size());
    for (const auto &s : styleNames)
    {
        styleItems.push_back(s.c_str());
    }

    if (ImGui::Combo("Style", &m_styleIndex, styleItems.data(), (int)styleItems.size()))
    {
        m_app.setActiveStyle(styleNames[m_styleIndex]);
    }

    auto paletteNames = m_app.getPaletteNames();
    std::vector<const char *> paletteItems;
    paletteItems.reserve(paletteNames.size());
    for (const auto &s : paletteNames)
    {
        paletteItems.push_back(s.c_str());
    }

    if (ImGui::Combo("Palette", &m_paletteIndex, paletteItems.data(), (int)paletteItems.size()))
    {
        m_app.setActivePalette(paletteNames[m_paletteIndex]);
    }

    // gain slider, range 0.0 to 4.0
    ImGui::SliderFloat("Gain", &m_app.audioGain, 0.0f, 4.0f);

    // fullscreen toggle, mirrors the F11 hotkey
    if (ImGui::Button("Fullscreen (F11)"))
    {
        m_app.toggleFullscreen();
    }

    ImGui::Separator();

    // audio input source selector, embedded in the same panel
    m_inputSelector->draw();

    ImGui::End();
}
