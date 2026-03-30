#pragma once

#include <vulkan/vulkan_raii.hpp>

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

    vk::RenderPass get() const
    {
        return *m_renderPass;
    }

  private:
    vk::raii::RenderPass m_renderPass{nullptr};
};

} // namespace rhi
