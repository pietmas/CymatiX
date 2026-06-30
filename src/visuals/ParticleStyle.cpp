#include <visuals/ParticleStyle.h>

#include <app/Config.h>
#include <rhi/BufferUtils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace visuals
{

// random float in -1..1, used to scatter the initial particle seed
static float randUV()
{
    return (float)(rand() % 2000) / 1000.0f - 1.0f;
}

// file / shader helpers (same pattern as RippleStyle)

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
    fprintf(stderr, "[ParticleStyle] failed to find suitible memory type\n");
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

// ParticleStyle

ParticleStyle::ParticleStyle(const rhi::VulkanDeps &deps, const palette::IPalette &palette)
    : m_deps(deps), m_extent(deps.extent)
{
    // query largePoints once at construction; communicate to shader via push constant
    vk::PhysicalDeviceFeatures features = m_deps.physicalDevice.getFeatures();
    m_largePointsSupported = (features.largePoints == vk::True);
    if (!m_largePointsSupported)
    {
        printf("[ParticleStyle] largePoints feature unsupported, particles will be 1px dots\n");
    }

    createDescriptorSetLayout();
    createDescriptorPool();
    createPipeline(m_deps.colorFormat);
    createPaletteBuffers();
    createParticleBuffer();
    createDescriptorSets(palette);
    createSpectrumBuffers();
    createComputeDescriptors();
    createComputePipeline();
}

// unmap memory before raii frees it
ParticleStyle::~ParticleStyle()
{
    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_paletteUBOMemory[i].unmapMemory();
        m_spectrumMemory[i].unmapMemory();
    }
    // particle buffer is DEVICE_LOCAL (never mapped), nothing to unmap
}

// private init helpers

// single UBO binding: palette at binding=0, color is per-particle so no SpectrumUBO
void ParticleStyle::createDescriptorSetLayout()
{
    vk::DescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    binding.descriptorCount = 1;
    binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    m_descriptorSetLayout = (*m_deps.device).createDescriptorSetLayout(layoutInfo);
}

// pool holds MAX_FRAMES_IN_FLIGHT sets, each with 1 UBO descriptor
void ParticleStyle::createDescriptorPool()
{
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eUniformBuffer;
    poolSize.descriptorCount = Config::MAX_FRAMES_IN_FLIGHT;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = Config::MAX_FRAMES_IN_FLIGHT;

    m_descriptorPool = (*m_deps.device).createDescriptorPool(poolInfo);
}

// point-list pipeline with 4 vertex attribs and alpha blending
void ParticleStyle::createPipeline(vk::Format colorFormat)
{
    const vk::raii::Device &device = (*m_deps.device);

    auto vertCode = readFile(SHADER_DIR "/particle.vert.spv");
    auto fragCode = readFile(SHADER_DIR "/particle.frag.spv");

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

    // one vertex binding, stride = sizeof(ParticleVertex) = 24 bytes
    vk::VertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(ParticleVertex);
    bindingDesc.inputRate = vk::VertexInputRate::eVertex;

    // five attributes: pos, vel, age, energy, freq. pad at offset 28 has no attribute
    vk::VertexInputAttributeDescription attrs[5]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = vk::Format::eR32G32Sfloat;
    attrs[0].offset = 0;

    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = vk::Format::eR32G32Sfloat;
    attrs[1].offset = 8;

    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = vk::Format::eR32Sfloat;
    attrs[2].offset = 16;

    attrs[3].location = 3;
    attrs[3].binding = 0;
    attrs[3].format = vk::Format::eR32Sfloat;
    attrs[3].offset = 20;

    attrs[4].location = 4;
    attrs[4].binding = 0;
    attrs[4].format = vk::Format::eR32Sfloat;
    attrs[4].offset = 24;

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 5;
    vertexInputInfo.pVertexAttributeDescriptions = attrs;

    // POINT_LIST means each vertex becomes a point sprite
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::ePointList;
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

    // alpha blending so the quadratic age fade actually shows on the framebuffer
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = vk::True;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // push constants live in the VERTEX stage (gl_PointSize decision)
    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
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
    pipelineInfo.renderPass = nullptr; // dynamic rendering
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;

    m_pipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
    // shader modules destroyed at end of scope
}

