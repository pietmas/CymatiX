#pragma once

#include <audio/AudioEngine.h>
#include <audio/FFTProcessor.h>
#include <palette/IPalette.h>
#include <palette/PaletteRegistry.h>
#include <rhi/CommandPool.h>
#include <rhi/Swapchain.h>
#include <rhi/Sync.h>
#include <rhi/VulkanContext.h>
#include <rhi/VulkanDeps.h>
#include <ui/UILayer.h>
#include <visuals/IVisualStyle.h>
#include <visuals/VisualStyleRegistry.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

class App
{
  public:
    App() = default;
    ~App() = default;

    App(const App &) = delete;
    App &operator=(const App &) = delete;
    App(App &&) = delete;
    App &operator=(App &&) = delete;

    void run();

    void setActiveStyle(const std::string &name);
    void setActivePalette(const std::string &name);
    std::vector<std::string> getStyleNames() const;
    std::vector<std::string> getPaletteNames() const;

    float audioGain = 1.0f;

  private:
    void initWindow();
    void initVulkan();
    void initAudio();
    void mainLoop();
    void shutdown();

    void drawFrame();
    void update();
    void recreateSwapchain();
    rhi::VulkanDeps makeDeps() const;

    static void framebufferResizeCallback(GLFWwindow *window, int width, int height);

    GLFWwindow *m_window = nullptr;

    std::unique_ptr<rhi::VulkanContext> m_context;
    std::unique_ptr<rhi::Swapchain> m_swapchain;
    std::unique_ptr<rhi::CommandPool> m_commandPool;
    std::unique_ptr<rhi::Sync> m_sync;
    std::unique_ptr<ui::UILayer> m_uiLayer;

    // visual style and palette
    palette::PaletteRegistry m_paletteRegistry;
    std::unique_ptr<palette::IPalette> m_activePalette;
    visuals::VisualStyleRegistry m_styleRegistry;
    std::unique_ptr<visuals::IVisualStyle> m_activeStyle;
    std::string m_activeStyleName;

    // audio subsystem
    std::unique_ptr<audio::AudioEngine> m_audioEngine;
    std::unique_ptr<audio::FFTProcessor> m_fftProcessor;
    std::deque<float> m_audioHistory;  // sliding window for FFT
    std::vector<float> m_smoothedMags; // EMA-smoothed FFT output

    int m_currentFrame = 0;
    bool m_framebufferResized = false;
    int m_debugFrameCount = 0;
    float m_lastFrameTime = 0.0f;

    float m_fpsAccum = 0.0f;
    int m_fpsSamples = 0;
};
