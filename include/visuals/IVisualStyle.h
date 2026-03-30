#pragma once

#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace visuals
{

class IVisualStyle
{
  public:
    virtual ~IVisualStyle() = default;

    // called once per frame before render(), magnitudes is valid until next call
    virtual void update(const float *magnitudes, uint32_t count, float deltaTime) = 0;

    // record draw commands into cmd for the given frame-in-flight index
    virtual void render(vk::CommandBuffer cmd, uint32_t frameIndex) = 0;

    // called after swapchain recreation
    virtual void onResize(vk::Extent2D newExtent) = 0;
};

} // namespace visuals