// palette UBO per frame-in-flight
void ParticleStyle::createPaletteBuffers()
{
    const vk::raii::Device &device = (*m_deps.device);
    vk::PhysicalDevice physDev = m_deps.physicalDevice;

    m_paletteUBOBuffers.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_paletteUBOMemory.reserve(Config::MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
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

// DEVICE_LOCAL particle buffer, seeded once via a staging upload (GPU owns it after)
void ParticleStyle::createParticleBuffer()
{
    // build the initial cloud on the CPU: random positions and ages so it starts populated
    std::vector<ParticleVertex> seed(MAX_PARTICLES);
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        seed[i].pos[0] = randUV(); // -1..1
        seed[i].pos[1] = randUV();
        seed[i].vel[0] = 0.0f;
        seed[i].vel[1] = 0.0f;
        seed[i].age = (float)(rand() % 1000) / 1000.0f;
        seed[i].energy = 0.0f;
        seed[i].freq = 0.0f;
        seed[i].pad = 0.0f;
    }

    vk::DeviceSize bufferSize = (vk::DeviceSize)MAX_PARTICLES * sizeof(ParticleVertex);

    // STORAGE so compute can write it, VERTEX so graphics can fetch it, TRANSFER_DST for the copy
    rhi::AllocatedBuffer buf = rhi::createBuffer(
        m_deps,
        bufferSize,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal
    );
    m_particleBuffer = std::move(buf.buffer);
    m_particleMemory = std::move(buf.memory);

    // copy the seed into the device-local buffer through a transient staging buffer
    rhi::uploadToDeviceLocal(
        m_deps,
        m_deps.transientCmdPool,
        m_deps.graphicsQueue,
        *m_particleBuffer,
        seed.data(),
        bufferSize
    );
}

// alloc descriptor sets and wire palette binding
void ParticleStyle::createDescriptorSets(const palette::IPalette &palette)
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

        vk::DescriptorBufferInfo paletteInfo{};
        paletteInfo.buffer = *m_paletteUBOBuffers[i];
        paletteInfo.offset = 0;
        paletteInfo.range = sizeof(PaletteUBOData);

        vk::WriteDescriptorSet write{};
        write.dstSet = *m_descriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.pBufferInfo = &paletteInfo;

        device.updateDescriptorSets({write}, {});
    }
}

// per-frame spectrum SSBO, host-visible + mapped, zero-filled so first dispatch reads zeros
void ParticleStyle::createSpectrumBuffers()
{
    const vk::raii::Device &device = (*m_deps.device);
    vk::PhysicalDevice physDev = m_deps.physicalDevice;

    m_spectrumBuffers.reserve(Config::MAX_FRAMES_IN_FLIGHT);
    m_spectrumMemory.reserve(Config::MAX_FRAMES_IN_FLIGHT);

    vk::DeviceSize size = (vk::DeviceSize)SPECTRUM_COUNT * sizeof(float);

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_spectrumBuffers.emplace_back(nullptr);
        m_spectrumMemory.emplace_back(nullptr);
        createMappedBuffer(
            device,
            physDev,
            size,
            vk::BufferUsageFlagBits::eStorageBuffer,
            m_spectrumBuffers[i],
            m_spectrumMemory[i],
            m_spectrumMapped[i]
        );
        // cold start: first compute dispatch may run before update() copies real data
        memset(m_spectrumMapped[i], 0, size);
    }
}

// compute descriptor layout/pool/sets: binding 0 = particle SSBO, binding 1 = spectrum SSBO
void ParticleStyle::createComputeDescriptors()
{
    const vk::raii::Device &device = (*m_deps.device);

    // two storage buffer bindings, both visible to the compute stage
    vk::DescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    m_computeDSLayout = device.createDescriptorSetLayout(layoutInfo);

    // each frame's set needs 2 storage descriptors (particle + spectrum)
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eStorageBuffer;
    poolSize.descriptorCount = Config::MAX_FRAMES_IN_FLIGHT * 2;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = Config::MAX_FRAMES_IN_FLIGHT;

    m_computeDescriptorPool = device.createDescriptorPool(poolInfo);

    std::vector<vk::DescriptorSetLayout> layouts(Config::MAX_FRAMES_IN_FLIGHT, *m_computeDSLayout);

    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = *m_computeDescriptorPool;
    allocInfo.descriptorSetCount = Config::MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    m_computeDescriptorSets = device.allocateDescriptorSets(allocInfo);

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        // binding 0: the one shared particle buffer (same handle in every set)
        vk::DescriptorBufferInfo particleInfo{};
        particleInfo.buffer = *m_particleBuffer;
        particleInfo.offset = 0;
        particleInfo.range = VK_WHOLE_SIZE;

        // binding 1: this frame's own spectrum buffer
        vk::DescriptorBufferInfo spectrumInfo{};
        spectrumInfo.buffer = *m_spectrumBuffers[i];
        spectrumInfo.offset = 0;
        spectrumInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet writes[2]{};
        writes[0].dstSet = *m_computeDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[0].pBufferInfo = &particleInfo;

        writes[1].dstSet = *m_computeDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &spectrumInfo;

        device.updateDescriptorSets({writes[0], writes[1]}, {});
    }
}

// compute pipeline: own layout with the compute DS layout + 16-byte COMPUTE push range
void ParticleStyle::createComputePipeline()
{
    const vk::raii::Device &device = (*m_deps.device);

    auto compCode = readFile(SHADER_DIR "/particle_simulate.comp.spv");
    vk::raii::ShaderModule compModule = createShaderModule(device, compCode);

    vk::PushConstantRange pcRange{};
    pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcRange.offset = 0;
    pcRange.size = sizeof(ComputePush);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &*m_computeDSLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pcRange;

    m_computePipelineLayout = device.createPipelineLayout(layoutInfo);

    vk::PipelineShaderStageCreateInfo stage{};
    stage.stage = vk::ShaderStageFlagBits::eCompute;
    stage.module = *compModule;
    stage.pName = "main";

    vk::ComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.stage = stage;
    pipelineInfo.layout = *m_computePipelineLayout;

    m_computePipeline = device.createComputePipeline(nullptr, pipelineInfo);
    // compute shader module destroyed at end of scope
}

