#pragma once

#include <vector>
#include <vulkan/vulkan_raii.hpp>

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

    // recreate after resize or out-of-date
    void recreate(const VulkanContext &ctx);

    vk::SwapchainKHR getSwapchain() const
    {
        return *m_swapchain;
    }
    const vk::raii::SwapchainKHR &getSwapchainRaii() const
    {
        return m_swapchain;
    }
    vk::Format getImageFormat() const
    {
        return m_imageFormat;
    }
    vk::Extent2D getExtent() const
    {
        return m_extent;
    }
    const std::vector<vk::Image> &getImages() const
    {
        return m_images;
    }
    const std::vector<vk::ImageView> &getImageViews() const
    {
        return m_imageViewHandles;
    }
    uint32_t getImageCount() const
    {
        return (uint32_t)m_images.size();
    }

  private:
    void createSwapchain(const VulkanContext &ctx);
    void createImageViews(const VulkanContext &ctx);
    void cleanupSwapchain();

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &available
    ) const;
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &available
    ) const;
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities) const;

    vk::raii::SwapchainKHR m_swapchain{nullptr};
    vk::Format m_imageFormat = vk::Format::eUndefined;
    vk::Extent2D m_extent = {0, 0};

    std::vector<vk::Image> m_images; // owned by the swapchain, not us
    std::vector<vk::raii::ImageView> m_imageViews;
    std::vector<vk::ImageView> m_imageViewHandles; // raw handles mirroring m_imageViews

    GLFWwindow *m_window = nullptr; // not owned
};

} // namespace rhi
