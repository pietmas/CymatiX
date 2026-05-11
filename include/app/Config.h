#pragma once

#include <cstdint>

// general app settings, put everything in one place so its easy to tweak

namespace Config
{

// window
constexpr uint32_t WINDOW_WIDTH = 800;
constexpr uint32_t WINDOW_HEIGHT = 600;
constexpr const char *APP_NAME = "CymatiX";

// how many frames we allow to be "in flight" at the same time
// 2 means the CPU can start working on frame N+1 while the GPU is still
// rendering frame N
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// audio pipeline -- change here to affect AudioEngine, FFTProcessor, and all visual styles
constexpr uint32_t SAMPLE_RATE = 44100;
constexpr int FFT_SIZE = 8192;
constexpr float BIN_HZ = static_cast<float>(SAMPLE_RATE) / static_cast<float>(FFT_SIZE);

} // namespace Config
