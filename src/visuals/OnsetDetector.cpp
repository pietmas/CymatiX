#include <visuals/OnsetDetector.h>

#include <algorithm>

namespace visuals
{

// compute per-band energy and detect onsets
void OnsetDetector::update(const float *magnitudes, uint32_t binCount)
{
    for (int b = 0; b < NUM_BANDS; b++)
    {
        m_onsets[b] = false;

        float energy = 0.0f;
        int end = std::min((int)binCount, BAND_END[b]);
        for (int i = BAND_START[b]; i < end; i++)
        {
            energy += magnitudes[i] * magnitudes[i];
        }
        energy /= float(end - BAND_START[b]); // mean square energy

        m_energies[b] = energy;

        // guard against cold start: dont fire on the very first loud frame
        if (energy > m_averages[b] * THRESHOLD && m_averages[b] > 1e-6f)
        {
            m_onsets[b] = true;
        }

        m_averages[b] = m_averages[b] * ALPHA + energy * (1.0f - ALPHA);
    }
}

bool OnsetDetector::hasOnset(int band) const
{
    return m_onsets[band];
}

float OnsetDetector::bandEnergy(int band) const
{
    return m_energies[band];
}

} // namespace visuals
