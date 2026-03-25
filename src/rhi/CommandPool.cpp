#include <rhi/CommandPool.h>
#include <rhi/VulkanContext.h>

namespace rhi
{

// create pool, allocate command buffers
void CommandPool::init(const VulkanContext &ctx)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx.getQueueFamilyIndices().graphicsFamily.value();

    VK_CHECK(vkCreateCommandPool(ctx.getDevice(), &poolInfo, nullptr, &m_commandPool));

    allocateCommandBuffers(ctx);
}

// pool destroy frees all allocated buffers
void CommandPool::destroy(const VulkanContext &ctx)
{
    vkDestroyCommandPool(ctx.getDevice(), m_commandPool, nullptr);
    m_commandPool = VK_NULL_HANDLE;
    m_commandBuffers.clear();
}

// one command buffer per fif
void CommandPool::allocateCommandBuffers(const VulkanContext &ctx)
{
    m_commandBuffers.resize(Config::MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

    VK_CHECK(vkAllocateCommandBuffers(ctx.getDevice(), &allocInfo, m_commandBuffers.data()));
}

} // namespace rhi
