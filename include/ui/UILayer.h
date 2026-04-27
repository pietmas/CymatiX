#pragma once

#include <ui/ControlPanel.h>
#include <vulkan/vulkan_raii.hpp>

#include <memory>

struct GLFWwindow;
class App;

namespace rhi
{
class VulkanContext;
class Swapchain;
} // namespace rhi

namespace ui
{

class UILayer
{
  public:
    void init(
        const rhi::VulkanContext &ctx,
        const rhi::Swapchain &swapchain,
        GLFWwindow *window,
        App &app
    );
    void shutdown();
    void buildFrame();
    void renderDrawData(vk::CommandBuffer cmd);
    float getPanelWidth() const; // 20% of display width, or 0 when hidden

  private:
    vk::raii::DescriptorPool m_descriptorPool{nullptr};
    std::unique_ptr<ControlPanel> m_controlPanel;
    bool m_showPanel = true;
};

} // namespace ui
