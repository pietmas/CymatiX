#pragma once

#include <vulkan/vulkan_raii.hpp>

namespace rhi
{

// lightweight bundle of Vulkan handles passed to style factory lambdas
struct VulkanDeps
{
    const vk::raii::Device *device = nullptr; // not owned
    vk::PhysicalDevice physicalDevice;
    vk::Format colorFormat = vk::Format::eUndefined;
    vk::Extent2D extent;
    vk::CommandPool transientCmdPool = nullptr; // for one-time uploads
    vk::Queue graphicsQueue = nullptr;          // submit target for uploads
};

} // namespace rhi
