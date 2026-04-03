#pragma once

#include <audio/AudioEngine.h>
#include <audio/FFTProcessor.h>
#include <palette/BioluminescentPalette.h>
#include <rhi/CommandPool.h>
#include <rhi/RenderPass.h>
#include <rhi/Swapchain.h>
#include <rhi/Sync.h>
#include <rhi/VulkanContext.h>
#include <rhi/VulkanDeps.h>
#include <visuals/IVisualStyle.h>
#include <visuals/VisualStyleRegistry.h>

#include <deque>
#include <memory>
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
    std::unique_ptr<rhi::RenderPass> m_renderPass;
    std::unique_ptr<rhi::CommandPool> m_commandPool;
    std::unique_ptr<rhi::Sync> m_sync;

    // visual style and palette
    palette::BioluminescentPalette m_palette; // value type, no GPU resources
    visuals::VisualStyleRegistry m_styleRegistry;
    std::unique_ptr<visuals::IVisualStyle> m_activeStyle;

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
