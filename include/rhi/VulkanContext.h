#pragma once

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

// exceptions throw on failure; VK_CHECK just evaluates (kept for callsites that
// havent been cleaned up yet and for the GLFW surface raw-C check)
#define VK_CHECK(call) (void)(call)

struct GLFWwindow;

namespace rhi
{

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapchainSupportDetails
{
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes; // FIFO, MAILBOX, IMMEDIATE
};

class VulkanContext
{
  public:
    VulkanContext() = default;
    ~VulkanContext() = default;

    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;
    VulkanContext(VulkanContext &&) = delete;
    VulkanContext &operator=(VulkanContext &&) = delete;

    void init(GLFWwindow *window);
    void destroy();

    // return const ref to raii device -- callers use it to create raii resources
    const vk::raii::Device &getDevice() const
    {
        return m_device;
    }
    // raw physical device handle -- sufficient for getMemoryProperties etc.
    vk::PhysicalDevice getPhysicalDevice() const
    {
        return *m_physicalDevice;
    }
    vk::SurfaceKHR getSurface() const
    {
        return *m_surface;
    }
    vk::Queue getGraphicsQueue() const
    {
        return *m_graphicsQueue;
    }
    vk::Queue getPresentQueue() const
    {
        return *m_presentQueue;
    }
    const QueueFamilyIndices &getQueueFamilyIndices() const
    {
        return m_queueFamilyIndices;
    }

    SwapchainSupportDetails querySwapchainSupport(vk::PhysicalDevice device) const;

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        vk::DebugUtilsMessageTypeFlagsEXT messageType,
        const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData
    );

  private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow *window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    bool isDeviceSuitable(vk::PhysicalDevice device) const;
    int rateDeviceSuitability(vk::PhysicalDevice device) const;
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device) const;
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device) const;
    bool checkValidationLayerSupport() const;
    std::vector<const char *> getRequiredExtensions() const;

    // m_raiiContext must be first -- it owns the dynamic loader used by all raii objects
    vk::raii::Context m_raiiContext;
    vk::raii::Instance m_instance{nullptr};
    vk::raii::DebugUtilsMessengerEXT m_debugMessenger{nullptr};
    vk::raii::SurfaceKHR m_surface{nullptr};
    vk::raii::PhysicalDevice m_physicalDevice{nullptr};
    vk::raii::Device m_device{nullptr};
    vk::raii::Queue m_graphicsQueue{nullptr};
    vk::raii::Queue m_presentQueue{nullptr};

    QueueFamilyIndices m_queueFamilyIndices;

    const std::vector<const char *> m_deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
    const bool m_enableValidationLayers = false;
#else
    const bool m_enableValidationLayers = true;
#endif

    const std::vector<const char *> m_validationLayers = {"VK_LAYER_KHRONOS_validation"};
};

} // namespace rhi
