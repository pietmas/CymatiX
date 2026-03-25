#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace rhi
{

class VulkanContext;

class Swapchain
{
  public:
    Swapchain() = default;
    ~Swapchain() = default;

    Swapchain(const Swapchain &) = delete;
    Swapchain &operator=(const Swapchain &) = delete;
    Swapchain(Swapchain &&) = delete;
    Swapchain &operator=(Swapchain &&) = delete;

    void init(const VulkanContext &ctx, GLFWwindow *window);
    void destroy(const VulkanContext &ctx);

    // recreate after resize or outofdate error
    void recreate(const VulkanContext &ctx);

    VkSwapchainKHR getSwapchain() const
    {
        return m_swapchain;
    }
    VkFormat getImageFormat() const
    {
        return m_imageFormat;
    }
    VkExtent2D getExtent() const
    {
        return m_extent;
    }
    const std::vector<VkFramebuffer> &getFramebuffers() const
    {
        return m_framebuffers;
    }
    uint32_t getImageCount() const
    {
        return (uint32_t)m_images.size();
    }

    // framebuffers are created after the render pass exists, so we do it
    // separatly
    void createFramebuffers(const VulkanContext &ctx, VkRenderPass renderPass);

  private:
    void createSwapchain(const VulkanContext &ctx);
    void createImageViews(const VulkanContext &ctx);
    void cleanupSwapchain(const VulkanContext &ctx);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available
    ) const;
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &available) const;
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent = {0, 0};

    std::vector<VkImage> m_images; // owned by the swapchain, not us
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;

    VkRenderPass m_renderPass = VK_NULL_HANDLE; // not owned, just stored for recreate
    GLFWwindow *m_window = nullptr;             // not owned
};

} // namespace rhi
