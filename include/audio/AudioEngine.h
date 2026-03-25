#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace audio
{

struct AudioEngineImpl;

class AudioEngine
{
  public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;
    AudioEngine(AudioEngine &&) = delete;
    AudioEngine &operator=(AudioEngine &&) = delete;

    // load an audio file for playback (WAV, MP3, FLAC, OGG, etc.)
    bool load(const std::string &path);
    void play();
    void pause();

    // copy up to count mono float32 samples from the ring buffer into buf
    // returns the number of samples written; call in a loop to drain
    uint32_t getLatestSamples(float *buf, uint32_t count);

    bool isPlaying() const;
    uint32_t getSampleRate() const;

  private:
    std::unique_ptr<AudioEngineImpl> m_impl;
};

} // namespace audio
