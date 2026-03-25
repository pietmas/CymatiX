#include <app/App.h>
#include <app/Config.h>
#include <visuals/LissajousStyle.h>
#include <visuals/WaveInterferenceStyle.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>

// open window, register resize callback
void App::initWindow()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_window = glfwCreateWindow(
        Config::WINDOW_WIDTH,
        Config::WINDOW_HEIGHT,
        Config::APP_NAME,
        nullptr,
        nullptr
    );

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

// init vulkan objs in creation order
void App::initVulkan()
{
    m_context = std::make_unique<rhi::VulkanContext>();
    m_context->init(m_window);

    m_swapchain = std::make_unique<rhi::Swapchain>();
    m_swapchain->init(*m_context, m_window);

    m_renderPass = std::make_unique<rhi::RenderPass>();
    m_renderPass->init(*m_context, *m_swapchain);

    m_swapchain->createFramebuffers(*m_context, m_renderPass->get());

    m_commandPool = std::make_unique<rhi::CommandPool>();
    m_commandPool->init(*m_context);

    m_sync = std::make_unique<rhi::Sync>();
    m_sync->init(*m_context, m_swapchain->getImageCount());

    // active visual style -- same interface for all styles, easy swap
    m_activeStyle = std::make_unique<visuals::LissajousStyle>(
        *m_context,
        m_renderPass->get(),
        m_swapchain->getExtent(),
        m_palette
    );
}

// poll events, draw until window closes
void App::mainLoop()
{
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();
        update();
        drawFrame();
    }

    vkDeviceWaitIdle(m_context->getDevice());
}

// load audio file, start FFT processor
void App::initAudio()
{
    m_audioEngine = std::make_unique<audio::AudioEngine>();
    m_fftProcessor = std::make_unique<audio::FFTProcessor>(2048);

    // place audio file next to executable; WAV, MP3, FLAC, OGG all work
    if (m_audioEngine->load("test/test2.wav"))
        m_audioEngine->play();
    else
        printf("[App] tip: place test/test2.wav next to the executable\n");
}

// drain ring buffer, slide window, run FFT each frame
void App::update()
{
    // drain samples written since last frame
    static constexpr uint32_t CHUNK = 512;
    static float tmp[CHUNK];

    uint32_t got = 0;
    while ((got = m_audioEngine->getLatestSamples(tmp, CHUNK)) > 0)
    {
        for (uint32_t i = 0; i < got; i++)
        {
            m_audioHistory.push_back(tmp[i]);
        }
    }

    // keep only latest fftSize samples
    int fftSize = m_fftProcessor->getFFTSize();
    while ((int)m_audioHistory.size() > fftSize)
    {
        m_audioHistory.pop_front();
    }
    // run FFT once full window available
    if ((int)m_audioHistory.size() == fftSize)
    {
        static std::vector<float> samples;
        samples.assign(m_audioHistory.begin(), m_audioHistory.end());
        m_fftProcessor->process(samples.data(), (uint32_t)fftSize);
    }

    // compute dt, feed spectrum to active style
    float now = (float)glfwGetTime();
    float dt = now - m_lastFrameTime;
    m_lastFrameTime = now;

    // update title with frame/fps once per second
    /*m_fpsAccum += dt;
    m_fpsSamples++;
    if (m_fpsAccum >= 1.0f)
    {
        float fps = (float)m_fpsSamples / m_fpsAccum;
        char title[64];
        snprintf(
            title,
            sizeof(title),
            "CymatiX  |  frame %-6d  |  %.1f fps",
            m_debugFrameCount,
            fps
        );
        glfwSetWindowTitle(m_window, title);
        m_fpsAccum = 0.0f;
        m_fpsSamples = 0;
    }*/

    auto mags = m_fftProcessor->getMagnitudes();

    // EMA smoothing on FFT
    if (m_smoothedMags.size() != mags.size())
        m_smoothedMags.assign(mags.size(), 0.0f);

    constexpr float kSmooth = 0.2f;
    for (size_t i = 0; i < mags.size(); i++)
        m_smoothedMags[i] += kSmooth * (mags[i] - m_smoothedMags[i]);

    m_activeStyle->update(m_smoothedMags.data(), (uint32_t)m_smoothedMags.size(), dt);

    // print dominant bin once/sec ca (at 60 fps)
    m_debugFrameCount++;
    if (m_debugFrameCount % 60 == 0 && m_audioEngine->isPlaying())
    {
        auto mags = m_fftProcessor->getMagnitudes();
        int peakBin = 1;
        float peakMag = 0.0f;

        for (int i = 1; i < (int)mags.size(); i++) // skip bin 0
        {
            if (mags[i] > peakMag)
            {
                peakMag = mags[i];
                peakBin = i;
            }
        }

        // freq = bin * sampleRate / fftSize
        float freq = (float)peakBin * (float)m_audioEngine->getSampleRate() / (float)fftSize;
        printf("[FFT] peak bin: %d  (%.0f Hz)  mag: %.4f\n", peakBin, freq, peakMag);
    }
}

