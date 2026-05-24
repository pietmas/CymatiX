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
    static int s_micDevice = 0;
    const char *labels[] = {"File", "Microphone", "System Loopback"};
    int current = (int)m_app.audioSource;

    if (ImGui::BeginCombo("Source", labels[current]))
    {
        // File is always selectable
        if (ImGui::Selectable("File", m_app.audioSource == AudioSource::File))
        {
            m_app.switchAudioSource(AudioSource::File);
        }

        auto devices = m_app.getCaptureDeviceNames();
        bool micSelected = (m_app.audioSource == AudioSource::Microphone);
        if (ImGui::Selectable("Microphone", micSelected))
        {
            m_app.switchAudioSource(AudioSource::Microphone, s_micDevice);
        }

        // loopback: grayed with tooltip if unavailable
        ImGui::BeginDisabled(!m_app.isLoopbackAvailable());
        if (ImGui::Selectable("System Loopback", m_app.audioSource == AudioSource::Loopback))
        {
            m_app.switchAudioSource(AudioSource::Loopback);
        }
        ImGui::EndDisabled();
        if (!m_app.isLoopbackAvailable())
        {
            ImGui::SetItemTooltip("System loopback not available.\n"
                                  "On Linux: configure a PipeWire monitor source.\n"
                                  "See README for instructions.");
        }

        ImGui::EndCombo();
    }

    // capture device sub-combo, shown when mic is active
    if (m_app.audioSource == AudioSource::Microphone && !m_app.getCaptureDeviceNames().empty())
    {
        auto devices = m_app.getCaptureDeviceNames();
        std::vector<const char *> names;
        for (auto &n : devices)
        {
            names.push_back(n.c_str());
        }
        if (ImGui::Combo("Capture Device", &s_micDevice, names.data(), (int)names.size()))
        {
            m_app.switchAudioSource(AudioSource::Microphone, s_micDevice);
        }
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
