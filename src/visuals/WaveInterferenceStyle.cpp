#include <rhi/VulkanContext.h>
#include <visuals/WaveInterferenceStyle.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace visuals
{

// file / shader helpers (

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

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

    return shaderModule;
}

static uint32_t
findMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
    {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
        {
            return i;
        }
    }

    fprintf(stderr, "[WaveInterferenceStyle] failed to find suitable memory type\n");
    abort();
}

static void createMappedBuffer(
    VkDevice device,
    VkPhysicalDevice physDev,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer &outBuffer,
    VkDeviceMemory &outMemory,
    void *&outMapped
)
{
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &bufInfo, nullptr, &outBuffer));

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        physDev,
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &outMemory));
    VK_CHECK(vkBindBufferMemory(device, outBuffer, outMemory, 0));
    VK_CHECK(vkMapMemory(device, outMemory, 0, size, 0, &outMapped));
}

// constructor

WaveInterferenceStyle::WaveInterferenceStyle(
    const rhi::VulkanContext &ctx,
    VkRenderPass renderPass,
    VkExtent2D extent,
    const palette::IPalette &palette
)
    : m_ctx(ctx), m_extent(extent)
{
    createDescriptorSetLayout();
    createDescriptorPool();
    createPipeline(renderPass);
    createUBOBuffers();
    createDescriptorSets(palette);
}

// destructor

WaveInterferenceStyle::~WaveInterferenceStyle()
{
    VkDevice device = m_ctx.getDevice();

    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);

    // pool destroy
    vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkUnmapMemory(device, m_spectrumUBOMemory[i]);
        vkDestroyBuffer(device, m_spectrumUBOBuffers[i], nullptr);
        vkFreeMemory(device, m_spectrumUBOMemory[i], nullptr);

        vkUnmapMemory(device, m_paletteUBOMemory[i]);
        vkDestroyBuffer(device, m_paletteUBOBuffers[i], nullptr);
        vkFreeMemory(device, m_paletteUBOMemory[i], nullptr);
    }
}

// private init helpers

void WaveInterferenceStyle::createDescriptorSetLayout()
{
    VkDescriptorSetLayoutBinding bindings[2]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VK_CHECK(
        vkCreateDescriptorSetLayout(m_ctx.getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout)
    );
}

void WaveInterferenceStyle::createDescriptorPool()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = Config::MAX_FRAMES_IN_FLIGHT * 2;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = Config::MAX_FRAMES_IN_FLIGHT;

    VK_CHECK(vkCreateDescriptorPool(m_ctx.getDevice(), &poolInfo, nullptr, &m_descriptorPool));
}

// full-screen procedural pipeline
void WaveInterferenceStyle::createPipeline(VkRenderPass renderPass)
{
    VkDevice device = m_ctx.getDevice();

    auto vertCode = readFile(SHADER_DIR "/wave_interference.vert.spv");
    auto fragCode = readFile(SHADER_DIR "/wave_interference.frag.spv");

    VkShaderModule vertModule = createShaderModule(device, vertCode);
    VkShaderModule fragModule = createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

    // no vertex buffer
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // alpha blend for wave pattern
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;

    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_pipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VK_CHECK(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline)
    );

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

void WaveInterferenceStyle::createUBOBuffers()
{
    VkDevice device = m_ctx.getDevice();
    VkPhysicalDevice physDev = m_ctx.getPhysicalDevice();

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        createMappedBuffer(
            device,
            physDev,
            sizeof(SpectrumUBOData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            m_spectrumUBOBuffers[i],
            m_spectrumUBOMemory[i],
            m_spectrumMapped[i]
        );

        createMappedBuffer(
            device,
            physDev,
            sizeof(PaletteUBOData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            m_paletteUBOBuffers[i],
            m_paletteUBOMemory[i],
            m_paletteMapped[i]
        );
    }
}

void WaveInterferenceStyle::createDescriptorSets(const palette::IPalette &palette)
{
    VkDevice device = m_ctx.getDevice();

    VkDescriptorSetLayout layouts[Config::MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
        layouts[i] = m_descriptorSetLayout;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = Config::MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts;

    VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets));

    PaletteUBOData paletteData = palette.getUBOData();

    for (int i = 0; i < Config::MAX_FRAMES_IN_FLIGHT; i++)
    {
        memcpy(m_paletteMapped[i], &paletteData, sizeof(PaletteUBOData));

        VkDescriptorBufferInfo spectrumInfo{};
        spectrumInfo.buffer = m_spectrumUBOBuffers[i];
        spectrumInfo.offset = 0;
        spectrumInfo.range = sizeof(SpectrumUBOData);

        VkDescriptorBufferInfo paletteInfo{};
        paletteInfo.buffer = m_paletteUBOBuffers[i];
        paletteInfo.offset = 0;
        paletteInfo.range = sizeof(PaletteUBOData);

        VkWriteDescriptorSet writes[2]{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &spectrumInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].pBufferInfo = &paletteInfo;

        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
}

//  per-frame logic

// shader does wave math procedurally
void WaveInterferenceStyle::update(const float *magnitudes, uint32_t count, float deltaTime)
{
    m_time += deltaTime;

    uint32_t bins = (count < MAX_SPECTRUM_BINS) ? count : MAX_SPECTRUM_BINS;
    memcpy(m_pendingSpectrum.magnitudes, magnitudes, bins * sizeof(float));
    m_pendingSpectrum.time = m_time;
}

void WaveInterferenceStyle::render(VkCommandBuffer cmd, uint32_t frameIndex)
{
    memcpy(m_spectrumMapped[frameIndex], &m_pendingSpectrum, sizeof(SpectrumUBOData));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipelineLayout,
        0,
        1,
        &m_descriptorSets[frameIndex],
        0,
        nullptr
    );

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void WaveInterferenceStyle::onResize(VkExtent2D newExtent)
{
    m_extent = newExtent;
}

} // namespace visuals