// destroy in reverse creation order
void App::shutdown()
{
    // audio independent of Vulkan
    m_fftProcessor.reset();
    m_audioEngine.reset();

    // style owns VkPipeline, VkBuffer, VkDescriptorSet
    m_activeStyle.reset();

    m_sync->destroy(*m_context);
    m_commandPool->destroy(*m_context);
    m_renderPass->destroy(*m_context);
    m_swapchain->destroy(*m_context);
    m_context->destroy();

    glfwDestroyWindow(m_window);
    glfwTerminate();
}

// recreate swapchain on resize / out-of-date
void App::recreateSwapchain()
{
    m_swapchain->recreate(*m_context);
    m_activeStyle->onResize(m_swapchain->getExtent());
}

// record + submit one frame
void App::drawFrame()
{
    VkFence fence = m_sync->getInFlightFence(m_currentFrame);
    VK_CHECK(vkWaitForFences(m_context->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_context->getDevice(),
        m_swapchain->getSwapchain(),
        UINT64_MAX,
        m_sync->getImageAvailableSemaphore(m_currentFrame),
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }

    vkResetFences(m_context->getDevice(), 1, &fence);

    VkCommandBuffer cmd = m_commandPool->getBuffer(m_currentFrame);
    vkResetCommandBuffer(cmd, 0);

    // record commands
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
    {
        fprintf(stderr, "failed to begin command buffer\n");
        abort();
    }

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass->get();
    renderPassInfo.framebuffer = m_swapchain->getFramebuffers()[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->getExtent();
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // viewport + scissor to match swapchain extent
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchain->getExtent().width;
    viewport.height = (float)m_swapchain->getExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchain->getExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // delegate to active style
    m_activeStyle->render(cmd, (uint32_t)m_currentFrame);

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        fprintf(stderr, "failed to record command buffer\n");
        abort();
    }

    // submit to graphics queue
    VkSemaphore waitSemaphores[] = {m_sync->getImageAvailableSemaphore(m_currentFrame)};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {m_sync->getRenderFinishedSemaphore(imageIndex)};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS)
    {
        fprintf(stderr, "failed to submit draw command buffer\n");
        abort();
    }

    // present image
    VkSwapchainKHR swapchains[] = {m_swapchain->getSwapchain()};

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized)
    {
        m_framebufferResized = false;
        recreateSwapchain();
    }

    m_currentFrame = (m_currentFrame + 1) % Config::MAX_FRAMES_IN_FLIGHT;
}

// entry point
void App::run()
{
    initWindow();
    initVulkan();
    initAudio();
    mainLoop();
    shutdown();
}

// GLFW resize callback
void App::framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
    (void)width;
    (void)height;
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
}
