#include <visuals/ChladniStyle.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include <glm/glm.hpp>

namespace visuals
{

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

    fprintf(stderr, "[ChladniStyle] failed to find suitable memory type\n");
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

// ChladniStyle

ChladniStyle::ChladniStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette)
    : m_deps(deps), m_extent(deps.extent)
{
    createDescriptorSetLayout();
    createDescriptorPool();
    createPipeline(m_deps.colorFormat);
    createUBOBuffers();
    createDescriptorSets(palette);
}

// unmap memory before raii frees it
ChladniStyle::~ChladniStyle()
{
    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_spectrumUBOMemory[i].unmapMemory();
        m_paletteUBOMemory[i].unmapMemory();
    }
}

// private init helpers

// two UBO bindings: set=0 binding=0 (spectrum), set=0 binding=1 (palette)
void ChladniStyle::createDescriptorSetLayout()
{
    vk::DescriptorSetLayoutBinding bindings[2]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    m_descriptorSetLayout = (*m_deps.device).createDescriptorSetLayout(layoutInfo);
}

// pool holds MAX_FRAMES_IN_FLIGHT sets, each with 2 UBO descriptors
void ChladniStyle::createDescriptorPool()
{
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eUniformBuffer;
    poolSize.descriptorCount = Config::MAX_FRAMES_IN_FLIGHT * 2; // spectrum + palette per frame

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = Config::MAX_FRAMES_IN_FLIGHT;

    m_descriptorPool = (*m_deps.device).createDescriptorPool(poolInfo);
}

// Chladni graphics pipeline
void ChladniStyle::createPipeline(vk::Format colorFormat)
{
    const vk::raii::Device &device = (*m_deps.device);

    auto vertCode = readFile(SHADER_DIR "/chladni.vert.spv");
    auto fragCode = readFile(SHADER_DIR "/chladni.frag.spv");

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

    // no vertex bindings
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

    // no blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = vk::False;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // push constant range
    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pcRange.offset = 0;
    pcRange.size = 32;

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
    pipelineInfo.renderPass = nullptr; // must be null with dynamic rendering
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    m_pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
    // shader modules destroyed here (end of scope)
}

// spectrum UBO + palette UBO per frame-in-flight
void ChladniStyle::createUBOBuffers()
{
    const vk::raii::Device &device = (*m_deps.device);
    vk::PhysicalDevice physDev = m_deps.physicalDevice;

    m_spectrumUBOBuffers.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_spectrumUBOMemory.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_paletteUBOBuffers.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_paletteUBOMemory.reserve(Config::MAX_FRAMES_IN_FLIGHT);

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
    }
}

// alloc descriptor sets, wire to UBO buffers
void ChladniStyle::createDescriptorSets(const palette::IPalette &palette)
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

    // upload palette once at init
    PaletteUBOData paletteData = palette.getUBOData();

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        memcpy(m_paletteMapped[i], &paletteData, sizeof(PaletteUBOData));

        vk::DescriptorBufferInfo spectrumInfo{};
        spectrumInfo.buffer = *m_spectrumUBOBuffers[i];
        spectrumInfo.offset = 0;
        spectrumInfo.range = sizeof(SpectrumUBOData);

        vk::DescriptorBufferInfo paletteInfo{};
        paletteInfo.buffer = *m_paletteUBOBuffers[i];
        paletteInfo.offset = 0;
        paletteInfo.range = sizeof(PaletteUBOData);

        vk::WriteDescriptorSet writes[2]{};

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

        device.updateDescriptorSets({writes[0], writes[1]}, {});
    }
}

// per-frame logic

