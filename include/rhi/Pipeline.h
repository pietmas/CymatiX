#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

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

    vk::Pipeline get() const
    {
        return *m_pipeline;
    }
    vk::PipelineLayout getLayout() const
    {
        return *m_pipelineLayout;
    }

  private:
    vk::raii::ShaderModule createShaderModule(
        const vk::raii::Device &device,
        const std::vector<char> &code
    ) const;

    static std::vector<char> readFile(const std::string &path);

    vk::raii::PipelineLayout m_pipelineLayout{nullptr};
    vk::raii::Pipeline m_pipeline{nullptr};
};

} // namespace rhi
