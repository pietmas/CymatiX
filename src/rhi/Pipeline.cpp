#include <rhi/Pipeline.h>
#include <rhi/Swapchain.h>
#include <rhi/VulkanContext.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace rhi
{

// read binary file into buffer
std::vector<char> Pipeline::readFile(const std::string &path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        fprintf(stderr, "failed to open file: %s\n", path.c_str());
        abort();
    }

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), (std::streamsize)fileSize);
    file.close();

    return buffer;
}

// wrap SPIR-V in shader module, scoped to pipeline creation
vk::raii::ShaderModule
Pipeline::createShaderModule(const vk::raii::Device &device, const std::vector<char> &code) const
{
    vk::ShaderModuleCreateInfo createInfo{};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    return device.createShaderModule(createInfo);
}

// build graphics pipeline, shaders + fixed-function state
void Pipeline::init(
    const VulkanContext &ctx,
    const Swapchain &swapchain,
    vk::Format colorFormat
)
{
    (void)swapchain; // extent no longer needed, viewport/scissor are dynamic

    const vk::raii::Device &device = ctx.getDevice();

    auto vertCode = readFile(SHADER_DIR "/triangle.vert.spv");
    auto fragCode = readFile(SHADER_DIR "/triangle.frag.spv");

    // shader modules only needed during create, raii handles cleanup at scope end
    vk::raii::ShaderModule vertModule = createShaderModule(device, vertCode);
    vk::raii::ShaderModule fragModule = createShaderModule(device, fragCode);

    vk::PipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertStageInfo.module = *vertModule;
    vertStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragStageInfo.module = *fragModule;
    fragStageInfo.pName = "main";

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

    // no vertex buffer, vertices hardcoded in shader
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = vk::False;

    // dynamic viewport+scissor, no pipeline recreate on resize
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
    rasterizer.cullMode = vk::CullModeFlagBits::eBack;
    rasterizer.frontFace = vk::FrontFace::eClockwise;
    rasterizer.depthBiasEnable = vk::False;

    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = vk::False;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // alpha blend disabled
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = vk::False;

    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = vk::False;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // empty layout, no push constants or descriptors
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    m_pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

    // tell the pipeline what format the color attachment will have at draw time
    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.colorAttachmentCount    = 1;
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
    // vertModule + fragModule destroyed here (end of scope)
}

// reset raii handles, destructors call vkDestroyPipeline / vkDestroyPipelineLayout
void Pipeline::destroy(const VulkanContext &ctx)
{
    (void)ctx;
    m_pipeline = nullptr;
    m_pipelineLayout = nullptr;
}

} // namespace rhi