// compute mode params from spectrum, rate-limit transitions, update spectrum UBO cache
void ChladniStyle::update(const float *magnitudes, uint32_t count, float deltaTime)
{
    m_time += deltaTime;

    if (count == 0)
    {
        return;
    }

    // split spectrum into low [0, count/4) and high [count/4, count)
    uint32_t splitBin = count / 4;

    // energy centroid of low band -> m
    float lowWeightSum = 0.0f;
    float lowTotalMag = 0.0f;
    for (uint32_t i = 0; i < splitBin; i++)
    {
        lowWeightSum += float(i) * magnitudes[i];
        lowTotalMag += magnitudes[i];
    }
    float lowCentroid =
        (lowTotalMag > 0.0f) ? (lowWeightSum / lowTotalMag) / float(splitBin - 1) : 0.0f;

    // energy centroid of high band -> n
    float highWeightSum = 0.0f;
    float highTotalMag = 0.0f;
    for (uint32_t i = splitBin; i < count; i++)
    {
        highWeightSum += float(i - splitBin) * magnitudes[i];
        highTotalMag += magnitudes[i];
    }
    uint32_t highBandWidth = count - splitBin;
    float highCentroid =
        (highTotalMag > 0.0f) ? (highWeightSum / highTotalMag) / float(highBandWidth - 1) : 0.0f;

    m_targetM = 1.0f + lowCentroid * 25.0f;
    m_targetN = 1.0f + highCentroid * 10.0f;

    // spectral flux: sum of positive magnitude changes since last frame
    if (m_prevMagnitudes.size() != count)
    {
        m_prevMagnitudes.assign(count, 0.0f);
    }
    float flux = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        float diff = magnitudes[i] - m_prevMagnitudes[i];
        if (diff > 0.0f)
        {
            flux += diff;
        }
    }
    memcpy(m_prevMagnitudes.data(), magnitudes, count * sizeof(float));

    // rolling peak flux, 2s half-life; normalized flux is transient intensity in [0,1]
    m_peakFlux = std::max(flux, m_peakFlux * std::pow(0.5f, deltaTime / 2.0f));
    float normalizedFlux = (m_peakFlux > 1e-6f) ? std::clamp(flux / m_peakFlux, 0.0f, 1.0f) : 0.0f;

    // rate limit: 0.5/sec during sustained pads, up to 4.0/sec on drum hits
    float rateLimit = glm::mix(0.5f, 4.0f, normalizedFlux);
    float deltaM =
        std::clamp(m_targetM - m_currentM, -rateLimit * deltaTime, rateLimit * deltaTime);
    m_currentM += deltaM;
    float deltaN =
        std::clamp(m_targetN - m_currentN, -rateLimit * deltaTime, rateLimit * deltaTime);
    m_currentN += deltaN;

    // RMS energy for line width
    float sumSq = 0.0f;
    for (uint32_t i = 0; i < count; i++)
    {
        sumSq += magnitudes[i] * magnitudes[i];
    }
    float rms = std::sqrt(sumSq / float(count));

    // rolling peak RMS, 5s half-life; auto-gain keeps line width visible at any loudness level
    m_peakRMS = std::max(rms, m_peakRMS * std::pow(0.5f, deltaTime / 5.0f));
    float normalizedRMS = (m_peakRMS > 1e-6f) ? std::clamp(rms / m_peakRMS, 0.0f, 1.0f) : 0.0f;
    m_lineWidth = glm::mix(0.03f, 0.12f, normalizedRMS);

    // cache spectrum for render()
    uint32_t bins = std::min(count, static_cast<uint32_t>(MAX_SPECTRUM_BINS));
    memcpy(m_pendingSpectrum.magnitudes, magnitudes, bins * sizeof(float));
    m_pendingSpectrum.time = m_time;
}

// bind pipeline, push static constants, draw fullscreen triangle
void ChladniStyle::render(vk::CommandBuffer cmd, uint32_t frameIndex)
{
    memcpy(m_spectrumMapped[frameIndex], &m_pendingSpectrum, sizeof(SpectrumUBOData));

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *m_pipelineLayout,
        0,
        {*m_descriptorSets[frameIndex]},
        {}
    );

    PushConstants pc{};
    pc.m = m_currentM;
    pc.n = m_currentN;
    // cos(m*PI*x)*cos(n*PI*y) - cos(n*PI*x)*cos(m*PI*y) = 0 everywhere when m==n -> white screen
    if (std::abs(pc.m - pc.n) < 0.1f)
    {
        pc.n = pc.m + 0.1f;
    }
    pc.blend = 0.0f;
    pc.lineWidth = m_lineWidth;
    pc.time = m_time;

    cmd.pushConstants<PushConstants>(*m_pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, pc);

    cmd.draw(3, 1, 0, 0);
}

// store new extent
void ChladniStyle::onResize(vk::Extent2D newExtent)
{
    m_extent = newExtent;
}

} // namespace visuals
