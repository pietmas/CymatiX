#pragma once

#include <app/Config.h>
#include <array>
#include <vector>
#include <vulkan/vulkan.h>

namespace rhi
{

class VulkanContext;

class Sync
{
  public:
    Sync() = default;
    ~Sync() = default;

    Sync(const Sync &) = delete;
    Sync &operator=(const Sync &) = delete;
    Sync(Sync &&) = delete;
    Sync &operator=(Sync &&) = delete;

    void init(const VulkanContext &ctx, uint32_t swapchainImageCount);
    void destroy(const VulkanContext &ctx);

    // indexed by currentFrame
    VkSemaphore getImageAvailableSemaphore(int frame) const
    {
        return m_imageAvailableSemaphores[frame];
    }
    // indexed by imageIndex returned from vkAcquireNextImageKHR
    VkSemaphore getRenderFinishedSemaphore(uint32_t imageIndex) const
    {
        return m_renderFinishedSemaphores[imageIndex];
    }
    VkFence getInFlightFence(int frame) const
    {
        return m_inFlightFences[frame];
    }

  private:
    // one per frame in flight - used before we know which image we'll get
    std::array<VkSemaphore, Config::MAX_FRAMES_IN_FLIGHT> m_imageAvailableSemaphores;
    // one per swapchain image
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::array<VkFence, Config::MAX_FRAMES_IN_FLIGHT> m_inFlightFences;
};

} // namespace rhi
