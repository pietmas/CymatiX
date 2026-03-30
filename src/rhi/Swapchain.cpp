#include <GLFW/glfw3.h>
#include <rhi/Swapchain.h>
#include <rhi/VulkanContext.h>

#include <algorithm>
#include <limits>

namespace rhi
{

// init swapchain, create image views
void Swapchain::init(const VulkanContext &ctx, GLFWwindow *window)
{
    m_window = window;
    createSwapchain(ctx);
    createImageViews(ctx);
}

// destroy raii objects
void Swapchain::destroy(const VulkanContext &ctx)
{
    (void)ctx;
    cleanupSwapchain();
}

// tear down + rebuild swapchain (on resize)
void Swapchain::recreate(const VulkanContext &ctx)
{
    // wait while minimized
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    ctx.getDevice().waitIdle();

    cleanupSwapchain();
    createSwapchain(ctx);
    createImageViews(ctx);

    if (m_renderPass)
    {
        createFramebuffers(ctx, m_renderPass);
    }
}

// create VkSwapchainKHR
void Swapchain::createSwapchain(const VulkanContext &ctx)
{
    SwapchainSupportDetails support = ctx.querySwapchainSupport(ctx.getPhysicalDevice());

    vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    vk::Extent2D extent = chooseSwapExtent(support.capabilities);

    // one extra image so driver doesnt block us
    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo{};
    createInfo.surface = ctx.getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    const QueueFamilyIndices &indices = ctx.getQueueFamilyIndices();
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = vk::True;
    createInfo.oldSwapchain = nullptr;

    m_swapchain = ctx.getDevice().createSwapchainKHR(createInfo);
    m_images = m_swapchain.getImages();
    m_imageFormat = surfaceFormat.format;
    m_extent = extent;
}

// VkImageView per swapchain image
void Swapchain::createImageViews(const VulkanContext &ctx)
{
    m_imageViews.clear();
    m_imageViews.reserve(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++)
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = m_images[i];
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_imageFormat;
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        m_imageViews.push_back(ctx.getDevice().createImageView(viewInfo));
    }
}

// one framebuffer per image view
void Swapchain::createFramebuffers(const VulkanContext &ctx, vk::RenderPass renderPass)
{
    m_renderPass = renderPass;
    m_framebuffers.clear();
    m_framebufferHandles.clear();
    m_framebuffers.reserve(m_imageViews.size());
    m_framebufferHandles.reserve(m_imageViews.size());

    for (size_t i = 0; i < m_imageViews.size(); i++)
    {
        vk::ImageView attachments[] = {*m_imageViews[i]};

        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_extent.width;
        fbInfo.height = m_extent.height;
        fbInfo.layers = 1;

        m_framebuffers.push_back(ctx.getDevice().createFramebuffer(fbInfo));
        m_framebufferHandles.push_back(*m_framebuffers.back());
    }
}

// clear raii objects -- their destructors call the Vulkan destroy funcs
void Swapchain::cleanupSwapchain()
{
    m_framebuffers.clear();
    m_framebufferHandles.clear();
    m_imageViews.clear();
    m_swapchain = nullptr;
}

// prefer SRGB+nonlinear, fallback to first
vk::SurfaceFormatKHR
Swapchain::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &available) const
{
    for (const auto &fmt : available)
    {
        if (fmt.format == vk::Format::eB8G8R8A8Srgb &&
            fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            return fmt;
        }
    }
    return available[0];
}

// prefer MAILBOX, fallback to FIFO
vk::PresentModeKHR Swapchain::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &available
) const
{
    for (const auto &mode : available)
    {
        if (mode == vk::PresentModeKHR::eMailbox)
        {
            printf("[Swapchain] present mode: MAILBOX (uncapped, no tearing)\n");
            return mode;
        }
    }
    printf("[Swapchain] present mode: FIFO (vsync, capped to monitor refresh)\n");
    return vk::PresentModeKHR::eFifo;
}

// use driver extent if available, else clamp to window size
vk::Extent2D Swapchain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    vk::Extent2D actual = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    actual.width = std::clamp(
        actual.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );
    actual.height = std::clamp(
        actual.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );

    return actual;
}

} // namespace rhi
