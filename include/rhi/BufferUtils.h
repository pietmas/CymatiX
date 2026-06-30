#pragma once

#include <rhi/VulkanDeps.h>

#include <vulkan/vulkan_raii.hpp>

namespace rhi
{

// raii buffer + its backing memory, returned together so the caller owns both
struct AllocatedBuffer
{
    vk::raii::Buffer buffer{nullptr};
    vk::raii::DeviceMemory memory{nullptr};
};

// create a buffer + bind freshly allocated memory of the requested property flags
AllocatedBuffer createBuffer(
    const rhi::VulkanDeps &deps,
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties
);

// upload bytes of host data into a DEVICE_LOCAL buffer via a transient staging buffer
void uploadToDeviceLocal(
    const rhi::VulkanDeps &deps,
    vk::CommandPool cmdPool,
    vk::Queue queue,
    vk::Buffer dst,
    const void *src,
    vk::DeviceSize bytes
);

} // namespace rhi
