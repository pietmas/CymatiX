#pragma once

#include <app/Config.h>
#include <app/SharedTypes.h>
#include <palette/IPalette.h>
#include <rhi/VulkanDeps.h>
#include <visuals/IVisualStyle.h>

#include <cstdint>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include <glm/glm.hpp>

namespace visuals
{

// CPU-side particle state, simulated each frame
struct Particle
{
    glm::vec2 pos;
    glm::vec2 vel;
    float age;    // 0.0 = just born, 1.0 = dead
    float energy; // band energy at birth, drives size/color
};

// GPU vertex layout, mirrors compute Particle struct. 32 bytes so std430 stride matches
struct ParticleVertex
{
    float pos[2];
    float vel[2];
    float age;
    float energy; // birth loudness -> brightness + size
    float freq;   // birth freq 0..1 -> palette hue
    float pad;    // pad to 32 bytes (std430)
};

class ParticleStyle : public IVisualStyle
{
  public:
    ParticleStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette);
    ~ParticleStyle() override;

    ParticleStyle(const ParticleStyle &) = delete;
    ParticleStyle &operator=(const ParticleStyle &) = delete;
    ParticleStyle(ParticleStyle &&) = delete;
    ParticleStyle &operator=(ParticleStyle &&) = delete;

    void update(const float *magnitudes, uint32_t count, float deltaTime) override;
    void render(vk::CommandBuffer cmd, uint32_t frameIndex) override;
    void onResize(vk::Extent2D newExtent) override;
    void computeDispatch(vk::CommandBuffer cmd, uint32_t frameIndex) override;

  private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(vk::Format colorFormat);
    void createPaletteBuffers();
    void createParticleBuffer();
    void createDescriptorSets(const palette::IPalette &palette);
    void createSpectrumBuffers();
    void createComputeDescriptors();
    void createComputePipeline();

    struct PushConstants
    {
        int largePoints; // 1 if supported, else 0
        float pad[3];
    };

    // compute push constants, 24 bytes, COMPUTE stage. bass/loudness precomputed CPU side
    struct ComputePush
    {
        float dt;
        float time;
        uint32_t frameSeed;
        uint32_t particleCount;
        float bass;     // low-band mean energy, drives the swirl strength
        float loudness; // overall mean energy, drives the outward push
    };

    static constexpr int MAX_PARTICLES = 10000;

    // FFT bins uploaded to the compute shader each frame
    static constexpr int SPECTRUM_COUNT = 1024;

    bool m_largePointsSupported = false;

    rhi::VulkanDeps m_deps;
    vk::Extent2D m_extent;

    vk::raii::DescriptorSetLayout m_descriptorSetLayout{nullptr};
    vk::raii::DescriptorPool m_descriptorPool{nullptr};
    vk::raii::PipelineLayout m_pipelineLayout{nullptr};
    vk::raii::Pipeline m_pipeline{nullptr};

    // per-frame palette UBOs (binding=0)
    std::vector<vk::raii::Buffer> m_paletteUBOBuffers;
    std::vector<vk::raii::DeviceMemory> m_paletteUBOMemory;
    void *m_paletteMapped[Config::MAX_FRAMES_IN_FLIGHT]{};

    // single DEVICE_LOCAL particle buffer, seeded once via staging (GPU owns it after)
    vk::raii::Buffer m_particleBuffer{nullptr};
    vk::raii::DeviceMemory m_particleMemory{nullptr};

    std::vector<vk::raii::DescriptorSet> m_descriptorSets;

    // per-frame spectrum SSBOs, host-visible and permanently mapped
    std::vector<vk::raii::Buffer> m_spectrumBuffers;
    std::vector<vk::raii::DeviceMemory> m_spectrumMemory;
    void *m_spectrumMapped[Config::MAX_FRAMES_IN_FLIGHT]{};

    // latest magnitudes stashed in update(), copied into the frame's SSBO in computeDispatch()
    float m_pendingSpectrum[SPECTRUM_COUNT]{};

    // compute pipeline + its own descriptor layout/pool/sets (separate from graphics)
    vk::raii::DescriptorSetLayout m_computeDSLayout{nullptr};
    vk::raii::DescriptorPool m_computeDescriptorPool{nullptr};
    vk::raii::PipelineLayout m_computePipelineLayout{nullptr};
    vk::raii::Pipeline m_computePipeline{nullptr};
    std::vector<vk::raii::DescriptorSet> m_computeDescriptorSets;

    float m_lastDt = 0.0f;
    float m_time = 0.0f;
    uint32_t m_frameSeed = 0;

    // precomputed each frame from spectrum, fed to live force
    float m_bass = 0.0f;
    float m_loudness = 0.0f;
};

} // namespace visuals
