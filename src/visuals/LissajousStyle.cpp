#include <visuals/LissajousStyle.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

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

// find memory type matching typeFilter + props
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

    fprintf(stderr, "[LissajousStyle] failed to find suitable memory type\n");
    abort();
}

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

// LissajousStyle

LissajousStyle::LissajousStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette)
    : m_deps(deps), m_extent(deps.extent)
{
    createDescriptorSetLayout();
    createDescriptorPool();
    createPipeline(m_deps.renderPass);
    createUBOBuffers();
    createVertexBuffer();
    createDescriptorSets(palette);
}

// unmap memory before raii frees it, then raii handles the rest
LissajousStyle::~LissajousStyle()
{
    m_vertexBufferMemory.unmapMemory();

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_spectrumUBOMemory[i].unmapMemory();
        m_paletteUBOMemory[i].unmapMemory();
    }
}

// private init helpers

// two UBO bindings: set=0 binding=0 (spectrum), set=0 binding=1 (palette)

void LissajousStyle::createDescriptorSetLayout()
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
void LissajousStyle::createDescriptorPool()
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

// Lissajous graphics pipeline
void LissajousStyle::createPipeline(vk::RenderPass renderPass)
{
    const vk::raii::Device &device = (*m_deps.device);

    auto vertCode = readFile(SHADER_DIR "/lissajous.vert.spv");
    auto fragCode = readFile(SHADER_DIR "/lissajous.frag.spv");

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

    // one binding at slot 0: packed vec2 positions
    vk::VertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(float) * 2;
    bindingDesc.inputRate = vk::VertexInputRate::eVertex;

    vk::VertexInputAttributeDescription attrDesc{};
    attrDesc.binding = 0;
    attrDesc.location = 0;
    attrDesc.format = vk::Format::eR32G32Sfloat;
    attrDesc.offset = 0;

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attrDesc;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eLineStrip;
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

    // additive blending: overlapping lines accumulate brightness
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = vk::True;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // layout references descriptor set layout
    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &*m_descriptorSetLayout;

    m_pipelineLayout = device.createPipelineLayout(layoutInfo);

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
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
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    m_pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
    // shader modules destroyed here (end of scope)
}

// spectrum UBO + palette UBO per frame-in-flight
void LissajousStyle::createUBOBuffers()
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

// vertex buffer for 4096 vec2 points
void LissajousStyle::createVertexBuffer()
{
    createMappedBuffer(
        (*m_deps.device),
        m_deps.physicalDevice,
        N_POINTS * sizeof(float) * 2,
        vk::BufferUsageFlagBits::eVertexBuffer,
        m_vertexBuffer,
        m_vertexBufferMemory,
        m_vertexMapped
    );
}

// alloc descriptor sets, wire to UBO buffers
void LissajousStyle::createDescriptorSets(const palette::IPalette &palette)
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

    // upload palette once
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

// Lissajous params from FFT bands
void LissajousStyle::update(const float *magnitudes, uint32_t count, float deltaTime)
{
    m_time += deltaTime;

    // extract band energies

    // bass: bins 1..10 (skip bin 0)
    float bass = 0.0f;
    for (uint32_t i = 1; i <= 10 && i < count; i++)
    {
        bass += magnitudes[i];
    }
    bass /= 10.0f;

    // mid: bins 11..79
    float mid = 0.0f;
    for (uint32_t i = 11; i <= 79 && i < count; i++)
    {
        mid += magnitudes[i];
    }
    mid /= 69.0f;

    // treble: bins 80..200
    float treble = 0.0f;
    for (uint32_t i = 80; i <= 200 && i < count; i++)
    {
        treble += magnitudes[i];
    }
    treble /= 121.0f;

    // map bands to Lissajous parameters

    // freq ratios: 1.0=circle, higher more complex
    float a = 0.5f + bass * 16.0f * 4.0f;
    float b = 10.0f + treble * 8.0f * 4.0f;

    // near-integer so curve closes cleanly
    if (a > 5.0f)
        a = 5.0f;
    if (a < 1.0f)
        a = 1.0f;
    if (b > 5.0f)
        b = 5.0f;
    if (b < 1.0f)
        b = 1.0f;

    // phase offset for tilt
    float delta = mid * 12.0f * (float)M_PI;

    // amplitude from bass, min to stay visible
    float amplitude = bass * 8.0f * 0.8f + 0.2f;
    if (amplitude > 1.0f)
        amplitude = 1.0f;
    if (amplitude < 0.1f)
        amplitude = 0.1f;

    // curve points
    struct Vec2
    {
        float x;
        float y;
    };
    auto *pts = reinterpret_cast<Vec2 *>(m_vertexMapped);

    for (uint32_t i = 0; i < N_POINTS; i++)
    {
        float t = (float)i / (float)(N_POINTS - 1) * 2.0f * (float)M_PI;
        pts[i].x = amplitude * sinf(a * t + delta);
        pts[i].y = amplitude * sinf(b * t);
    }

    // cache spectrum for render
    uint32_t bins = (count < MAX_SPECTRUM_BINS) ? count : MAX_SPECTRUM_BINS;
    memcpy(m_pendingSpectrum.magnitudes, magnitudes, bins * sizeof(float));
    m_pendingSpectrum.time = m_time;
}

// upload UBO, record draw
void LissajousStyle::render(vk::CommandBuffer cmd, uint32_t frameIndex)
{
    memcpy(m_spectrumMapped[frameIndex], &m_pendingSpectrum, sizeof(SpectrumUBOData));

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);

    cmd.bindVertexBuffers(0, {*m_vertexBuffer}, {vk::DeviceSize{0}});

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *m_pipelineLayout,
        0,
        {*m_descriptorSets[frameIndex]},
        {}
    );

    cmd.draw(N_POINTS, 1, 0, 0);
}

// store new extent (vp/scissor are dynamic)
void LissajousStyle::onResize(vk::Extent2D newExtent)
{
    m_extent = newExtent;
}

} // namespace visuals
