#pragma once

#include <vulkan/vulkan.h>

namespace rhi
{

class VulkanContext;
class Swapchain;

class RenderPass
{
  public:
    RenderPass() = default;
    ~RenderPass() = default;

    RenderPass(const RenderPass &) = delete;
    RenderPass &operator=(const RenderPass &) = delete;
    RenderPass(RenderPass &&) = delete;
    RenderPass &operator=(RenderPass &&) = delete;

    void init(const VulkanContext &ctx, const Swapchain &swapchain);
    void destroy(const VulkanContext &ctx);

    VkRenderPass get() const
    {
        return m_renderPass;
    }

  private:
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};

} // namespace rhi
