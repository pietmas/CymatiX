#include <rhi/Sync.h>
#include <rhi/VulkanContext.h>

namespace rhi
{

// create semaphores + fences
// imageAvailable + fences: one per frame-in-flight
// renderFinished: one per swapchain imag, presentation engine needs separate signal per image
void Sync::init(const VulkanContext &ctx, uint32_t swapchainImageCount)
{
    const vk::raii::Device &device = ctx.getDevice();

    vk::SemaphoreCreateInfo semaphoreInfo{};

    // fence starts signaled, first frame doesnt block
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_imageAvailableSemaphores.push_back(device.createSemaphore(semaphoreInfo));
        m_inFlightFences.push_back(device.createFence(fenceInfo));
    }

    for (uint32_t i = 0; i < swapchainImageCount; i++)
    {
        m_renderFinishedSemaphores.push_back(device.createSemaphore(semaphoreInfo));
    }
}

// clear vectors, raii destructors call vkDestroySemaphore / vkDestroyFence
void Sync::destroy(const VulkanContext &ctx)
{
    (void)ctx;
    m_inFlightFences.clear();
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
}

} // namespace rhi
