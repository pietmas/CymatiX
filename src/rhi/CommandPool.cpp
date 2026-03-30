#include <rhi/CommandPool.h>
#include <rhi/VulkanContext.h>

namespace rhi
{

// create pool, allocate command buffers
void CommandPool::init(const VulkanContext &ctx)
{
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = ctx.getQueueFamilyIndices().graphicsFamily.value();

    m_commandPool = ctx.getDevice().createCommandPool(poolInfo);

    allocateCommandBuffers(ctx);
}

// pool destroy frees all allocated buffers
void CommandPool::destroy(const VulkanContext &ctx)
{
    (void)ctx;
    m_commandBuffers.clear();
    m_commandPool = nullptr;
}

// one command buffer per fif
void CommandPool::allocateCommandBuffers(const VulkanContext &ctx)
{
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = *m_commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = Config::MAX_FRAMES_IN_FLIGHT;

    m_commandBuffers = ctx.getDevice().allocateCommandBuffers(allocInfo);
}

} // namespace rhi
