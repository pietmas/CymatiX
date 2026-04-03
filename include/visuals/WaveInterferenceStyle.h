#pragma once

#include <app/Config.h>
#include <app/SharedTypes.h>
#include <palette/IPalette.h>
#include <rhi/VulkanDeps.h>
#include <visuals/IVisualStyle.h>

#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace visuals
{

class WaveInterferenceStyle : public IVisualStyle
{
  public:
    WaveInterferenceStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette);
    ~WaveInterferenceStyle() override;

    WaveInterferenceStyle(const WaveInterferenceStyle &) = delete;
    WaveInterferenceStyle &operator=(const WaveInterferenceStyle &) = delete;
    WaveInterferenceStyle(WaveInterferenceStyle &&) = delete;
    WaveInterferenceStyle &operator=(WaveInterferenceStyle &&) = delete;

    void update(const float *magnitudes, uint32_t count, float deltaTime) override;
    void render(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void onResize(vk::Extent2D newExtent) override;

  private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(vk::RenderPass renderPass);
    void createUBOBuffers();
    void createDescriptorSets(const palette::IPalette &palette);

    rhi::VulkanDeps m_deps;
    vk::Extent2D m_extent;

    vk::raii::DescriptorSetLayout m_descriptorSetLayout{nullptr};
    vk::raii::DescriptorPool m_descriptorPool{nullptr};
    vk::raii::PipelineLayout m_pipelineLayout{nullptr};
    vk::raii::Pipeline m_pipeline{nullptr};

    // per-frame spectrum UBOs (written every frame)
    std::vector<vk::raii::Buffer> m_spectrumUBOBuffers;
    std::vector<vk::raii::DeviceMemory> m_spectrumUBOMemory;
    void *m_spectrumMapped[Config::MAX_FRAMES_IN_FLIGHT]{};

    // per-frame palette UBOs (written once at init)
    std::vector<vk::raii::Buffer> m_paletteUBOBuffers;
    std::vector<vk::raii::DeviceMemory> m_paletteUBOMemory;
    void *m_paletteMapped[Config::MAX_FRAMES_IN_FLIGHT]{};

    std::vector<vk::raii::DescriptorSet> m_descriptorSets;

    SpectrumUBOData m_pendingSpectrum{};
    float m_time = 0.0f;
};

} // namespace visuals
