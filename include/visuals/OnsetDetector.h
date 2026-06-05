#pragma once

#include <cstdint>

namespace visuals
{

class OnsetDetector
{
  public:
    void update(const float *magnitudes, uint32_t binCount);
    bool hasOnset(int band) const;
    float bandEnergy(int band) const;

  private:
    static constexpr int NUM_BANDS = 4;
    static constexpr int BAND_START[4] = {0, 64, 192, 384};
    static constexpr int BAND_END[4] = {64, 192, 384, 1024};
    static constexpr float ALPHA = 0.97f;
    static constexpr float THRESHOLD = 2.5f;

    float m_averages[NUM_BANDS]{};
    float m_energies[NUM_BANDS]{};
    bool m_onsets[NUM_BANDS]{};
};

} // namespace visuals
