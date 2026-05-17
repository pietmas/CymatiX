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

class ChladniStyle : public IVisualStyle
{
  public:
    ChladniStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette);
    ~ChladniStyle() override;

    ChladniStyle(const ChladniStyle &) = delete;
    ChladniStyle &operator=(const ChladniStyle &) = delete;
    ChladniStyle(ChladniStyle &&) = delete;
    ChladniStyle &operator=(ChladniStyle &&) = delete;

    void update(const float *magnitudes, uint32_t count, float deltaTime) override;
    void render(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void onResize(vk::Extent2D newExtent) override;

  private:
    struct PushConstants
    {
        float m;
        float n;
        float theta;
        float lineWidth;
        float time;
        float signal;
        float pad[2]; // to 32 bytes
    };

    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(vk::Format colorFormat);
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

    // mode state
    float m_currentM = 3.0f;
    float m_currentN = 5.0f;
    float m_targetM = 3.0f;
    float m_targetN = 5.0f;
    float m_lineWidth = 0.04f;
    float m_time = 0.0f;
    float m_theta = 0.0f;
    float m_targetTheta = 0.0f;

    // auto-rate: spectral flux tracking
    std::vector<float> m_prevMagnitudes;
    float m_peakFlux = 0.0f;

    // auto-gain: rolling peak RMS
    float m_peakRMS = 0.0f;

    SpectrumUBOData m_pendingSpectrum{};
};

} // namespace visuals
