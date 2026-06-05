#pragma once

#include <app/Config.h>
#include <app/SharedTypes.h>
#include <palette/IPalette.h>
#include <rhi/VulkanDeps.h>
#include <visuals/IVisualStyle.h>
#include <visuals/OnsetDetector.h>

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

// GPU-side vertex layout, mirrors Particle exactly so we can memcpy directly
struct ParticleVertex
{
    float pos[2];
    float vel[2];
    float age;
    float energy;
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

  private:
    void simulateParticles(float dt);
    void uploadParticles();
    void spawnParticles(int band, float energy);

    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(vk::Format colorFormat);
    void createPaletteBuffers();
    void createParticleBuffer();
    void createDescriptorSets(const palette::IPalette &palette);

    struct PushConstants
    {
        int largePoints; // 1 if supported, else 0
        float pad[3];
    };

    static constexpr int MAX_PARTICLES = 10000;
    static constexpr int SPAWN_BATCH = 8; // particles per onset event

    // fixed spawn positions per onset band (same as RippleStyle)
    static const glm::vec2 BAND_POS[4];

    std::vector<Particle> m_particles;
    uint32_t m_aliveCount = 0;

    OnsetDetector m_onset;
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

    // single vertex buffer, permanently mapped, written under in-flight fence
    vk::raii::Buffer m_particleBuffer{nullptr};
    vk::raii::DeviceMemory m_particleMemory{nullptr};
    void *m_particleMapped = nullptr;

    std::vector<vk::raii::DescriptorSet> m_descriptorSets;
};

} // namespace visuals
