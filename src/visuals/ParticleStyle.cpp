#include <visuals/ParticleStyle.h>

#include <app/Config.h>

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

// same four spawn positions as RippleStyle so bursts line up visually
const glm::vec2 ParticleStyle::BAND_POS[4] = {
    {0.0f, 0.0f},
    {0.3f, 0.2f},
    {-0.4f, 0.5f},
    {0.7f, -0.6f},
};

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

    // pre-size simulation array; never realloc at runtime
    m_particles.resize(MAX_PARTICLES);

    createDescriptorSetLayout();
    createDescriptorPool();
    createPipeline(m_deps.colorFormat);
    createPaletteBuffers();
    createParticleBuffer();
    createDescriptorSets(palette);
}

// unmap memory before raii frees it
ParticleStyle::~ParticleStyle()
{
    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        m_paletteUBOMemory[i].unmapMemory();
    }
    if (m_particleMapped != nullptr)
    {
        m_particleMemory.unmapMemory();
        m_particleMapped = nullptr;
    }
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

    // four attributes: pos (vec2), vel (vec2), age (float), energy (float)
    vk::VertexInputAttributeDescription attrs[4]{};
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

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 4;
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

// HOST_VISIBLE vertex buffer for MAX_PARTICLES, permanently mapped, written under in-flight fence
void ParticleStyle::createParticleBuffer()
{
    const vk::raii::Device &device = (*m_deps.device);
    vk::PhysicalDevice physDev = m_deps.physicalDevice;

    vk::DeviceSize bufferSize = (vk::DeviceSize)MAX_PARTICLES * sizeof(ParticleVertex);

    createMappedBuffer(
        device,
        physDev,
        bufferSize,
        vk::BufferUsageFlagBits::eVertexBuffer,
        m_particleBuffer,
        m_particleMemory,
        m_particleMapped
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

// per-frame logic

// detect onsets, spawn particles, run CPU simulation
void ParticleStyle::update(const float *magnitudes, uint32_t count, float deltaTime)
{
    if (count > 0)
    {
        m_onset.update(magnitudes, count);
        for (int b = 0; b < 4; b++)
        {
            if (m_onset.hasOnset(b))
            {
                spawnParticles(b, m_onset.bandEnergy(b));
            }
        }
    }

    simulateParticles(deltaTime);
    uploadParticles();
}

// bind pipeline, push constants, bind vertex buffer, draw m_aliveCount points
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

    if (m_aliveCount == 0)
    {
        return;
    }

    vk::Buffer vbuf = *m_particleBuffer;
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, {vbuf}, {offset});

    cmd.draw(m_aliveCount, 1, 0, 0);
}

// store new extent
void ParticleStyle::onResize(vk::Extent2D newExtent)
{
    m_extent = newExtent;
}

// step alive particles by dt, kill expired ones
void ParticleStyle::simulateParticles(float dt)
{
    uint32_t alive = 0;
    for (uint32_t i = 0; i < m_aliveCount; i++)
    {
        Particle p = m_particles[i];

        p.age += dt / 4.0f; // 4-second lifetime
        if (p.age >= 1.0f)
        {
            continue; // dead, dont copy forward
        }

        // spiral field: gives clockwise curl when combined with radial spawn velocity
        p.vel += glm::vec2(-p.pos.y, p.pos.x) * 0.4f * dt;

        // drag keeps particles from flying off-screen on high-energy transients
        p.vel *= (1.0f - 0.5f * dt);

        p.pos += p.vel * dt;

        m_particles[alive++] = p;
    }
    m_aliveCount = alive;
}

// memcpy alive particles into mapped vertex buffer
void ParticleStyle::uploadParticles()
{
    if (m_aliveCount == 0)
    {
        return;
    }
    // Particle and ParticleVertex have identical layout (6 floats, 24 bytes)
    size_t bytes = (size_t)m_aliveCount * sizeof(ParticleVertex);
    memcpy(m_particleMapped, m_particles.data(), bytes);
}

// spawn up to SPAWN_BATCH new particles around the band's fixed UV position
void ParticleStyle::spawnParticles(int band, float energy)
{
    int toSpawn = std::min(SPAWN_BATCH, MAX_PARTICLES - (int)m_aliveCount);
    for (int i = 0; i < toSpawn; i++)
    {
        // random angular offset within radius 0.1 around band position
        float angle = (float)(rand() % 1000) / 1000.0f * 2.0f * 3.14159f;
        float radius = (float)(rand() % 100) / 100.0f * 0.1f;
        glm::vec2 spawnPos = BAND_POS[band] + glm::vec2(cosf(angle), sinf(angle)) * radius;

        // radial outward velocity from origin, scaled by energy and clamped
        float speed = 0.2f + energy * 10.0f;
        speed = std::min(speed, 1.5f);

        glm::vec2 dir;
        if (glm::length(spawnPos) < 1e-4f)
        {
            // sub-bass spawns at origin, normalize would be undefined, use random angle
            dir = glm::vec2(cosf(angle), sinf(angle));
        }
        else
        {
            dir = glm::normalize(spawnPos);
        }

        Particle p{};
        p.pos = spawnPos;
        p.vel = dir * speed;
        p.age = 0.0f;
        p.energy = std::min(energy * 5.0f, 1.0f);

        m_particles[m_aliveCount++] = p;
    }
}

} // namespace visuals
