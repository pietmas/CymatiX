#pragma once

#include <app/Config.h>
#include <vector>
#include <vulkan/vulkan.h>

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

    VkCommandPool getPool() const
    {
        return m_commandPool;
    }
    const std::vector<VkCommandBuffer> &getBuffers() const
    {
        return m_commandBuffers;
    }
    VkCommandBuffer getBuffer(int frameIndex) const
    {
        return m_commandBuffers[frameIndex];
    }

  private:
    void allocateCommandBuffers(const VulkanContext &ctx);

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers; // one per frame in flight, not owned
};

} // namespace rhi
