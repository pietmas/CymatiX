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

// fill debug messenger create info
static void populateDebugMessengerCreateInfo(vk::DebugUtilsMessengerCreateInfoEXT &createInfo)
{
    createInfo = vk::DebugUtilsMessengerCreateInfoEXT{};
    createInfo.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                 vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                             vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
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

// destroy in reverse order, raii handles destruction when reset to nullptr
void VulkanContext::destroy()
{
    m_presentQueue = nullptr;
    m_graphicsQueue = nullptr;
    m_device = nullptr;
    m_physicalDevice = nullptr;
    m_surface = nullptr;
    m_debugMessenger = nullptr;
    m_instance = nullptr;
}

// VkInstance + validation layers + required extensions
void VulkanContext::createInstance()
{
    if (m_enableValidationLayers && !checkValidationLayerSupport())
    {
        fprintf(stderr, "validation layers requested but not available\n");
        abort();
    }

    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "CymatiX";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto extensions = getRequiredExtensions();

    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
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

    m_instance = vk::raii::Instance(m_raiiContext, createInfo);
}

// debug messenger routes validation output to console
void VulkanContext::setupDebugMessenger()
{
    if (!m_enableValidationLayers)
    {
        return;
    }

    vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(createInfo);
}

// GLFW creates window surface, wrap result in raii handle
void VulkanContext::createSurface(GLFWwindow *window)
{
    VkSurfaceKHR rawSurface;
    VkResult res =
        glfwCreateWindowSurface(static_cast<VkInstance>(*m_instance), window, nullptr, &rawSurface);
    if (res != VK_SUCCESS)
    {
        fprintf(stderr, "failed to create window surface\n");
        abort();
    }
    m_surface = vk::raii::SurfaceKHR(m_instance, rawSurface);
}

// score device for selection
int VulkanContext::rateDeviceSuitability(vk::PhysicalDevice device) const
{
    vk::PhysicalDeviceProperties props = device.getProperties();

    int score = 0;

    // discrete > integrated
    if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
    {
        score += 1000;
    }
    else if (props.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
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
    vk::raii::PhysicalDevices devices(m_instance);

    if (devices.empty())
    {
        fprintf(stderr, "no GPUs with Vulkan support found\n");
        abort();
    }

    // score -> index into devices
    std::map<int, uint32_t> candidates;

    for (uint32_t i = 0; i < (uint32_t)devices.size(); i++)
    {
        vk::PhysicalDevice dev = *devices[i];
        if (!isDeviceSuitable(dev))
        {
            continue;
        }
        int score = rateDeviceSuitability(dev);

        vk::PhysicalDeviceProperties props = dev.getProperties();
        fprintf(stdout, "[GPU] candidate: %s (score %d)\n", props.deviceName.data(), score);

        candidates.insert(std::make_pair(score, i));
    }

    if (candidates.empty() || candidates.rbegin()->first <= 0)
    {
        fprintf(stderr, "failed to find a suitable GPU\n");
        abort();
    }

    uint32_t bestIdx = candidates.rbegin()->second;
    m_physicalDevice = std::move(devices[bestIdx]);

    vk::PhysicalDeviceProperties props = m_physicalDevice.getProperties();
    fprintf(stdout, "[GPU] selected: %s\n", props.deviceName.data());
}

// logical device + queue handles
void VulkanContext::createLogicalDevice()
{
    m_queueFamilyIndices = findQueueFamilies(*m_physicalDevice);

    std::set<uint32_t> uniqueQueueFamilies = {
        m_queueFamilyIndices.graphicsFamily.value(),
        m_queueFamilyIndices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;

    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // enable 1.3 core features via pNext chain; pEnabledFeatures must be null when using Features2
    vk::PhysicalDeviceVulkan13Features vk13Features{};
    vk13Features.dynamicRendering = vk::True;
    vk13Features.synchronization2 = vk::True;

    vk::PhysicalDeviceFeatures2 features2{};
    features2.pNext = &vk13Features;

    vk::DeviceCreateInfo createInfo{};
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = nullptr;
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

    m_device = m_physicalDevice.createDevice(createInfo);
    m_graphicsQueue = m_device.getQueue(m_queueFamilyIndices.graphicsFamily.value(), 0);
    m_presentQueue = m_device.getQueue(m_queueFamilyIndices.presentFamily.value(), 0);
}

// check queue families, extensions, swapchain support
bool VulkanContext::isDeviceSuitable(vk::PhysicalDevice device) const
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
bool VulkanContext::checkDeviceExtensionSupport(vk::PhysicalDevice device) const
{
    auto available = device.enumerateDeviceExtensionProperties();

    std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto &ext : available)
    {
        required.erase(ext.extensionName.data());
    }
    return required.empty();
}

// find graphics + present queue family indices
QueueFamilyIndices VulkanContext::findQueueFamilies(vk::PhysicalDevice device) const
{
    QueueFamilyIndices indices;

    auto queueFamilies = device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < (uint32_t)queueFamilies.size(); i++)
    {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics)
        {
            indices.graphicsFamily = i;
        }

        if (device.getSurfaceSupportKHR(i, *m_surface))
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
    auto availableLayers = m_raiiContext.enumerateInstanceLayerProperties();

    for (const char *layerName : m_validationLayers)
    {
        bool found = false;
        for (const auto &layerProps : availableLayers)
        {
            if (strcmp(layerName, layerProps.layerName.data()) == 0)
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

#ifndef NDEBUG
    printf("[VulkanContext] enabled instance extensions (%zu):\n", extensions.size());
    for (const char *ext : extensions)
        printf("  %s\n", ext);
    printf("[VulkanContext] enabled device extensions (%zu):\n", m_deviceExtensions.size());
    for (const char *ext : m_deviceExtensions)
        printf("  %s\n", ext);
    printf("[VulkanContext] enabled validation layers (%zu):\n", m_validationLayers.size());
    for (const char *layer : m_validationLayers)
        printf("  %s\n", layer);
#endif

    return extensions;
}

// query surface formats + present modes
SwapchainSupportDetails VulkanContext::querySwapchainSupport(vk::PhysicalDevice device) const
{
    SwapchainSupportDetails details;

    details.capabilities = device.getSurfaceCapabilitiesKHR(*m_surface);
    details.formats = device.getSurfaceFormatsKHR(*m_surface);
    details.presentModes = device.getSurfacePresentModesKHR(*m_surface);

    return details;
}

// print validation msg to stderr; vk::False = dont abort
VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanContext::debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    vk::DebugUtilsMessageTypeFlagsEXT messageType,
    const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData
)
{
    (void)messageSeverity;
    (void)messageType;
    (void)pUserData;

    fprintf(stderr, "[Validation] %s\n", pCallbackData->pMessage);
    return vk::False;
}

} // namespace rhi
