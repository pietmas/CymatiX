#include <visuals/RippleStyle.h>

#include <app/Config.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <glm/glm.hpp>

namespace visuals
{

// fixed spawn positions per onset band (UV space, range [-1,1])
const glm::vec2 RippleStyle::BAND_POS[4] = {
    {0.0f, 0.0f},
    {0.3f, 0.2f},
    {-0.4f, 0.5f},
    {0.7f, -0.6f},
};

// file / shader helpers

static std::vector<char> readFile(const std::string &path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        fprintf(stderr, "failed to open shader file: %s\n", path.c_str());
        abort();
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), (std::streamsize)fileSize);
    file.close();
    return buffer;
}

static vk::raii::ShaderModule
createShaderModule(const vk::raii::Device &device, const std::vector<char> &code)
{
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    return device.createShaderModule(createInfo);
}

// find suitable device memory type
static uint32_t
findMemoryType(vk::PhysicalDevice physDev, uint32_t typeFilter, vk::MemoryPropertyFlags props)
{
    vk::PhysicalDeviceMemoryProperties memProps = physDev.getMemoryProperties();
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }
    fprintf(stderr, "[RippleStyle] failed to find suitible memory type\n");
    abort();
}

// allocate host-visible mapped buffer
static void createMappedBuffer(
    const vk::raii::Device &device,
    vk::PhysicalDevice physDev,
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::raii::Buffer &outBuffer,
    vk::raii::DeviceMemory &outMemory,
    void *&outMapped
)
{
    vk::BufferCreateInfo bufInfo{};
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = vk::SharingMode::eExclusive;

    outBuffer = device.createBuffer(bufInfo);

    vk::MemoryRequirements memReqs = outBuffer.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        physDev,
        memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    );

    outMemory = device.allocateMemory(allocInfo);
    outBuffer.bindMemory(*outMemory, 0);
    outMapped = outMemory.mapMemory(0, size);
}

// RippleStyle

RippleStyle::RippleStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette)
    : m_deps(deps), m_extent(deps.extent)
{
    createDescriptorSetLayout();
    createDescriptorPool();
    createPipeline(m_deps.colorFormat);
    createUBOBuffers();
    createDescriptorSets(palette);
}

// unmap memory before raii frees it
RippleStyle::~RippleStyle()
{
    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_spectrumUBOMemory[i].unmapMemory();
        m_paletteUBOMemory[i].unmapMemory();
        m_sourceUBOMemory[i].unmapMemory();
    }
}

// private init helpers

// three UBO bindings: spectrum=0, palette=1, sources=2 (own layout, not shared)
void RippleStyle::createDescriptorSetLayout()
{
    vk::DescriptorSetLayoutBinding bindings[3]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

    bindings[2].binding = 2;
    bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    m_descriptorSetLayout = (*m_deps.device).createDescriptorSetLayout(layoutInfo);
}

// pool holds MAX_FRAMES_IN_FLIGHT sets, each with 3 UBO descriptors
void RippleStyle::createDescriptorPool()
{
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eUniformBuffer;
    poolSize.descriptorCount = Config::MAX_FRAMES_IN_FLIGHT * 3;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = Config::MAX_FRAMES_IN_FLIGHT;

    m_descriptorPool = (*m_deps.device).createDescriptorPool(poolInfo);
}

