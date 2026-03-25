#include <audio/FFTProcessor.h>

#include <cmath>
#include <cstdio>

// MSVC doesnt define M_PI
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio
{

// KissFFT config + precompute Hann window
FFTProcessor::FFTProcessor(int fftSize) : m_fftSize(fftSize)
{
    m_cfg = kiss_fftr_alloc(m_fftSize, 0, nullptr, nullptr);

    m_window.resize(m_fftSize);
    m_windowed.resize(m_fftSize);
    m_output.resize(m_fftSize / 2 + 1);
    m_magnitudes.resize(m_fftSize / 2 + 1, 0.0f);

    // Hann window: w[n] = 0.5 * (1 - cos(2*pi*n / (N-1)))
    for (int i = 0; i < m_fftSize; i++)
        m_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (m_fftSize - 1)));
}

FFTProcessor::~FFTProcessor()
{
    if (m_cfg)
        kiss_fftr_free(m_cfg);
}

// window samples, run FFT, compute magnitudes
void FFTProcessor::process(const float *samples, uint32_t count)
{
    if ((int)count < m_fftSize)
        return;

    // Hann window
    for (int i = 0; i < m_fftSize; i++)
        m_windowed[i] = samples[i] * m_window[i];

    kiss_fftr(m_cfg, m_windowed.data(), m_output.data());

    // complex -> magnitude
    // scale = 2/N: *2 for single-sided, /N to normalize
    float scale = 2.0f / (float)m_fftSize;
    for (int i = 0; i < m_fftSize / 2 + 1; i++)
    {
        float re = m_output[i].r;
        float im = m_output[i].i;
        m_magnitudes[i] = sqrtf(re * re + im * im) * scale;
    }
}

std::span<const float> FFTProcessor::getMagnitudes() const
{
    return {m_magnitudes.data(), m_magnitudes.size()};
}

} // namespace audio