// per-frame logic

// per-frame update; stash sim scalars + spectrum for computeDispatch to upload
void ParticleStyle::update(const float *magnitudes, uint32_t count, float deltaTime)
{
    m_lastDt = deltaTime;
    m_time += deltaTime;
    m_frameSeed++;

    // copy magnitudes to staging array; computeDispatch pushes to frame SSBO. zero-pad tail
    uint32_t n = std::min(count, (uint32_t)SPECTRUM_COUNT);
    memcpy(m_pendingSpectrum, magnitudes, n * sizeof(float));
    if (n < (uint32_t)SPECTRUM_COUNT)
    {
        memset(m_pendingSpectrum + n, 0, (SPECTRUM_COUNT - n) * sizeof(float));
    }

    // precompute bass + loudness so shader live force dont loop spectrum per particle.
    // RMS keeps same scale as magnitudes. skip DC bin, bass = first 64 bins, loudness = all
    float bassSum = 0.0f;
    float totalSum = 0.0f;
    for (uint32_t k = 1; k < (uint32_t)SPECTRUM_COUNT; k++)
    {
        float e = m_pendingSpectrum[k] * m_pendingSpectrum[k];
        totalSum += e;
        if (k < 64)
        {
            bassSum += e;
        }
    }
    m_bass = std::sqrt(bassSum / 63.0f);
    m_loudness = std::sqrt(totalSum / (float)(SPECTRUM_COUNT - 1));
}

// bind pipeline, push constants, bind vertex buffer, draw all particles
void ParticleStyle::render(vk::CommandBuffer cmd, uint32_t frameIndex)
{
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);

    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *m_pipelineLayout,
        0,
        {*m_descriptorSets[frameIndex]},
        {}
    );

    PushConstants pc{};
    pc.largePoints = m_largePointsSupported ? 1 : 0;

    cmd.pushConstants<PushConstants>(*m_pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pc);

    // every particle is always alive now (GPU self-respawns), so draw the whole buffer
    vk::Buffer vbuf = *m_particleBuffer;
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, {vbuf}, {offset});

    cmd.draw(MAX_PARTICLES, 1, 0, 0);
}

// store new extent
void ParticleStyle::onResize(vk::Extent2D newExtent)
{
    m_extent = newExtent;
}

// record pre-barrier, dispatch the compute sim, record post-barrier. App calls this
// before beginRendering (dispatch is illegal inside a dynamic rendering block)
void ParticleStyle::computeDispatch(vk::CommandBuffer cmd, uint32_t frameIndex)
{
    // upload this frame's spectrum before the GPU reads it (host-coherent, no flush needed)
    memcpy(m_spectrumMapped[frameIndex], m_pendingSpectrum, SPECTRUM_COUNT * sizeof(float));

    // pre-dispatch: previous frame's vertex read -> this frame's compute write (WAR).
    // one particle buffer shared across frames-in-flight needs this read->write barrier
    vk::BufferMemoryBarrier2 pre{};
    pre.srcStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput;
    pre.srcAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
    pre.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    pre.dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre.buffer = *m_particleBuffer;
    pre.offset = 0;
    pre.size = VK_WHOLE_SIZE;

    vk::DependencyInfo depPre{};
    depPre.bufferMemoryBarrierCount = 1;
    depPre.pBufferMemoryBarriers = &pre;
    cmd.pipelineBarrier2(depPre);

    // dispatch the simulation
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_computePipeline);
    cmd.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        *m_computePipelineLayout,
        0,
        {*m_computeDescriptorSets[frameIndex]},
        {}
    );

    ComputePush push{};
    push.dt = m_lastDt;
    push.time = m_time;
    push.frameSeed = m_frameSeed;
    push.particleCount = (uint32_t)MAX_PARTICLES;
    push.bass = m_bass;
    push.loudness = m_loudness;
    cmd.pushConstants<ComputePush>(
        *m_computePipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        push
    );

    uint32_t groups = (MAX_PARTICLES + 255) / 256; // ceil(N/256) = 40 groups
    cmd.dispatch(groups, 1, 1);

    // post-dispatch: compute write -> vertex read (RAW), so the draw sees this frame's sim
    vk::BufferMemoryBarrier2 post{};
    post.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
    post.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
    post.dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput;
    post.dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead;
    post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post.buffer = *m_particleBuffer;
    post.offset = 0;
    post.size = VK_WHOLE_SIZE;

    vk::DependencyInfo depPost{};
    depPost.bufferMemoryBarrierCount = 1;
    depPost.pBufferMemoryBarriers = &post;
    cmd.pipelineBarrier2(depPost);
}

} // namespace visuals
