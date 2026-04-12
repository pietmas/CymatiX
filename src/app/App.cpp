#include <app/App.h>
#include <app/Config.h>
#include <palette/BioluminescentPalette.h>
#include <palette/CyberpunkPalette.h>
#include <visuals/LissajousStyle.h>
#include <visuals/WaveInterferenceStyle.h>

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

    m_commandPool = std::make_unique<rhi::CommandPool>();
    m_commandPool->init(*m_context);

    m_sync = std::make_unique<rhi::Sync>();
    m_sync->init(*m_context, m_swapchain->getImageCount());

    // register palettes before styles so m_activePalette is ready
    m_paletteRegistry.registerPalette(
        "bioluminescent",
        [] { return std::make_unique<palette::BioluminescentPalette>(); }
    );
    m_paletteRegistry.registerPalette(
        "cyberpunk",
        [] { return std::make_unique<palette::CyberpunkPalette>(); }
    );
    m_activePalette = m_paletteRegistry.create("cyberpunk");

    // register all styles with factory lambdas that capture deps by value
    m_styleRegistry.registerStyle(
        "lissajous",
        [deps = makeDeps()](const palette::IPalette &p)
        { return std::make_unique<visuals::LissajousStyle>(deps, p); }
    );
    m_styleRegistry.registerStyle(
        "wave_interference",
        [deps = makeDeps()](const palette::IPalette &p)
        { return std::make_unique<visuals::WaveInterferenceStyle>(deps, p); }
    );

    m_activeStyle = m_styleRegistry.create("lissajous", *m_activePalette);

    switchStyle("lissajous");
    switchPalette("cyberpunk");
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

    m_context->getDevice().waitIdle();
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
        auto mags2 = m_fftProcessor->getMagnitudes();
        int peakBin = 1;
        float peakMag = 0.0f;

        for (int i = 1; i < (int)mags2.size(); i++) // skip bin 0
        {
            if (mags2[i] > peakMag)
            {
                peakMag = mags2[i];
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

    // style owns pipeline, buffers, descriptor sets
    m_activeStyle.reset();

    m_sync->destroy(*m_context);
    m_commandPool->destroy(*m_context);
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
    const vk::raii::Device &device = m_context->getDevice();
    vk::Fence fence = m_sync->getInFlightFence(m_currentFrame);

    // wait for this frame's fence returns vk::Result (eSuccess or eTimeout)
    (void)device.waitForFences({fence}, vk::True, UINT64_MAX);

    // acquire next image, eErrorOutOfDateKHR throws, eSuboptimalKHR is in .first
    uint32_t imageIndex;
    try
    {
        auto [result, idx] = m_swapchain->getSwapchainRaii().acquireNextImage(
            UINT64_MAX,
            m_sync->getImageAvailableSemaphore(m_currentFrame),
            nullptr
        );
        if (result == vk::Result::eSuboptimalKHR)
        {
            m_framebufferResized = true;
        }
        imageIndex = idx;
    }
    catch (vk::OutOfDateKHRError &)
    {
        recreateSwapchain();
        return;
    }

    device.resetFences({fence});

    vk::CommandBuffer cmd = m_commandPool->getBuffer(m_currentFrame);
    cmd.reset({});

    // record commands
    vk::CommandBufferBeginInfo beginInfo{};
    cmd.begin(beginInfo);

    // barrier 1: transition image to color attachment layout
    vk::ImageMemoryBarrier2 toColor{};
    toColor.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
    toColor.srcAccessMask = vk::AccessFlagBits2::eNone;
    toColor.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    toColor.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    toColor.oldLayout = vk::ImageLayout::eUndefined;
    toColor.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
    toColor.image = m_swapchain->getImages()[imageIndex];
    toColor.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

    vk::DependencyInfo dep{};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toColor;
    cmd.pipelineBarrier2(dep);

    // begin dynamic rendering
    vk::ClearValue clearColor{};
    clearColor.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};

    vk::RenderingAttachmentInfo colorAttachment{};
    colorAttachment.imageView = m_swapchain->getImageViews()[imageIndex];
    colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.clearValue = clearColor;

    vk::RenderingInfo renderingInfo{};
    renderingInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderingInfo.renderArea.extent = m_swapchain->getExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    cmd.beginRendering(renderingInfo);

    // viewport + scissor to match swapchain extent
    vk::Viewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchain->getExtent().width;
    viewport.height = (float)m_swapchain->getExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    cmd.setViewport(0, {viewport});

    vk::Rect2D scissor{};
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = m_swapchain->getExtent();
    cmd.setScissor(0, {scissor});

    // delegate to active style
    m_activeStyle->render(cmd, (uint32_t)m_currentFrame);

    cmd.endRendering();

    // barrier 2: transition image back to present layout
    vk::ImageMemoryBarrier2 toPresent{};
    toPresent.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    toPresent.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    toPresent.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
    toPresent.dstAccessMask = vk::AccessFlagBits2::eNone;
    toPresent.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
    toPresent.image = m_swapchain->getImages()[imageIndex];
    toPresent.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

    vk::DependencyInfo dep2{};
    dep2.imageMemoryBarrierCount = 1;
    dep2.pImageMemoryBarriers = &toPresent;
    cmd.pipelineBarrier2(dep2);
    cmd.end();

    // submit to graphics queue
    vk::Semaphore waitSemaphores[] = {m_sync->getImageAvailableSemaphore(m_currentFrame)};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::Semaphore signalSemaphores[] = {m_sync->getRenderFinishedSemaphore(imageIndex)};

    vk::SubmitInfo submitInfo{};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    m_context->getGraphicsQueue().submit({submitInfo}, fence);

    // present image, eErrorOutOfDateKHR throws, eSuboptimalKHR is in result
    vk::SwapchainKHR swapchains[] = {m_swapchain->getSwapchain()};

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    try
    {
        vk::Result result = m_context->getPresentQueue().presentKHR(presentInfo);
        if (result == vk::Result::eSuboptimalKHR || m_framebufferResized)
        {
            m_framebufferResized = false;
            recreateSwapchain();
        }
    }
    catch (vk::OutOfDateKHRError &)
    {
        m_framebufferResized = false;
        recreateSwapchain();
    }

    m_currentFrame = (m_currentFrame + 1) % Config::MAX_FRAMES_IN_FLIGHT;
}

// bundle up vulkan handles for passing to style factories
rhi::VulkanDeps App::makeDeps() const
{
    rhi::VulkanDeps deps{};
    deps.device = &m_context->getDevice();
    deps.physicalDevice = m_context->getPhysicalDevice();
    deps.colorFormat = m_swapchain->getImageFormat();
    deps.extent = m_swapchain->getExtent();
    return deps;
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

// stall GPU before swapping style so the old one isnt destroyed mid-frame
void App::switchStyle(const std::string &name)
{
    m_context->getDevice().waitIdle();
    auto next = m_styleRegistry.create(name, *m_activePalette);
    if (!next)
    {
        fprintf(stderr, "[App] switchStyle: unknown style '%s', keeping current\n", name.c_str());
        return;
    }
    m_activeStyle = std::move(next);
}

// palette data flows via UBO next frame, no GPU stall needed
void App::switchPalette(const std::string &name)
{
    auto next = m_paletteRegistry.create(name);
    if (!next)
    {
        fprintf(
            stderr,
            "[App] switchPalette: unknown palette '%s', keeping current\n",
            name.c_str()
        );
        return;
    }
    m_activePalette = std::move(next);
}

// GLFW resize callback
void App::framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
    width;
    height;
    auto *app = reinterpret_cast<App *>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
}