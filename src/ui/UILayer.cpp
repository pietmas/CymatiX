#include <app/App.h>
#include <rhi/Swapchain.h>
#include <rhi/VulkanContext.h>
#include <ui/UILayer.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>

#include <cstdio>

// create descriptor pool, initalize imgui context, vulkan and glfw backends
void ui::UILayer::init(
    const rhi::VulkanContext &ctx,
    const rhi::Swapchain &swapchain,
    GLFWwindow *window,
    App &app
)
{
    // imgui needs its own pool for the font texture sampler
    vk::DescriptorPoolSize poolSize{};
    poolSize.type = vk::DescriptorType::eCombinedImageSampler;
    poolSize.descriptorCount = 1000;

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    m_descriptorPool = vk::raii::DescriptorPool(ctx.getDevice(), poolInfo);

    // create imgui context, enable docking, dark theme
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    // glfw backend installs mouse/keyboard callbacks
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // fill init info, using dynamic rendering so no VkRenderPass needed
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = ctx.getInstance();
    initInfo.PhysicalDevice = ctx.getPhysicalDevice();
    initInfo.Device = *ctx.getDevice();
    initInfo.QueueFamily = ctx.getQueueFamilyIndices().graphicsFamily.value();
    initInfo.Queue = ctx.getGraphicsQueue();
    initInfo.DescriptorPool = *m_descriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = swapchain.getImageCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    initInfo.UseDynamicRendering = true;

    // tell imgui what color format to expect so it compiles its pipeline correctly
    VkFormat colorFmt = (VkFormat)swapchain.getImageFormat();
    initInfo.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;

    ImGui_ImplVulkan_Init(&initInfo);

    // upload fonts, no-arg form handles the one-time submit internally
    ImGui_ImplVulkan_CreateFontsTexture();

    m_controlPanel = std::make_unique<ControlPanel>(app);

    printf("[UILayer] imgui initialized with dynamic rendering\n");
}

// build imgui frame, called after glfwPollEvents, before command recording
void ui::UILayer::buildFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Tab toggles the panel, visuals go full-screen when hidden
    if (ImGui::IsKeyPressed(ImGuiKey_Tab))
        m_showPanel = !m_showPanel;

    // passthru dockspace
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

    if (m_showPanel)
        m_controlPanel->draw();

    ImGui::Render();
}

// 20% of current display width when visible, 0 when hidden
float ui::UILayer::getPanelWidth() const
{
    if (!m_showPanel)
        return 0.0f;
    return ImGui::GetIO().DisplaySize.x * 0.2f;
}

// inject imgui draw calls into the active rendering block
void ui::UILayer::renderDrawData(vk::CommandBuffer cmd)
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

// destory imgui backends and context in reverse init order
void ui::UILayer::shutdown()
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_descriptorPool = nullptr; // raii destroys the pool
}
