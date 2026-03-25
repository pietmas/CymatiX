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

class LissajousStyle : public IVisualStyle
{
  public:
    static constexpr uint32_t N_POINTS = 4096;

    LissajousStyle(
        const rhi::VulkanContext &ctx,
        VkRenderPass renderPass,
        VkExtent2D extent,
        const palette::IPalette &palette
    );
    ~LissajousStyle() override;

    LissajousStyle(const LissajousStyle &) = delete;
    LissajousStyle &operator=(const LissajousStyle &) = delete;
    LissajousStyle(LissajousStyle &&) = delete;
    LissajousStyle &operator=(LissajousStyle &&) = delete;

    void update(const float *magnitudes, uint32_t count, float deltaTime) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex) override;
    void onResize(VkExtent2D newExtent) override;

  private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(VkRenderPass renderPass);
    void createUBOBuffers();
    void createVertexBuffer();
    void createDescriptorSets(const palette::IPalette &palette);

    const rhi::VulkanContext &m_ctx;
    VkExtent2D m_extent;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // per-frame spectrum UBOs (written every frame)
    VkBuffer m_spectrumUBOBuffers[Config::MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_spectrumUBOMemory[Config::MAX_FRAMES_IN_FLIGHT];
    void *m_spectrumMapped[Config::MAX_FRAMES_IN_FLIGHT];

    // per-frame palette UBOs (written once at init)
    VkBuffer m_paletteUBOBuffers[Config::MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_paletteUBOMemory[Config::MAX_FRAMES_IN_FLIGHT];
    void *m_paletteMapped[Config::MAX_FRAMES_IN_FLIGHT];

    // single host-visible vertex buffer - safe to share across frames for fence
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    void *m_vertexMapped = nullptr;

    VkDescriptorSet m_descriptorSets[Config::MAX_FRAMES_IN_FLIGHT];

    // spectrum data assembled in update, uploaded to UBO in render
    SpectrumUBOData m_pendingSpectrum{};

    float m_time = 0.0f;
};

} // namespace visuals
