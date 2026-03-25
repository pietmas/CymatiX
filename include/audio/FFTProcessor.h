#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <kiss_fftr.h>

namespace audio
{

class FFTProcessor
{
  public:
    explicit FFTProcessor(int fftSize = 2048);
    ~FFTProcessor();

    FFTProcessor(const FFTProcessor &) = delete;
    FFTProcessor &operator=(const FFTProcessor &) = delete;
    FFTProcessor(FFTProcessor &&) = delete;
    FFTProcessor &operator=(FFTProcessor &&) = delete;

    // apply Hann window, run the FFT, compute magnitude spectrum
    void process(const float *samples, uint32_t count);

    std::span<const float> getMagnitudes() const;

    int getFFTSize() const
    {
        return m_fftSize;
    }
    int getBinCount() const
    {
        return m_fftSize / 2 + 1;
    }

  private:
    int m_fftSize;

    kiss_fftr_cfg m_cfg;         // KissFFT real FFT config
    std::vector<float> m_window; // precomputed Hann window coefficients
    std::vector<float> m_windowed;
    std::vector<kiss_fft_cpx> m_output; // complex output from FFT (N/2+1 bins)
    std::vector<float> m_magnitudes;    // magnitude per bin, normalized
};

} // namespace audio
