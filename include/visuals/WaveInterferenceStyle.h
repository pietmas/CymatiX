#pragma once

#include <app/Config.h>
#include <app/SharedTypes.h>
#include <palette/IPalette.h>
#include <visuals/IVisualStyle.h>

#include <vulkan/vulkan.h>

namespace rhi
{
class VulkanContext;
}

namespace visuals
{

class WaveInterferenceStyle : public IVisualStyle
{
  public:
    WaveInterferenceStyle(
        const rhi::VulkanContext &ctx,
        VkRenderPass renderPass,
        VkExtent2D extent,
        const palette::IPalette &palette
    );
    ~WaveInterferenceStyle() override;

    WaveInterferenceStyle(const WaveInterferenceStyle &) = delete;
    WaveInterferenceStyle &operator=(const WaveInterferenceStyle &) = delete;
    WaveInterferenceStyle(WaveInterferenceStyle &&) = delete;
    WaveInterferenceStyle &operator=(WaveInterferenceStyle &&) = delete;

    void update(const float *magnitudes, uint32_t count, float deltaTime) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex) override;
    void onResize(VkExtent2D newExtent) override;

  private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(VkRenderPass renderPass);
    void createUBOBuffers();
    void createDescriptorSets(const palette::IPalette &palette);

    const rhi::VulkanContext &m_ctx;
    VkExtent2D m_extent;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // per-frame spectrum UBOs
    VkBuffer m_spectrumUBOBuffers[Config::MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_spectrumUBOMemory[Config::MAX_FRAMES_IN_FLIGHT];
    void *m_spectrumMapped[Config::MAX_FRAMES_IN_FLIGHT];

    // per-frame palette UBOs
    VkBuffer m_paletteUBOBuffers[Config::MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_paletteUBOMemory[Config::MAX_FRAMES_IN_FLIGHT];
    void *m_paletteMapped[Config::MAX_FRAMES_IN_FLIGHT];

    VkDescriptorSet m_descriptorSets[Config::MAX_FRAMES_IN_FLIGHT];

    // spectrum data assembled in update, uploaded in render
    SpectrumUBOData m_pendingSpectrum{};

    float m_time = 0.0f;
};

} // namespace visuals
