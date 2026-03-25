#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <rhi/VulkanContext.h>

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rhi
{

// not in core load manually
static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger
)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT
    )vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks *pAllocator
)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT
    )vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

// fill debug messenger create info
static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo)
{
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = VulkanContext::debugCallback;
}

// init steps in order
void VulkanContext::init(GLFWwindow *window)
{
    createInstance();
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
}

// destroy in reverse order
void VulkanContext::destroy()
{
    vkDestroyDevice(m_device, nullptr);

    if (m_enableValidationLayers)
    {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

// VkInstance + validation layers + required extensions
void VulkanContext::createInstance()
{
    if (m_enableValidationLayers && !checkValidationLayerSupport())
    {
        fprintf(stderr, "validation layers requested but not available\n");
        abort();
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "CymatiX";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    auto extensions = getRequiredExtensions();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = (uint32_t)m_validationLayers.size();
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = &debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
}

// debug messenger routes validation output to console
void VulkanContext::setupDebugMessenger()
{
    if (!m_enableValidationLayers)
    {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    VK_CHECK(CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger));
}

// GLFW creates window surface
void VulkanContext::createSurface(GLFWwindow *window)
{
    VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface));
}

// score device for selection
int VulkanContext::rateDeviceSuitability(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;

    // discrete > integrated
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
        score += 1000;
    }
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
    {
        score += 100;
    }
    else
    {
        score += 10;
    }
    score += (int)props.limits.maxImageDimension2D;

    return score;
}

// rate all devices, pick highest score
void VulkanContext::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        fprintf(stderr, "no GPUs with Vulkan support found\n");
        abort();
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // score -> device
    std::map<int, VkPhysicalDevice> candidates;

    for (const auto &device : devices)
    {
        if (!isDeviceSuitable(device))
        {
            continue;
        }
        int score = rateDeviceSuitability(device);

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        fprintf(stdout, "[GPU] candidate: %s (score %d)\n", props.deviceName, score);

        candidates.insert(std::make_pair(score, device));
    }

    if (candidates.empty() || candidates.rbegin()->first <= 0)
    {
        fprintf(stderr, "failed to find a suitable GPU\n");
        abort();
    }

    m_physicalDevice = candidates.rbegin()->second;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    fprintf(stdout, "[GPU] selected: %s\n", props.deviceName);
}

// logical device + queue handles
void VulkanContext::createLogicalDevice()
{
    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice);

    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilyIndices.graphicsFamily.value(),
        m_queueFamilyIndices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = (uint32_t)m_deviceExtensions.size();
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (m_enableValidationLayers)
    {
        createInfo.enabledLayerCount = (uint32_t)m_validationLayers.size();
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));

    vkGetDeviceQueue(m_device, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);
}

// check queue families, extensions, swapchain support
bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);
    bool swapchainAdequate = false;

    if (extensionsSupported)
    {
        SwapchainSupportDetails details = querySwapchainSupport(device);
        swapchainAdequate = !details.formats.empty() && !details.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapchainAdequate;
}

// check VK_KHR_swapchain support
bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) const
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto &ext : available)
    {
        required.erase(ext.extensionName);
    }
    return required.empty();
}

// find graphics + present queue family indices
QueueFamilyIndices VulkanContext::findQueueFamilies(VkPhysicalDevice device) const
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < (uint32_t)queueFamilies.size(); i++)
    {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport)
        {
            indices.presentFamily = i;
        }
        if (indices.isComplete())
        {
            break;
        }
    }

    return indices;
}

// check requested validation layers are available
bool VulkanContext::checkValidationLayerSupport() const
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName : m_validationLayers)
    {
        bool found = false;
        for (const auto &layerProps : availableLayers)
        {
            if (strcmp(layerName, layerProps.layerName) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }

    return true;
}

// GLFW required extensions + debug utils if validation on
std::vector<const char *> VulkanContext::getRequiredExtensions() const
{
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (m_enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

// query surface formats + present modes
SwapchainSupportDetails VulkanContext::querySwapchainSupport(VkPhysicalDevice device) const
{
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device,
            m_surface,
            &formatCount,
            details.formats.data()
        );
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device,
            m_surface,
            &presentModeCount,
            details.presentModes.data()
        );
    }

    return details;
}

// print validation msg to stderr; VK_FALSE = dont abort
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData
)
{
    (void)messageSeverity;
    (void)messageType;
    (void)pUserData;

    fprintf(stderr, "[Validation] %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

} // namespace rhi
