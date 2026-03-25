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

// destroy framebuffers, image views, swapchain
void Swapchain::destroy(const VulkanContext &ctx)
{
    cleanupSwapchain(ctx);
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

    vkDeviceWaitIdle(ctx.getDevice());

    cleanupSwapchain(ctx);
    createSwapchain(ctx);
    createImageViews(ctx);

    if (m_renderPass != VK_NULL_HANDLE)
    {
        createFramebuffers(ctx, m_renderPass);
    }
}

// create VkSwapchainKHR
void Swapchain::createSwapchain(const VulkanContext &ctx)
{
    SwapchainSupportDetails support = ctx.querySwapchainSupport(ctx.getPhysicalDevice());

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = chooseSwapExtent(support.capabilities);

    // one extra image so driver doesnt block us
    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
    {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = ctx.getSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const QueueFamilyIndices &indices = ctx.getQueueFamilyIndices();
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(ctx.getDevice(), &createInfo, nullptr, &m_swapchain));

    uint32_t count;
    vkGetSwapchainImagesKHR(ctx.getDevice(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(ctx.getDevice(), m_swapchain, &count, m_images.data());

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;
}

// VkImageView per swapchain image
void Swapchain::createImageViews(const VulkanContext &ctx)
{
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_imageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(ctx.getDevice(), &viewInfo, nullptr, &m_imageViews[i]));
    }
}

// one framebuffer per image view
void Swapchain::createFramebuffers(const VulkanContext &ctx, VkRenderPass renderPass)
{
    m_renderPass = renderPass;
    m_framebuffers.resize(m_imageViews.size());

    for (size_t i = 0; i < m_imageViews.size(); i++)
    {
        VkImageView attachments[] = {m_imageViews[i]};

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_extent.width;
        fbInfo.height = m_extent.height;
        fbInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(ctx.getDevice(), &fbInfo, nullptr, &m_framebuffers[i]));
    }
}

// destroy framebuffers, image views, then swapchain
void Swapchain::cleanupSwapchain(const VulkanContext &ctx)
{
    for (auto fb : m_framebuffers)
    {
        vkDestroyFramebuffer(ctx.getDevice(), fb, nullptr);
    }
    m_framebuffers.clear();

    for (auto iv : m_imageViews)
    {
        vkDestroyImageView(ctx.getDevice(), iv, nullptr);
    }
    m_imageViews.clear();

    vkDestroySwapchainKHR(ctx.getDevice(), m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

// prefer SRGB+nonlinear, fallback to first
VkSurfaceFormatKHR
Swapchain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available) const
{
    for (const auto &fmt : available)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return fmt;
        }
    }
    return available[0];
}

// prefer MAILBOX, fallback to FIFO
VkPresentModeKHR Swapchain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &available
) const
{
    for (const auto &mode : available)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            printf("[Swapchain] present mode: MAILBOX (uncapped, no tearing)\n");
            return mode;
        }
    }
    printf("[Swapchain] present mode: FIFO (vsync, capped to monitor refresh)\n");
    return VK_PRESENT_MODE_FIFO_KHR;
}

// use driver extent if available, else clamp to window size
VkExtent2D Swapchain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    VkExtent2D actual = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

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
