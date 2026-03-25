#include <rhi/Sync.h>
#include <rhi/VulkanContext.h>

namespace rhi
{

// create semaphores + fences
// imageAvailable + fences: one per frame-in-flight
// renderFinished: one per swapchain image. presentation engine needs separate signal per image
void Sync::init(const VulkanContext &ctx, uint32_t swapchainImageCount)
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // fence starts signaled, first frame doesnt block
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(vkCreateSemaphore(
            ctx.getDevice(),
            &semaphoreInfo,
            nullptr,
            &m_imageAvailableSemaphores[i]
        ));
        VK_CHECK(vkCreateFence(ctx.getDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]));
    }

    // resize first, vector[i] on empty is UB
    m_renderFinishedSemaphores.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; i++)
    {
        VK_CHECK(vkCreateSemaphore(
            ctx.getDevice(),
            &semaphoreInfo,
            nullptr,
            &m_renderFinishedSemaphores[i]
        ));
    }
}

// destroy all sync objects
void Sync::destroy(const VulkanContext &ctx)
{
    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(ctx.getDevice(), m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(ctx.getDevice(), m_inFlightFences[i], nullptr);
    }

    for (auto sem : m_renderFinishedSemaphores)
    {
        vkDestroySemaphore(ctx.getDevice(), sem, nullptr);
    }
    m_renderFinishedSemaphores.clear();
}

} // namespace rhi
