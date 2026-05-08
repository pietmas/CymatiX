#include <app/App.h>
#include <app/SharedTypes.h>
#include <ui/InputSelector.h>

#include <imgui.h>

#include <cstdio>

// store app ref
ui::InputSelector::InputSelector(App &app) : m_app(app) {}

// draw input source controls, called inside an existing ImGui window
void ui::InputSelector::draw()
{
    const char *labels[] = {"File", "Microphone", "System Loopback"};
    int current = (int)m_app.audioSource;

    if (ImGui::BeginCombo("Source", labels[current]))
    {
        // File is always selectable
        if (ImGui::Selectable("File", m_app.audioSource == AudioSource::File))
        {
            m_app.audioSource = AudioSource::File;
        }

        // mic and loopback not implemented yet, grayed out
        ImGui::BeginDisabled(true);
        ImGui::Selectable("Microphone", false);
        ImGui::Selectable("System Loopback", false);
        ImGui::EndDisabled();

        ImGui::EndCombo();
    }

    if (m_app.audioSource == AudioSource::File)
    {
        ImGui::Text("File: test/test2.wav");
        if (ImGui::Button("Change File"))
        {
            printf("TODO: file dialog \n");
        }
    }
}
