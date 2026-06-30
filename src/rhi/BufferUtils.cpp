#include <rhi/BufferUtils.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rhi
{

// find a memory type index matching the type filter and required property flags
static uint32_t
findMemoryType(vk::PhysicalDevice phys, uint32_t typeFilter, vk::MemoryPropertyFlags props)
{
    vk::PhysicalDeviceMemoryProperties memProps = phys.getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    fprintf(stderr, "[BufferUtils] failed to find suitible memory type\n");
    abort();
}

// create buffer, allocate matching memory, bind, return both
AllocatedBuffer createBuffer(
    const rhi::VulkanDeps &deps,
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties
)
{
    vk::BufferCreateInfo bi{};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = vk::SharingMode::eExclusive;

    vk::raii::Buffer buffer(*deps.device, bi);

    vk::MemoryRequirements req = buffer.getMemoryRequirements();

    vk::MemoryAllocateInfo ai{};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(deps.physicalDevice, req.memoryTypeBits, properties);

    vk::raii::DeviceMemory memory(*deps.device, ai);
    buffer.bindMemory(*memory, 0);

    return {std::move(buffer), std::move(memory)};
}

// upload host data into a device-local buffer via a transient staging buffer
void uploadToDeviceLocal(
    const rhi::VulkanDeps &deps,
    vk::CommandPool cmdPool,
    vk::Queue queue,
    vk::Buffer dst,
    const void *src,
    vk::DeviceSize bytes
)
{
    // staging buffer is host-visible so we can memcpy into it
    AllocatedBuffer staging = createBuffer(
        deps,
        bytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );

    void *mapped = staging.memory.mapMemory(0, bytes);
    memcpy(mapped, src, bytes);
    staging.memory.unmapMemory();

    // one-time command buffer: record copy, submit, wait
    vk::CommandBufferAllocateInfo cbi{};
    cbi.commandPool = cmdPool;
    cbi.level = vk::CommandBufferLevel::ePrimary;
    cbi.commandBufferCount = 1;

    vk::raii::CommandBuffers cmds(*deps.device, cbi);
    vk::raii::CommandBuffer &cmd = cmds.front();

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmd.begin(beginInfo);

    vk::BufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = bytes;
    cmd.copyBuffer(staging.buffer, dst, copyRegion);
    cmd.end();

    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    VkCommandBuffer raw = *cmd;
    submit.pCommandBuffers = (vk::CommandBuffer *)&raw;
    queue.submit(submit);
    queue.waitIdle(); // staging dtor at scope exit needs copy fully done
}

} // namespace rhi
