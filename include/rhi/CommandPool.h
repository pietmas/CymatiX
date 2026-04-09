#pragma once

#include <app/Config.h>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace rhi
{

class VulkanContext;

class CommandPool
{
  public:
    CommandPool() = default;
    ~CommandPool() = default;

    CommandPool(const CommandPool &) = delete;
    CommandPool &operator=(const CommandPool &) = delete;
    CommandPool(CommandPool &&) = delete;
    CommandPool &operator=(CommandPool &&) = delete;

    void init(const VulkanContext &ctx);
    void destroy(const VulkanContext &ctx);

    vk::CommandPool getPool() const
    {
        return *m_commandPool;
    }
    // raw handle for recording ownership stays in the raii vector
    vk::CommandBuffer getBuffer(int frameIndex) const
    {
        return *m_commandBuffers[frameIndex];
    }

  private:
    void allocateCommandBuffers(const VulkanContext &ctx);

    vk::raii::CommandPool m_commandPool{nullptr};
    std::vector<vk::raii::CommandBuffer> m_commandBuffers; // one per frame in flight
};

} // namespace rhi
