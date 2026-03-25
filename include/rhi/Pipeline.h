#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace rhi
{

class VulkanContext;
class Swapchain;
class RenderPass;

class Pipeline
{
  public:
    Pipeline() = default;
    ~Pipeline() = default;

    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;
    Pipeline(Pipeline &&) = delete;
    Pipeline &operator=(Pipeline &&) = delete;

    void init(const VulkanContext &ctx, const Swapchain &swapchain, const RenderPass &renderPass);
    void destroy(const VulkanContext &ctx);

    VkPipeline get() const
    {
        return m_pipeline;
    }
    VkPipelineLayout getLayout() const
    {
        return m_pipelineLayout;
    }

  private:
    VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &code) const;

    static std::vector<char> readFile(const std::string &path);

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace rhi