// ripple graphics pipeline: ripple.vert.spv / ripple.frag.spv, no vertex bindings
void RippleStyle::createPipeline(vk::Format colorFormat)
{
    const vk::raii::Device &device = (*m_deps.device);

    auto vertCode = readFile(SHADER_DIR "/ripple.vert.spv");
    auto fragCode = readFile(SHADER_DIR "/ripple.frag.spv");

    vk::raii::ShaderModule vertModule = createShaderModule(device, vertCode);
    vk::raii::ShaderModule fragModule = createShaderModule(device, fragCode);

    vk::PipelineShaderStageCreateInfo vertStage{};
    vertStage.stage = vk::ShaderStageFlagBits::eVertex;
    vertStage.module = *vertModule;
    vertStage.pName = "main";

    vk::PipelineShaderStageCreateInfo fragStage{};
    fragStage.stage = vk::ShaderStageFlagBits::eFragment;
    fragStage.module = *fragModule;
    fragStage.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

    // no vertex bindings, fullscreen triangle, vertices hardcoded in shader
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = vk::False;

    vk::DynamicState dynamicStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = vk::False;
    rasterizer.rasterizerDiscardEnable = vk::False;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eClockwise;
    rasterizer.depthBiasEnable = vk::False;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = vk::False;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = vk::False;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 16-byte push constant range, fragment stage only
    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pcRange.offset = 0;
    pcRange.size = sizeof(PushConstants);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &*m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    m_pipelineLayout = device.createPipelineLayout(layoutInfo);

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = *m_pipelineLayout;
    pipelineInfo.renderPass = nullptr; // dynamic rendering, no render pass
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    m_pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
    // shader modules destroyed here at end of scope
}

// spectrum + palette + sources UBO per frame-in-flight
void RippleStyle::createUBOBuffers()
{
    const vk::raii::Device &device = (*m_deps.device);
    vk::PhysicalDevice physDev = m_deps.physicalDevice;

    m_spectrumUBOBuffers.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_spectrumUBOMemory.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_paletteUBOBuffers.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_paletteUBOMemory.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_sourceUBOBuffers.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_sourceUBOMemory.reserve(Config::MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_spectrumUBOBuffers.emplace_back(nullptr);
        m_spectrumUBOMemory.emplace_back(nullptr);
        createMappedBuffer(
            device,
            physDev,
            sizeof(SpectrumUBOData),
            vk::BufferUsageFlagBits::eUniformBuffer,
            m_spectrumUBOBuffers[i],
            m_spectrumUBOMemory[i],
            m_spectrumMapped[i]
        );

        m_paletteUBOBuffers.emplace_back(nullptr);
        m_paletteUBOMemory.emplace_back(nullptr);
        createMappedBuffer(
            device,
            physDev,
            sizeof(PaletteUBOData),
            vk::BufferUsageFlagBits::eUniformBuffer,
            m_paletteUBOBuffers[i],
            m_paletteUBOMemory[i],
            m_paletteMapped[i]
        );

        m_sourceUBOBuffers.emplace_back(nullptr);
        m_sourceUBOMemory.emplace_back(nullptr);
        createMappedBuffer(
            device,
            physDev,
            sizeof(RippleSourcesUBO),
            vk::BufferUsageFlagBits::eUniformBuffer,
            m_sourceUBOBuffers[i],
            m_sourceUBOMemory[i],
            m_sourceMapped[i]
        );
    }
}

// alloc descriptor sets and wire all three bindings
void RippleStyle::createDescriptorSets(const palette::IPalette &palette)
{
    const vk::raii::Device &device = (*m_deps.device);

    std::vector<vk::DescriptorSetLayout> layouts(
        Config::MAX_FRAMES_IN_FLIGHT,
        *m_descriptorSetLayout
    );

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *m_descriptorPool;
    allocInfo.descriptorSetCount = Config::MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets = device.allocateDescriptorSets(allocInfo);

    PaletteUBOData paletteData = palette.getUBOData();

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        memcpy(m_paletteMapped[i], &paletteData, sizeof(PaletteUBOData));

        // zero-init source UBO; wil be written each frame once onset logic is wired
        RippleSourcesUBO zero{};
        memcpy(m_sourceMapped[i], &zero, sizeof(RippleSourcesUBO));

        vk::DescriptorBufferInfo spectrumInfo{};
        spectrumInfo.buffer = *m_spectrumUBOBuffers[i];
        spectrumInfo.offset = 0;
        spectrumInfo.range = sizeof(SpectrumUBOData);

        vk::DescriptorBufferInfo paletteInfo{};
        paletteInfo.buffer = *m_paletteUBOBuffers[i];
        paletteInfo.offset = 0;
        paletteInfo.range = sizeof(PaletteUBOData);

        vk::DescriptorBufferInfo sourceInfo{};
        sourceInfo.buffer = *m_sourceUBOBuffers[i];
        sourceInfo.offset = 0;
        sourceInfo.range = sizeof(RippleSourcesUBO);

        vk::WriteDescriptorSet writes[3]{};

        writes[0].dstSet = *m_descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].pBufferInfo = &spectrumInfo;

        writes[1].dstSet = *m_descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[1].pBufferInfo = &paletteInfo;

        writes[2].dstSet = *m_descriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[2].pBufferInfo = &sourceInfo;

        device.updateDescriptorSets({writes[0], writes[1], writes[2]}, {});
    }
}

