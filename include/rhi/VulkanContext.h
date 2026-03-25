#pragma once

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

#define VK_CHECK(call)                                                                             \
    do                                                                                             \
    {                                                                                              \
        VkResult _res = (call);                                                                    \
        if (_res != VK_SUCCESS)                                                                    \
        {                                                                                          \
            fprintf(stderr, "[VK_CHECK] %s returned %d\n", #call, (int)_res);                      \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

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
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes; // FIFO, MAILBOX, IMMEDIATE
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

    VkInstance getInstance() const
    {
        return m_instance;
    }
    VkSurfaceKHR getSurface() const
    {
        return m_surface;
    }
    VkPhysicalDevice getPhysicalDevice() const
    {
        return m_physicalDevice;
    }
    VkDevice getDevice() const
    {
        return m_device;
    }
    VkQueue getGraphicsQueue() const
    {
        return m_graphicsQueue;
    }
    VkQueue getPresentQueue() const
    {
        return m_presentQueue;
    }
    const QueueFamilyIndices &getQueueFamilyIndices() const
    {
        return m_queueFamilyIndices;
    }

    SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device) const;

  private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface(GLFWwindow *window);
    void pickPhysicalDevice();
    void createLogicalDevice();

    bool isDeviceSuitable(VkPhysicalDevice device) const;
    int rateDeviceSuitability(VkPhysicalDevice device) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;

    bool checkValidationLayerSupport() const;
    std::vector<const char *> getRequiredExtensions() const;

  public:
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData
    );

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE; // not owned
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

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
