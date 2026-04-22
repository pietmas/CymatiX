#pragma once

#include <vulkan/vulkan_raii.hpp>

struct GLFWwindow;

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
    void init(const rhi::VulkanContext &ctx, const rhi::Swapchain &swapchain, GLFWwindow *window);
    void shutdown();
    void buildFrame();
    void renderDrawData(vk::CommandBuffer cmd);
    float getPanelWidth() const { return 400.0f; }

  private:
    vk::raii::DescriptorPool m_descriptorPool{nullptr};
};

} // namespace ui