// per-frame logic

// detect onsets, spawn sources, cache spectrum for render()
void RippleStyle::update(const float *magnitudes, uint32_t count, float deltaTime)
{
    m_time += deltaTime;

    if (count == 0)
    {
        return;
    }

    m_onset.update(magnitudes, count);
    for (int b = 0; b < 4; b++)
    {
        if (m_onset.hasOnset(b))
        {
            spawnSource(b, m_onset.bandEnergy(b));
        }
    }

    // cache spectrum for render(), same pattern as ChladniStyle
    uint32_t bins = std::min(count, static_cast<uint32_t>(MAX_SPECTRUM_BINS));
    memcpy(m_pendingSpectrum.magnitudes, magnitudes, bins * sizeof(float));
    m_pendingSpectrum.time = m_time;
}

// upload UBOs for this frame, bind pipeline, push constants, draw fullscreen triangle
void RippleStyle::render(vk::CommandBuffer cmd, uint32_t frameIndex)
{
    // upload spectrum UBO
    memcpy(m_spectrumMapped[frameIndex], &m_pendingSpectrum, sizeof(SpectrumUBOData));

    // build sources UBO from active sources and upload
    RippleSourcesUBO sourcesData{};
    sourcesData.activeCount = m_activeCount;
    for (int i = 0; i < m_activeCount; i++)
    {
        sourcesData.sources[i] = glm::vec4(
            m_sources[i].cx,
            m_sources[i].cy,
            m_sources[i].frequency,
            m_sources[i].spawnTime
        );
    }
    memcpy(m_sourceMapped[frameIndex], &sourcesData, sizeof(RippleSourcesUBO));

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *m_pipelineLayout,
        0,
        {*m_descriptorSets[frameIndex]},
        {}
    );

    PushConstants pc{};
    pc.time = m_time;
    pc.activeCount = m_activeCount;

    cmd.pushConstants<PushConstants>(*m_pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, pc);

    cmd.draw(3, 1, 0, 0);
}

// store new extent
void RippleStyle::onResize(vk::Extent2D newExtent)
{
    m_extent = newExtent;
}

// find a free slot or evict oldest, then write source data
void RippleStyle::spawnSource(int band, float energy)
{
    int slot = -1;
    for (int i = 0; i < MAX_SOURCES; i++)
    {
        if (i >= m_activeCount)
        {
            slot = i;
            break;
        }
    }
    if (slot == -1)
    {
        evictOldestSource();
        slot = m_activeCount; // evict decremented count, this slot is now free
    }
    // map band energy to visual frequency range [1.0, 8.0]
    float freqNorm = std::clamp(energy / 0.1f, 0.0f, 1.0f);
    m_sources[slot].cx = BAND_POS[band].x;
    m_sources[slot].cy = BAND_POS[band].y;
    m_sources[slot].frequency = 1.0f + freqNorm * 7.0f;
    m_sources[slot].spawnTime = m_time;
    if (slot == m_activeCount)
    {
        m_activeCount++;
    }
}

// remove oldest source (smallest spawnTime) by swap with last active
void RippleStyle::evictOldestSource()
{
    int oldestIdx = 0;
    for (int i = 1; i < m_activeCount; i++)
    {
        if (m_sources[i].spawnTime < m_sources[oldestIdx].spawnTime)
        {
            oldestIdx = i;
        }
    }
    m_sources[oldestIdx] = m_sources[m_activeCount - 1];
    m_activeCount--;
}

} // namespace visuals
