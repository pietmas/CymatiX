#pragma once

#include <app/Config.h>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

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
    vk::Semaphore getImageAvailableSemaphore(int frame) const
    {
        return *m_imageAvailableSemaphores[frame];
    }
    // indexed by imageIndex returned from acquireNextImageKHR
    vk::Semaphore getRenderFinishedSemaphore(uint32_t imageIndex) const
    {
        return *m_renderFinishedSemaphores[imageIndex];
    }
    vk::Fence getInFlightFence(int frame) const
    {
        return *m_inFlightFences[frame];
    }

  private:
    // one per frame in flight
    std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
    // one per swapchain image
    std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence> m_inFlightFences;
};

} // namespace rhi
