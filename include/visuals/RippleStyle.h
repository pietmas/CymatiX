#pragma once

#include <app/Config.h>
#include <app/SharedTypes.h>
#include <palette/IPalette.h>
#include <rhi/VulkanDeps.h>
#include <visuals/IVisualStyle.h>
#include <visuals/OnsetDetector.h>

#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>

namespace visuals
{

class RippleStyle : public IVisualStyle
{
  public:
    RippleStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette);
    ~RippleStyle() override;

    RippleStyle(const RippleStyle &) = delete;
    RippleStyle &operator=(const RippleStyle &) = delete;
    RippleStyle(RippleStyle &&) = delete;
    RippleStyle &operator=(RippleStyle &&) = delete;

    void update(const float *magnitudes, uint32_t count, float deltaTime) override;
    void render(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void onResize(vk::Extent2D newExtent) override;

  private:
    struct RippleSource
    {
        float cx, cy, frequency, spawnTime;
    };

    // std140: vec4[16] + int + float[3] = 256 + 4 + 12 = 272 bytes
    struct RippleSourcesUBO
    {
        glm::vec4 sources[16]; // x=cx, y=cy, z=frequency, w=spawnTime
        int activeCount;
        float pad[3];
    };

    struct PushConstants
    {
        float time;
        int activeCount;
        float pad[2];
    };

    void spawnSource(int band, float energy);
    void evictOldestSource();

    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(vk::Format colorFormat);
    void createUBOBuffers();
    void createDescriptorSets(const palette::IPalette &palette);

    static constexpr int MAX_SOURCES = 16;

    RippleSource m_sources[MAX_SOURCES]{};
    int m_activeCount = 0;
    float m_time = 0.0f;

    // fixed spawn positions per band in UV space [-1,1]
    static const glm::vec2 BAND_POS[4];

    OnsetDetector m_onset;
    SpectrumUBOData m_pendingSpectrum{};

    rhi::VulkanDeps m_deps;
    vk::Extent2D m_extent;

    vk::raii::DescriptorSetLayout m_descriptorSetLayout{nullptr};
    vk::raii::DescriptorPool m_descriptorPool{nullptr};
    vk::raii::PipelineLayout m_pipelineLayout{nullptr};
    vk::raii::Pipeline m_pipeline{nullptr};

    // per-frame spectrum UBOs (binding=0)
    std::vector<vk::raii::Buffer> m_spectrumUBOBuffers;
    std::vector<vk::raii::DeviceMemory> m_spectrumUBOMemory;
    void *m_spectrumMapped[Config::MAX_FRAMES_IN_FLIGHT]{};

    // per-frame palette UBOs (binding=1)
    std::vector<vk::raii::Buffer> m_paletteUBOBuffers;
    std::vector<vk::raii::DeviceMemory> m_paletteUBOMemory;
    void *m_paletteMapped[Config::MAX_FRAMES_IN_FLIGHT]{};

    // per-frame ripple sources UBOs (binding=2)
    std::vector<vk::raii::Buffer> m_sourceUBOBuffers;
    std::vector<vk::raii::DeviceMemory> m_sourceUBOMemory;
    void *m_sourceMapped[Config::MAX_FRAMES_IN_FLIGHT]{};

    std::vector<vk::raii::DescriptorSet> m_descriptorSets;
};

} // namespace visuals
